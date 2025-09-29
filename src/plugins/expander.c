#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "plugin_common.h"
#include "util.h"

static plugin_node N;

static char *insert_spaces_dup(const char *s) {
    size_t len = strlen(s);
    if (len == 0) return dup_cstr("");
    // Worst case: char + space for all but last => 2*len - 1 + NUL <= 2*len
    char *o = (char *)malloc(len * 2);
    if (!o) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; ++i) {
        o[j++] = s[i];
        if (i + 1 < len) o[j++] = ' ';
    }
    o[j] = '\0';
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
        char *o = insert_spaces_dup(s);
        free(s);
        if (!o) continue;
        if (node->next_place) (void)node->next_place(o);
        free(o);
    }
    return NULL;
}

const char* plugin_get_name(void) { return "expander"; }

const char* plugin_init(int queue_size) { return plugin_node_init(&N, queue_size, worker) == 0 ? NULL : "expander: init failed"; }
void plugin_attach(const char* (*next_place_work)(const char*)) { plugin_node_attach(&N, next_place_work); }
const char* plugin_place_work(const char* str) { return plugin_node_place_work(&N, str); }
const char* plugin_fini(void) { return plugin_node_fini(&N); }
const char* plugin_wait_finished(void) { return plugin_node_wait(&N); }


