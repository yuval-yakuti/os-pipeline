#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

// Portable logging helpers (no GNU extension)
static inline void log_vfmt(FILE *stream, const char *prefix, const char *fmt, va_list ap) {
    fputs(prefix, stream);
    vfprintf(stream, fmt, ap);
    fputc('\n', stream);
}

static inline void log_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vfmt(stderr, "[error] ", fmt, ap);
    va_end(ap);
}

static inline void log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vfmt(stderr, "[info] ", fmt, ap);
    va_end(ap);
}

#define LOG_ERR(...) log_err(__VA_ARGS__)
#define LOG_INFO(...) log_info(__VA_ARGS__)

static inline char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static inline char *strndup_safe(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static inline long parse_long_env(const char *name, long def_val) {
    const char *v = getenv(name);
    if (!v || !*v) return def_val;
    char *end = NULL;
    errno = 0;
    long out = strtol(v, &end, 10);
    if (errno != 0 || end == v) return def_val;
    return out;
}

#endif // UTIL_H


