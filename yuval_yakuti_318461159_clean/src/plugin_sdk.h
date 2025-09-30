#ifndef PLUGIN_SDK_H
#define PLUGIN_SDK_H

#include <pthread.h>
#include "bq.h"

typedef struct plugin_context plugin_context;

typedef struct plugin_api {
    int (*plugin_init)(plugin_context **ctx);
    int (*attach)(plugin_context *ctx, string_bq *in_q, string_bq *out_q);
    // place_work takes ownership of the provided pointer (no copy).
    int (*place_work)(plugin_context *ctx, char *line);
    void (*shutdown)(plugin_context *ctx);
    void (*wait)(plugin_context *ctx);
} plugin_api;

// Function names the dynamic loader expects in each plugin module
#define PLUGIN_FN_INIT "plugin_init"
#define PLUGIN_FN_ATTACH "attach"
#define PLUGIN_FN_PLACE_WORK "place_work"
#define PLUGIN_FN_SHUTDOWN "shutdown"
#define PLUGIN_FN_WAIT "wait"

// Helper for plugins: spawn a worker thread that moves items from in->out.
// Each plugin will implement its own worker routine.

struct plugin_context {
    pthread_t thread;
    string_bq *in_q;
    string_bq *out_q;
    void *user; // plugin-specific data
};

#endif // PLUGIN_SDK_H


