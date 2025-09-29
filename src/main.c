#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <errno.h>
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

typedef struct plugin_handle_t {
    fn_init init;
    fn_fini fini;
    fn_place place_work;
    fn_attach attach;
    fn_wait wait_finished;
    char name[64];
    void *handle;
} plugin_handle_t;

static void print_usage(void) {
    printf("Usage: ./analyzer <queue_size> <plugin1> <plugin2> ... <pluginN>\n");
    printf("Arguments:\n");
    printf(" queue_size Maximum number of items in each plugin's queue\n");
    printf(" plugin1..N Names of plugins to load (without .so extension)\n");
    printf("Available plugins:\n");
    printf(" logger - Logs all strings that pass through\n");
    printf(" typewriter - Simulates typewriter effect with delays\n");
    printf(" uppercaser - Converts strings to uppercase\n");
    printf(" rotator - Move every character to the right. Last character moves to the beginning.\n");
    printf(" flipper - Reverses the order of characters\n");
    printf(" expander - Expands each character with spaces\n");
    printf("Example:\n");
    printf(" ./analyzer 20 uppercaser rotator logger\n");
    printf(" echo 'hello' | ./analyzer 20 uppercaser rotator logger\n");
    printf(" echo '<END>' | ./analyzer 20 uppercaser rotator logger\n");
}

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

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "invalid arguments\n");
        print_usage();
        return 1;
    }

    char *end = NULL;
    errno = 0;
    long qsize_long = strtol(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || qsize_long <= 0 || qsize_long > 1000000) {
        fprintf(stderr, "invalid queue_size\n");
        print_usage();
        return 1;
    }
    int queue_size = (int)qsize_long;

    int num = argc - 2;
    plugin_handle_t *plugins = (plugin_handle_t *)calloc((size_t)num, sizeof(plugin_handle_t));
    if (!plugins) { fprintf(stderr, "OOM\n"); return 1; }

    // Load plugins
    for (int i = 0; i < num; ++i) {
        const char *tok = argv[i + 2];
        snprintf(plugins[i].name, sizeof(plugins[i].name), "%s", tok);
        char so_path[256];
#if defined(__APPLE__)
        snprintf(so_path, sizeof(so_path), "build/plugins/%s.dylib", tok);
#else
        snprintf(so_path, sizeof(so_path), "build/plugins/%s.so", tok);
#endif
        plugins[i].handle = dlopen(so_path, RTLD_NOW);
        if (!plugins[i].handle) {
            fprintf(stderr, "dlopen failed for %s: %s\n", so_path, dlerror());
            print_usage();
            free(plugins);
            return 1;
        }
        plugins[i].init = (fn_init)load_symbol(plugins[i].handle, "plugin_init");
        plugins[i].place_work = (fn_place)load_symbol(plugins[i].handle, "plugin_place_work");
        plugins[i].attach = (fn_attach)load_symbol(plugins[i].handle, "plugin_attach");
        plugins[i].fini = (fn_fini)load_symbol(plugins[i].handle, "plugin_fini");
        plugins[i].wait_finished = (fn_wait)load_symbol(plugins[i].handle, "plugin_wait_finished");
        if (!plugins[i].init || !plugins[i].place_work || !plugins[i].attach || !plugins[i].fini || !plugins[i].wait_finished) {
            fprintf(stderr, "%s: missing required symbols\n", tok);
            print_usage();
            for (int j = 0; j <= i; ++j) if (plugins[j].handle) dlclose(plugins[j].handle);
            free(plugins);
            return 1;
        }
    }

    // Initialize
    for (int i = 0; i < num; ++i) {
        const char *err = plugins[i].init(queue_size);
        if (err) {
            fprintf(stderr, "%s: init failed: %s\n", plugins[i].name, err);
            for (int j = 0; j <= i; ++j) { (void)plugins[j].fini(); (void)plugins[j].wait_finished(); if (plugins[j].handle) dlclose(plugins[j].handle); }
            free(plugins);
            return 2;
        }
    }

    // Attach
    for (int i = 0; i + 1 < num; ++i) {
        plugins[i].attach(plugins[i + 1].place_work);
    }
    if (num > 0) plugins[num - 1].attach(NULL);

    // Read stdin with fgets up to 1024 (without trailing \n)
    char buf[1025];
    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        size_t n = strlen(buf);
        if (n > 0 && buf[n - 1] == '\n') { buf[n - 1] = '\0'; n--; }
        const char *line = buf;
        const char *err = plugins[0].place_work(line);
        if (err) { fprintf(stderr, "place_work failed in %s: %s\n", plugins[0].name, err); break; }
        if (strcmp(line, "<END>") == 0) break;
    }

    // Always send final sentinel one more time to close a pipeline where no <END> was provided
    (void)plugins[0].place_work(BQ_END_SENTINEL);

    // Wait then fini
    for (int i = 0; i < num; ++i) (void)plugins[i].wait_finished();
    for (int i = 0; i < num; ++i) (void)plugins[i].fini();

    // Unload
    for (int i = 0; i < num; ++i) if (plugins[i].handle) dlclose(plugins[i].handle);
    free(plugins);

    printf("Pipeline shutdown complete\n");
    return 0;
}


