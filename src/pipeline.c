#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct loaded_plugin {
    void *handle;
    plugin_api api;
    plugin_context *ctx;
    string_bq *in_q;
    string_bq *out_q;
    char name[64];
} loaded_plugin;

static void *load_symbol(void *handle, const char *sym) {
    dlerror();
    void *fn = dlsym(handle, sym);
    const char *err = dlerror();
    if (err) {
        LOG_ERR("dlsym failed for %s: %s", sym, err);
        return NULL;
    }
    return fn;
}

static int setup_plugin(loaded_plugin *lp) {
    if (lp->api.plugin_init(&lp->ctx) != 0) {
        LOG_ERR("%s: plugin_init failed", lp->name);
        return -1;
    }
    if (lp->api.attach(lp->ctx, lp->in_q, lp->out_q) != 0) {
        LOG_ERR("%s: attach failed", lp->name);
        return -1;
    }
    return 0;
}

static void shutdown_plugin(loaded_plugin *lp) {
    if (!lp) return;
    if (lp->api.shutdown && lp->ctx) lp->api.shutdown(lp->ctx);
}

static void wait_plugin(loaded_plugin *lp) {
    if (!lp) return;
    if (lp->api.wait && lp->ctx) lp->api.wait(lp->ctx);
}

static char *next_token(char **p) {
    if (!p || !*p) return NULL;
    char *s = *p;
    if (*s == '\0') return NULL;
    char *comma = strchr(s, ',');
    if (!comma) {
        *p = s + strlen(s);
        return s;
    }
    *comma = '\0';
    *p = comma + 1;
    return s;
}

static int mkdir_p(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    return mkdir(path, 0755);
}

typedef struct sink_ctx {
    string_bq *q;
} sink_ctx;

static void *sink_worker(void *arg) {
    sink_ctx *s = (sink_ctx *)arg;
    char *line = NULL;
    while (bq_pop(s->q, &line) == 0) {
        if (line == BQ_END_SENTINEL || (line && strcmp(line, "<END>") == 0)) {
            // Do not free sentinel; just break
            break;
        }
        // Print to stdout
        fprintf(stdout, "%s\n", line);
        fflush(stdout);
        free(line);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s name1,name2,...\n", argv[0]);
        return 1;
    }

    mkdir_p("build");
    mkdir_p("build/plugins");
    mkdir_p("output");

    char *spec = dup_cstr(argv[1]);
    if (!spec) {
        LOG_ERR("OOM");
        return 1;
    }

    // Count plugins and allocate
    size_t num = 1;
    for (const char *p = spec; *p; ++p) if (*p == ',') num++;
    loaded_plugin *plugins = (loaded_plugin *)calloc(num, sizeof(loaded_plugin));
    if (!plugins) {
        LOG_ERR("OOM");
        free(spec);
        return 1;
    }

    // Create queues: one between each adjacent stage
    const size_t Q_CAP = 128;
    string_bq **queues = (string_bq **)calloc(num + 1, sizeof(string_bq *));
    if (!queues) {
        LOG_ERR("OOM");
        free(plugins);
        free(spec);
        return 1;
    }
    for (size_t i = 0; i < num + 1; ++i) {
        queues[i] = bq_create(Q_CAP);
        if (!queues[i]) {
            LOG_ERR("Failed creating queue %zu", i);
            for (size_t j = 0; j < i; ++j) bq_destroy(queues[j]);
            free(queues);
            free(plugins);
            free(spec);
            return 1;
        }
    }

    // Load each plugin
    char *cursor = spec;
    for (size_t i = 0; i < num; ++i) {
        char *tok = next_token(&cursor);
        if (!tok || !*tok) {
            LOG_ERR("Invalid plugin name at position %zu", i);
            return 1;
        }
        snprintf(plugins[i].name, sizeof(plugins[i].name), "%s", tok);
        plugins[i].in_q = queues[i];
        plugins[i].out_q = queues[i + 1];

        // Build full path to module
        char so_path[256];
#if defined(__APPLE__)
        snprintf(so_path, sizeof(so_path), "build/plugins/%s.dylib", tok);
#else
        snprintf(so_path, sizeof(so_path), "build/plugins/%s.so", tok);
#endif
        plugins[i].handle = dlopen(so_path, RTLD_NOW);
        if (!plugins[i].handle) {
            LOG_ERR("dlopen failed for %s: %s", so_path, dlerror());
            return 1;
        }
        plugins[i].api.plugin_init = (int (*)(plugin_context **))load_symbol(plugins[i].handle, PLUGIN_FN_INIT);
        plugins[i].api.attach = (int (*)(plugin_context *, string_bq *, string_bq *))load_symbol(plugins[i].handle, PLUGIN_FN_ATTACH);
        plugins[i].api.place_work = (int (*)(plugin_context *, char *))load_symbol(plugins[i].handle, PLUGIN_FN_PLACE_WORK);
        plugins[i].api.shutdown = (void (*)(plugin_context *))load_symbol(plugins[i].handle, PLUGIN_FN_SHUTDOWN);
        plugins[i].api.wait = (void (*)(plugin_context *))load_symbol(plugins[i].handle, PLUGIN_FN_WAIT);
        if (!plugins[i].api.plugin_init || !plugins[i].api.attach || !plugins[i].api.place_work || !plugins[i].api.shutdown || !plugins[i].api.wait) {
            LOG_ERR("%s: missing required symbols", tok);
            return 1;
        }
        if (setup_plugin(&plugins[i]) != 0) return 1;
    }

    // Start sink consuming the last queue to avoid leaks and print results
    pthread_t sink_thread;
    sink_ctx sctx = { .q = queues[num] };
    pthread_create(&sink_thread, NULL, sink_worker, &sctx);

    // Read stdin and feed first plugin via its input queue by place_work()
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, stdin)) != -1) {
        // trim trailing \n
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        // send to first plugin
        if (plugins[0].api.place_work(plugins[0].ctx, line) != 0) {
            LOG_ERR("place_work failed in %s", plugins[0].name);
            free(line);
            line = NULL;
            break;
        }
        line = NULL; // ownership transferred; allocate new buffer next getline
    }
    free(line);

    // Signal end-of-stream once to the first plugin
    plugins[0].api.place_work(plugins[0].ctx, (char *)BQ_END_SENTINEL);

    // Shutdown/wait plugins
    for (size_t i = 0; i < num; ++i) {
        shutdown_plugin(&plugins[i]);
    }
    for (size_t i = 0; i < num; ++i) {
        wait_plugin(&plugins[i]);
    }

    // Ensure the final queue is closed to unblock sink if the last plugin is a sink
    bq_close(queues[num]);
    // Wait for sink to finish
    pthread_join(sink_thread, NULL);

    for (size_t i = 0; i < num; ++i) {
        if (plugins[i].handle) dlclose(plugins[i].handle);
    }

    for (size_t i = 0; i < num + 1; ++i) {
        bq_close(queues[i]);
        bq_destroy(queues[i]);
    }
    free(queues);
    free(plugins);
    free(spec);
    return 0;
}


