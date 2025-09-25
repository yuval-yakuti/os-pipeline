#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Minimal monitor used only for testing semantics
typedef struct test_monitor {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready_count;   // counts signals to avoid lost wakeups
    int closed;        // when set, broadcast releases all waiters
} test_monitor;

static int tm_init(test_monitor *m) {
    if (pthread_mutex_init(&m->mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&m->cond, NULL) != 0) return -1;
    m->ready_count = 0;
    m->closed = 0;
    return 0;
}

static void tm_close(test_monitor *m) {
    pthread_mutex_lock(&m->mutex);
    m->closed = 1;
    pthread_cond_broadcast(&m->cond);
    pthread_mutex_unlock(&m->mutex);
}

// Wait for one unit of readiness or closed; returns 0 on event, 1 on closed, -1 on timeout
static int tm_wait(test_monitor *m, long timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }

    pthread_mutex_lock(&m->mutex);
    while (m->ready_count == 0 && !m->closed) {
        int rc = pthread_cond_timedwait(&m->cond, &m->mutex, &ts);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&m->mutex);
            return -1;
        }
    }
    if (m->closed && m->ready_count == 0) {
        pthread_mutex_unlock(&m->mutex);
        return 1;
    }
    if (m->ready_count > 0) m->ready_count--;
    pthread_mutex_unlock(&m->mutex);
    return 0;
}

static void tm_signal(test_monitor *m) {
    pthread_mutex_lock(&m->mutex);
    m->ready_count++;
    pthread_cond_signal(&m->cond);
    pthread_mutex_unlock(&m->mutex);
}

// --- Tests ---

typedef struct thread_arg { test_monitor *m; int result; } thread_arg;

static void *waiter_consume(void *p) {
    thread_arg *a = (thread_arg *)p;
    a->result = tm_wait(a->m, 500 /*ms*/);
    return NULL;
}

static int test_signal_before_after_wait(void) {
    test_monitor m; tm_init(&m);

    // Case A: signal before waiter starts
    tm_signal(&m);
    thread_arg a1 = { .m = &m, .result = -2 };
    pthread_t t1; pthread_create(&t1, NULL, waiter_consume, &a1);
    pthread_join(t1, NULL);
    if (a1.result != 0) { fprintf(stderr, "case A failed: %d\n", a1.result); return 1; }

    // Case B: waiter before signal
    thread_arg a2 = { .m = &m, .result = -2 };
    pthread_t t2; pthread_create(&t2, NULL, waiter_consume, &a2);
    // Small stagger to ensure waiter is waiting (no long sleep)
    struct timespec s = {0, 2000000L}; nanosleep(&s, NULL); // 2ms
    tm_signal(&m);
    pthread_join(t2, NULL);
    if (a2.result != 0) { fprintf(stderr, "case B failed: %d\n", a2.result); return 1; }

    return 0;
}

static void *waiter_until_closed(void *p) {
    thread_arg *a = (thread_arg *)p;
    a->result = tm_wait(a->m, 1000 /*ms*/);
    return NULL;
}

static int test_broadcast_on_close(void) {
    test_monitor m; tm_init(&m);
    const int N = 4;
    pthread_t ts[N]; thread_arg args[N];
    for (int i = 0; i < N; ++i) { args[i].m = &m; args[i].result = -2; pthread_create(&ts[i], NULL, waiter_until_closed, &args[i]); }
    // Stagger a bit, then close
    struct timespec s = {0, 2000000L}; nanosleep(&s, NULL); // 2ms
    tm_close(&m);
    for (int i = 0; i < N; ++i) pthread_join(ts[i], NULL);
    for (int i = 0; i < N; ++i) if (args[i].result != 1) { fprintf(stderr, "close waiter %d got %d\n", i, args[i].result); return 1; }
    return 0;
}

static int test_timeout_when_not_signaled(void) {
    test_monitor m; tm_init(&m);
    int rc = tm_wait(&m, 30 /*ms*/); // no signal => should timeout
    if (rc != -1) { fprintf(stderr, "expected timeout, got %d\n", rc); return 1; }
    return 0;
}

int main(void) {
    if (test_signal_before_after_wait() != 0) return 1;
    if (test_broadcast_on_close() != 0) return 1;
    if (test_timeout_when_not_signaled() != 0) return 1;
    printf("monitor_test OK\n");
    return 0;
}


