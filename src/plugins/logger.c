#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct logger_ctx {
    plugin_context base;
    FILE *fp;
} logger_ctx;

static void *worker(void *arg) {
    logger_ctx *ctx = (logger_ctx *)arg;
    char *s = NULL;
    while (bq_pop(ctx->base.in_q, &s) == 0) {
        if (s == BQ_END_SENTINEL || (s && strcmp(s, "<END>") == 0)) {
            bq_push(ctx->base.out_q, s);
            break;
        }
        fprintf(ctx->fp, "%s\n", s);
        fflush(ctx->fp);
        char *dup = dup_cstr(s);
        free(s);
        if (!dup) continue;
        bq_push(ctx->base.out_q, dup);
    }
    return NULL;
}

int plugin_init(plugin_context **ctx_out) {
    if (!ctx_out) return -1;
    logger_ctx *ctx = (logger_ctx *)calloc(1, sizeof(logger_ctx));
    if (!ctx) return -1;
    *ctx_out = (plugin_context *)ctx;
    return 0;
}

int attach(plugin_context *ctx_base, string_bq *in_q, string_bq *out_q) {
    logger_ctx *ctx = (logger_ctx *)ctx_base;
    ctx->base.in_q = in_q;
    ctx->base.out_q = out_q;
    // Ensure output dir exists
    struct stat st;
    if (stat("output", &st) != 0) mkdir("output", 0755);
    ctx->fp = fopen("output/pipeline.log", "a");
    if (!ctx->fp) {
        LOG_ERR("logger: failed to open output/pipeline.log: %s", strerror(errno));
        return -1;
    }
    return pthread_create(&ctx->base.thread, NULL, worker, ctx);
}

int place_work(plugin_context *ctx_base, char *line) {
    logger_ctx *ctx = (logger_ctx *)ctx_base;
    return bq_push(ctx->base.in_q, line);
}

void shutdown(plugin_context *ctx_base) {
    (void)ctx_base;
}

void plugin_wait(plugin_context *ctx_base) __attribute__((visibility("default"))) __asm__("wait");
void plugin_wait(plugin_context *ctx_base) {
    logger_ctx *ctx = (logger_ctx *)ctx_base;
    pthread_join(ctx->base.thread, NULL);
    if (ctx->fp) fclose(ctx->fp);
    free(ctx);
}

// New ABI names (thin wrappers)
void plugin_fini(plugin_context *ctx) { shutdown(ctx); }
void plugin_wait_finished(plugin_context *ctx) { plugin_wait(ctx); }


