/*
 * elevator pas
 */
#include "iosched-profile.h"
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <scsi/scsi_device.h>
#include <linux/delay.h>

#include "base_module.h"

#define DEADLINE_TIME_READ 10
#define DEADLINE_TIME_WRITE 10

#define LONG_LATENCY 300000 /* in ns */

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

// About latency profile
static struct latency_log *latency_log;
#define rq_latency(rq) (rq->elv.priv[0])
static int profile_latency = 1;

struct pas_data {
    struct list_head queue_read;
    struct list_head queue_write;
    int in_flight;
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

// Name: do_prediction_long_latency
// Return: 1 if it likely takes long latency, 0 otherwise.
static int do_prediction_long_latency(struct request_queue *q, struct request *rq) {
    struct prediction_parv_t prediction_parv; 
    struct scsi_device *sdev = q->queuedata;
    int data_dir;
    struct timespec ts_stime;

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

    // setup parameters (stime)
    getrawmonotonic(&ts_stime);
    prediction_parv.stime = timespec_to_ns(&ts_stime);

    do_prediction(&prediction_parv);
    // do_preiction returns latency in ns.
    if (prediction_parv.predicted_latency > LONG_LATENCY)
        return 1;
    else
        return 0;
}

static int pas_handle_write_queue(struct request_queue *q, int force) {
    struct pas_data *nd = q->elevator->elevator_data;
    struct request *rq;

    info_printk("Dispatching write");
    // The write queue should be not empty
    BUG_ON(list_empty(&nd->queue_write));

    rq = list_entry(nd->queue_write.next, struct request, queuelist);
    list_del_init(&rq->queuelist);
    elv_dispatch_sort(q, rq);
    nd->in_flight++;
    if (profile_latency)
        iosched_profile_dispatch(latency_log, rq_latency(rq));
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
    if (profile_latency)
        iosched_profile_dispatch(latency_log, rq_latency(rq));
    return 1;
}

static int pas_dispatch(struct request_queue *q, int force)
{
    struct pas_data *nd = q->elevator->elevator_data;

    info_printk("Start dispatching!");
    if (nd->in_flight > 0) {
        info_printk("No concurrent request is allowed");
        return 0;
    }

    if (list_empty(&nd->queue_read)) {
        if (list_empty(&nd->queue_write)) {
            info_printk("All queues are empty!");
            return 0;
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
            //if (time_after(rq_read->fifo_time, rq_write->fifo_time)) {
            //if (time_after(rq_write->fifo_time, rq_read->fifo_time)) {
            if (rq_write->fifo_time < rq_read->fifo_time) {
                // write is urgent
                if (do_prediction_long_latency(q, rq_write)) {
                    if (do_prediction_long_latency(q, rq_read)) {
                        if (profile_latency)
                            iosched_profile_set_predicted_long(
                                    rq_latency(rq_write), 1);
                        return pas_handle_write_queue(q, force);
                    }
                    else {
                        if (profile_latency)
                            iosched_profile_set_predicted_long(
                                    rq_latency(rq_read), 0);
                        return pas_dispatch_read(q);
                    }
                }
                else {
                    if (profile_latency)
                        iosched_profile_set_predicted_long(
                                rq_latency(rq_write), 0);
                    return pas_handle_write_queue(q, force);
                }
            }
            else {
                // read is urgent
                if (do_prediction_long_latency(q, rq_read)) {
                    if (do_prediction_long_latency(q, rq_write)) {
                        if (profile_latency)
                            iosched_profile_set_predicted_long(
                                    rq_latency(rq_read), 1);
                        return pas_dispatch_read(q);
                    }
                    else {
                        if (profile_latency)
                            iosched_profile_set_predicted_long(
                                    rq_latency(rq_write), 0);
                        return pas_handle_write_queue(q, force);
                    }
                }
                else {
                    if (profile_latency)
                        iosched_profile_set_predicted_long(
                                rq_latency(rq_read), 0);
                    return pas_dispatch_read(q);
                }
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
            static unsigned long seq = 0;
            info_printk("Adding a request!");

            if (rq_data_dir(rq) == READ) {
                rq->fifo_time = seq++;
                list_add_tail(&rq->queuelist, &nd->queue_read);
            }
            else if (rq_data_dir(rq) == WRITE) {
                rq->fifo_time = seq++;
                list_add_tail(&rq->queuelist, &nd->queue_write);
            }
            else {
                printk(KERN_WARNING
                        "[pas-iosched] pas_add_request: Unknown data dir!\n");
            }

            if (profile_latency) {
                rq_latency(rq) = iosched_profile_add(latency_log);
                iosched_profile_set_read(rq_latency(rq), rq_data_dir(rq) == READ);
            }
        }

        static void pas_completed_request(struct request_queue *q, struct request *rq) {
            struct pas_data *nd = q->elevator->elevator_data;
            if (profile_latency)
                iosched_profile_complete(latency_log, rq_latency(rq));
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

            INIT_LIST_HEAD(&nd->queue_read);
            INIT_LIST_HEAD(&nd->queue_write);
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
            .elevator_name = "pas",
            .elevator_owner = THIS_MODULE,
        };

        static int __init pas_init(void)
        {
            int err = 0;
            if (profile_latency && (latency_log = iosched_profile_init()) == NULL) {
                err = -ENOMEM;
                goto err_out_none;
            }
            if ((err = elv_register(&elevator_pas)))
                goto err_out_profile_cleanup;
            return 0;

err_out_profile_cleanup:
            if (profile_latency)
                iosched_profile_cleanup(latency_log);
err_out_none:
            return err;
        }

        static void __exit pas_exit(void)
        {
            elv_unregister(&elevator_pas);
            if (profile_latency)
                iosched_profile_cleanup(latency_log);
        }


        module_init(pas_init);
        module_exit(pas_exit);


        MODULE_AUTHOR("HPCS, SNU");
        MODULE_LICENSE("GPL");
        MODULE_DESCRIPTION("Prediction-Aware IO scheduler");
