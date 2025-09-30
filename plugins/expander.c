#include <stdlib.h>
#include <string.h>

#include "plugin_common.h"
#include "plugin_sdk.h"

static plugin_context_t g_ctx;

static char* expand_with_spaces(char* input) {
    if (!input) {
        return NULL;
    }
    size_t len = strlen(input);
    if (len == 0) {
        return input;
    }
    size_t out_len = len * 2;
    char* out = (char*)malloc(out_len);
    if (!out) {
        return input; /* fall back to original */
    }
    size_t j = 0;
    for (size_t i = 0; i < len; ++i) {
        out[j++] = input[i];
        if (i + 1 < len) {
            out[j++] = ' ';
        }
    }
    out[j] = '\0';
    return out;
}

const char* plugin_get_name(void) { return "expander"; }

const char* plugin_init(int queue_size) {
    return common_plugin_init(&g_ctx, expand_with_spaces, "expander", queue_size);
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

