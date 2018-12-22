#ifndef BAE_MODULE_INTERNAL_H
#define BAE_MODULE_INTERNAL_H

// maximum number of allocation volumes we support
#define ALLOC_VOLUMES 8
#define REQ_SAMPLING_NUMBER 20

#define REQ_TYPE_NORMAL 0
#define REQ_TYPE_BUFFER 1
#define REQ_TYPE_GC     2


#define CALIB_TYPE_UNKNOWN      (-1)
#define CALIB_TYPE_UNPREDICTED  (0)
#define CALIB_TYPE_PREDICTED_EBT    (1)
#define CALIB_TYPE_PREDICTED_TIMING (2)

/*
 * structure
 */
// inflight queue
struct inflight_elem_t {
    uint64_t lba;
    uint32_t data_len;
    char rw;
    uint64_t stime;
    struct list_head list;
};

// interval distribution (dictionary)
struct interval_dic_elem_t {
    uint32_t interval;
    int count;
    struct list_head list;
};

// interval list
struct interval_list_elem_t {
    uint32_t interval;
    struct list_head list;
};

struct interval_list_head_t {
    int total_count;
    struct list_head list;
};

// interval group distribution
//  (* compare to fixed slope threshold *)
struct interval_group_elem_t {
    uint32_t interval_start;
    uint32_t interval_end;
    int count;
    struct list_head list;
};

// normal latency sampling
struct latency_sample_t {
    uint64_t rlat[REQ_SAMPLING_NUMBER];
    int cur_idx;
    int total_count;
    uint64_t total_latency;
};

struct volume_info_t {
    int devid;

    int32_t current_request_in_buffer;
    uint32_t current_high_interval;
    uint32_t last_high_interval;

    uint64_t estimate_block_time;
    uint32_t buffer_backpressure;
    uint32_t preemptive_read;
    uint32_t preemptive_write;
    uint32_t explicit_flushing_counter;

    // FIXME we should use klist for thread-safety
    spinlock_t inflight_queue_lock;
    struct list_head inflight_queue;

    // FIXME we should use klist for thread-safety
    spinlock_t interval_dist_lock;
    struct list_head interval_dic;

    struct interval_list_head_t interval_list_head;

    struct list_head interval_group_dic;
};

struct ssd_info_t {
    //uint64_t low_hit;
    //uint64_t low_total;
    //uint64_t high_hit;
    //uint64_t high_total;
    //uint64_t all_hit;
    //uint64_t all_total;

    // normal latency sampling
    struct latency_sample_t normal_read_sample;
    struct latency_sample_t normal_write_sample;

    // volume info
    struct volume_info_t volume_info_arr[ALLOC_VOLUMES];
};

const char * const possibility_text[] = {
    "dev-0",
    "dev-1",
    "dev-2",
    "dev-3",
    "dev-4",
    "dev-5",
    "dev-6",
    "dev-7",
    "dev-8",
    "dev-9",
    "dev-10",
    "dev-11",
    "dev-12",
    "dev-13",
    "dev-14",
    "dev-15",
    "nvme",
};

#endif
