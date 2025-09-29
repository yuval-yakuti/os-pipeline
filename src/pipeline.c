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
#include "util.h"

typedef const char* (*fn_get_name)(void);
typedef const char* (*fn_init)(int);
typedef const char* (*fn_place)(const char*);
typedef void        (*fn_attach)(const char* (*)(const char*));
typedef const char* (*fn_fini)(void);
typedef const char* (*fn_wait)(void);

typedef struct loaded_plugin {
    void *handle;
    char name[64];
    fn_get_name get_name;
    fn_init init;
    fn_place place_work;
    fn_attach attach;
    fn_fini fini;
    fn_wait wait_finished;
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

// New SDK has init/fini/wait on plain functions, no opaque ctx here.

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

    const size_t Q_CAP = 128;

    // Load each plugin
    char *cursor = spec;
    for (size_t i = 0; i < num; ++i) {
        char *tok = next_token(&cursor);
        if (!tok || !*tok) {
            LOG_ERR("Invalid plugin name at position %zu", i);
            return 1;
        }
        snprintf(plugins[i].name, sizeof(plugins[i].name), "%s", tok);
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
        plugins[i].get_name = (fn_get_name)load_symbol(plugins[i].handle, "plugin_get_name");
        plugins[i].init = (fn_init)load_symbol(plugins[i].handle, "plugin_init");
        plugins[i].place_work = (fn_place)load_symbol(plugins[i].handle, "plugin_place_work");
        plugins[i].attach = (fn_attach)load_symbol(plugins[i].handle, "plugin_attach");
        plugins[i].fini = (fn_fini)load_symbol(plugins[i].handle, "plugin_fini");
        plugins[i].wait_finished = (fn_wait)load_symbol(plugins[i].handle, "plugin_wait_finished");
        if (!plugins[i].init || !plugins[i].place_work || !plugins[i].attach || !plugins[i].fini || !plugins[i].wait_finished) {
            LOG_ERR("%s: missing required symbols", tok);
            return 1;
        }
        if (plugins[i].get_name) {
            const char *nm = plugins[i].get_name();
            if (nm && *nm) snprintf(plugins[i].name, sizeof(plugins[i].name), "%s", nm);
        }
        const char *err = plugins[i].init((int)Q_CAP);
        if (err) { LOG_ERR("%s: init failed: %s", plugins[i].name[0] ? plugins[i].name : tok, err); return 1; }
    }

    // Wire callbacks in order
    for (size_t i = 0; i + 1 < num; ++i) {
        LOG_INFO("attach %s -> %s", plugins[i].name, plugins[i+1].name);
        plugins[i].attach(plugins[i + 1].place_work);
    }
    if (num > 0) { LOG_INFO("attach %s -> (end)", plugins[num-1].name); plugins[num - 1].attach(NULL); }

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
        const char *err = plugins[0].place_work(line);
        free(line);
        line = NULL;
        if (err) { LOG_ERR("place_work failed in %s: %s", plugins[0].name, err); break; }
    }
    free(line);

    // Signal end-of-stream once to the first plugin
    (void)plugins[0].place_work(BQ_END_SENTINEL);

    // Wait for all plugins to finish processing before finalizing
    for (size_t i = 0; i < num; ++i) (void)plugins[i].wait_finished();
    for (size_t i = 0; i < num; ++i) (void)plugins[i].fini();

    for (size_t i = 0; i < num; ++i) {
        if (plugins[i].handle) dlclose(plugins[i].handle);
    }

    
    free(plugins);
    free(spec);
    return 0;
}


