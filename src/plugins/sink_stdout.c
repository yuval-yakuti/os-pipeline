#include <stdio.h>
#include <stdlib.h>

#include "plugin_common.h"
#include "plugin_sdk.h"

static plugin_context_t g_ctx;

static char* sink_process(char* input) {
    if (!input) {
        return NULL;
    }
    fprintf(stdout, "%s\n", input);
    fflush(stdout);
    return NULL; /* consume the string, nothing to forward */
}

const char* plugin_get_name(void) { return "sink_stdout"; }

const char* plugin_init(int queue_size) {
    return common_plugin_init(&g_ctx, sink_process, "sink_stdout", queue_size);
}

void plugin_attach(const char* (*next_place_work)(const char*)) {
    (void)next_place_work;
    common_plugin_attach(&g_ctx, NULL);
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

