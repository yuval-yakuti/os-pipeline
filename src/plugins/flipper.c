#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "plugin_common.h"
#include "util.h"

static plugin_node N;

static char *flip_dup(const char *s) {
    size_t n = strlen(s);
    char *o = (char *)malloc(n + 1);
    if (!o) return NULL;
    for (size_t i = 0; i < n; ++i) o[i] = s[n - 1 - i];
    o[n] = '\0';
    return o;
}

static void *worker(void *arg) {
    plugin_node *node = (plugin_node *)arg;
    char *s = NULL;
    while (bq_pop(node->queue, &s) == 0) {
        if (s == BQ_END_SENTINEL || (s && strcmp(s, "<END>") == 0)) {
            if (node->next_place) (void)node->next_place(BQ_END_SENTINEL);
            break;
        }
        char *o = flip_dup(s);
        free(s);
        if (!o) continue;
        if (node->next_place) (void)node->next_place(o);
        free(o);
    }
    return NULL;
}

const char* plugin_get_name(void) { return "flipper"; }

const char* plugin_init(int queue_size) { return plugin_node_init(&N, queue_size, worker) == 0 ? NULL : "flipper: init failed"; }
void plugin_attach(const char* (*next_place_work)(const char*)) { plugin_node_attach(&N, next_place_work); }
const char* plugin_place_work(const char* str) { return plugin_node_place_work(&N, str); }
const char* plugin_fini(void) { return plugin_node_fini(&N); }
const char* plugin_wait_finished(void) { return plugin_node_wait(&N); }


