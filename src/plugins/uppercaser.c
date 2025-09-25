#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct upper_ctx {
    plugin_context base;
} upper_ctx;

static char *to_upper_dup(const char *s) {
    size_t n = strlen(s);
    char *o = (char *)malloc(n + 1);
    if (!o) return NULL;
    for (size_t i = 0; i < n; ++i) o[i] = (char)toupper((unsigned char)s[i]);
    o[n] = '\0';
    return o;
}

static void *worker(void *arg) {
    upper_ctx *ctx = (upper_ctx *)arg;
    char *s = NULL;
    while (bq_pop(ctx->base.in_q, &s) == 0) {
        if (s == BQ_END_SENTINEL) { bq_push(ctx->base.out_q, s); break; }
        char *o = to_upper_dup(s);
        free(s);
        if (!o) continue;
        bq_push(ctx->base.out_q, o);
    }
    return NULL;
}

int plugin_init(plugin_context **ctx_out) {
    if (!ctx_out) return -1;
    upper_ctx *ctx = (upper_ctx *)calloc(1, sizeof(upper_ctx));
    if (!ctx) return -1;
    *ctx_out = (plugin_context *)ctx;
    return 0;
}

int attach(plugin_context *ctx_base, string_bq *in_q, string_bq *out_q) {
    upper_ctx *ctx = (upper_ctx *)ctx_base;
    ctx->base.in_q = in_q;
    ctx->base.out_q = out_q;
    return pthread_create(&ctx->base.thread, NULL, worker, ctx);
}

int place_work(plugin_context *ctx_base, char *line) {
    upper_ctx *ctx = (upper_ctx *)ctx_base;
    return bq_push(ctx->base.in_q, line);
}

void shutdown(plugin_context *ctx_base) { (void)ctx_base; }
void plugin_wait(plugin_context *ctx_base) __attribute__((visibility("default"))) __asm__("wait");
void plugin_wait(plugin_context *ctx_base) { upper_ctx *ctx = (upper_ctx *)ctx_base; pthread_join(ctx->base.thread, NULL); free(ctx); }

// New ABI names (thin wrappers)
void plugin_fini(plugin_context *ctx) { shutdown(ctx); }
void plugin_wait_finished(plugin_context *ctx) { plugin_wait(ctx); }


