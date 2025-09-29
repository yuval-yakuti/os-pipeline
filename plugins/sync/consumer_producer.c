#include <stdlib.h>
#include <string.h>
#include "consumer_producer.h"

const char* consumer_producer_init(consumer_producer_t* q, int capacity) {
    if (!q || capacity <= 0) return "cp: invalid args";
    q->items = (char **)calloc((size_t)capacity, sizeof(char *));
    if (!q->items) return "cp: OOM";
    q->capacity = capacity;
    q->count = 0;
    q->head = 0;
    q->tail = 0;
    if (monitor_init(&q->not_full_monitor) != 0) return "cp: monitor init";
    if (monitor_init(&q->not_empty_monitor) != 0) return "cp: monitor init";
    if (monitor_init(&q->finished_monitor) != 0) return "cp: monitor init";
    return NULL;
}

void consumer_producer_destroy(consumer_producer_t* q) {
    if (!q) return;
    for (int i = 0; i < q->capacity; ++i) {
        if (q->items[i]) free(q->items[i]);
    }
    free(q->items);
    monitor_destroy(&q->not_full_monitor);
    monitor_destroy(&q->not_empty_monitor);
    monitor_destroy(&q->finished_monitor);
}

const char* consumer_producer_put(consumer_producer_t* q, const char* item) {
    if (!q) return "cp: null";
    // Wait until not full
    for (;;) {
        pthread_mutex_lock(&q->not_full_monitor.mutex);
        int full = (q->count == q->capacity);
        if (!full) {
            pthread_mutex_unlock(&q->not_full_monitor.mutex);
            break;
        }
        pthread_mutex_unlock(&q->not_full_monitor.mutex);
        (void)monitor_wait(&q->not_full_monitor);
    }
    // Insert
    size_t n = strlen(item);
    char *dup = (char *)malloc(n + 1);
    if (!dup) return "cp: OOM";
    memcpy(dup, item, n + 1);

    pthread_mutex_lock(&q->not_empty_monitor.mutex);
    q->items[q->tail] = dup;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    monitor_signal(&q->not_empty_monitor);
    pthread_mutex_unlock(&q->not_empty_monitor.mutex);
    return NULL;
}

char* consumer_producer_get(consumer_producer_t* q) {
    if (!q) return NULL;
    // Wait until not empty
    for (;;) {
        pthread_mutex_lock(&q->not_empty_monitor.mutex);
        int empty = (q->count == 0);
        if (!empty) {
            pthread_mutex_unlock(&q->not_empty_monitor.mutex);
            break;
        }
        pthread_mutex_unlock(&q->not_empty_monitor.mutex);
        (void)monitor_wait(&q->not_empty_monitor);
    }
    // Remove
    pthread_mutex_lock(&q->not_full_monitor.mutex);
    char *out = q->items[q->head];
    q->items[q->head] = NULL;
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    monitor_signal(&q->not_full_monitor);
    pthread_mutex_unlock(&q->not_full_monitor.mutex);
    return out;
}

void consumer_producer_signal_finished(consumer_producer_t* q) {
    monitor_signal(&q->finished_monitor);
}

int consumer_producer_wait_finished(consumer_producer_t* q) {
    return monitor_wait(&q->finished_monitor);
}


