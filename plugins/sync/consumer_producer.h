#ifndef SYNC_CONSUMER_PRODUCER_H
#define SYNC_CONSUMER_PRODUCER_H

#include "monitor.h"

typedef struct {
    char **items;                 /* circular buffer storage */
    int capacity;                 /* maximum number of items */
    int count;                    /* current number of items */
    int head;                     /* index of next item to consume */
    int tail;                     /* index of next slot to produce */
    int closed;                   /* set once <END> is queued */
    monitor_t not_full_monitor;   /* signaled when producers may enqueue */
    monitor_t not_empty_monitor;  /* signaled when consumers may dequeue */
    monitor_t finished_monitor;   /* signaled when processing fully done */
    pthread_mutex_t mutex;        /* protects buffer state */
} consumer_producer_t;

const char* consumer_producer_init(consumer_producer_t* queue, int capacity);
void        consumer_producer_destroy(consumer_producer_t* queue);
const char* consumer_producer_put(consumer_producer_t* queue, const char* item);
char*       consumer_producer_get(consumer_producer_t* queue);
void        consumer_producer_signal_finished(consumer_producer_t* queue);
int         consumer_producer_wait_finished(consumer_producer_t* queue);

#endif // SYNC_CONSUMER_PRODUCER_H
