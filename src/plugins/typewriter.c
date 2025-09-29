#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct typewriter_state {
    string_bq *q;
    pthread_t thread;
    const char* (*next_place)(const char*);
    long delay_us; // per character
} typewriter_state;

static typewriter_state G = {0};

static void sleep_us(long micros) {
    if (micros <= 0) return;
    struct timespec ts;
    ts.tv_sec = micros / 1000000;
    ts.tv_nsec = (micros % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

static void *worker(void *arg) {
    (void)arg;
    char *s = NULL;
    while (bq_pop(G.q, &s) == 0) {
        if (s == BQ_END_SENTINEL || (s && strcmp(s, "<END>") == 0)) {
            if (G.next_place) (void)G.next_place(BQ_END_SENTINEL);
            break;
        }
        size_t n = strlen(s);
        for (size_t i = 0; i < n; ++i) {
            fputc(s[i], stdout);
            fflush(stdout);
            sleep_us(G.delay_us);
        }
        fputc('\n', stdout);
        fflush(stdout);
        if (G.next_place) (void)G.next_place(s);
        free(s);
    }
    return NULL;
}

const char* plugin_get_name(void) { return "typewriter"; }

const char* plugin_init(int queue_size) {
    if (queue_size <= 0) queue_size = 128;
    G.q = bq_create((size_t)queue_size);
    if (!G.q) return "typewriter: queue create failed";
    G.delay_us = parse_long_env("TYPEWRITER_DELAY_US", 100000);
    if (pthread_create(&G.thread, NULL, worker, NULL) != 0) return "typewriter: thread create failed";
    return NULL;
}

void plugin_attach(const char* (*next_place_work)(const char*)) { G.next_place = next_place_work; }

const char* plugin_place_work(const char* str) {
    if (!G.q) return "typewriter: not initialized";
    if (str == BQ_END_SENTINEL || (str && strcmp(str, "<END>") == 0)) {
        return bq_push(G.q, (char *)BQ_END_SENTINEL) == 0 ? NULL : "typewriter: push failed";
    }
    char *dup = dup_cstr(str ? str : "");
    if (!dup) return "typewriter: OOM";
    return bq_push(G.q, dup) == 0 ? NULL : "typewriter: push failed";
}

const char* plugin_fini(void) { if (G.q) bq_close(G.q); return NULL; }
const char* plugin_wait_finished(void) { if (G.thread) pthread_join(G.thread, NULL); if (G.q) { bq_destroy(G.q); G.q=NULL; } G.next_place=NULL; return NULL; }


