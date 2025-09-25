#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct rot_ctx {
    plugin_context base;
    int n;
} rot_ctx;

static char rotate_char(char c, int n) {
    if (c >= 'a' && c <= 'z') {
        return (char)('a' + (c - 'a' + n) % 26);
    }
    if (c >= 'A' && c <= 'Z') {
        return (char)('A' + (c - 'A' + n) % 26);
    }
    return c;
}

static char *rot_dup(const char *s, int n) {
    size_t len = strlen(s);
    char *o = (char *)malloc(len + 1);
    if (!o) return NULL;
    for (size_t i = 0; i < len; ++i) o[i] = rotate_char(s[i], n);
    o[len] = '\0';
    return o;
}

static void *worker(void *arg) {
    rot_ctx *ctx = (rot_ctx *)arg;
    char *s = NULL;
    while (bq_pop(ctx->base.in_q, &s) == 0) {
        if (s == BQ_END_SENTINEL) { bq_push(ctx->base.out_q, s); break; }
        char *o = rot_dup(s, ctx->n);
        free(s);
        if (!o) continue;
        bq_push(ctx->base.out_q, o);
    }
    return NULL;
}

int plugin_init(plugin_context **ctx_out) {
    if (!ctx_out) return -1;
    rot_ctx *ctx = (rot_ctx *)calloc(1, sizeof(rot_ctx));
    if (!ctx) return -1;
    ctx->n = (int)parse_long_env("ROTATE_N", 13);
    *ctx_out = (plugin_context *)ctx;
    return 0;
}

int attach(plugin_context *ctx_base, string_bq *in_q, string_bq *out_q) {
    rot_ctx *ctx = (rot_ctx *)ctx_base;
    ctx->base.in_q = in_q;
    ctx->base.out_q = out_q;
    return pthread_create(&ctx->base.thread, NULL, worker, ctx);
}

int place_work(plugin_context *ctx_base, char *line) {
    rot_ctx *ctx = (rot_ctx *)ctx_base;
    return bq_push(ctx->base.in_q, line);
}

void shutdown(plugin_context *ctx_base) { (void)ctx_base; }
void plugin_wait(plugin_context *ctx_base) __attribute__((visibility("default"))) __asm__("wait");
void plugin_wait(plugin_context *ctx_base) { rot_ctx *ctx = (rot_ctx *)ctx_base; pthread_join(ctx->base.thread, NULL); free(ctx); }

// New ABI names (thin wrappers)
void plugin_fini(plugin_context *ctx) { shutdown(ctx); }
void plugin_wait_finished(plugin_context *ctx) { plugin_wait(ctx); }


