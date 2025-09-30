#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "sync/monitor.h"

typedef struct {
    monitor_t* monitor;
    int result;
} waiter_arg_t;

static void* waiter_thread(void* arg) {
    waiter_arg_t* wa = (waiter_arg_t*)arg;
    wa->result = monitor_wait(wa->monitor);
    return NULL;
}

static int test_signal_before_wait(void) {
    monitor_t monitor;
    if (monitor_init(&monitor) != 0) {
        fprintf(stderr, "monitor_init failed\n");
        return 1;
    }

    monitor_signal(&monitor);

    waiter_arg_t arg = { .monitor = &monitor, .result = -1 };
    pthread_t thread;
    pthread_create(&thread, NULL, waiter_thread, &arg);
    pthread_join(thread, NULL);

    monitor_destroy(&monitor);
    return arg.result == 0 ? 0 : 1;
}

static int test_wait_then_signal(void) {
    monitor_t monitor;
    if (monitor_init(&monitor) != 0) {
        return 1;
    }

    waiter_arg_t arg = { .monitor = &monitor, .result = -1 };
    pthread_t thread;
    pthread_create(&thread, NULL, waiter_thread, &arg);

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 5 * 1000 * 1000 };
    nanosleep(&ts, NULL);

    monitor_signal(&monitor);
    pthread_join(thread, NULL);
    monitor_destroy(&monitor);
    return arg.result == 0 ? 0 : 1;
}

static int test_multiple_waiters(void) {
    monitor_t monitor;
    if (monitor_init(&monitor) != 0) {
        return 1;
    }

    const int waiters = 3;
    pthread_t threads[waiters];
    waiter_arg_t args[waiters];

    for (int i = 0; i < waiters; ++i) {
        args[i].monitor = &monitor;
        args[i].result = -1;
        pthread_create(&threads[i], NULL, waiter_thread, &args[i]);
    }

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 5 * 1000 * 1000 };
    nanosleep(&ts, NULL);

    for (int i = 0; i < waiters; ++i) {
        monitor_signal(&monitor);
        nanosleep(&ts, NULL);
    }

    for (int i = 0; i < waiters; ++i) {
        pthread_join(threads[i], NULL);
        if (args[i].result != 0) {
            monitor_destroy(&monitor);
            return 1;
        }
    }

    monitor_destroy(&monitor);
    return 0;
}

static int test_reset(void) {
    monitor_t monitor;
    if (monitor_init(&monitor) != 0) {
        return 1;
    }

    monitor_signal(&monitor);
    monitor_reset(&monitor);

    waiter_arg_t arg = { .monitor = &monitor, .result = -1 };
    pthread_t thread;
    pthread_create(&thread, NULL, waiter_thread, &arg);

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 5 * 1000 * 1000 };
    nanosleep(&ts, NULL);

    monitor_signal(&monitor);
    pthread_join(thread, NULL);

    monitor_destroy(&monitor);
    return arg.result == 0 ? 0 : 1;
}

int main(void) {
    if (test_signal_before_wait() != 0) {
        fprintf(stderr, "test_signal_before_wait failed\n");
        return 1;
    }
    if (test_wait_then_signal() != 0) {
        fprintf(stderr, "test_wait_then_signal failed\n");
        return 1;
    }
    if (test_multiple_waiters() != 0) {
        fprintf(stderr, "test_multiple_waiters failed\n");
        return 1;
    }
    if (test_reset() != 0) {
        fprintf(stderr, "test_reset failed\n");
        return 1;
    }
    printf("monitor_test OK\n");
    return 0;
}
