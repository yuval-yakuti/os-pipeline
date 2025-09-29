#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "plugin_common.h"
#include "plugin_sdk.h"

static plugin_context_t g_ctx;
static long g_delay_us = 100000;

static void sleep_us(long delay) {
    if (delay <= 0) {
        return;
    }
    struct timespec ts;
    ts.tv_sec = delay / 1000000;
    ts.tv_nsec = (delay % 1000000) * 1000L;
    nanosleep(&ts, NULL);
}

static long parse_delay_env(void) {
    const char* env = getenv("TYPEWRITER_DELAY_US");
    if (!env || !*env) {
        return 100000;
    }
    char* end = NULL;
    errno = 0;
    long val = strtol(env, &end, 10);
    if (errno != 0 || end == env || val < 0) {
        return 100000;
    }
    return val;
}

static char* typewriter_process(char* input) {
    if (!input) {
        return NULL;
    }
    size_t len = strlen(input);
    for (size_t i = 0; i < len; ++i) {
        fputc(input[i], stdout);
        fflush(stdout);
        sleep_us(g_delay_us);
    }
    fputc('\n', stdout);
    fflush(stdout);
    return input;
}

const char* plugin_get_name(void) { return "typewriter"; }

const char* plugin_init(int queue_size) {
    g_delay_us = parse_delay_env();
    return common_plugin_init(&g_ctx, typewriter_process, "typewriter", queue_size);
}

void plugin_attach(const char* (*next_place_work)(const char*)) {
    common_plugin_attach(&g_ctx, next_place_work);
}

const char* plugin_place_work(const char* str) {
    return common_plugin_place_work(&g_ctx, str);
}

const char* plugin_wait_finished(void) {
    return common_plugin_wait_finished(&g_ctx);
}

const char* plugin_fini(void) {
    return common_plugin_fini(&g_ctx);
}

