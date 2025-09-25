#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct sink_stdout_ctx {
    plugin_context base;
} sink_stdout_ctx;

static void *worker(void *arg) {
    sink_stdout_ctx *ctx = (sink_stdout_ctx *)arg;
    char *s = NULL;
    while (bq_pop(ctx->base.in_q, &s) == 0) {
        if (s == BQ_END_SENTINEL || (s && strcmp(s, "<END>") == 0)) {
            // Do not print. Close downstream if exists and exit
            if (ctx->base.out_q) bq_close(ctx->base.out_q);
            break;
        }
        fprintf(stdout, "%s\n", s);
        fflush(stdout);
        free(s);
    }
    return NULL;
}

int plugin_init(plugin_context **ctx_out) {
    if (!ctx_out) return -1;
    sink_stdout_ctx *ctx = (sink_stdout_ctx *)calloc(1, sizeof(sink_stdout_ctx));
    if (!ctx) return -1;
    *ctx_out = (plugin_context *)ctx;
    return 0;
}

// Export both names: attach and plugin_attach
static int do_attach(sink_stdout_ctx *ctx, string_bq *in_q, string_bq *out_q) {
    ctx->base.in_q = in_q;
    ctx->base.out_q = out_q;
    return pthread_create(&ctx->base.thread, NULL, worker, ctx);
}

int attach(plugin_context *ctx_base, string_bq *in_q, string_bq *out_q) {
    return do_attach((sink_stdout_ctx *)ctx_base, in_q, out_q);
}
int plugin_attach(plugin_context *ctx_base, string_bq *in_q, string_bq *out_q) {
    return do_attach((sink_stdout_ctx *)ctx_base, in_q, out_q);
}

// Export both names: place_work and plugin_place_work
static int do_place_work(sink_stdout_ctx *ctx, char *line) {
    return bq_push(ctx->base.in_q, line);
}

int place_work(plugin_context *ctx_base, char *line) {
    return do_place_work((sink_stdout_ctx *)ctx_base, line);
}
int plugin_place_work(plugin_context *ctx_base, char *line) {
    return do_place_work((sink_stdout_ctx *)ctx_base, line);
}

// Export both names: shutdown and plugin_shutdown
static void do_shutdown(sink_stdout_ctx *ctx) { bq_close(ctx->base.in_q); }

void shutdown(plugin_context *ctx_base) { do_shutdown((sink_stdout_ctx *)ctx_base); }
void plugin_shutdown(plugin_context *ctx_base) { do_shutdown((sink_stdout_ctx *)ctx_base); }

// Avoid conflict with POSIX wait symbol but also provide plugin_wait alias named "wait"
void plugin_wait(plugin_context *ctx_base) __attribute__((visibility("default"))) __asm__("wait");
void plugin_wait(plugin_context *ctx_base) {
    sink_stdout_ctx *ctx = (sink_stdout_ctx *)ctx_base;
    pthread_join(ctx->base.thread, NULL);
    free(ctx);
}

// New ABI names (thin wrappers)
void plugin_fini(plugin_context *ctx) { shutdown(ctx); }
void plugin_wait_finished(plugin_context *ctx) { plugin_wait(ctx); }


