#ifndef PLUGINS_COMMON_H
#define PLUGINS_COMMON_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bq.h"
#include "util.h"

typedef struct base_plugin_ctx {
    pthread_t thread;
    string_bq *in_q;
    string_bq *out_q;
    int running;
} base_plugin_ctx;

typedef struct base_plugin_ctx base_ctx;

static inline int base_attach(base_ctx *ctx, string_bq *in_q, string_bq *out_q) {
    ctx->in_q = in_q;
    ctx->out_q = out_q;
    ctx->running = 1;
    return 0;
}

typedef char *(*transform_fn)(const char *line, void *ud);

static void *transform_worker(void *arg);

typedef struct transform_plugin_ctx {
    base_ctx base;
    transform_fn fn;
    void *user_data;
} transform_plugin_ctx;

static inline int transform_attach(transform_plugin_ctx *ctx, string_bq *in_q, string_bq *out_q) {
    return base_attach(&ctx->base, in_q, out_q);
}

static inline int transform_start(transform_plugin_ctx *ctx, void *(*worker)(void *)) {
    return pthread_create(&ctx->base.thread, NULL, worker, ctx);
}

static inline void base_shutdown(base_ctx *ctx) {
    (void)ctx;
}

static inline void base_wait(base_ctx *ctx) {
    pthread_join(ctx->thread, NULL);
}

#endif // PLUGINS_COMMON_H


