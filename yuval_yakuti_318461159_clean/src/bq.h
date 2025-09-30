#ifndef BQ_H
#define BQ_H

#include <pthread.h>
#include <stddef.h>

// Bounded blocking queue of C strings (char*). Ownership rules:
// - push() transfers ownership to the queue
// - pop() transfers ownership to the caller
// - The special sentinel pointer is the literal "<END>" and must not be freed

typedef struct string_bq {
    char **buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    int closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} string_bq;

// Global sentinel used across the pipeline to signal end-of-stream.
// Comparing pointer addresses is sufficient, do not free it.
extern const char *BQ_END_SENTINEL;

// Create a bounded queue with the given capacity (>0). Returns NULL on error.
string_bq *bq_create(size_t capacity);

// Close the queue: wakes up waiting consumers/producers.
// After close, push() fails; pending items can still be popped.
void bq_close(string_bq *q);

// Destroy queue and release internal resources. Does not free any remaining
// strings inside; ensure the pipeline drains items before calling.
void bq_destroy(string_bq *q);

// Push a string into the queue. Blocks if full until space or closed.
// Returns 0 on success, -1 if queue is closed.
int bq_push(string_bq *q, char *str);

// Pop a string from the queue. Blocks if empty until item available or closed.
// Returns 0 on success and stores into *out. Returns -1 if queue is empty and closed.
int bq_pop(string_bq *q, char **out);

#endif // BQ_H


