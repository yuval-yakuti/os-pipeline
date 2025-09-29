#include <stdlib.h>
#include <string.h>

#include "plugin_common.h"
#include "plugin_sdk.h"

static plugin_context_t g_ctx;

static char* rotate_right(char* input) {
    if (!input) {
        return NULL;
    }
    size_t len = strlen(input);
    if (len <= 1) {
        return input;
    }
    char last = input[len - 1];
    memmove(input + 1, input, len - 1);
    input[0] = last;
    return input;
}

const char* plugin_get_name(void) { return "rotator"; }

const char* plugin_init(int queue_size) {
    return common_plugin_init(&g_ctx, rotate_right, "rotator", queue_size);
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

