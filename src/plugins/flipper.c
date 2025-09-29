#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct flip_state {
    string_bq *q;
    pthread_t thread;
    const char* (*next_place)(const char*);
} flip_state;

static flip_state G = {0};

static char *flip_dup(const char *s) {
    size_t n = strlen(s);
    char *o = (char *)malloc(n + 1);
    if (!o) return NULL;
    for (size_t i = 0; i < n; ++i) o[i] = s[n - 1 - i];
    o[n] = '\0';
    return o;
}

static void *worker(void *arg) {
    (void)arg;
    char *s = NULL;
    while (bq_pop(G.q, &s) == 0) {
        if (s == BQ_END_SENTINEL || (s && strcmp(s, "<END>") == 0)) {
            if (G.next_place) (void)G.next_place(BQ_END_SENTINEL);
            break;
        }
        char *o = flip_dup(s);
        free(s);
        if (!o) continue;
        if (G.next_place) (void)G.next_place(o);
        free(o);
    }
    return NULL;
}

const char* plugin_get_name(void) { return "flipper"; }

const char* plugin_init(int queue_size) {
    if (queue_size <= 0) queue_size = 128;
    G.q = bq_create((size_t)queue_size);
    if (!G.q) return "flipper: queue create failed";
    if (pthread_create(&G.thread, NULL, worker, NULL) != 0) return "flipper: thread create failed";
    return NULL;
}

void plugin_attach(const char* (*next_place_work)(const char*)) { G.next_place = next_place_work; }

const char* plugin_place_work(const char* str) {
    if (!G.q) return "flipper: not initialized";
    if (str == BQ_END_SENTINEL || (str && strcmp(str, "<END>") == 0)) {
        return bq_push(G.q, (char *)BQ_END_SENTINEL) == 0 ? NULL : "flipper: push failed";
    }
    char *dup = dup_cstr(str ? str : "");
    if (!dup) return "flipper: OOM";
    return bq_push(G.q, dup) == 0 ? NULL : "flipper: push failed";
}

const char* plugin_fini(void) { if (G.q) bq_close(G.q); return NULL; }
const char* plugin_wait_finished(void) { if (G.thread) pthread_join(G.thread, NULL); if (G.q) { bq_destroy(G.q); G.q=NULL; } G.next_place=NULL; return NULL; }


