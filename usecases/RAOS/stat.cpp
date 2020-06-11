#include <pthread.h>
#include <cstdio>

#include "stat.hpp"

#define NUMBER_OF_RECENT_ELEMENTS 50
void update_ssd_stat_read(job_ctx_t *job_ctx, int block_size, int target_ssd_id, double elapsed) {
	pthread_mutex_lock(job_ctx->ssd_status[target_ssd_id].ssd_lock);
	job_ctx->ssd_status[target_ssd_id].read_elapsed_list->push_back(elapsed);
	job_ctx->ssd_status[target_ssd_id].all_read_elapsed_list->push_back(elapsed);

	if (job_ctx->ssd_status[target_ssd_id].read_elapsed_list->size() > NUMBER_OF_RECENT_ELEMENTS) {
		job_ctx->ssd_status[target_ssd_id].read_elapsed_list->pop_front();
	}
	job_ctx->ssd_status[target_ssd_id].read_data += block_size;
	pthread_mutex_unlock(job_ctx->ssd_status[target_ssd_id].ssd_lock);
}

void update_ssd_stat_write(job_ctx_t *job_ctx, int block_size, data_loc_t *dataloc, double *elapsed) {
	for (int replica = 0; replica < dataloc->num_replica; replica++) {
		int target_ssd_idx = dataloc->ssd_ids[replica];
		ssd_context_t *ssd_info = &job_ctx->ssd_status[target_ssd_idx];

		pthread_mutex_lock(ssd_info->ssd_lock);
		ssd_info->write_elapsed_list->push_back(elapsed[replica]);
		ssd_info->all_write_elapsed_list->push_back(elapsed[replica]);

		if (ssd_info->write_elapsed_list->size() > NUMBER_OF_RECENT_ELEMENTS) {
			ssd_info->write_elapsed_list->pop_front();
		}
		ssd_info->write_data += block_size;
		pthread_mutex_unlock(ssd_info->ssd_lock);
	}
}