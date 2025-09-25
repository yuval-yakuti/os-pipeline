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
#include "util.h"

typedef struct typewriter_ctx {
    plugin_context base;
    long delay_us; // per character
} typewriter_ctx;

static void sleep_us(long micros) {
    if (micros <= 0) return;
    struct timespec ts;
    ts.tv_sec = micros / 1000000;
    ts.tv_nsec = (micros % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

static void *worker(void *arg) {
    typewriter_ctx *ctx = (typewriter_ctx *)arg;
    char *s = NULL;
    while (bq_pop(ctx->base.in_q, &s) == 0) {
        if (s == BQ_END_SENTINEL) {
            bq_push(ctx->base.out_q, s);
            break;
        }
        if (ctx->delay_us > 0) {
            size_t n = strlen(s);
            for (size_t i = 0; i < n; ++i) {
                sleep_us(ctx->delay_us);
            }
        }
        char *dup = dup_cstr(s);
        free(s);
        if (!dup) continue;
        bq_push(ctx->base.out_q, dup);
    }
    return NULL;
}

int plugin_init(plugin_context **ctx_out) {
    if (!ctx_out) return -1;
    typewriter_ctx *ctx = (typewriter_ctx *)calloc(1, sizeof(typewriter_ctx));
    if (!ctx) return -1;
    ctx->delay_us = parse_long_env("TYPEWRITER_DELAY_US", 100000);
    *ctx_out = (plugin_context *)ctx;
    return 0;
}

int attach(plugin_context *ctx_base, string_bq *in_q, string_bq *out_q) {
    typewriter_ctx *ctx = (typewriter_ctx *)ctx_base;
    ctx->base.in_q = in_q;
    ctx->base.out_q = out_q;
    return pthread_create(&ctx->base.thread, NULL, worker, ctx);
}

int place_work(plugin_context *ctx_base, char *line) {
    typewriter_ctx *ctx = (typewriter_ctx *)ctx_base;
    return bq_push(ctx->base.in_q, line);
}

void shutdown(plugin_context *ctx_base) { (void)ctx_base; }

void plugin_wait(plugin_context *ctx_base) __attribute__((visibility("default"))) __asm__("wait");
void plugin_wait(plugin_context *ctx_base) {
    typewriter_ctx *ctx = (typewriter_ctx *)ctx_base;
    pthread_join(ctx->base.thread, NULL);
    free(ctx);
}

// New ABI names (thin wrappers)
void plugin_fini(plugin_context *ctx) { shutdown(ctx); }
void plugin_wait_finished(plugin_context *ctx) { plugin_wait(ctx); }


