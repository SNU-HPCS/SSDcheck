/*
 * base module
 *  ToDo
 *   (1) ioctl and export function (Done)
 *   (2) separation between prediction routine and update routine (Done)
 *   (3) write_count modification
 *    (3-1) buffer calibration
 *    (3-2) backpressure
 */
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/proc_fs.h>
#include "base_module.h"
#include "base_module_internal.h"

MODULE_LICENSE("GPL");

/*
 * module param
 */
int dump_latency = 0;
module_param(dump_latency, int, 0644);
MODULE_PARM_DESC(dump_latency, "dump latency for extracting trace");

int dump_request = -1;
module_param(dump_request, int, 0644);
MODULE_PARM_DESC(dump_request, "dump each request for debugging purpose");

/*
 * static variables
 */
static struct ssd_info_t ssd_info_arr[DEV_ENTRY_NUM];
static struct dev_info_t* dev_info_arr[DEV_ENTRY_NUM];
static struct engine_info_t* engine_info_arr[DEV_ENTRY_NUM];

/*
 * function declaration
 */
static void dump_submit_request(int devid, uint32_t opcode, uint64_t lba, uint32_t data_len,
        uint64_t stime);
static void dump_complete_request(int devid, uint32_t opcode, uint64_t lba, uint32_t data_len,
        uint64_t stime, uint64_t ctime);

static void insert_inflight_queue(int devid, uint64_t lba, uint32_t data_len,
        char rw, uint64_t stime);
static int remove_request_in_inflight_queue(int devid, struct volume_info_t *volume_info,
        uint64_t lba, uint32_t data_len, uint64_t stime, uint64_t ctime);
static void update_internal_info(int devid, struct volume_info_t *volume_info,
        uint64_t lba, uint32_t data_len, char rw, uint64_t stime, uint64_t ctime);

static int get_volume_idx(int devid, uint64_t lba);
static int get_buffer_calibration_type(int devid, struct volume_info_t *volume_info, char rw, uint64_t stime, uint64_t ctime);

static void put_interval_in_dist(int devid, struct volume_info_t *volume_info, uint32_t cur_interval);
static void update_group_interval_dist(int devid, struct volume_info_t *volume_info);
static void sort_insert(struct interval_dic_elem_t *dic_elem,
        struct list_head *dic_list);
static void sort_insert_group(struct interval_group_elem_t *group_elem,
        struct list_head *group_list);

static inline int is_request_in_buffer(struct dev_info_t *dev_info, struct volume_info_t *volume_info)
{
    if ((volume_info->current_request_in_buffer % dev_info->buffer_size) != 0)
        return 1;
    else
        return 0;
}

static inline int is_explicit_flushing_triggered(struct dev_info_t *dev_info, struct volume_info_t *volume_info)
{
    if (unlikely(dev_info->buffer_size == 0))
        return 0;

    /*if (volume_info->explicit_flushing_counter == EXPLICIT_FLUSHING_COUNTER_MAX)*/
    if (volume_info->explicit_flushing_counter > 0)
        return 1;
    else
        return 0;
}

static inline void inc_explicit_flushing_counter(struct volume_info_t *volume_info)
{
    if (volume_info->explicit_flushing_counter < EXPLICIT_FLUSHING_COUNTER_MAX)
        volume_info->explicit_flushing_counter++;
}

static inline void dec_explicit_flushing_counter(struct volume_info_t *volume_info)
{
    if (volume_info->explicit_flushing_counter > 0)
        volume_info->explicit_flushing_counter--;
}

static inline void reset_explicit_flushing_counter(struct volume_info_t *volume_info)
{
    volume_info->explicit_flushing_counter = 0;
}

static int is_silent_flushing_triggered(int devid, struct volume_info_t *volume_info, int target_buffer_count);
static int is_buffer_flushing_triggered(int devid, int target_buffer_count);
static int is_known_gc_triggered(int devid, struct volume_info_t *volume_info, int target_high_interval);
/*static int is_unknown_gc_triggered(int devid, int volume_idx, char rw);*/
static int is_matched_marginal_interval(int devid, struct volume_info_t *volume_info, int target_interval);

static uint64_t get_temporal_ebt(struct volume_info_t *volume_info, uint64_t stime, uint64_t overhead);

// conservative updating (do not overwrite previous EBT)
static inline void update_estimate_block_time(int devid, struct volume_info_t *volume_info,
        uint64_t stime, uint64_t overhead) {
    uint64_t update_time = stime + overhead;

    if (volume_info->estimate_block_time < update_time) {
        volume_info->estimate_block_time = update_time;
        /*engine_info_arr[devid]->debug_estimate_block_time = volume_info->estimate_block_time;*/
    }
    return;
}

// explicitly update EBT (kind of reset)
static inline void update_estimate_block_time_force(int devid, struct volume_info_t *volume_info,
        uint64_t update_time) {
    volume_info->estimate_block_time = update_time;
    /*engine_info_arr[devid]->debug_estimate_block_time = volume_info->estimate_block_time;*/
    return;
}

static void update_normal_request_latency(int devid, char rw, uint64_t rlat);

void do_prediction(struct prediction_parv_t *prediction_parv);

// backpressure
static inline int is_buffer_backpressure(struct dev_info_t *dev_info, struct volume_info_t *volume_info)
{
    if (unlikely(dev_info->buffer_size == 0))
        return 0;
    
    if (volume_info->buffer_backpressure == BUFFER_BACKPRESSURE_MAX)
        return 1;
    else
        return 0;
}

static inline void inc_buffer_backpressure(struct volume_info_t *volume_info)
{
    if (volume_info->buffer_backpressure < BUFFER_BACKPRESSURE_MAX)
        volume_info->buffer_backpressure++;
}

static inline void dec_buffer_backpressure(struct volume_info_t *volume_info)
{
    if (volume_info->buffer_backpressure > 0)
        volume_info->buffer_backpressure--;
}

static inline void reset_buffer_backpressure(struct volume_info_t *volume_info)
{
    volume_info->buffer_backpressure = 0;
}

// read preemption
static inline int is_preemptive_read(struct dev_info_t *dev_info, struct volume_info_t *volume_info)
{
    if (unlikely(dev_info->buffer_size == 0))
        return 0;
    
    if (volume_info->preemptive_read == PREEMPTIVE_READ_MAX)
        return 1;
    else
        return 0;
}

static inline void inc_preemptive_read(struct volume_info_t *volume_info)
{
    if (volume_info->preemptive_read < PREEMPTIVE_READ_MAX)
        volume_info->preemptive_read++;
}

static inline void dec_preemptive_read(struct volume_info_t *volume_info)
{
    if (volume_info->preemptive_read > 0)
        volume_info->preemptive_read--;
}

static inline void reset_preemptive_read(struct volume_info_t *volume_info)
{
    volume_info->preemptive_read = 0;
}

