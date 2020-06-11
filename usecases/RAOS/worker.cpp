#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <random>
#include <iostream>
#include <algorithm>

#include "worker.hpp"
#include "stat.hpp"

static int intRand(const int &min, const int &max) {
	static thread_local std::mt19937 generator;
	std::uniform_int_distribution<int> distribution(min,max);
	return distribution(generator);
}

static void get_target_location_read(job_ctx_t *job_ctx, data_loc_t *dataloc, int *target_ssd_id, uint64_t *target_offset) {
	if (job_ctx->arg_ctx->exec_mode == EXEC_MODE_NORMAL) {
		int item_idx = intRand(0, dataloc->num_replica - 1);
		*target_ssd_id = dataloc->ssd_ids[item_idx];
		*target_offset = dataloc->offsets[item_idx];
	} else if (job_ctx->arg_ctx->exec_mode == EXEC_MODE_IDLEAWARE) {
		int target_replica_idx;

		// active SSD selection
		while (true) {
			target_replica_idx = intRand(0, dataloc->num_replica - 1);
			ssd_context_t *ssd_info = &job_ctx->ssd_status[dataloc->ssd_ids[target_replica_idx]];

			if (ssd_info->state == SSD_STATE_ACTIVE) {
				break;
			}
		}

		*target_ssd_id = dataloc->ssd_ids[target_replica_idx];
		*target_offset = dataloc->offsets[target_replica_idx];
	} else {
		assert(0);
	}
}

int global_ssd_idx = 0;
pthread_mutex_t *global_ssd_idx_lock;

static void get_target_location_write(job_ctx_t *job_ctx, data_loc_t *dataloc) {
	int cur_ssd_idx = 0;

	if (job_ctx->arg_ctx->exec_mode == EXEC_MODE_NORMAL) {
		assert(TOTAL_SSD_NUM > dataloc->num_replica);
		int base_ssd_idx;
		pthread_mutex_lock(global_ssd_idx_lock);
		base_ssd_idx = global_ssd_idx++;
		pthread_mutex_unlock(global_ssd_idx_lock);

		for (int replica = 0; replica < dataloc->num_replica; replica++) {
			int ssd_idx = (base_ssd_idx + replica) % TOTAL_SSD_NUM;
			assert(job_ctx->ssd_status[ssd_idx].cur_ssd_id == ssd_idx);
			pthread_mutex_lock(job_ctx->ssd_status[ssd_idx].ssd_lock);
			dataloc->ssd_ids[replica] = ssd_idx;
			dataloc->offsets[replica] = job_ctx->ssd_status[ssd_idx].cur_offset;
			job_ctx->ssd_status[ssd_idx].cur_offset += job_ctx->arg_ctx->block_size;
			pthread_mutex_unlock(job_ctx->ssd_status[ssd_idx].ssd_lock);
		}
	} else if (job_ctx->arg_ctx->exec_mode == EXEC_MODE_IDLEAWARE) {
        assert(TOTAL_SSD_NUM > dataloc->num_replica);
        for (int replica = 0; replica < dataloc->num_replica; replica++) {
            while (true) {
                int ssd_idx = intRand(0, TOTAL_SSD_NUM - 1);
                bool dup_ssd = false;

                for (int j = 0; j < cur_ssd_idx; j++) {
                    if (ssd_idx == dataloc->ssd_ids[j]) {
                        dup_ssd = true;
                        break;
                    }
                }

                if (dup_ssd) {
                    continue;
                } else {
                    assert(job_ctx->ssd_status[ssd_idx].cur_ssd_id == ssd_idx);
                    if (job_ctx->ssd_status[ssd_idx].state == SSD_STATE_ACTIVE) {
                        pthread_mutex_lock(job_ctx->ssd_status[ssd_idx].ssd_lock);
                        dataloc->ssd_ids[replica] = ssd_idx;
                        dataloc->offsets[replica] = job_ctx->ssd_status[ssd_idx].cur_offset;
                        job_ctx->ssd_status[ssd_idx].cur_offset += job_ctx->arg_ctx->block_size;
                        pthread_mutex_unlock(job_ctx->ssd_status[ssd_idx].ssd_lock);
                        cur_ssd_idx++;
                        break;
                    }
                }
            }
        }
	} else {
		assert(0);
	}
}

int inactive_ssd_num = 0;
int flush_ssd_idx = -1;
system_clock::time_point flush_ts;

