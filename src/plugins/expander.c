#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct exp_ctx { plugin_context base; } exp_ctx;

static char *collapse_and_trim(const char *s) {
    const char *p = s;
    // skip leading spaces/tabs
    while (*p == ' ' || *p == '\t') p++;
    size_t len = strlen(p);
    char *tmp = (char *)malloc(len + 1);
    if (!tmp) return NULL;
    size_t j = 0;
    int in_space = 0;
    for (size_t i = 0; p[i] != '\0'; ++i) {
        char c = p[i];
        if (c == ' ' || c == '\t') {
            if (!in_space) {
                tmp[j++] = ' ';
                in_space = 1;
            }
        } else {
            tmp[j++] = c;
            in_space = 0;
        }
    }
    // trim trailing space
    if (j > 0 && tmp[j - 1] == ' ') j--;
    tmp[j] = '\0';
    // shrink to fit
    char *out = (char *)realloc(tmp, j + 1);
    return out ? out : tmp;
}

static void *worker(void *arg) {
    exp_ctx *ctx = (exp_ctx *)arg;
    char *s = NULL;
    while (bq_pop(ctx->base.in_q, &s) == 0) {
        if (s == BQ_END_SENTINEL) { bq_push(ctx->base.out_q, s); break; }
        char *o = collapse_and_trim(s);
        free(s);
        if (!o) continue;
        bq_push(ctx->base.out_q, o);
    }
    return NULL;
}

int plugin_init(plugin_context **ctx_out) {
    if (!ctx_out) return -1;
    exp_ctx *ctx = (exp_ctx *)calloc(1, sizeof(exp_ctx));
    if (!ctx) return -1;
    *ctx_out = (plugin_context *)ctx;
    return 0;
}

int attach(plugin_context *ctx_base, string_bq *in_q, string_bq *out_q) {
    exp_ctx *ctx = (exp_ctx *)ctx_base;
    ctx->base.in_q = in_q;
    ctx->base.out_q = out_q;
    return pthread_create(&ctx->base.thread, NULL, worker, ctx);
}

int place_work(plugin_context *ctx_base, char *line) {
    exp_ctx *ctx = (exp_ctx *)ctx_base;
    return bq_push(ctx->base.in_q, line);
}

void shutdown(plugin_context *ctx_base) { (void)ctx_base; }
void plugin_wait(plugin_context *ctx_base) __attribute__((visibility("default"))) __asm__("wait");
void plugin_wait(plugin_context *ctx_base) { exp_ctx *ctx = (exp_ctx *)ctx_base; pthread_join(ctx->base.thread, NULL); free(ctx); }

// New ABI names (thin wrappers)
void plugin_fini(plugin_context *ctx) { shutdown(ctx); }
void plugin_wait_finished(plugin_context *ctx) { plugin_wait(ctx); }


