// Shared plugin infrastructure for queue/thread/callback wiring.
#ifndef PLUGINS_PLUGIN_COMMON_H
#define PLUGINS_PLUGIN_COMMON_H

#include <pthread.h>
#include "bq.h"

typedef struct plugin_node {
    string_bq *queue;
    pthread_t thread;
    const char* (*next_place)(const char*);
    // transform callback provided by concrete plugin
    // in: input C string, out: malloc'ed output or NULL to drop; sentinel handled by infra
    char *(*transform)(const char *in);
} plugin_node;

// Initialize queue and thread with provided worker function
int plugin_node_init(plugin_node *n, int queue_size, void *(*worker)(void *));
void plugin_node_attach(plugin_node *n, const char* (*next_place)(const char*));
const char* plugin_node_place_work(plugin_node *n, const char *str);
const char* plugin_node_fini(plugin_node *n);      // close queue
const char* plugin_node_wait(plugin_node *n);      // join thread and destroy queue

#endif // PLUGINS_PLUGIN_COMMON_H

