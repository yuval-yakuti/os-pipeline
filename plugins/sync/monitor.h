#ifndef SYNC_MONITOR_H
#define SYNC_MONITOR_H

#include <pthread.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int signaled;
} monitor_t;

int monitor_init(monitor_t* monitor);
void monitor_destroy(monitor_t* monitor);
void monitor_signal(monitor_t* monitor);
void monitor_reset(monitor_t* monitor);
int monitor_wait(monitor_t* monitor);

#endif // SYNC_MONITOR_H

