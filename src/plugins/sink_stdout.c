#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "plugin_common.h"
#include "util.h"

static plugin_node N;

static void *worker(void *arg) {
    plugin_node *node = (plugin_node *)arg;
    char *s = NULL;
    while (bq_pop(node->queue, &s) == 0) {
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

const char* plugin_init(int queue_size) { return plugin_node_init(&N, queue_size, worker) == 0 ? NULL : "sink_stdout: init failed"; }
void plugin_attach(const char* (*next_place_work)(const char*)) { (void)next_place_work; plugin_node_attach(&N, NULL); }
const char* plugin_place_work(const char* str) { return plugin_node_place_work(&N, str); }
const char* plugin_fini(void) { return plugin_node_fini(&N); }
const char* plugin_wait_finished(void) { return plugin_node_wait(&N); }


