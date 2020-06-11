#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <chrono>
#include <cstring>

#include "utils.hpp"
#include "key_value_map.hpp"
#include "worker.hpp"

static void job_ctx_free(job_ctx_t *job_ctx) {
	if (job_ctx->stats) free(job_ctx->stats);
    if (job_ctx->ssd_status) {
        for (int i = 0; i < TOTAL_SSD_NUM; i++) {
            free(job_ctx->ssd_status[i].ssd_lock);

            delete (job_ctx->ssd_status[i].read_elapsed_list);
            delete (job_ctx->ssd_status[i].all_read_elapsed_list);
            delete (job_ctx->ssd_status[i].write_elapsed_list);
            delete (job_ctx->ssd_status[i].all_write_elapsed_list);
        }
        free(job_ctx->ssd_status);
    }
    if (job_ctx->kv_map) {
        for (int i = 0; i < job_ctx->max_key_num; i++) {
            free(job_ctx->kv_map[i].dataloc->ssd_ids);
            free(job_ctx->kv_map[i].dataloc->offsets);
            free(job_ctx->kv_map[i].dataloc);
        }
        free(job_ctx->kv_map);
    }
	if (job_ctx->key_lock) free(job_ctx->key_lock);
	if (job_ctx->worker_barrier) free(job_ctx->worker_barrier);
}

int main(int argc, char *argv[]) {
	int rc = 0, i, status;
	pthread_t *threads = nullptr;
	arg_ctx_t arg_ctx;
	job_ctx_t job_ctx;
	thread_arg_t *thread_args = nullptr;
	int tot_read_count = 0, tot_write_count = 0;
	double elapsed = 0, per_thread_elapsed;

	global_ssd_idx = 0;
	global_ssd_idx_lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(global_ssd_idx_lock, nullptr);

	memset(&job_ctx, 0, sizeof(job_ctx_t));
	// argument parsing
	if ((rc = arg_parser(argc, argv, &arg_ctx)) != 0) {
		goto err;
	}

	// init job_context
	job_ctx.arg_ctx = &arg_ctx;
	job_ctx.max_key_num = arg_ctx.tot_req_count;
	job_ctx.cur_key = 0;
	job_ctx.kv_map = (value_context_t*)malloc(sizeof(value_context_t) * job_ctx.max_key_num);
	for (i = 0; i < job_ctx.max_key_num; i++) {
		job_ctx.kv_map[i].is_valid = false;
		job_ctx.kv_map[i].dataloc = nullptr;
	}

	job_ctx.ssd_status = (ssd_context_t*)malloc(sizeof(ssd_context_t) * TOTAL_SSD_NUM);
	for (i = 0; i < TOTAL_SSD_NUM; i++) {
		job_ctx.ssd_status[i].ssd_lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
		pthread_mutex_init(job_ctx.ssd_status[i].ssd_lock, nullptr);
		job_ctx.ssd_status[i].cur_offset = 0;
		job_ctx.ssd_status[i].cur_ssd_id = i;
		job_ctx.ssd_status[i].state = SSD_STATE_ACTIVE;

		job_ctx.ssd_status[i].read_elapsed_list = new(std::list<double>);
        job_ctx.ssd_status[i].all_read_elapsed_list = new(std::list<double>);
		job_ctx.ssd_status[i].write_elapsed_list = new(std::list<double>);
		job_ctx.ssd_status[i].all_write_elapsed_list = new(std::list<double>);
        job_ctx.ssd_status[i].read_data = 0;
        job_ctx.ssd_status[i].write_data = 0;
	}

	job_ctx.stats = (stat_info_t*)malloc(sizeof(stat_info_t) * arg_ctx.th_num);
	for (i = 0; i < arg_ctx.th_num; i++) {
		memset(&job_ctx.stats[i], 0, sizeof(stat_info_t));
	}

	// init thread & barriers
	job_ctx.worker_barrier = (pthread_barrier_t*)malloc(sizeof(pthread_barrier_t));
	pthread_barrier_init(job_ctx.worker_barrier, nullptr, arg_ctx.th_num);
	job_ctx.key_lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(job_ctx.key_lock, nullptr);

	thread_args = (thread_arg_t*)malloc(sizeof(thread_arg_t) * arg_ctx.th_num);
	for (i = 0; i < arg_ctx.th_num; i++) {
		thread_args[i].global_ctx = &job_ctx;
		thread_args[i].tid = i;
	}

	threads = (pthread_t*)malloc(sizeof(pthread_t) * arg_ctx.th_num);
	for (i = 0; i < arg_ctx.th_num; i++) {
		if ((rc = pthread_create(&threads[i], nullptr, worker, &thread_args[i])) < 0) {
			goto err;
		}
	}

	for (i = 0; i < arg_ctx.th_num; i++) {
		pthread_join(threads[i], (void **)&status);
	}

	/// Dump results
	for (i = 0; i < arg_ctx.th_num; i++) {
		tot_read_count += job_ctx.stats[i].read_count;
		tot_write_count += job_ctx.stats[i].write_count;
		per_thread_elapsed = (std::chrono::duration<double> (job_ctx.stats[i].end - job_ctx.stats[i].start)).count();
		if (elapsed < per_thread_elapsed) {
			elapsed = per_thread_elapsed;
		}
	}
	printf("Total [Read/Write] => [%d/%d] elapsed: %lf\n", tot_read_count, tot_write_count, elapsed);
	printf("Total BW [Read/Write] => [%.2lf/%.2lf]MB/s\n",
			double(tot_read_count * arg_ctx.block_size) / 1024 / 1024 / elapsed, double(tot_write_count * arg_ctx.block_size) / 1024 / 1024 / elapsed);

    free(global_ssd_idx_lock);
	if (threads) free(threads);
	if(thread_args) free(thread_args);
	job_ctx_free(&job_ctx);
	return 0;

err:
    free(global_ssd_idx_lock);
	if (threads) free(threads);
	if(thread_args) free(thread_args);
	job_ctx_free(&job_ctx);
	return rc;
}