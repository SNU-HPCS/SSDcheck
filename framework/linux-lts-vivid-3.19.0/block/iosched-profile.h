#ifndef IOSCHED_PROFILE_H
#define IOSCHED_PROFILE_H

#include <linux/stat.h>

#define PROFILE_REQUEST_MAX (100000000)

struct single_latency {
    uint32_t sched; // in ns
    uint32_t ssd;   // in ns
    void *priv_0;
    union {
        uint32_t flag;
        struct {
            int is_read:1; // 1 = read, 0 = write
            int is_predicted_long:1; // 1 = Predicted as long latency, 0 = not
            int reserved:30;
        };
    };
};

struct latency_log {
    long unsigned head;
    struct single_latency latency[PROFILE_REQUEST_MAX];
};


struct single_latency *iosched_profile_add(struct latency_log *latency_log);
void iosched_profile_dispatch(struct latency_log *latency_log,
        struct single_latency *single_latency);
void iosched_profile_complete(struct latency_log *latency_log,
        struct single_latency *single_latency);
struct latency_log *iosched_profile_init(void);
void iosched_profile_cleanup(struct latency_log *latency_log);

// inline functions
static inline void iosched_profile_set_read(struct single_latency *sl,
        int value) {
    sl->is_read = value;
}
static inline void iosched_profile_set_predicted_long(struct single_latency *sl,
        int value) {
    sl->is_predicted_long = value;
}

static inline int iosched_profile_get_read(struct single_latency *sl) {
    return sl->is_read;
}
static inline int iosched_profile_get_predicted_long(struct single_latency *sl) {
    return sl->is_predicted_long;
}


#endif//IOSCHED_PROFILE_H