// write preemption
static inline int is_preemptive_write(struct dev_info_t *dev_info, struct volume_info_t *volume_info)
{
    if (unlikely(dev_info->buffer_size == 0))
        return 0;
    
    if (volume_info->preemptive_write == PREEMPTIVE_WRITE_MAX)
        return 1;
    else
        return 0;
}

static inline void inc_preemptive_write(struct volume_info_t *volume_info)
{
    if (volume_info->preemptive_write < PREEMPTIVE_WRITE_MAX)
        volume_info->preemptive_write++;
}

static inline void dec_preemptive_write(struct volume_info_t *volume_info)
{
    if (volume_info->preemptive_write > 0)
        volume_info->preemptive_write--;
}

static inline void reset_preemptive_write(struct volume_info_t *volume_info)
{
    volume_info->preemptive_write = 0;
}


// thresholds
static inline uint64_t get_dram_lat_threshold(int devid) {
    return dev_info_arr[devid]->dram_lat_threshold;
}
static inline uint64_t get_normal_lat_threshold(int devid) {
    return dev_info_arr[devid]->normal_lat_threshold;
}
static inline uint64_t get_gc_lat_threshold(int devid) {
    return dev_info_arr[devid]->gc_lat_threshold;
}
static inline uint64_t get_buffer_overhead(int devid) {
    return dev_info_arr[devid]->buffer_overhead;
}
static inline uint64_t get_gc_overhead(int devid) {
    return dev_info_arr[devid]->gc_overhead;
}

static inline int get_slope_threshold(int devid) {
    return 10;
}
static inline int get_window_threshold(int devid) {
    return 20;
}
static inline int get_error_threshold(int devid) {
    return 1;
}
static inline int get_margin_threshold(int devid) {
    return 3; // -> 0.003
}

static inline int get_req_type(int devid, char rw, uint64_t rlat)
{
    int req_type;

    if (rlat >= get_gc_lat_threshold(devid))
        req_type = REQ_TYPE_GC;
    else if (rlat >= get_normal_lat_threshold(devid))
        req_type = REQ_TYPE_BUFFER;
    else {
        if (rw == 'w') {
            if (rlat >= get_dram_lat_threshold(devid))
                req_type = REQ_TYPE_BUFFER;
            else
                req_type = REQ_TYPE_NORMAL;
        } else {
            req_type = REQ_TYPE_NORMAL;
        }
    }

    return req_type;
}


// for clear the internal distribution
static void clear_dev_info(int devid);
static void clear_engine_info(int devid);
static void clear_ssd_info(int devid);
static void clear_volume_info(struct volume_info_t *volume_info);

// initalize
static void init_volume_info(int devid, struct volume_info_t *volune_info);
static void init_latency_sample(struct latency_sample_t *latency_sample);

/*
 * for debugfs (mmap)
 * this memory page is shared to a client
 */
static unsigned long page_addr = 0;

/*
 * export functions
 */
void add_submit_request(int devid, uint32_t opcode, uint64_t lbn, uint32_t data_len,
        uint64_t stime)
{
    char rw;
    int volume_idx;
    uint64_t lba = lbn * 512;
    struct volume_info_t *volume_info = NULL;
    struct dev_info_t *dev_info = dev_info_arr[devid];

    // dump (if dump_request param is devid)
    dump_submit_request(devid, opcode, lba, data_len, stime);

    if (dev_info->buffer_size == 0)
        return;

    // check opcode
    if (opcode == 42) {
        rw = 'w';
    } else if (opcode == 40) {
        rw = 'r';
    } else {
        // unknown request, ignore it
        return;
    }

    // extract volume index
    volume_idx = get_volume_idx(devid, lba);
    volume_info = &ssd_info_arr[devid].volume_info_arr[volume_idx];

    if (rw == 'w') {
        volume_info->current_request_in_buffer++;
        /*engine_info_arr[devid]->debug_current_request_in_buffer= volume_info->current_request_in_buffer;*/
    }

    // inflight_stime.append(stime)
    insert_inflight_queue(devid, lba, data_len, rw, stime);
    
    return;
}
EXPORT_SYMBOL(add_submit_request);

void add_complete_request(int devid, uint32_t opcode, uint64_t lbn, uint32_t data_len,
        uint64_t stime, uint64_t ctime)
{
    char rw;
    int volume_idx;
    uint64_t lba = lbn * 512;
    struct volume_info_t *volume_info = NULL;
    struct dev_info_t *dev_info = dev_info_arr[devid];

    // dump (if dump_request param is devid)
    dump_complete_request(devid, opcode, lba, data_len, stime, ctime);

    if (dev_info->buffer_size == 0)
        return;

    if (opcode == 42) {
        rw = 'w';
    } else if (opcode == 40) {
        rw = 'r';
    } else {
        // unknown request, ignore it
        return;
    }

    // extract volume index
    volume_idx = get_volume_idx(devid, lba);
    volume_info = &ssd_info_arr[devid].volume_info_arr[volume_idx];

    // remove request in inflight queue
    if (remove_request_in_inflight_queue(devid, volume_info,
                lba, data_len, stime, ctime)) {
        // FAIL to remove
        return;
    }

    // update interval distribution
    update_internal_info(devid, volume_info,
            lba, data_len, rw, stime, ctime);
}
EXPORT_SYMBOL(add_complete_request);

void clear_devid(int devid)
{
    clear_dev_info(devid);
    clear_engine_info(devid);
    clear_ssd_info(devid);
}
EXPORT_SYMBOL(clear_devid);

void clear_all(void)
{
    int i;
    for(i=0; i<DEV_ENTRY_NUM; i++) {
        clear_devid(i);
    }
    return;
}
EXPORT_SYMBOL(clear_all);

/*
 * static functions
 */
static void dump_submit_request(int devid, uint32_t opcode, uint64_t lba, uint32_t data_len,
        uint64_t stime)
{
    // we don't need to dump submit events
    return;
}

static void dump_complete_request(int devid, uint32_t opcode, uint64_t lba, uint32_t data_len,
        uint64_t stime, uint64_t ctime)
{
    if (dump_latency) {
        // dump traces
        if (dump_request == devid) {
            trace_printk("[DEBUG] %s, %d, %lld, %lld, %lld\n",
                    possibility_text[devid], opcode, lba, stime, ctime);
        }
    } else {
        /*if (dump_request == devid) {*/
            /*trace_printk("[DEBUG] %s, rw:%d, lba:%lld, data_len:%d, stime:%lld, ctime:%lld (rlat:%lld)\n",*/
                    /*possibility_text[devid], lba, data_len, stime, ctime, (ctime-stime));*/
        /*}*/
    }
    return;
}

/*
 * insert a new request in the tail
 */
