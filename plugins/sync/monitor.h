#ifndef SYNC_MONITOR_H
#define SYNC_MONITOR_H

#include <pthread.h>

typedef struct {
    pthread_mutex_t mutex;      /* protects signaled flag */
    pthread_cond_t condition;   /* condition variable used for waits */
    int signaled;               /* "remembered" signal (0/1) */
} monitor_t;

/* Initialize a monitor. Returns 0 on success, -1 on failure. */
int monitor_init(monitor_t* monitor);

/* Destroy a monitor and release its resources. */
void monitor_destroy(monitor_t* monitor);

/* Signal the monitor and wake all waiting threads. */
void monitor_signal(monitor_t* monitor);

/* Manually clear the monitor's signaled state. */
void monitor_reset(monitor_t* monitor);

/*
 * Wait until monitor_signal is called.
 * The function automatically resets the signaled flag before returning
 * so that subsequent waiters will block again.
 * Returns 0 on success, -1 on error.
 */
int monitor_wait(monitor_t* monitor);

#endif // SYNC_MONITOR_H
