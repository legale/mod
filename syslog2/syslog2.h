#ifndef SYSLOG2_H
#define SYSLOG2_H

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif //_POSIX_SOURCE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE

#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/syscall.h>
#include <syslog.h>

#ifndef EXPORT_API
#define EXPORT_API __attribute__((visibility("default")))
#endif

#define SYSLOG2_DEFAULT_LEVEL LOG_INFO
#define MAX_THREADS 256

#ifndef FUNC_START_DEBUG
#define FUNC_START_DEBUG syslog2(LOG_DEBUG, "START")
#endif

extern const char *last_function[MAX_THREADS];
extern pid_t thread_ids[MAX_THREADS];
extern pthread_t pthread_ids[MAX_THREADS];

// макрос для записи имени функции
#define SET_CURRENT_FUNCTION()           \
  do {                                   \
    pid_t tid = syscall(SYS_gettid);     \
    size_t index = tid % MAX_THREADS;    \
    last_function[index] = __func__;     \
    thread_ids[index] = tid;             \
    pthread_ids[index] = pthread_self(); \
  } while (0)

// макрос для установки имени треда (до 15 символов)
#define PTHREAD_SET_NAME(name)                 \
  do {                                         \
    char __buf[16];                            \
    size_t __len = strlen(name);               \
    if (__len > 15) __len = 15;                \
    memcpy(__buf, name, __len);                \
    __buf[__len] = '\0';                       \
    pthread_setname_np(pthread_self(), __buf); \
  } while (0)

#ifdef __cplusplus
extern "C" {
#endif

EXPORT_API void setup_syslog2(const char *ident, int level, bool use_syslog);
EXPORT_API void syslog2_(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, ...);
EXPORT_API void syslog2_printf_(int pri, const char *func, const char *file, int line, const char *fmt, ...);
EXPORT_API void syslog2_print_last_functions(void);
EXPORT_API int syslog2_get_pri();

#define syslog2(pri, fmt, ...) \
  syslog2_(pri, __func__, __FILE__, __LINE__, fmt, true, ##__VA_ARGS__)

#define syslog2_nnl(pri, fmt, ...) \
  syslog2_(pri, __func__, __FILE__, __LINE__, fmt, false, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // SYSLOG2_H