static void insert_inflight_queue(int devid, uint64_t lba, uint32_t data_len,
        char rw, uint64_t stime)
{
    unsigned long flags;
    struct inflight_elem_t *inflight_elem;
    int volume_idx;
    struct volume_info_t *volume_info = NULL;

    // extract volume index
    volume_idx = get_volume_idx(devid, lba);
    volume_info = &ssd_info_arr[devid].volume_info_arr[volume_idx];


    // init elem
    inflight_elem = kmalloc(sizeof(struct inflight_elem_t),
            GFP_KERNEL);
    inflight_elem->lba = lba;
    inflight_elem->data_len = data_len;
    inflight_elem->rw = rw;
    inflight_elem->stime = stime;

    // insert an item into the queue
    spin_lock_irqsave(&volume_info->inflight_queue_lock, flags);
    list_add_tail(&inflight_elem->list, &volume_info->inflight_queue);
    spin_unlock_irqrestore(&volume_info->inflight_queue_lock, flags);
}

static int remove_request_in_inflight_queue(int devid, struct volume_info_t *volume_info,
        uint64_t lba, uint32_t data_len, uint64_t stime, uint64_t ctime)
{
    unsigned long flags;
    struct inflight_elem_t *inflight_elem, *if_next;
    int found = 0;
    int ret = 0;

    spin_lock_irqsave(&volume_info->inflight_queue_lock, flags);

    list_for_each_entry_safe (inflight_elem, if_next,
            &volume_info->inflight_queue,
            list) {
        if ((stime == inflight_elem->stime) &&
                (lba == inflight_elem->lba) &&
                (data_len == inflight_elem->data_len)) {
            list_del_init(&inflight_elem->list);
            kfree(inflight_elem);
            found = 1;
            break;
        }
    }


    if (unlikely(found == 0)) {
        int count = 0;

        printk(KERN_ERR "[DEBUG] <remove_request_in_inflight_queue> inflight queue empty!\n");
        printk(KERN_ERR "[DEBUG] devid:%d, lba:%lld, data_len:%d, stime:%lld, ctime:%lld, rlat:%lld\n",
                devid, lba, data_len, stime, ctime, ctime - stime);

        // dump inflight queue
        list_for_each_entry (inflight_elem,
                &volume_info->inflight_queue, list) {
            count++;
            printk(KERN_ERR "[DEBUG] lba:%lld, data_len:%d, stime:%lld\n",
                    inflight_elem->lba, inflight_elem->data_len, inflight_elem->stime);
        }
        printk(KERN_ERR "[DEBUG] queue count:%d\n", count);

        ret = -1;
        goto out;
    }
out:
    spin_unlock_irqrestore(&volume_info->inflight_queue_lock, flags);
    return ret;
}

/*
 * <internal info update>
 * [READ]
 *   => check explicit buffer flushing occurs
 *   => role of internal machine has cleared (delay resolved)
 *   <EBT update>
 *     => if incremental GC support, delayed read means (preemptive read execution), so there can huge latency overhead remains
 *     => ???
 * [WRITE]
 *   => buffer calibration
 *     - silent update (no overhead detected)
 *       -> reset/decrease backpressure counter
 *       <EBT update>
 *         -> if 
 *     - calibration (overhead detected, do counter calibration)
 *       -> increase backpressure counter
 *   <EBT update>
 *     => if 
 * 
 */
