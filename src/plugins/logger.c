#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "plugin_common.h"
#include "plugin_sdk.h"

static plugin_context_t g_ctx;
static FILE* g_fp = NULL;

static char* logger_process(char* input) {
    if (!input) {
        return NULL;
    }
    fprintf(stdout, "[logger] %s\n", input);
    fflush(stdout);
    if (g_fp) {
        fprintf(g_fp, "%s\n", input);
        fflush(g_fp);
    }
    return input;
}

const char* plugin_get_name(void) { return "logger"; }

const char* plugin_init(int queue_size) {
    struct stat st;
    if (stat("output", &st) != 0) {
        (void)mkdir("output", 0755);
    }
    g_fp = fopen("output/pipeline.log", "a");
    if (!g_fp) {
        return "logger: failed to open output/pipeline.log";
    }
    const char* err = common_plugin_init(&g_ctx, logger_process, "logger", queue_size);
    if (err) {
        fclose(g_fp);
        g_fp = NULL;
    }
    return err;
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
    const char* err = common_plugin_fini(&g_ctx);
    if (g_fp) {
        fclose(g_fp);
        g_fp = NULL;
    }
    return err;
}

