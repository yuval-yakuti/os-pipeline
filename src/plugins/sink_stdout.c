#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct sink_state {
    string_bq *q;
    pthread_t thread;
} sink_state;

static sink_state G = {0};

static void *worker(void *arg) {
    (void)arg;
    char *s = NULL;
    while (bq_pop(G.q, &s) == 0) {
        if (s == BQ_END_SENTINEL || (s && strcmp(s, "<END>") == 0)) {
            break;
        }
        fprintf(stdout, "%s\n", s);
        fflush(stdout);
        free(s);
    }
    return NULL;
}

const char* plugin_get_name(void) { return "sink_stdout"; }

const char* plugin_init(int queue_size) {
    if (queue_size <= 0) queue_size = 128;
    G.q = bq_create((size_t)queue_size);
    if (!G.q) return "sink_stdout: queue create failed";
    if (pthread_create(&G.thread, NULL, worker, NULL) != 0) return "sink_stdout: thread create failed";
    return NULL;
}

void plugin_attach(const char* (*next_place_work)(const char*)) { (void)next_place_work; }

const char* plugin_place_work(const char* str) {
    if (!G.q) return "sink_stdout: not initialized";
    if (str == BQ_END_SENTINEL || (str && strcmp(str, "<END>") == 0)) {
        return bq_push(G.q, (char *)BQ_END_SENTINEL) == 0 ? NULL : "sink_stdout: push failed";
    }
    char *dup = dup_cstr(str ? str : "");
    if (!dup) return "sink_stdout: OOM";
    return bq_push(G.q, dup) == 0 ? NULL : "sink_stdout: push failed";
}

const char* plugin_fini(void) { if (G.q) bq_close(G.q); return NULL; }
const char* plugin_wait_finished(void) { if (G.thread) pthread_join(G.thread, NULL); if (G.q) { bq_destroy(G.q); G.q=NULL; } return NULL; }