static void update_internal_info(int devid, struct volume_info_t *volume_info,
        uint64_t lba, uint32_t data_len, char rw, uint64_t stime, uint64_t ctime)
{
    struct dev_info_t *dev_info = dev_info_arr[devid];
    /*struct engine_info_t *engine_info = engine_info_arr[devid];*/
    /*uint64_t normal_lat_threshold = get_normal_lat_threshold(devid);*/
    /*uint64_t gc_lat_threshold = get_gc_lat_threshold(devid);*/
    /*uint64_t dram_lat_threshold = get_dram_lat_threshold(devid);*/
    uint64_t rlat = ctime - stime;

    int req_type;
    // silent_flushing = 1 => maybe flushing occurs.
    // silent_flushing = 0 => flushing strongly occurs.
    int is_buffer_flushing_occurred = 0, is_silent_flushing = 0;
    int is_gc_occurred = 0;
    int is_ebt_reset = 0;
    int is_ebt_overhead_updated = 0;
    int is_guess_buffer_flushing = 0;
    int is_buf_counter_reset = 0;

    //////////////////
    // check parameters
    //////////////////
    if (dev_info->buffer_size == 0)
        return;

    //////////////////
    // type of request
    /////////////////
    req_type = get_req_type(devid, rw, rlat);

    //////////////////////////
    // update latency to support dynamic latency
    // (FIXME) => current version only supports normal latency
    //////////////////////////
    switch (req_type) {
        case REQ_TYPE_NORMAL:
            update_normal_request_latency(devid, rw, rlat);
            break;
        case REQ_TYPE_BUFFER:
        case REQ_TYPE_GC:
            break;
    }

    //////////////////////////
    // internal info update
    //////////////////////////
    if (rw == 'r') { // [READ]
        if (dev_info->explicit_buffer_flushing) {
            is_ebt_reset = 1;
            switch (req_type) {
                case REQ_TYPE_NORMAL:
                    break;
                case REQ_TYPE_BUFFER:
                case REQ_TYPE_GC:
                    if (is_request_in_buffer(dev_info, volume_info)) {
                        is_buffer_flushing_occurred = 1;
                        is_silent_flushing = 0;
                    }
                    break;
            }
        } else {
            // incremental GC should be implemented in here???
            /*is_ebt_reset = 1;*/
            switch (req_type) {
                case REQ_TYPE_NORMAL:
                    is_ebt_reset = 1;
                    dec_preemptive_read(volume_info);
                    break;
                case REQ_TYPE_BUFFER:
                case REQ_TYPE_GC:
                    //  * incremental GC (read first SSD, a.k.a. preemptive read operation)
                    //    - read request is slowdown, 
                    // slowdown occurs
                    //  (1) check calibration is needed
                    inc_preemptive_read(volume_info);

                    if (is_preemptive_read(dev_info, volume_info)) {
                        // we don't need to increase high interval
                        is_buf_counter_reset = 1;
                        /*is_gc_occurred = 1;*/
                    } else {
                        // preemptive read does not trigger yet,
                        // do buffer calibration
                        is_buffer_flushing_occurred = 1;
                        switch (get_buffer_calibration_type(devid, volume_info, rw, stime, ctime)) {
                            case CALIB_TYPE_PREDICTED_TIMING:
                                is_silent_flushing = 0;
                                /*inc_buffer_backpressure(volume_info);*/
                                break;
                            case CALIB_TYPE_PREDICTED_EBT:
                                is_silent_flushing = 1;
                                /*inc_buffer_backpressure(volume_info);*/
                                break;
                            case CALIB_TYPE_UNPREDICTED:
                                is_silent_flushing = 0; // explicit reset
                                /*dec_buffer_backpressure(volume_info);*/
                                /*reset_buffer_backpressure(volume_info);*/
                                break;
                            case CALIB_TYPE_UNKNOWN:
                                printk(KERN_ERR "[DEBUG] ERR unknown calibration type\n");
                                return;
                        }
                    }

                    // orthogonal, if latency is really high, we can say gc is triggered
                    if (req_type == REQ_TYPE_GC) {
                        is_gc_occurred = 1;
                    }
                    break;
            }
        }
    } else { // [WRITE]
        switch (req_type) {
            case REQ_TYPE_NORMAL:
                if (is_buffer_flushing_triggered(devid, volume_info->current_request_in_buffer)) {
                    is_buffer_flushing_occurred = 1;
                    is_guess_buffer_flushing = 1;
                    is_ebt_overhead_updated = 1;
                } else if (is_silent_flushing_triggered(devid, volume_info, volume_info->current_request_in_buffer)) {
                    trace_printk("[normal] silent flushing triggered (%d)\n", volume_info->current_request_in_buffer);
                    is_buffer_flushing_occurred = 1;
                    is_silent_flushing = 1;
                    dec_buffer_backpressure(volume_info);
                    /*reset_buffer_backpressure(volume_info);*/
                    /*is_ebt_overhead_updated = 1;*/
                }

                dec_preemptive_write(volume_info);
                break;
            case REQ_TYPE_BUFFER:
            case REQ_TYPE_GC:
                // slowdown occurs
                // (1) check calibration is needed
                // (2) update backpressure
                inc_preemptive_write(volume_info);
                if (is_preemptive_write(dev_info, volume_info)) {
                    // we don't need to increase high interval
                    is_buf_counter_reset = 1;
                } else {
                    is_buffer_flushing_occurred = 1;
                    switch (get_buffer_calibration_type(devid, volume_info, rw, stime, ctime)) {
                        case CALIB_TYPE_PREDICTED_TIMING:
                            trace_printk("[DEBUG] timing current write count:%d\n",volume_info->current_request_in_buffer);
                            is_silent_flushing = 0;
                            inc_buffer_backpressure(volume_info);
                            break;
                        case CALIB_TYPE_PREDICTED_EBT:
                            trace_printk("[DEBUG] EBT current write count:%d\n",volume_info->current_request_in_buffer);
                            //is_silent_flushing = 1;
                            is_silent_flushing = 0; // high-latency write -> backpressure and no silent flushing
                            inc_buffer_backpressure(volume_info);
                            break;
                        case CALIB_TYPE_UNPREDICTED:
                            is_silent_flushing = 0; // explicit reset
                            dec_buffer_backpressure(volume_info);
                            /*reset_buffer_backpressure(volume_info);*/
                            trace_printk("[JIHUN] CALIB_TYPE_UNPREDICTED\n");
                            break;
                        case CALIB_TYPE_UNKNOWN:
                            trace_printk("[DEBUG] ERR unknown calibration type\n");
                            return;
                    }
                }

                if (req_type == REQ_TYPE_GC) {
                    is_gc_occurred = 1;
                }
                break;
        }
    }

    //////////////////////////
    // update various counters
    //////////////////////////
    // preemptive logic
    if (is_buf_counter_reset) {
        volume_info->current_request_in_buffer = 0;
    }

    if (is_buffer_flushing_occurred && !is_guess_buffer_flushing) {
        // increase interval when buffer flushing occurs
        //  (1) silent flushing
        //  (2) explicit flushing
        volume_info->current_high_interval++;
        /*engine_info->debug_current_high_interval = volume_info->current_high_interval;*/

        // reset buffer counter
        if (is_silent_flushing) {
            volume_info->current_request_in_buffer %= dev_info->buffer_size;
            if ((int)dev_info->buffer_size - volume_info->current_request_in_buffer <= get_error_threshold(devid))
                volume_info->current_request_in_buffer -= (int)dev_info->buffer_size;
        } else {
            volume_info->current_request_in_buffer = 0;
        }
        /*engine_info->debug_current_request_in_buffer = volume_info->current_request_in_buffer;*/
    }

    if (is_gc_occurred) {
        // (1) put high interval in the interval distribution
        put_interval_in_dist(devid, volume_info, volume_info->current_high_interval);

        // (2) reset current high interval and store previous high interval
        volume_info->last_high_interval = volume_info->current_high_interval;
        /*engine_info->debug_last_high_interval = volume_info->last_high_interval;*/
        volume_info->current_high_interval = 0;
        /*engine_info->debug_current_high_interval = volume_info->current_high_interval;*/
    }

    //////////////////////////
    // update estimate_block_time
    //////////////////////////
    if (is_ebt_reset) {
        update_estimate_block_time_force(devid, volume_info, ctime);
    } else if (is_ebt_overhead_updated) {
        // add overhead
        if (is_buffer_flushing_occurred) {
            if (is_known_gc_triggered(devid, volume_info, volume_info->current_high_interval)) {
                update_estimate_block_time(devid, volume_info, stime, get_gc_overhead(devid));
            } else {
                update_estimate_block_time(devid, volume_info, stime, get_buffer_overhead(devid));
            }
        }
    }

    return;
}

static int get_volume_idx(int devid, uint64_t lba) {
    int bitval = 0;
    int res_idx = 0;
    int i;

    for (i=0; i<3; i++) {
        if (dev_info_arr[devid]->bitidx_list[i] == 0)
            break;

        bitval = ((lba & (1 << dev_info_arr[devid]->bitidx_list[i])) >> dev_info_arr[devid]->bitidx_list[i]);
        res_idx += bitval << i;
    }

    return res_idx;
}

/*
 * buffer calibration (version 1)
 *   => do calibration by using buffer counters
 */
/*
static int get_buffer_calibration_type(int devid, struct volume_info_t *volume_info)
{
    int buffer_size = dev_info_arr[devid]->buffer_size;
    int req_remainder = volume_info->current_request_in_buffer % buffer_size;
    int error_threshold = get_error_threshold(devid);


    if (unlikely(buffer_size == 0))
        return CALIB_TYPE_UNKNOWN;

    if (volume_info->current_request_in_buffer < (buffer_size - error_threshold))
        return CALIB_TYPE_UNPREDICTED;
    else if (req_remainder >= 0 &&
            req_remainder <= error_threshold)
        return CALIB_TYPE_PREDICTED;
    else if (req_remainder >= (buffer_size - error_threshold) &&
            req_remainder < buffer_size)
        return CALIB_TYPE_PREDICTED;
    else
        return CALIB_TYPE_UNPREDICTED;
}
*/

/*
 * buffer calibration (version 2)
 *   => do calibration by using buffer counters
 */
