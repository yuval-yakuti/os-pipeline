#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct logger_state {
    string_bq *q;
    pthread_t thread;
    const char* (*next_place)(const char*);
    FILE *fp;
} logger_state;

static logger_state G = {0};

static void *worker(void *arg) {
    (void)arg;
    char *s = NULL;
    while (bq_pop(G.q, &s) == 0) {
        if (s == BQ_END_SENTINEL || (s && strcmp(s, "<END>") == 0)) {
            if (G.next_place) (void)G.next_place(BQ_END_SENTINEL);
            break;
        }
        fprintf(stdout, "[logger] %s\n", s);
        fflush(stdout);
        if (G.fp) { fprintf(G.fp, "%s\n", s); fflush(G.fp); }
        if (G.next_place) (void)G.next_place(s);
        free(s);
    }
    return NULL;
}

const char* plugin_get_name(void) { return "logger"; }

const char* plugin_init(int queue_size) {
    if (queue_size <= 0) queue_size = 128;
    G.q = bq_create((size_t)queue_size);
    if (!G.q) return "logger: queue create failed";
    struct stat st; if (stat("output", &st) != 0) mkdir("output", 0755);
    G.fp = fopen("output/pipeline.log", "a");
    if (!G.fp) return "logger: open output/pipeline.log failed";
    if (pthread_create(&G.thread, NULL, worker, NULL) != 0) return "logger: thread create failed";
    return NULL;
}

void plugin_attach(const char* (*next_place_work)(const char*)) { G.next_place = next_place_work; }

const char* plugin_place_work(const char* str) {
    if (!G.q) return "logger: not initialized";
    if (str == BQ_END_SENTINEL || (str && strcmp(str, "<END>") == 0)) {
        return bq_push(G.q, (char *)BQ_END_SENTINEL) == 0 ? NULL : "logger: push failed";
    }
    char *dup = dup_cstr(str ? str : "");
    if (!dup) return "logger: OOM";
    return bq_push(G.q, dup) == 0 ? NULL : "logger: push failed";
}

const char* plugin_fini(void) { if (G.q) bq_close(G.q); return NULL; }
const char* plugin_wait_finished(void) { if (G.thread) pthread_join(G.thread, NULL); if (G.fp) { fclose(G.fp); G.fp=NULL; } if (G.q) { bq_destroy(G.q); G.q=NULL; } G.next_place=NULL; return NULL; }


