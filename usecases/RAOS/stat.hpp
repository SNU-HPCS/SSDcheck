#ifndef USECASE_OBJECT_STORAGE_STAT_HPP
#define USECASE_OBJECT_STORAGE_STAT_HPP
#include "worker.hpp"

void update_ssd_stat_read(job_ctx_t *job_ctx, int block_size, int target_ssd_id, double elapsed);
void update_ssd_stat_write(job_ctx_t *job_ctx, int block_size, data_loc_t *dataloc, double *elapsed);

#endif //USECASE_OBJECT_STORAGE_STAT_HPP
