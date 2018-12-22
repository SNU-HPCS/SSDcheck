#ifndef IOSCHED_NVRAM_EMULATOR_H
#define IOSCHED_NVRAM_EMULATOR_H
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>

#define NE_NVRAM_DELAY 1000
#define NE_BIO_MAX_COUNT 256
#define NE_BIO_WAITING_NEXT(x) (((x) + 1) % NE_BIO_MAX_COUNT)

struct waiting_bio {
    struct bio *bio;
    long unsigned end_time;
};

struct nvram_handler {
    struct waiting_bio bio_array[NE_BIO_MAX_COUNT];
    int waiting_bio_head;
    int waiting_bio_tail;

    struct task_struct *task;
};

////////////////
struct nvram_handler *nvram_start_handler(void);
void nvram_stop_handler(struct nvram_handler *handler);
int nvram_enqueue_waiting_bio_rq(struct nvram_handler *handler,
        struct request *rq);
long unsigned nvram_get_ns(void);

#endif//IOSCHED_NVRAM_EMULATOR_H

