/*
 * elevator bn
 */
#include "iosched-nvram-emulator.h"
#include "iosched-profile.h"
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>

#define NVRAM_COUNT_MAX 0

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
    struct list_head queue_nvram;
    int in_flight;
    int nvram_count;

    struct nvram_handler *nvram_handler;
};

static void bn_nvram_add_request(struct bn_data *nd, struct request *rq) {
    info_printk("Adding request into nvram");

    nvram_enqueue_waiting_bio_rq(nd->nvram_handler, rq);

    // Adding the request in nvram queue should happen after calling
    // nvram_enqueue_waiting_bio_rq
    list_add_tail(&rq->queuelist, &nd->queue_nvram);
    if (profile_latency)
        iosched_profile_complete(latency_log, rq_latency(rq));
    nd->nvram_count++;
}

static int bn_dispatch_nvram(struct request_queue *q) {
    struct bn_data *nd = q->elevator->elevator_data;
    struct request *rq;

    info_printk("Dispatching nvram");
    // The nvram queue should be not empty
    BUG_ON(list_empty(&nd->queue_nvram));

    rq = list_entry(nd->queue_nvram.next, struct request, queuelist);
    list_del_init(&rq->queuelist);
    nd->nvram_count--;
    elv_dispatch_sort(q, rq);
    nd->in_flight++;
    if (profile_latency)
        iosched_profile_dispatch(latency_log, rq_latency(rq));
    return 1;
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

        if (rq_data_dir(rq) == WRITE) {
            if (nd->nvram_count < NVRAM_COUNT_MAX) {
                printk(KERN_INFO "What?????\n");
                bn_nvram_add_request(nd, rq);
                continue;
            }
        }

        elv_dispatch_sort(q, rq);
        nd->in_flight++;
        if (profile_latency)
            iosched_profile_dispatch(latency_log, rq_latency(rq));
        return 1;
    }

    if (!list_empty(&nd->queue_nvram)) {
        printk(KERN_INFO "What!!!!!\n");
        return bn_dispatch_nvram(q);
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

    nd->nvram_handler = nvram_start_handler();
    if (!nd->nvram_handler) {
        kfree(nd);
        return -ENOMEM;
    }

	INIT_LIST_HEAD(&nd->queue);
    INIT_LIST_HEAD(&nd->queue_nvram);
    nd->nvram_count = 0;
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
    BUG_ON(!list_empty(&nd->queue_nvram));
    nvram_stop_handler(nd->nvram_handler);
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
