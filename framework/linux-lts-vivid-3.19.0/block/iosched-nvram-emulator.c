#include "iosched-nvram-emulator.h"
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/bio.h>
#include <linux/delay.h>
//#include <linux/elevator.h>
#include <linux/time.h>

long unsigned nvram_get_ns(void){
    struct timespec tv;  
    getnstimeofday(&tv);
    return tv.tv_sec*1000000000+tv.tv_nsec;
}

#define nvram_handler_is_empty(h) ((h)->waiting_bio_head == (h)->waiting_bio_tail)
#define nvram_handler_is_full(h) \
    (NE_BIO_WAITING_NEXT((h)->waiting_bio_head) == \
     (h)->waiting_bio_tail)

static int thread_nvram_handler(void *aux) {
    struct nvram_handler *handler = aux;
    while (!kthread_should_stop()) {
        struct waiting_bio *wb;
        long unsigned cur_ns;

        if (nvram_handler_is_empty(handler))
            // No bio are waiting
            continue;
        // handle a new bio waiting
        wb = &handler->bio_array[handler->waiting_bio_tail];
        cur_ns = nvram_get_ns();
        if (wb->end_time > cur_ns)
            ndelay(wb->end_time - cur_ns);
        bio_endio(wb->bio, 0);
        handler->waiting_bio_tail =
            NE_BIO_WAITING_NEXT(handler->waiting_bio_tail);
    }

    if (handler->waiting_bio_head != handler->waiting_bio_tail)
        printk(KERN_WARNING "nvram-emul:"
                "There are unhandled nvram requests!\n");
    return 0;
}

int nvram_enqueue_waiting_bio_rq(struct nvram_handler *handler,
        struct request *rq) {
    struct waiting_bio *wb;
    struct bio *bio;
    struct bio *new_bio;

    while (nvram_handler_is_full(handler)) {
        printk(KERN_WARNING "nvram-emul: nvram_handler is full!\n");
        mdelay(1000);
        // Try until success
    }
    wb = &handler->bio_array[handler->waiting_bio_head];

    BUG_ON(rq->bio == NULL);
    bio = rq->bio;
    new_bio = bio_clone_bioset(bio, GFP_NOIO, bio->bi_pool);
    if (!new_bio) {
        printk(KERN_WARNING "nvram-emul: Failed to clone bio!\n");
        return -ENOMEM;
    }
    rq->bio = new_bio;

    // set the waiting bio
    wb->bio = bio;
    wb->end_time = nvram_get_ns();

    handler->waiting_bio_head =
        NE_BIO_WAITING_NEXT(handler->waiting_bio_head);
    return 0;
}

struct nvram_handler *nvram_start_handler(void) {
    struct nvram_handler *handler;

    handler = kmalloc(sizeof(struct nvram_handler), GFP_KERNEL);
    if (!handler) {
        printk(KERN_WARNING "nvram-emul: handler kmalloc failed\n");
        return NULL;
    }

    handler->task = kthread_run(&thread_nvram_handler, handler,
            "nvme_doorbell_watchdog");
    if (IS_ERR(handler->task)) {
        printk(KERN_WARNING "nvram-emul: failed to start thread\n");
        goto err_out_free_handler;
    }

    handler->waiting_bio_head = 0;
    handler->waiting_bio_tail = 0;

    return handler;

err_out_free_handler:
    kfree(handler);
    return NULL;
}

void nvram_stop_handler(struct nvram_handler *handler) {
    kthread_stop(handler->task);
    kfree(handler);
}