static int get_buffer_calibration_type(int devid, struct volume_info_t *volume_info, char rw, uint64_t stime, uint64_t ctime)
{
    int buffer_size = dev_info_arr[devid]->buffer_size;
    int req_remainder = volume_info->current_request_in_buffer % buffer_size;
    int error_threshold = get_error_threshold(devid);
    uint64_t normal_lat_threshold = get_normal_lat_threshold(devid);
    int64_t predicted_lat = (int64_t)volume_info->estimate_block_time - (int64_t)stime;
    uint64_t rlat = ctime - stime;

    if (unlikely(buffer_size == 0))
        return CALIB_TYPE_UNKNOWN;
    if (get_req_type(devid, rw, rlat) == REQ_TYPE_NORMAL)
        return CALIB_TYPE_UNKNOWN;

    // this is triggered when slowdown is detected
    //   FIXME: this version only works on IODEPTH 1
    if (predicted_lat >= (int64_t)normal_lat_threshold) {  // correctly predicted
        if (rw == 'r'){ 
            return CALIB_TYPE_PREDICTED_EBT;
        } else {
            if (volume_info->current_request_in_buffer > buffer_size + error_threshold)
                return CALIB_TYPE_UNPREDICTED;
            else
                return CALIB_TYPE_PREDICTED_EBT;
        }
    } else {
        // timing of buffer flushed?
        if (volume_info->current_request_in_buffer < (buffer_size - error_threshold)) {
            if (volume_info->current_request_in_buffer < error_threshold && rw == 'w') {
                // subsequent write slowdown (write preemptive)
                trace_printk("ignore write preemptive request\n");
                return CALIB_TYPE_PREDICTED_EBT;
            } else {
                trace_printk("UNPREDICTED (early flush)\n");
                return CALIB_TYPE_UNPREDICTED;
            }
        } else if (req_remainder >= 0 &&
                req_remainder <= error_threshold)
            return CALIB_TYPE_PREDICTED_TIMING;
        else if (req_remainder >= (buffer_size - error_threshold) &&
                req_remainder < buffer_size)
            return CALIB_TYPE_PREDICTED_TIMING;
        else if (req_remainder == error_threshold + 1) { // last available buffer_counter
            return CALIB_TYPE_UNPREDICTED;
        } else { // should be never happened
            printk(KERN_ERR "[ERROR] this routine shoulbe be never called (devid:%d, rw:%c, buf_count:%d, stime:%lld\n",
                    devid, rw, volume_info->current_request_in_buffer, stime);
            return CALIB_TYPE_UNPREDICTED;
        }
    }
}

/*
 * (1) put interval in a distribution (scope)
 * (2) add interval in interval_list (for window size)
 * (3) update interval group distribution
 */
static void put_interval_in_dist(int devid, struct volume_info_t *volume_info, uint32_t cur_interval)
{
    struct interval_dic_elem_t *interval_dic_elem, *tmp_dic;
    struct interval_list_elem_t *interval_list_elem;
    int found = 0;
    unsigned long flags;

    spin_lock_irqsave(&volume_info->interval_dist_lock, flags);

    /*
     * insert an interval into a dictionary.
     * (insertion sort)
     */
    list_for_each_entry (interval_dic_elem, 
            &volume_info->interval_dic, list) {
        if (interval_dic_elem->interval == cur_interval) {
            found = 1;
            interval_dic_elem->count += 1;
            break;
        }
    }
    if (!found) {
        tmp_dic = kmalloc(sizeof(struct interval_dic_elem_t),
                GFP_KERNEL);
        tmp_dic->interval = cur_interval;
        tmp_dic->count = 1;
        sort_insert(tmp_dic, &volume_info->interval_dic);
    }

    /*
     * add interval in the interval_list
     * check window size
     */
    interval_list_elem = kmalloc(sizeof(struct interval_list_elem_t),
            GFP_KERNEL);
    interval_list_elem->interval = cur_interval;
    list_add_tail(&interval_list_elem->list, &volume_info->interval_list_head.list);
    volume_info->interval_list_head.total_count += 1;

    if (volume_info->interval_list_head.total_count > get_window_threshold(devid)) {
        struct interval_list_elem_t *t_elem;
        struct interval_dic_elem_t *t_dic_elem, *t_dic_elem_next;

        // evict an oldest interval in the list
        t_elem = list_first_entry(&volume_info->interval_list_head.list,
                struct interval_list_elem_t, list);
        list_del_init(&t_elem->list);

        // evict an interval in the dictionary
        list_for_each_entry_safe(t_dic_elem, t_dic_elem_next,
                &volume_info->interval_dic, list) {
            if (t_dic_elem->interval == t_elem->interval) {
                if (t_dic_elem->count > 1) {
                    t_dic_elem->count--;
                } else if (t_dic_elem->count == 1) {
                    list_del_init(&t_dic_elem->list);
                    kfree(t_dic_elem);
                } else {
                    printk(KERN_ERR "[DEBUG] <put_interval_in_dist> failed!\n");
                }
                break;
            }
        }
        // decrease the total_count
        volume_info->interval_list_head.total_count -= 1;
        kfree(t_elem);
    }

    update_group_interval_dist(devid, volume_info);

    spin_unlock_irqrestore(&volume_info->interval_dist_lock, flags);
}

static void update_group_interval_dist(int devid, struct volume_info_t *volume_info)
{
    /*unsigned long flags;*/
    struct interval_group_elem_t *group_elem, *tmp_group_elem;
    struct interval_dic_elem_t *dic;
    uint32_t g_start = 0, g_end = 0, prev_interval;
    int g_count = 0, is_grouping = 0;

    /*spin_lock_irqsave(&volume_info->interval_group_dic_lock, flags);*/

    // clear all group interval
    list_for_each_entry_safe(group_elem, tmp_group_elem,
            &volume_info->interval_group_dic, list) {
        list_del_init(&group_elem->list);
        kfree(group_elem);
    }

    // reconstruct group interval distribution
    list_for_each_entry(dic,
            &volume_info->interval_group_dic, list) {
        if (!is_grouping) {
            prev_interval = dic->interval;

            is_grouping = 1;
            g_start = dic->interval;
            g_end = dic->interval;
            g_count = dic->count;
        } else {
            uint32_t interval_diff = dic->interval - prev_interval;
            int count_diff = dic->count;

            int slope = count_diff / interval_diff;
            if (slope >= get_slope_threshold(devid)) {
                prev_interval = dic->interval;
                g_end = dic->interval;
                g_count += dic->count;
            } else {
                struct interval_group_elem_t *new_group;
                new_group = kmalloc(sizeof(struct interval_group_elem_t),
                        GFP_KERNEL);
                new_group->interval_start = g_start;
                new_group->interval_end = g_end;
                new_group->count = g_count;
                sort_insert_group(new_group, &volume_info->interval_group_dic);

                is_grouping = 1;
                g_start = dic->interval;
                g_end = dic->interval;
                g_count = dic->count;
                prev_interval = dic->interval;
            }
        }
    }

    if (is_grouping) {
        struct interval_group_elem_t *new_group;
        new_group = kmalloc(sizeof(struct interval_group_elem_t),
                GFP_KERNEL);
        new_group->interval_start = g_start;
        new_group->interval_end = g_end;
        new_group->count = g_count;
        sort_insert_group(new_group, &volume_info->interval_group_dic);
        is_grouping = 0;
    }

    /*spin_unlock_irqrestore(&volume_info->interval_group_dic_lock, flags);*/
}

