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
#include "plugin_common.h"
#include "util.h"

static plugin_node N;
static long G_delay_us = 0;

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
    while (bq_pop(N.queue, &s) == 0) {
        if (s == BQ_END_SENTINEL || (s && strcmp(s, "<END>") == 0)) {
            if (N.next_place) (void)N.next_place(BQ_END_SENTINEL);
            break;
        }
        size_t n = strlen(s);
        for (size_t i = 0; i < n; ++i) {
            fputc(s[i], stdout);
            fflush(stdout);
            sleep_us(G_delay_us);
        }
        fputc('\n', stdout);
        fflush(stdout);
        if (N.next_place) (void)N.next_place(s);
        free(s);
    }
    return NULL;
}

const char* plugin_get_name(void) { return "typewriter"; }

const char* plugin_init(int queue_size) { G_delay_us = parse_long_env("TYPEWRITER_DELAY_US", 100000); return plugin_node_init(&N, queue_size, worker) == 0 ? NULL : "typewriter: init failed"; }
void plugin_attach(const char* (*next_place_work)(const char*)) { plugin_node_attach(&N, next_place_work); }
const char* plugin_place_work(const char* str) { return plugin_node_place_work(&N, str); }
const char* plugin_fini(void) { return plugin_node_fini(&N); }
const char* plugin_wait_finished(void) { return plugin_node_wait(&N); }


