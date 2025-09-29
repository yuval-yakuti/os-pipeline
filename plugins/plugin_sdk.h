/**
 * Plugin SDK - canonical interface for dynamically loaded string plugins.
 *
 * Each plugin must export the following symbols with C linkage:
 *   const char* plugin_get_name(void);
 *   const char* plugin_init(int queue_size);
 *   const char* plugin_fini(void);
 *   const char* plugin_place_work(const char* str);
 *   void        plugin_attach(const char* (*next_place_work)(const char*));
 *   const char* plugin_wait_finished(void);
 *
 * All returned const char* are NULL on success, or point to a static string
 * describing the error on failure. The strings must remain valid for the
 * duration of the call.
 */

#ifndef PLUGINS_PLUGIN_SDK_H
#define PLUGINS_PLUGIN_SDK_H

#ifdef __cplusplus
extern "C" {
#endif

// Function prototypes required by the host application
const char* plugin_get_name(void);
const char* plugin_init(int queue_size);
const char* plugin_fini(void);
const char* plugin_place_work(const char* str);
void        plugin_attach(const char* (*next_place_work)(const char*));
const char* plugin_wait_finished(void);

#ifdef __cplusplus
}
#endif

#endif /* PLUGINS_PLUGIN_SDK_H */