static void sort_insert(struct interval_dic_elem_t *dic_elem,
        struct list_head *dic_list)
{
    struct interval_dic_elem_t *t_dic_elem;
    // find first (t_dic_elem->interval > dic_elem->interval)
    list_for_each_entry(t_dic_elem,
            dic_list, list) {
        if (t_dic_elem->interval > dic_elem->interval)
            break;
    }
    list_add_tail(&dic_elem->list, &t_dic_elem->list);
}

static void sort_insert_group(struct interval_group_elem_t *group_elem,
        struct list_head *group_list)
{
    struct interval_group_elem_t *t_group_elem;

    list_for_each_entry(t_group_elem,
            group_list, list) {
        if (t_group_elem->interval_start > group_elem->interval_end)
           break; 
    }
    list_add_tail(&group_elem->list, &t_group_elem->list);
}

/*
 * To detect "uncovered buffer flushing"
 *  => if "buffer_count is larger than buffer_size" and
 *        "buffer_count overflows then error_threshold"
 */
static int is_silent_flushing_triggered(int devid, struct volume_info_t *volume_info, int target_buffer_count)
{
    uint32_t buffer_size = dev_info_arr[devid]->buffer_size;
    int req_remainder = target_buffer_count % buffer_size;
    int error_threshold = get_error_threshold(devid);

    if (unlikely(buffer_size == 0))
        return 0;

    if (target_buffer_count > buffer_size &&
            req_remainder >= (error_threshold + 1))
        return 1;
    else
        return 0;
}

/*
 * target_buffer_count
 */
static int is_buffer_flushing_triggered(int devid, int target_buffer_count)
{
    uint32_t buffer_size = dev_info_arr[devid]->buffer_size;
    int req_remainder = target_buffer_count % buffer_size;
    int error_threshold = get_error_threshold(devid);

    if (unlikely(buffer_size == 0))
        return 0;

    if (target_buffer_count < (buffer_size - error_threshold))
        return 0;

    if (req_remainder >= 0 &&
            req_remainder <= error_threshold) { // HIT
        return 1;
    } else if (req_remainder >= (buffer_size - error_threshold) &&
            req_remainder < buffer_size) { // HIT
        return 1;
    } else
        return 0;
}

static int is_known_gc_triggered(int devid, struct volume_info_t *volume_info, int target_high_interval) {
    uint32_t combined_high_interval;

    // Normal interval checking
    if (is_matched_marginal_interval(devid, volume_info, target_high_interval)) {
        return 1;
    }

    // Combination interval checking
    /*if (target_high_interval >= volume_info->last_high_interval) <= (to JH) I don't know why this condition is necessary???????*/
    combined_high_interval = target_high_interval + volume_info->last_high_interval;
    if (is_matched_marginal_interval(devid, volume_info, combined_high_interval)) {
        return 1;
    }

    return 0;
}

/*static int is_unknown_gc_triggered(int devid, int volume_idx, char rw) {*/
    /*int diff_high_write_count;*/
    /*struct volume_info_t *volume_info = NULL;*/
    /*volume_info = &ssd_info_arr[devid].volume_info_arr[volume_idx];*/

    /*diff_high_write_count = volume_info->curr_write_count - volume_info->prev_high_write_count;*/

    /*if (rw == 'r' && diff_high_write_count == 0)*/
        /*return 1;*/
    /*else if (rw == 'w' && diff_high_write_count <= 1)*/
        /*return 1;*/
    /*//else*/
    /*return 0;*/
/*}*/

static int is_matched_marginal_interval(int devid, struct volume_info_t *volume_info, int target_interval) {
    struct interval_group_elem_t *cur_dic_group;

    list_for_each_entry(cur_dic_group,
            &volume_info->interval_group_dic, list) {
        uint32_t i_start = cur_dic_group->interval_start;
        uint32_t i_end = cur_dic_group->interval_end;
        uint32_t diff_interval_start = i_start * get_margin_threshold(devid) / 1000;
        uint32_t diff_interval_end = i_end * get_margin_threshold(devid) / 1000;
        uint32_t margin_start = i_start - diff_interval_start;
        uint32_t margin_end = i_end + diff_interval_end;

        if (margin_start <= target_interval && target_interval <= margin_end)
            return 1;
    }

    return 0;
}

static uint64_t get_temporal_ebt(struct volume_info_t *volume_info, uint64_t stime, uint64_t overhead)
{
    uint64_t update_time = stime + overhead;

    if (volume_info->estimate_block_time < update_time) {
        return update_time;
    } else if (volume_info->estimate_block_time > stime) {
        return volume_info->estimate_block_time;
    } else {
        return stime;
    }
}



static void update_normal_request_latency(int devid, char rw, uint64_t rlat) {
    struct latency_sample_t *latency_sample = NULL;
    
    if (rw == 'r') {
        if (rlat < get_dram_lat_threshold(devid)) {
            return;
        }
        latency_sample = &ssd_info_arr[devid].normal_read_sample;
        /*engine_info_arr[devid]->debug_normal_read_lat = rlat;*/
    }
    else { // if (rw == 'w')
        latency_sample = &ssd_info_arr[devid].normal_write_sample;
        /*engine_info_arr[devid]->debug_normal_write_lat = rlat;*/
    }

    if (latency_sample->total_count == REQ_SAMPLING_NUMBER) {
        /*0 1 2 3 4 5 6 7 8 9*/
        // remove oldest latency
        latency_sample->total_latency -= latency_sample->rlat[latency_sample->cur_idx];

        // add new latency
        latency_sample->rlat[latency_sample->cur_idx] = rlat;
        latency_sample->total_latency += rlat;
        latency_sample->cur_idx++;

        if (latency_sample->cur_idx >= REQ_SAMPLING_NUMBER) {
            latency_sample->cur_idx = latency_sample->cur_idx % REQ_SAMPLING_NUMBER;
        }
    } else {
        latency_sample->rlat[latency_sample->cur_idx] = rlat;
        latency_sample->cur_idx++;
        latency_sample->total_count++;
        latency_sample->total_latency += rlat;

        if (unlikely(latency_sample->cur_idx >= REQ_SAMPLING_NUMBER)) {
            latency_sample->cur_idx = latency_sample->cur_idx % REQ_SAMPLING_NUMBER;
        }
    }

    if (rw == 'r') {
        engine_info_arr[devid]->normal_read_latency = latency_sample->total_latency / latency_sample->total_count;
    }
    else {
        engine_info_arr[devid]->normal_write_latency = latency_sample->total_latency / latency_sample->total_count;
    }

    return;
}

