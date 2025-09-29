#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "plugin_common.h"
#include "util.h"

static plugin_node N;

static char *rotate_right_by_one(const char *s) {
    size_t len = strlen(s);
    if (len == 0) return dup_cstr("");
    char *o = (char *)malloc(len + 1);
    if (!o) return NULL;
    // last char to front, shift right by one
    o[0] = s[len - 1];
    for (size_t i = 0; i < len - 1; ++i) o[i + 1] = s[i];
    o[len] = '\0';
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
        char *o = rotate_right_by_one(s);
        free(s);
        if (!o) continue;
        if (node->next_place) (void)node->next_place(o);
        free(o);
    }
    return NULL;
}

const char* plugin_get_name(void) { return "rotator"; }

const char* plugin_init(int queue_size) { return plugin_node_init(&N, queue_size, worker) == 0 ? NULL : "rotator: init failed"; }
void plugin_attach(const char* (*next_place_work)(const char*)) { plugin_node_attach(&N, next_place_work); }
const char* plugin_place_work(const char* str) { return plugin_node_place_work(&N, str); }
const char* plugin_fini(void) { return plugin_node_fini(&N); }
const char* plugin_wait_finished(void) { return plugin_node_wait(&N); }


