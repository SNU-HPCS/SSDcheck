/*
 * elevator pas
 */
#include "iosched-nvram-emulator.h"
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <scsi/scsi_device.h>
#include <linux/delay.h>

#include "base_module.h"

#define NVRAM_COUNT_MAX 0
#define DEADLINE_TIME_READ 10
#define DEADLINE_TIME_WRITE 10

#if 0
/* Return the last part of a pathname */
static inline const char *_myprintk_basename(const char *path)
{
        const char *tail = strrchr(path, '/');
            return tail ? tail+1 : path;
}

#define info_printk(format, ...)    (printk(KERN_INFO    "(%s:%s())[i]: "format"\n", _myprintk_basename(__FILE__), __func__, ##__VA_ARGS__))
#else
#define info_printk(format, ...)
#endif

struct pas_data {
    struct list_head queue_read;
    struct list_head queue_write;
    struct list_head queue_nvram;
    int in_flight;
    int nvram_count;

    struct nvram_handler *nvram_handler;
};

extern void do_prediction(struct prediction_parv_t *prediction_parv);
static int pas_dispatch(struct request_queue *q, int force);

/*
 * deadline_check_fifo returns 0 if there are no expired requests on the fifo,
 * 1 otherwise. Requires !list_empty(&dd->fifo_list[data_dir])
 */
static inline int deadline_check_fifo(struct request *rq)
{
	/*
	 * rq is expired!
	 */
	if (time_after_eq(jiffies, rq->fifo_time))
		return 1;

	return 0;
}

static int do_prediction_long_latency(struct request_queue *q, struct request *rq) {
    struct prediction_parv_t prediction_parv; 
    struct scsi_device *sdev = q->queuedata;
    int data_dir;

    BUG_ON(rq->bio == NULL);
    BUG_ON(rq->bio->bi_iter.bi_size != 4096);
    //////////////// example code ///////////////
    prediction_parv.devid = sdev->id;
    prediction_parv.lba = rq->bio->bi_iter.bi_sector << 9;
    prediction_parv.data_len = 4096; // we only support 4k

    data_dir = rq_data_dir(rq);
    if (data_dir == READ) {
        prediction_parv.rw = 'r';
    } else if (data_dir == WRITE) {
        prediction_parv.rw = 'w';
    } else {
        prediction_parv.rw = '?';
    } 

    //do_prediction(&prediction_parv);
    ////////////////////////////////////////////
    return 1;
}

/*
 * name: next_write_occur_bf_gc
 * return: 1 if next write occurs buffer flush or garbage collection. 0 else.
 */
static int next_write_occur_bf_gc(struct request_queue *q, struct request *rq) {
    // FIXME: fill me
    return 0;
}

static int pas_dispatch_nvram(struct request_queue *q) {
    struct pas_data *nd = q->elevator->elevator_data;
    struct request *rq;

    info_printk("Dispatching nvram");
    // The nvram queue should be not empty
    BUG_ON(list_empty(&nd->queue_nvram));

    rq = list_entry(nd->queue_nvram.next, struct request, queuelist);
    list_del_init(&rq->queuelist);
    nd->nvram_count--;
    elv_dispatch_sort(q, rq);
    nd->in_flight++;
    return 1;
}

static void pas_nvram_add_request(struct pas_data *nd, struct request *rq) {
    info_printk("Adding request into nvram");

    nvram_enqueue_waiting_bio_rq(nd->nvram_handler, rq);

    // Adding the request in nvram queue should happen after calling
    // nvram_enqueue_waiting_bio_rq
    list_add_tail(&rq->queuelist, &nd->queue_nvram);
    nd->nvram_count++;
}

static int pas_handle_write_queue(struct request_queue *q, int force) {
    struct pas_data *nd = q->elevator->elevator_data;
    struct request *rq = NULL;
    int use_nvram = 0; // move all write requests into nvram

    info_printk("Handling write queue");
    // The write queue should be not empty
    BUG_ON(list_empty(&nd->queue_write));

    while (1) {
        // Start to move requests into the nvram queue
        rq = list_entry(nd->queue_write.next, struct request, queuelist);
        list_del_init(&rq->queuelist);
        if (nd->nvram_count >= NVRAM_COUNT_MAX)
            break;

        if (use_nvram || do_prediction_long_latency(q, rq)) {
            pas_nvram_add_request(nd, rq);
            if (list_empty(&nd->queue_write)) {
                info_printk("Restarting dispatch");
                return pas_dispatch(q, force);
            }
            use_nvram = 1;
        }
        else
            break;
    }
    BUG_ON(rq == NULL);
    elv_dispatch_sort(q, rq);
    nd->in_flight++;
    return 1;
}

static int pas_dispatch_read(struct request_queue *q) {
    struct pas_data *nd = q->elevator->elevator_data;
    struct request *rq;

    info_printk("Dispatching read");
    // The read queue should be not empty
    BUG_ON(list_empty(&nd->queue_read));

    rq = list_entry(nd->queue_read.next, struct request, queuelist);
    list_del_init(&rq->queuelist);
    elv_dispatch_sort(q, rq);
    nd->in_flight++;
    return 1;
}

