#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "plugin_common.h"
#include "util.h"

static plugin_node N;
static FILE *G_fp = NULL;

static void *worker(void *arg) {
    (void)arg;
    char *s = NULL;
    while (bq_pop(N.queue, &s) == 0) {
        if (s == BQ_END_SENTINEL || (s && strcmp(s, "<END>") == 0)) {
            if (N.next_place) (void)N.next_place(BQ_END_SENTINEL);
            break;
        }
        fprintf(stdout, "[logger] %s\n", s);
        fflush(stdout);
        if (G_fp) { fprintf(G_fp, "%s\n", s); fflush(G_fp); }
        if (N.next_place) (void)N.next_place(s);
        free(s);
    }
    return NULL;
}

const char* plugin_get_name(void) { return "logger"; }

const char* plugin_init(int queue_size) {
    struct stat st; if (stat("output", &st) != 0) mkdir("output", 0755);
    G_fp = fopen("output/pipeline.log", "a");
    if (!G_fp) return "logger: open output/pipeline.log failed";
    return plugin_node_init(&N, queue_size, worker) == 0 ? NULL : "logger: init failed";
}

void plugin_attach(const char* (*next_place_work)(const char*)) { plugin_node_attach(&N, next_place_work); }

const char* plugin_place_work(const char* str) { return plugin_node_place_work(&N, str); }

const char* plugin_fini(void) { (void)plugin_node_fini(&N); return NULL; }
const char* plugin_wait_finished(void) { (void)plugin_node_wait(&N); if (G_fp) { fclose(G_fp); G_fp=NULL; } return NULL; }


