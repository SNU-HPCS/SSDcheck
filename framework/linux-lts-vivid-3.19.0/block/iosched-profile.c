#include "iosched-profile.h"
#include <linux/module.h>

static long unsigned get_ns(void){
    struct timespec tv;  
    getnstimeofday(&tv);
    return tv.tv_sec*1000000000+tv.tv_nsec;
}

struct single_latency *iosched_profile_add(struct latency_log *latency_log) {
    struct single_latency *lat;
    if (latency_log->head >= PROFILE_REQUEST_MAX)
        // Stop profiling
        return NULL;
    lat = &latency_log->latency[latency_log->head];
    latency_log->head += 1;

    lat->sched = get_ns();
    lat->ssd = 0xdeadbeef;
    lat->priv_0 = (void*)0xcafebabe;
    return lat;
}

void iosched_profile_dispatch(struct latency_log *latency_log,
        struct single_latency *lat) {
    lat->sched = get_ns() - lat->sched;
    lat->ssd = get_ns();
}

void iosched_profile_complete(struct latency_log *latency_log,
        struct single_latency *lat) {
    lat->ssd = get_ns() - lat->ssd;
}

struct latency_log *iosched_profile_init(void) {
    struct latency_log *latency_log = vmalloc(sizeof(struct latency_log));

    if (!latency_log) {
        printk(KERN_ERR "Failed to alloc memory for latency profile\n");
        return NULL;
    }

    latency_log->head = 0;

    return latency_log;
}

void iosched_profile_cleanup(struct latency_log *latency_log) {
    int i;

    printk(KERN_INFO "I/O scheduler latency profiling result");
    for (i = 0; i < latency_log->head; i++) {
        struct single_latency *lat = &latency_log->latency[i];
        int is_read = (lat->is_read ? 1 : 0);
        int is_predicted_long = (lat->is_predicted_long ? 1 : 0);

        if (i % 256 == 0) {
            printk("\n");
        }
        printk("(%u,%u,%u,%u)", lat->sched, lat->ssd, is_read,
                is_predicted_long);
    }
    printk("\n");
    vfree(latency_log);
}
