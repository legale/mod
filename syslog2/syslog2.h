#ifndef SYSLOG2_H
#define SYSLOG2_H

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif //_POSIX_SOURCE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE

#include "dbg_tracer/dbg_tracer.h"

#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/syscall.h>
#include <syslog.h>

#ifndef EXPORT_API
#define EXPORT_API __attribute__((visibility("default")))
#endif

#define SYSLOG2_DEFAULT_LEVEL LOG_INFO

#ifndef FUNC_START_DEBUG
#define FUNC_START_DEBUG         \
  do {                           \
    TRACE();                     \
    syslog2(LOG_DEBUG, "START"); \
  } while (0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

EXPORT_API void setup_syslog2(const char *ident, int level, bool use_syslog);
EXPORT_API void syslog2_(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, ...);
EXPORT_API void syslog2_printf_(int pri, const char *func, const char *file, int line, const char *fmt, ...);
EXPORT_API int syslog2_get_pri();

#define syslog2(pri, fmt, ...) \
  syslog2_(pri, __func__, __FILE__, __LINE__, fmt, true, ##__VA_ARGS__)

#define syslog2_printf(pri, fmt, ...) \
  syslog2_printf_(pri, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define syslog2_nnl(pri, fmt, ...) \
  syslog2_(pri, __func__, __FILE__, __LINE__, fmt, false, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // SYSLOG2_H