/* prediction engine
 *  calculated predicted latency with temporal counters
 *     ==> (Assume when the incoming request is executed)
 *
 *  [READ]
 *    => Check EBT (whether slowdown occurs or not)
 *    => if (explict buffer flushing (it leads to slowdown
 *
 *  [WRITE]
 */
void do_prediction(struct prediction_parv_t *prediction_parv) {
    int devid = prediction_parv->devid;
    uint64_t lba = prediction_parv->lba;
    int volume_idx = get_volume_idx(devid, lba);
    struct volume_info_t *volume_info = &ssd_info_arr[devid].volume_info_arr[volume_idx];
    /*uint32_t data_len = prediction_parv->data_len;*/
    char rw = prediction_parv->rw;
    uint64_t stime = prediction_parv->stime;
    uint64_t predicted_latency = 0;
    struct dev_info_t *dev_info = dev_info_arr[devid];
    struct engine_info_t *engine_info = engine_info_arr[devid];
    uint64_t t_est = 0, t_ebt = 0;
    int buffer_flushing = 0;
    int preemptive_read = 0;
    int preemptive_write = 0;

    // (0) initialization
    //    (set volume_idx)
    //    (set debug info)
    prediction_parv->volume_idx = volume_idx;
    engine_info->debug_buffer_backpressure = volume_info->buffer_backpressure;
    engine_info->debug_preemptive_read = volume_info->preemptive_read;
    engine_info->debug_preemptive_write = volume_info->preemptive_write;
    engine_info->debug_current_request_in_buffer = volume_info->current_request_in_buffer;
    engine_info->debug_current_high_interval = volume_info->current_high_interval;
    engine_info->debug_last_high_interval = volume_info->last_high_interval;
    engine_info->debug_estimate_block_time = volume_info->estimate_block_time;
    /*engine_info->debug_normal_read_lat = engine_info->normal_read_latency;*/

    // (1) calculate estimate block time
    if (rw == 'r') {
        // (1-1) [READ] slowdown might occur (if buffer is not empty)
        if (dev_info->explicit_buffer_flushing) {
            if (is_request_in_buffer(dev_info, volume_info))
                buffer_flushing = 1;
        } else {
            if (is_preemptive_read(dev_info, volume_info)) {
                // FIXME, we need to model, read latency for preemptive read request
                // currently just use buffer flushing overheads
                preemptive_read = 1;
            }
        }
    } else if (rw == 'w') {
        if (is_preemptive_write(dev_info, volume_info)) {
            preemptive_write = 1;
        } else {
            // (1-2) [WRITE] slowdown migh occur when buffer backpressure on
            if (dev_info->buffer_type == BUFFER_TYPE_SINGLE) {
                if (is_buffer_flushing_triggered(devid, volume_info->current_request_in_buffer + 1))
                    buffer_flushing = 1;
            } else if (dev_info->buffer_type == BUFFER_TYPE_DOUBLE) {
                if (is_buffer_flushing_triggered(devid, volume_info->current_request_in_buffer + 1) &&
                        is_buffer_backpressure(dev_info, volume_info))
                    buffer_flushing = 1;
            }
        }
    } else {
        // unknown rw ==> return latency 0
        predicted_latency = 0;
        goto out;
    }
    
    if (buffer_flushing) {
        if (is_known_gc_triggered(devid, volume_info, volume_info->current_high_interval + 1)) {
            t_ebt = get_temporal_ebt(volume_info, stime, dev_info->gc_overhead);
        } else {
            t_ebt = get_temporal_ebt(volume_info, stime, dev_info->buffer_overhead);
        }
    } else if (preemptive_read || preemptive_write) {
        t_ebt = get_temporal_ebt(volume_info, stime, dev_info->buffer_overhead);
    } else {
        if (rw == 'w'&&
                dev_info->buffer_type == BUFFER_TYPE_DOUBLE) { // double buffer
            t_ebt = stime;
        } else {
            if (volume_info->estimate_block_time > stime)
                t_ebt = volume_info->estimate_block_time;
            else
                t_ebt = stime;
        }
    }

    // sanity check
    if (unlikely (t_ebt < stime)) {
        printk (KERN_ERR "[DEBUG] t_ebt cannot be smaller than stime\n");
        predicted_latency = 0;
        goto out;
    }

    // (2) calculate estimate end time
    if (rw == 'r') {
        t_est = t_ebt + engine_info->normal_read_latency;
    } else {
        t_est = t_ebt + engine_info->normal_write_latency;
    }

    // (3) cacluate latency prediction
    predicted_latency = t_est - stime;

out:
    prediction_parv->predicted_latency = predicted_latency;
    return;
}
EXPORT_SYMBOL(do_prediction);


static void clear_dev_info(int devid)
{
    memset(dev_info_arr[devid], 0, sizeof(struct dev_info_t));
    return;
}

static void clear_engine_info(int devid)
{
    memset(engine_info_arr[devid], 0, sizeof(struct engine_info_t));
    return;
}

static void clear_ssd_info(int devid)
{
    int i;

    // init_latency_sample
    init_latency_sample(&ssd_info_arr[devid].normal_read_sample);
    init_latency_sample(&ssd_info_arr[devid].normal_write_sample);

    // init volume info
    for (i=0; i < ALLOC_VOLUMES; i++) {
        clear_volume_info(&ssd_info_arr[devid].volume_info_arr[i]);
    }
}

static void clear_volume_info(struct volume_info_t *volume_info)
{
    struct inflight_elem_t *if_elem, *if_tmp;
    struct interval_dic_elem_t *dic_elem, *dic_tmp;
    struct interval_list_elem_t *list_elem, *list_tmp;
    struct interval_group_elem_t *group_elem, *group_tmp;

    volume_info->devid = 0;

    volume_info->current_request_in_buffer = 0;
    volume_info->current_high_interval = 0;
    volume_info->last_high_interval = 0;
    
    volume_info->estimate_block_time = 0;
    volume_info->buffer_backpressure = 0;

    // clear inflight queue
    list_for_each_entry_safe (if_elem, if_tmp,
            &volume_info->inflight_queue, list) {
        list_del_init(&if_elem->list);
        kfree(if_elem);
    }

    // clear interval dic
    list_for_each_entry_safe (dic_elem, dic_tmp,
            &volume_info->interval_dic, list) {
        list_del_init(&dic_elem->list);
        kfree(dic_elem);
    }

    // clear interval list
    list_for_each_entry_safe(list_elem, list_tmp,
            &volume_info->interval_list_head.list, list) {
        list_del_init(&list_elem->list);
        kfree(list_elem);
    }
    volume_info->interval_list_head.total_count = 0;

    // clear interval group dic
    list_for_each_entry_safe(group_elem, group_tmp,
            &volume_info->interval_group_dic, list) {
        list_del_init(&group_elem->list);
        kfree(group_elem);
    }

    return;
}