static int pas_dispatch(struct request_queue *q, int force)
{
    struct pas_data *nd = q->elevator->elevator_data;

    info_printk("Start dispatching!");
    if (nd->in_flight > 0) {
        // No concurrent request is allowed
        info_printk("Too many in_flight!");
        return 0;
    }

    if (list_empty(&nd->queue_read)) {
        if (list_empty(&nd->queue_write)) {
            if (list_empty(&nd->queue_nvram)) {
                info_printk("All queues are empty!");
                return 0;
            }
            else {
                return pas_dispatch_nvram(q);
            }
        }
        else { // Read queue is empty && write queue is NOT empty
            return pas_handle_write_queue(q, force);
        }
    }
    else { // Read queue is NOT empty
        if (list_empty(&nd->queue_write)) {
            return pas_dispatch_read(q);
        }
        else { // Read queue is NOT empty && write queue is NOT empty
            struct request *rq_read = NULL, *rq_write = NULL;
            rq_read  = list_entry(nd->queue_read.next, struct request,
                    queuelist);
            rq_write = list_entry(nd->queue_write.next, struct request,
                    queuelist);
            if (time_after(rq_read->fifo_time, rq_write->fifo_time)) {
                // write is urgent
                if (deadline_check_fifo(rq_write)) {
                    // deadline is over
                    return pas_handle_write_queue(q, force);
                }

                if (next_write_occur_bf_gc(q, rq_write))
                    return pas_dispatch_read(q);
                else
                    return pas_handle_write_queue(q, force);
            }
            else {
                // read is urgent
                if (deadline_check_fifo(rq_read)) {
                    // deadline is over
                    return pas_dispatch_read(q);
                }

                if (do_prediction_long_latency(q, rq_read))
                    return pas_handle_write_queue(q, force);
                else
                    return pas_dispatch_read(q);
            }
        }
    }
    // Do not reach here
    BUG_ON(1);
}

// example code (HOW TO USE PREDICTION ENGINE)
//  - Fill follow entires in "struct prediction_parv_t"
//    1. devid
//    2. lba
//    3. data_len
//    4. rw
static void pas_add_request(struct request_queue *q, struct request *rq)
{
    struct pas_data *nd = q->elevator->elevator_data;
    info_printk("Adding a request!");

    if (rq_data_dir(rq) == READ) {
        rq->fifo_time = jiffies + DEADLINE_TIME_READ;
        list_add_tail(&rq->queuelist, &nd->queue_read);
    }
    else if (rq_data_dir(rq) == WRITE) {
        rq->fifo_time = jiffies + DEADLINE_TIME_WRITE;
        list_add_tail(&rq->queuelist, &nd->queue_write);
    }
    else {
        printk(KERN_WARNING
                "[pas-iosched] pas_add_request: Unknown data dir!\n");
    }
}

static void pas_completed_request(struct request_queue *q, struct request *rq) {
	struct pas_data *nd = q->elevator->elevator_data;
    nd->in_flight--;
}

static int pas_init_queue(struct request_queue *q, struct elevator_type *e)
{
    struct pas_data *nd;
    struct elevator_queue *eq;

    eq = elevator_alloc(q, e);
    if (!eq)
        return -ENOMEM;

    nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
    if (!nd) {
        kobject_put(&eq->kobj);
        return -ENOMEM;
    }
    eq->elevator_data = nd;

    nd->nvram_handler = nvram_start_handler();
    if (!nd->nvram_handler) {
        kfree(nd);
        return -ENOMEM;
    }

    INIT_LIST_HEAD(&nd->queue_read);
    INIT_LIST_HEAD(&nd->queue_write);
    INIT_LIST_HEAD(&nd->queue_nvram);
    nd->nvram_count = 0;
    nd->in_flight = 0;

    spin_lock_irq(q->queue_lock);
    q->elevator = eq;
    spin_unlock_irq(q->queue_lock);
    return 0;
}

static void pas_exit_queue(struct elevator_queue *e)
{
    struct pas_data *nd = e->elevator_data;

    BUG_ON(!list_empty(&nd->queue_read));
    BUG_ON(!list_empty(&nd->queue_write));
    BUG_ON(!list_empty(&nd->queue_nvram));
    nvram_stop_handler(nd->nvram_handler);
    kfree(nd);
}

static struct elevator_type elevator_pas = {
    .ops = {
        //.elevator_merge_req_fn        = pas_merged_requests,
        .elevator_dispatch_fn        = pas_dispatch,
        .elevator_add_req_fn        = pas_add_request,
        //.elevator_former_req_fn        = pas_former_request,
        //.elevator_latter_req_fn        = pas_latter_request,
        .elevator_init_fn        = pas_init_queue,
        .elevator_exit_fn        = pas_exit_queue,
		.elevator_completed_req_fn =	pas_completed_request,
    },
    .elevator_name = "pasn",
    .elevator_owner = THIS_MODULE,
};

static int __init pas_init(void)
{
    return elv_register(&elevator_pas);
}

static void __exit pas_exit(void)
{
    elv_unregister(&elevator_pas);
}


module_init(pas_init);
module_exit(pas_exit);


MODULE_AUTHOR("HPCS, SNU");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Prediction-Aware IO scheduler");
