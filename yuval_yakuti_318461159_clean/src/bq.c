#include "bq.h"
#include <stdlib.h>
#include <string.h>

const char *BQ_END_SENTINEL = "<END>";

static void init_mutexes(string_bq *q) {
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&q->mutex, &mattr);
    pthread_mutexattr_destroy(&mattr);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

string_bq *bq_create(size_t capacity) {
    if (capacity == 0) return NULL;
    string_bq *q = (string_bq *)calloc(1, sizeof(string_bq));
    if (!q) return NULL;
    q->buffer = (char **)calloc(capacity, sizeof(char *));
    if (!q->buffer) {
        free(q);
        return NULL;
    }
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->closed = 0;
    init_mutexes(q);
    return q;
}

void bq_close(string_bq *q) {
    if (!q) return;
    pthread_mutex_lock(&q->mutex);
    q->closed = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

void bq_destroy(string_bq *q) {
    if (!q) return;
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    free(q->buffer);
    free(q);
}

int bq_push(string_bq *q, char *str) {
    if (!q) return -1;
    pthread_mutex_lock(&q->mutex);
    while (!q->closed && q->count == q->capacity) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    if (q->closed) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    q->buffer[q->tail] = str;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

int bq_pop(string_bq *q, char **out) {
    if (!q || !out) return -1;
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0 && !q->closed) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    if (q->count == 0 && q->closed) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    char *val = q->buffer[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    *out = val;
    return 0;
}


