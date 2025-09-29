#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin_common.h"

#define END_TOKEN "<END>"

static int is_end_token(const char* str) {
    return str && strcmp(str, END_TOKEN) == 0;
}

void log_error(plugin_context_t* ctx, const char* message) {
    const char* name = ctx && ctx->name ? ctx->name : "plugin";
    fprintf(stderr, "[ERROR][%s] - %s\n", name, message ? message : "unknown error");
}

void log_info(plugin_context_t* ctx, const char* message) {
    const char* name = ctx && ctx->name ? ctx->name : "plugin";
    fprintf(stderr, "[INFO][%s] - %s\n", name, message ? message : "info");
}

void* plugin_consumer_thread(void* arg) {
    plugin_context_t* ctx = (plugin_context_t*)arg;
    if (!ctx) {
        return NULL;
    }

    for (;;) {
        char* item = consumer_producer_get(&ctx->queue);
        if (!item) {
            break; /* queue drained and closed */
        }

        if (is_end_token(item)) {
            if (ctx->next_place_work) {
                const char* err = ctx->next_place_work(END_TOKEN);
                if (err) {
                    log_error(ctx, err);
                }
            }
            break;
        }

        char* processed = item;
        if (ctx->process_function) {
            processed = ctx->process_function(item);
        }

        if (!processed) {
            /* Drop the string if plugin chose to consume it */
            free(item);
            continue;
        }

        if (processed != item) {
            free(item);
        }

        if (ctx->next_place_work) {
            const char* err = ctx->next_place_work(processed);
            if (err) {
                log_error(ctx, err);
            }
        }

        free(processed);
    }

    consumer_producer_signal_finished(&ctx->queue);
    ctx->thread_running = 0;
    ctx->finished = 1;
    return NULL;
}

const char* common_plugin_init(plugin_context_t* ctx,
                               plugin_process_fn process,
                               const char* name,
                               int queue_size) {
    if (!ctx || queue_size <= 0) {
        return "common_plugin_init: invalid arguments";
    }
    if (ctx->initialized) {
        return "common_plugin_init: already initialized";
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->name = name ? name : "plugin";
    ctx->process_function = process;

    const char* err = consumer_producer_init(&ctx->queue, queue_size);
    if (err) {
        return err;
    }

    ctx->initialized = 1;
    ctx->thread_running = 1;
    if (pthread_create(&ctx->consumer_thread, NULL, plugin_consumer_thread, ctx) != 0) {
        ctx->initialized = 0;
        ctx->thread_running = 0;
        consumer_producer_destroy(&ctx->queue);
        return "common_plugin_init: pthread_create failed";
    }
    return NULL;
}

void common_plugin_attach(plugin_context_t* ctx, const char* (*next_place)(const char*)) {
    if (!ctx) {
        return;
    }
    ctx->next_place_work = next_place;
}

const char* common_plugin_place_work(plugin_context_t* ctx, const char* str) {
    if (!ctx || !ctx->initialized) {
        return "common_plugin_place_work: plugin not initialized";
    }
    if (!str) {
        str = "";
    }
    return consumer_producer_put(&ctx->queue, str);
}

const char* common_plugin_wait_finished(plugin_context_t* ctx) {
    if (!ctx || !ctx->initialized) {
        return NULL;
    }
    if (!ctx->finished) {
        (void)consumer_producer_wait_finished(&ctx->queue);
    }
    if (ctx->thread_running) {
        pthread_join(ctx->consumer_thread, NULL);
        ctx->thread_running = 0;
    }
    ctx->finished = 1;
    return NULL;
}

const char* common_plugin_fini(plugin_context_t* ctx) {
    if (!ctx || !ctx->initialized) {
        return NULL;
    }

    (void)common_plugin_wait_finished(ctx);
    consumer_producer_destroy(&ctx->queue);
    ctx->initialized = 0;
    ctx->next_place_work = NULL;
    ctx->process_function = NULL;
    return NULL;
}
