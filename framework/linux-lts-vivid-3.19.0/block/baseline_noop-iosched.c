/*
 * elevator bn
 */
#include "iosched-profile.h"
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <scsi/scsi_device.h>
#include "base_module.h"

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

struct bn_data {
	struct list_head queue;
    int in_flight;
};

extern void do_prediction(struct prediction_parv_t *prediction_parv);
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

static int bn_dispatch(struct request_queue *q, int force)
{
	struct bn_data *nd = q->elevator->elevator_data;

    info_printk("Start dispatching!");
    if (nd->in_flight > 0) {
        // No concurrent request is allowed
        info_printk("Too many in_flight!");
        return 0;
    }

	while (!list_empty(&nd->queue)) {
		struct request *rq;
		rq = list_entry(nd->queue.next, struct request, queuelist);
		list_del_init(&rq->queuelist);

        elv_dispatch_sort(q, rq);
        nd->in_flight++;
        if (profile_latency) {
            int long_latency = do_prediction_long_latency(q, rq);
            iosched_profile_set_predicted_long(rq_latency(rq), long_latency);
            iosched_profile_set_read(rq_latency(rq), rq_data_dir(rq) == READ);
            iosched_profile_dispatch(latency_log, rq_latency(rq));
        }
        return 1;
    }
	return 0;
}

static void bn_completed_request(struct request_queue *q, struct request *rq) {
	struct bn_data *nd = q->elevator->elevator_data;
    if (profile_latency)
        iosched_profile_complete(latency_log, rq_latency(rq));
    nd->in_flight--;
}

static void bn_add_request(struct request_queue *q, struct request *rq)
{
	struct bn_data *nd = q->elevator->elevator_data;

    if (profile_latency)
        rq_latency(rq) = iosched_profile_add(latency_log);

	list_add_tail(&rq->queuelist, &nd->queue);
}

static int bn_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct bn_data *nd;
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

	INIT_LIST_HEAD(&nd->queue);
    nd->in_flight = 0;

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);
	return 0;
}

static void bn_exit_queue(struct elevator_queue *e)
{
	struct bn_data *nd = e->elevator_data;

	BUG_ON(!list_empty(&nd->queue));
	kfree(nd);
}

static struct elevator_type elevator_bn = {
	.ops = {
		.elevator_dispatch_fn		= bn_dispatch,
		.elevator_add_req_fn		= bn_add_request,
		.elevator_init_fn		= bn_init_queue,
		.elevator_exit_fn		= bn_exit_queue,
		.elevator_completed_req_fn =	bn_completed_request,
	},
	.elevator_name = "bn",
	.elevator_owner = THIS_MODULE,
};

static int __init bn_init(void)
{
    int err = 0;
    if (profile_latency && (latency_log = iosched_profile_init()) == NULL) {
        err = -ENOMEM;
        goto err_out_none;
    }
    if ((err = elv_register(&elevator_bn)))
        goto err_out_profile_cleanup;
    return 0;

err_out_profile_cleanup:
    if (profile_latency)
        iosched_profile_cleanup(latency_log);
err_out_none:
    return err;
}

static void __exit bn_exit(void)
{
	elv_unregister(&elevator_bn);
    if (profile_latency)
        iosched_profile_cleanup(latency_log);
}

module_init(bn_init);
module_exit(bn_exit);


MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("No-op IO scheduler");
