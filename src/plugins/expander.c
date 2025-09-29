#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "bq.h"
#include "plugin_sdk.h"
#include "util.h"

typedef struct exp_state {
    string_bq *q;
    pthread_t thread;
    const char* (*next_place)(const char*);
} exp_state;

static exp_state G = {0};

static char *insert_spaces_dup(const char *s) {
    size_t len = strlen(s);
    if (len == 0) return dup_cstr("");
    // Worst case: char + space for all but last => 2*len - 1 + NUL <= 2*len
    char *o = (char *)malloc(len * 2);
    if (!o) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; ++i) {
        o[j++] = s[i];
        if (i + 1 < len) o[j++] = ' ';
    }
    o[j] = '\0';
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
        char *o = insert_spaces_dup(s);
        free(s);
        if (!o) continue;
        if (G.next_place) (void)G.next_place(o);
        free(o);
    }
    return NULL;
}

const char* plugin_get_name(void) { return "expander"; }

const char* plugin_init(int queue_size) {
    if (queue_size <= 0) queue_size = 128;
    G.q = bq_create((size_t)queue_size);
    if (!G.q) return "expander: queue create failed";
    if (pthread_create(&G.thread, NULL, worker, NULL) != 0) return "expander: thread create failed";
    return NULL;
}

void plugin_attach(const char* (*next_place_work)(const char*)) { G.next_place = next_place_work; }

const char* plugin_place_work(const char* str) {
    if (!G.q) return "expander: not initialized";
    if (str == BQ_END_SENTINEL || (str && strcmp(str, "<END>") == 0)) {
        return bq_push(G.q, (char *)BQ_END_SENTINEL) == 0 ? NULL : "expander: push failed";
    }
    char *dup = dup_cstr(str ? str : "");
    if (!dup) return "expander: OOM";
    return bq_push(G.q, dup) == 0 ? NULL : "expander: push failed";
}

const char* plugin_fini(void) {
    if (G.q) bq_close(G.q);
    return NULL;
}

const char* plugin_wait_finished(void) {
    if (G.thread) pthread_join(G.thread, NULL);
    if (G.q) { bq_destroy(G.q); G.q = NULL; }
    G.next_place = NULL;
    return NULL;
}


