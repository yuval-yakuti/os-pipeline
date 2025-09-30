#ifndef PLUGINS_PLUGIN_COMMON_H
#define PLUGINS_PLUGIN_COMMON_H

#include <pthread.h>

#include "sync/consumer_producer.h"

typedef char* (*plugin_process_fn)(char* input);

typedef struct plugin_context_impl {
    const char* name;                                      /* plugin name */
    consumer_producer_t queue;                             /* inbound queue */
    pthread_t consumer_thread;                             /* worker thread */
    const char* (*next_place_work)(const char*);           /* next stage callback */
    plugin_process_fn process_function;                    /* plugin-specific transform */
    int initialized;                                       /* initialization flag */
    int thread_running;                                    /* thread state */
    int finished;                                          /* worker completion flag */
} plugin_context_t;

void*       plugin_consumer_thread(void* arg);
void        log_error(plugin_context_t* ctx, const char* message);
void        log_info(plugin_context_t* ctx, const char* message);

const char* common_plugin_init(plugin_context_t* ctx,
                               plugin_process_fn process,
                               const char* name,
                               int queue_size);
const char* common_plugin_place_work(plugin_context_t* ctx, const char* str);
void        common_plugin_attach(plugin_context_t* ctx, const char* (*next_place)(const char*));
const char* common_plugin_wait_finished(plugin_context_t* ctx);
const char* common_plugin_fini(plugin_context_t* ctx);

#endif /* PLUGINS_PLUGIN_COMMON_H */
