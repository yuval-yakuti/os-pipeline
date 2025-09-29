#include <stdlib.h>
#include <string.h>
#include "plugin_common.h"

int plugin_node_init(plugin_node *n, int queue_size, void *(*worker)(void *)) {
    if (!n) return -1;
    if (queue_size <= 0) queue_size = 128;
    n->queue = bq_create((size_t)queue_size);
    if (!n->queue) return -1;
    if (pthread_create(&n->thread, NULL, worker, n) != 0) return -1;
    return 0;
}

void plugin_node_attach(plugin_node *n, const char* (*next_place)(const char*)) { n->next_place = next_place; }

const char* plugin_node_place_work(plugin_node *n, const char *str) {
    if (!n || !n->queue) return "plugin_node: not initialized";
    if (str == BQ_END_SENTINEL || (str && strcmp(str, "<END>") == 0)) {
        return bq_push(n->queue, (char *)BQ_END_SENTINEL) == 0 ? NULL : "plugin_node: push failed";
    }
    const char *s = str ? str : "";
    size_t len = strlen(s);
    char *dup = (char *)malloc(len + 1);
    if (!dup) return "plugin_node: OOM";
    memcpy(dup, s, len + 1);
    return bq_push(n->queue, dup) == 0 ? NULL : "plugin_node: push failed";
}

const char* plugin_node_fini(plugin_node *n) { if (n && n->queue) bq_close(n->queue); return NULL; }

const char* plugin_node_wait(plugin_node *n) {
    if (!n) return NULL;
    pthread_join(n->thread, NULL);
    if (n->queue) { bq_destroy(n->queue); n->queue = NULL; }
    n->next_place = NULL;
    n->transform = NULL;
    return NULL;
}


