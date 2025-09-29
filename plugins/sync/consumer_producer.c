#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "consumer_producer.h"

#define END_TOKEN "<END>"

static int is_end_token(const char* item) {
    return item && strcmp(item, END_TOKEN) == 0;
}

static char* dup_if_needed(const char* item, int* is_end) {
    *is_end = is_end_token(item);
    if (*is_end) {
        return (char*)END_TOKEN;
    }
    size_t len = strlen(item);
    char* copy = (char*)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, item, len + 1);
    return copy;
}

const char* consumer_producer_init(consumer_producer_t* q, int capacity) {
    if (!q || capacity <= 0) {
        return "consumer_producer_init: invalid arguments";
    }

    q->items = (char**)calloc((size_t)capacity, sizeof(char*));
    if (!q->items) {
        return "consumer_producer_init: out of memory";
    }
    q->capacity = capacity;
    q->count = 0;
    q->head = 0;
    q->tail = 0;
    q->closed = 0;

    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        free(q->items);
        q->items = NULL;
        return "consumer_producer_init: mutex init failed";
    }
    if (monitor_init(&q->not_full_monitor) != 0) {
        pthread_mutex_destroy(&q->mutex);
        free(q->items);
        q->items = NULL;
        return "consumer_producer_init: monitor init failed";
    }
    if (monitor_init(&q->not_empty_monitor) != 0) {
        monitor_destroy(&q->not_full_monitor);
        pthread_mutex_destroy(&q->mutex);
        free(q->items);
        q->items = NULL;
        return "consumer_producer_init: monitor init failed";
    }
    if (monitor_init(&q->finished_monitor) != 0) {
        monitor_destroy(&q->not_empty_monitor);
        monitor_destroy(&q->not_full_monitor);
        pthread_mutex_destroy(&q->mutex);
        free(q->items);
        q->items = NULL;
        return "consumer_producer_init: monitor init failed";
    }

    return NULL;
}

void consumer_producer_destroy(consumer_producer_t* q) {
    if (!q) {
        return;
    }

    if (q->items) {
        for (int i = 0; i < q->capacity; ++i) {
            char* item = q->items[i];
            if (item && !is_end_token(item)) {
                free(item);
            }
        }
        free(q->items);
        q->items = NULL;
    }

    monitor_destroy(&q->finished_monitor);
    monitor_destroy(&q->not_empty_monitor);
    monitor_destroy(&q->not_full_monitor);
    pthread_mutex_destroy(&q->mutex);
}

const char* consumer_producer_put(consumer_producer_t* q, const char* item) {
    if (!q || !item) {
        return "consumer_producer_put: invalid arguments";
    }

    int is_end = 0;
    char* copy = dup_if_needed(item, &is_end);
    if (!copy && !is_end) {
        return "consumer_producer_put: out of memory";
    }

    pthread_mutex_lock(&q->mutex);

    if (q->closed && !is_end) {
        pthread_mutex_unlock(&q->mutex);
        if (!is_end && copy) {
            free(copy);
        }
        return "consumer_producer_put: queue closed";
    }

    while (!q->closed && q->count == q->capacity) {
        pthread_mutex_unlock(&q->mutex);
        (void)monitor_wait(&q->not_full_monitor);
        pthread_mutex_lock(&q->mutex);
    }

    if (q->closed && !is_end) {
        pthread_mutex_unlock(&q->mutex);
        if (!is_end && copy) {
            free(copy);
        }
        return "consumer_producer_put: queue closed";
    }

    q->items[q->tail] = copy;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    if (is_end) {
        q->closed = 1;
    }

    pthread_mutex_unlock(&q->mutex);
    monitor_signal(&q->not_empty_monitor);
    return NULL;
}

char* consumer_producer_get(consumer_producer_t* q) {
    if (!q) {
        return NULL;
    }

    pthread_mutex_lock(&q->mutex);
    while (q->count == 0 && !q->closed) {
        pthread_mutex_unlock(&q->mutex);
        (void)monitor_wait(&q->not_empty_monitor);
        pthread_mutex_lock(&q->mutex);
    }

    if (q->count == 0 && q->closed) {
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }

    char* item = q->items[q->head];
    q->items[q->head] = NULL;
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    pthread_mutex_unlock(&q->mutex);
    monitor_signal(&q->not_full_monitor);
    return item;
}

void consumer_producer_signal_finished(consumer_producer_t* q) {
    if (!q) {
        return;
    }
    monitor_signal(&q->finished_monitor);
}

int consumer_producer_wait_finished(consumer_producer_t* q) {
    if (!q) {
        return -1;
    }
    return monitor_wait(&q->finished_monitor);
}
