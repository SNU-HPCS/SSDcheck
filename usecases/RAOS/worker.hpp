#ifndef USECASE_OBJECT_STORAGE_WORKER_HPP
#define USECASE_OBJECT_STORAGE_WORKER_HPP

#include <pthread.h>
#include <chrono>
#include <list>

#include "utils.hpp"
#include "key_value_map.hpp"

using std::chrono::system_clock;

typedef enum {
    SSD_STATE_UNKNOWN = 0,
    SSD_STATE_INACTIVE,
    SSD_STATE_ACTIVE
} SSD_STATE_T;

typedef struct _ssd_context_t {
	int cur_ssd_id;
	uint64_t cur_offset;
	SSD_STATE_T state;

	// SSD lock
	pthread_mutex_t *ssd_lock;

	// To track ssd's status (ssd_lock should be taken to modify below variables)
	std::list<double> *read_elapsed_list;
	std::list<double> *write_elapsed_list;

	// Overall stats
	std::list<double> *all_read_elapsed_list;
	std::list<double> *all_write_elapsed_list;

	// To track an amount of written data
	uint64_t read_data;
	uint64_t write_data;
} ssd_context_t;

typedef struct _stat_info_t {
	system_clock::time_point start;
	system_clock::time_point end;

	int read_count;
	int write_count;
	uint64_t read_data;
	uint64_t write_data;
} stat_info_t;

typedef struct _job_ctx_t {
	pthread_barrier_t *worker_barrier;
	arg_ctx_t *arg_ctx;

	// SSD status
	ssd_context_t *ssd_status;

	// mapping table
	pthread_mutex_t *key_lock;
	int cur_key;
	int max_key_num;
	value_context_t *kv_map;

	// stat
	stat_info_t *stats;
}job_ctx_t;

typedef struct _thread_arg_t {
	int tid;
	job_ctx_t *global_ctx;
} thread_arg_t;

void *worker(void *data);

extern int global_ssd_idx;
extern pthread_mutex_t *global_ssd_idx_lock;

#endif //USECASE_OBJECT_STORAGE_WORKER_HPP
