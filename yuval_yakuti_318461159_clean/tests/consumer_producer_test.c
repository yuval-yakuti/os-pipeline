#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sync/consumer_producer.h"

typedef struct {
    consumer_producer_t *q;
    int count;
} prod_arg;

typedef struct {
    consumer_producer_t *q;
    int got;
} cons_arg;

static void *producer(void *p) {
    prod_arg *a = (prod_arg *)p;
    for (int i = 0; i < a->count; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "I%d", i);
        consumer_producer_put(a->q, buf);
    }
    consumer_producer_signal_finished(a->q);
    return NULL;
}

static void *consumer(void *p) {
    cons_arg *a = (cons_arg *)p;
    for (;;) {
        char *s = consumer_producer_get(a->q);
        if (s == NULL) continue;
        if (strcmp(s, "<END>") == 0) { free(s); break; }
        a->got++;
        free(s);
        // stop when finished signaled and queue drained
        if (a->got >= 10) break;
    }
    return NULL;
}

int main_(void) {
    consumer_producer_t q;
    const char *err = consumer_producer_init(&q, 4);
    if (err) { fprintf(stderr, "init err: %s\n", err); return 1; }
    prod_arg pa = { .q = &q, .count = 10 };
    cons_arg ca = { .q = &q, .got = 0 };
    pthread_t pt, ct;
    pthread_create(&pt, NULL, producer, &pa);
    pthread_create(&ct, NULL, consumer, &ca);
    pthread_join(pt, NULL);
    // push sentinel for consumer thread to exit
    consumer_producer_put(&q, "<END>");
    pthread_join(ct, NULL);
    consumer_producer_destroy(&q);
    if (ca.got != 10) { fprintf(stderr, "got %d items\n", ca.got); return 1; }
    printf("consumer_producer_test OK\n");
    return 0;
}

#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sync/consumer_producer.h"

static int streq(const char* a, const char* b) {
    return strcmp(a ? a : "", b ? b : "") == 0;
}

static int test_basic_flow(void) {
    consumer_producer_t queue;
    const char* err = consumer_producer_init(&queue, 4);
    if (err) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }

    consumer_producer_put(&queue, "alpha");
    consumer_producer_put(&queue, "beta");

    char* first = consumer_producer_get(&queue);
    char* second = consumer_producer_get(&queue);

    int ok = streq(first, "alpha") && streq(second, "beta");

    free(first);
    free(second);

    consumer_producer_put(&queue, "<END>");
    char* end = consumer_producer_get(&queue);
    if (!streq(end, "<END>")) {
        ok = 0;
    }
    /* end points to static sentinel; do not free */

    consumer_producer_destroy(&queue);
    return ok ? 0 : 1;
}

typedef struct {
    consumer_producer_t* queue;
    char* items[4];
    int count;
} consumer_args_t;

static void* consumer_runner(void* arg) {
    consumer_args_t* args = (consumer_args_t*)arg;
    while (1) {
        char* item = consumer_producer_get(args->queue);
        if (!item) {
            break;
        }
        if (streq(item, "<END>")) {
            break;
        }
        args->items[args->count++] = item;
    }
    return NULL;
}

static int test_blocking_behavior(void) {
    consumer_producer_t queue;
    if (consumer_producer_init(&queue, 1) != NULL) {
        fprintf(stderr, "queue init failed\n");
        return 1;
    }

    consumer_args_t args = { .queue = &queue, .count = 0 };
    pthread_t thread;
    pthread_create(&thread, NULL, consumer_runner, &args);

    consumer_producer_put(&queue, "one");
    consumer_producer_put(&queue, "two");
    consumer_producer_put(&queue, "<END>");

    pthread_join(thread, NULL);

    int ok = (args.count == 2) && streq(args.items[0], "one") && streq(args.items[1], "two");

    for (int i = 0; i < args.count; ++i) {
        free(args.items[i]);
    }

    consumer_producer_destroy(&queue);
    return ok ? 0 : 1;
}

static int test_put_after_close_fails(void) {
    consumer_producer_t queue;
    if (consumer_producer_init(&queue, 2) != NULL) {
        fprintf(stderr, "queue init failed\n");
        return 1;
    }

    consumer_producer_put(&queue, "<END>");
    (void)consumer_producer_get(&queue); /* drain sentinel */

    const char* err = consumer_producer_put(&queue, "extra");
    consumer_producer_destroy(&queue);
    return err ? 0 : 1;
}

int main(void) {
    if (test_basic_flow() != 0) {
        fprintf(stderr, "test_basic_flow failed\n");
        return 1;
    }
    if (test_blocking_behavior() != 0) {
        fprintf(stderr, "test_blocking_behavior failed\n");
        return 1;
    }
    if (test_put_after_close_fails() != 0) {
        fprintf(stderr, "test_put_after_close_fails failed\n");
        return 1;
    }
    printf("consumer_producer_test OK\n");
    return 0;
}