void *worker(void *data) {
	auto *thread_arg = (thread_arg_t*)data;
	auto *job_ctx = (job_ctx_t*)thread_arg->global_ctx;
	int tid = thread_arg->tid;
	const int block_size = job_ctx->arg_ctx->block_size;
	const int read_ratio = job_ctx->arg_ctx->read_ratio;
	const int thinktime = job_ctx->arg_ctx->thinktime;  // ms
	int ssd_fds[TOTAL_SSD_NUM];
	char *dummy_buffer;
	system_clock::time_point log_ts, cur_ts;

	dummy_buffer = (char*)valloc(block_size);
	memset(dummy_buffer, 1, block_size);

	for (int i = 0; i < TOTAL_SSD_NUM; i++) {
		ssd_fds[i] = open(get_ssd_devname(i), O_RDWR | O_DIRECT | O_SYNC);
		assert(ssd_fds[i] >= 0);
	}

	pthread_barrier_wait(job_ctx->worker_barrier);
	/// Do start
	log_ts = system_clock::now();
	job_ctx->stats[tid].start = system_clock::now();
	while (true) {
		int rw = intRand(0, 99);
		usleep(thinktime * 1000);

		// Read or Write?
		if (rw < read_ratio) {
			/// read
			bool read_success = false;
			if (job_ctx->cur_key == 0) {
				printf("[DEBUG] cur_key is 0, there's no readable objects in the storage\n");
				continue;
			}

			for (int retry = 0; retry < 3; retry++) {
				int read_key = intRand(0, job_ctx->cur_key - 1);
				value_context_t *value_ctx = &job_ctx->kv_map[read_key];

				if (value_ctx->is_valid) {
					int target_ssd_id;
					uint64_t target_offset;
					system_clock::time_point start, end;
					double read_elapsed;

					get_target_location_read(job_ctx, value_ctx->dataloc, &target_ssd_id, &target_offset);
					start = system_clock::now();
					if (pread(ssd_fds[target_ssd_id], dummy_buffer, block_size, target_offset) < 0) {
						perror("read failed");
						assert(0);
					}
					end = system_clock::now();
					read_elapsed = (std::chrono::duration<double>(end - start)).count();
					job_ctx->stats[tid].read_count++;
					job_ctx->stats[tid].read_data += (block_size);
					update_ssd_stat_read(job_ctx, block_size, target_ssd_id, read_elapsed);
//					printf("[DEBUG] [tid:%d] <READ> (read_key:%d) ssd [%d], offset[%lx]\n", tid, read_key, target_ssd_id , target_offset);

					read_success = true;
					break;
				}
			}

			if (!read_success) {
				printf("[FATAL] Fail to read data");
				assert(job_ctx->kv_map[0].is_valid && job_ctx->kv_map[0].dataloc);
			}
		} else {
			/// write
			int write_key;
			value_context_t *value_ctx = nullptr;
			data_loc_t *tmp_dataloc = nullptr;
			char debug_msg[4096];
			memset(debug_msg, 0, 4096);
			system_clock::time_point start, end;
			double write_elapsed[4];
			assert(job_ctx->arg_ctx->num_replica <= 4);

			pthread_mutex_lock(job_ctx->key_lock);
			write_key = job_ctx->cur_key++;
			pthread_mutex_unlock(job_ctx->key_lock);
			if (write_key >= job_ctx->max_key_num)
				break;

			value_ctx = &job_ctx->kv_map[write_key];

			tmp_dataloc = (data_loc_t*)malloc(sizeof(data_loc_t));
			tmp_dataloc->num_replica = job_ctx->arg_ctx->num_replica;
			tmp_dataloc->ssd_ids = (int*)malloc(sizeof(int) * tmp_dataloc->num_replica);
			for (int i = 0 ;i < tmp_dataloc->num_replica; i++) { tmp_dataloc->ssd_ids[i] = -1; }
			tmp_dataloc->offsets = (uint64_t*)malloc(sizeof(uint64_t) * tmp_dataloc->num_replica);

			get_target_location_write(job_ctx, tmp_dataloc);
			for (int replica = 0; replica < tmp_dataloc->num_replica; replica++) {
				start = system_clock::now();
				if (pwrite(ssd_fds[tmp_dataloc->ssd_ids[replica]], dummy_buffer, block_size, tmp_dataloc->offsets[replica]) < 0) {
					perror("write failed");
					assert(0);
				}
				end = system_clock::now();
				write_elapsed[replica] = (std::chrono::duration<double> (end - start)).count();
//				printf("write elapsed:%.2lf\n",write_elapsed[replica]);
				sprintf(debug_msg + strlen(debug_msg), "(%d, %lx) ", tmp_dataloc->ssd_ids[replica], tmp_dataloc->offsets[replica]);
			}
			job_ctx->stats[tid].write_count += tmp_dataloc->num_replica;
			job_ctx->stats[tid].write_data += (block_size * tmp_dataloc->num_replica);
			update_ssd_stat_write(job_ctx, block_size, tmp_dataloc, write_elapsed);
//			printf("[DEBUG] [tid:%d] <WRITE> (write_key:%d) %s\n", tid, write_key, debug_msg);

			value_ctx->dataloc = tmp_dataloc;
			value_ctx->is_valid = true;
		}


		if (tid == 0) {
			cur_ts = system_clock::now();
			double t_elapsed = (std::chrono::duration<double> (cur_ts - log_ts)).count();
			if (t_elapsed > 1.0f) {
				char debug_msg[4096];
				memset(debug_msg, 0, 4096);
				sprintf(debug_msg, "[DEBUG] ==== Monitoring info ====\n");
				for (int i = 0; i < TOTAL_SSD_NUM; i++) {
                    pthread_mutex_lock(job_ctx->ssd_status[i].ssd_lock);

					double acc_read_elapsed = accumulate(job_ctx->ssd_status[i].read_elapsed_list->begin(),
							job_ctx->ssd_status[i].read_elapsed_list->end(),
							0.0);
					double acc_write_elapsed = accumulate(job_ctx->ssd_status[i].write_elapsed_list->begin(),
							job_ctx->ssd_status[i].write_elapsed_list->end(),
							0.0);
					double avg_read_elapsed = acc_read_elapsed / (double) job_ctx->ssd_status[i].read_elapsed_list->size();
					double max_read_elapsed = *(max_element(job_ctx->ssd_status[i].read_elapsed_list->begin(), job_ctx->ssd_status[i].read_elapsed_list->end()));
                    double min_read_elapsed = *(min_element(job_ctx->ssd_status[i].read_elapsed_list->begin(), job_ctx->ssd_status[i].read_elapsed_list->end()));
                    double avg_write_elapsed = acc_write_elapsed / (double) job_ctx->ssd_status[i].write_elapsed_list->size();
                    double max_write_elapsed = *(max_element(job_ctx->ssd_status[i].write_elapsed_list->begin(), job_ctx->ssd_status[i].write_elapsed_list->end()));
                    double min_write_elapsed = *(min_element(job_ctx->ssd_status[i].write_elapsed_list->begin(), job_ctx->ssd_status[i].write_elapsed_list->end()));
					double acc_write_data = job_ctx->ssd_status[i].write_data;
					double acc_read_data = job_ctx->ssd_status[i].read_data;
					job_ctx->ssd_status[i].write_data = 0;
					job_ctx->ssd_status[i].read_data = 0;

					if (max_write_elapsed > 0.05) {
						if (inactive_ssd_num == 0) {
							job_ctx->ssd_status[i].state = SSD_STATE_INACTIVE;
							job_ctx->ssd_status[i].read_elapsed_list->clear();
							job_ctx->ssd_status[i].write_elapsed_list->clear();
							inactive_ssd_num++;
							flush_ssd_idx = i;
							flush_ts = system_clock::now();
						}
					}

					pthread_mutex_unlock(job_ctx->ssd_status[i].ssd_lock);
//					sprintf(debug_msg + strlen(debug_msg), "[DEBUG] SSD(%d) Bandwidth (Read/Write) => (%.2lf/%.2lf)MB/s // Avg. Latency (Read:%.2lf, Write:%.2lf) // (Min/Max) => (Read: (%.2lf/%.2lf), Write: (%.2lf, %.2lf))\n",
//							i, acc_read_data / t_elapsed / 1024 / 1024,
//							acc_write_data / t_elapsed / 1024 / 1024,
//                            avg_read_elapsed, avg_write_elapsed,
//                            min_read_elapsed, max_read_elapsed,
//                            min_write_elapsed, max_write_elapsed);
				}
//				printf("%s", debug_msg);
				log_ts = cur_ts;
			}


			double t_flush_elapsed = (std::chrono::duration<double> (cur_ts - flush_ts)).count();

			if (t_flush_elapsed > 5.0f) {
				if (inactive_ssd_num) {
					pthread_mutex_lock(job_ctx->ssd_status[flush_ssd_idx].ssd_lock);
					inactive_ssd_num = 0;
					job_ctx->ssd_status[flush_ssd_idx].state = SSD_STATE_ACTIVE;
					pthread_mutex_unlock(job_ctx->ssd_status[flush_ssd_idx].ssd_lock);
				}
			}
		}
	}
	job_ctx->stats[tid].end = system_clock::now();


    if (tid == 0) {
        for (int i = 0; i < TOTAL_SSD_NUM; i++) {
            printf("SSD (%d)\n",i);
            print_cdf(job_ctx->ssd_status[i].all_write_elapsed_list);
        }

        // Dump lat log
        for (int i = 0; i < TOTAL_SSD_NUM; i++) {
            char fname[1024];

            sprintf(fname, "%s.ssd_%d", job_ctx->arg_ctx->log_fname, i);
            FILE *fp = fopen(fname, "w");

            for (double & iter : *job_ctx->ssd_status[i].all_write_elapsed_list) {
                fprintf(fp, "%lf\n", iter);
            }
            fclose(fp);
        }
    }

	free(dummy_buffer);

	return NULL;
}