/*****************************************
 * debug fs (for mmap)
 *****************************************/
#ifndef VM_RESERVED
# define  VM_RESERVED   (VM_DONTEXPAND | VM_DONTDUMP)
#endif
static struct dentry *debugfs_file;

struct mmap_info
{
    int reference;      
};

void mmap_open(struct vm_area_struct *vma)
{
    struct mmap_info *info = (struct mmap_info *)vma->vm_private_data;
    info->reference++;
}

void mmap_close(struct vm_area_struct *vma)
{
    struct mmap_info *info = (struct mmap_info *)vma->vm_private_data;
    info->reference--;
}

static int mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    struct page *page;
    struct mmap_info *info;

    info = (struct mmap_info *)vma->vm_private_data;
    if (!page_addr) {
        printk("No data\n");
        return 0;    
    }

    page = virt_to_page(page_addr);

    get_page(page);
    vmf->page = page;

    return 0;
}

struct vm_operations_struct mmap_vm_ops =
{
    .open =     mmap_open,
    .close =    mmap_close,
    .fault =    mmap_fault,    
};

int op_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &mmap_vm_ops;
    vma->vm_flags |= VM_RESERVED;    
    vma->vm_private_data = filp->private_data;
    mmap_open(vma);
    return 0;
}

int mmapfop_close(struct inode *inode, struct file *filp)
{
    struct mmap_info *info = filp->private_data;

    kfree(info);
    filp->private_data = NULL;
    return 0;
}

int mmapfop_open(struct inode *inode, struct file *filp)
{
    struct mmap_info *info = kmalloc(sizeof(struct mmap_info), GFP_KERNEL);    
    filp->private_data = info;
    return 0;
}

static const struct file_operations mmap_fops = {
    .open = mmapfop_open,
    .release = mmapfop_close,
    .mmap = op_mmap,
};

static void init_volume_info(int devid, struct volume_info_t *volume_info)
{
    volume_info->devid = devid;

    volume_info->current_request_in_buffer = 0;
    volume_info->current_high_interval = 0;
    volume_info->last_high_interval = 0;
    
    volume_info->estimate_block_time = 0;
    volume_info->buffer_backpressure = 0;
 
    volume_info->preemptive_read = 0;
    volume_info->preemptive_write = 0;
    volume_info->explicit_flushing_counter = 0;   

    // init lock and list
    INIT_LIST_HEAD(&volume_info->inflight_queue);
    spin_lock_init(&volume_info->inflight_queue_lock);

    INIT_LIST_HEAD(&volume_info->interval_dic);
    INIT_LIST_HEAD(&volume_info->interval_list_head.list);
    volume_info->interval_list_head.total_count = 0;
    INIT_LIST_HEAD(&volume_info->interval_group_dic);
    spin_lock_init(&volume_info->interval_dist_lock);
}

static void init_latency_sample(struct latency_sample_t *latency_sample)
{
    latency_sample->total_count = 0;
    latency_sample->total_latency = 0;
    memset(latency_sample->rlat, 0, sizeof(uint64_t) * REQ_SAMPLING_NUMBER);
}

static long prediction_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	/*struct binder_proc *proc = filp->private_data;*/
	/*struct binder_thread *thread;*/
	unsigned int size = _IOC_SIZE(cmd);
	void __user *ubuf = (void __user *)arg;
    struct prediction_parv_t prediction_parv;
    struct timespec ts_stime;

	/*pr_info("prediction_ioctl: %d:%d %x %lx\n",
			proc->pid, current->pid, cmd, arg);*/

    // copy an argument from user-level
    if (size != sizeof(struct prediction_parv_t)) {
        ret = -EINVAL;
        goto out;
    }
    if (copy_from_user(&prediction_parv, ubuf, sizeof(prediction_parv))) {
        ret = -EFAULT;
        goto out;
    }


    // setup parameters (stime)
    getrawmonotonic(&ts_stime);
    prediction_parv.stime = timespec_to_ns(&ts_stime);

    // do prediction
    do_prediction(&prediction_parv);

    // dump current engine info for debugging
    prediction_parv.debug_engine_info = *engine_info_arr[prediction_parv.devid];

    // copy results to user
    if (copy_to_user(ubuf, &prediction_parv, sizeof(prediction_parv))) {
        ret = -EFAULT;
        goto out;
    }

out:
    return ret;
}

/*static int prediction_binder_open(struct inode *nodp, struct file *filp)*/
/*{*/
    /*return 0;*/
/*}*/

/*static int binder_release(struct inode *nodp, struct file *filp)*/
/*{*/
	/*return 0;*/
/*}*/

static const struct file_operations prediction_binder_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = prediction_ioctl,
	.compat_ioctl = prediction_ioctl,
	/*.open = prediction_binder_open,*/
	/*.release = binder_release,*/
};

static struct miscdevice binder_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "prediction_binder",
	.fops = &prediction_binder_fops
};


static int __init setup_base_module(void)
{
    int i_dev, i_vol;

    for (i_dev=0; i_dev < DEV_ENTRY_NUM; i_dev++) {
        init_latency_sample(&ssd_info_arr[i_dev].normal_read_sample);
        init_latency_sample(&ssd_info_arr[i_dev].normal_write_sample);

        for (i_vol=0; i_vol < ALLOC_VOLUMES; i_vol++) {
            init_volume_info(i_dev, &ssd_info_arr[i_dev].volume_info_arr[i_vol]);
        }
    }

    // for debugfs (mmap)
    debugfs_file = debugfs_create_file("mmap_example", 0644, NULL, NULL, &mmap_fops);
    page_addr = get_zeroed_page(GFP_KERNEL);

    // for character device (ioctl) (communicating with client)
	misc_register(&binder_miscdev);

    // init dev_info & engine_info
    for (i_dev=0; i_dev < DEV_ENTRY_NUM; i_dev++) {
        dev_info_arr[i_dev] = (struct dev_info_t*) (page_addr +
                i_dev * (sizeof(struct dev_info_t) + sizeof(struct engine_info_t)));
        clear_dev_info(i_dev);

        engine_info_arr[i_dev] = (struct engine_info_t*) (page_addr +
                i_dev * (sizeof(struct dev_info_t) + sizeof(struct engine_info_t)) +
                sizeof(struct dev_info_t));
        clear_engine_info(i_dev);
    }

	return 0;	/* everything is ok */
}
module_init(setup_base_module)

static void __exit exit_base_module(void) {
    debugfs_remove(debugfs_file);
    free_page(page_addr);
    misc_deregister(&binder_miscdev);
}
module_exit(exit_base_module)
