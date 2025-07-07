#ifndef SYSLOG2_H_
#define SYSLOG2_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../timeutil/timeutil.h"

#include <pthread.h>     //pthread_setname_np
#include <stdarg.h>      // va_list, va_start(), va_end()
#include <stdbool.h>     //bool type
#include <stddef.h>
#include <stdio.h>       // printf()
#include <string.h>      //memcpy
#include <sys/syscall.h> // SYS_gettid
#include <syslog.h>      // syslog()
#include <time.h>        // struct timespec
#include <unistd.h>      // syscall()

// Использование:
#define PTHREAD_SET_NAME inline_pthread_set_name(__func__, sizeof(__func__) - 1)

// Максимальное количество потоков
#define MAX_THREADS 512

// Глобальный массив для хранения последней вызванной функции каждого потока
extern const char *last_function[MAX_THREADS];
extern pid_t thread_ids[MAX_THREADS]; // Реальные идентификаторы потоков
extern pthread_t pthread_ids[MAX_THREADS];

// Макрос для записи имени функции
// Макрос для записи имени функции
#define SET_CURRENT_FUNCTION()           \
  do {                                   \
    pid_t tid = syscall(SYS_gettid);     \
    size_t index = tid % MAX_THREADS;    \
    last_function[index] = __func__;     \
    thread_ids[index] = tid;             \
    pthread_ids[index] = pthread_self(); \
  } while (0)

// global cached mask value
extern int cached_mask;

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifndef DBG
#define DBG printf(__FILE__ ":%d %s\n", __LINE__, __func__)
#endif

#ifndef FUNC_START_DEBUG
#define FUNC_START_DEBUG                      \
  do {                                        \
    if ((cached_mask & LOG_MASK(LOG_INFO))) { \
      SET_CURRENT_FUNCTION();                 \
      syslog2(LOG_INFO, "");                  \
    }                                         \
  } while (0)
#endif

#ifndef FUNC_START_DEBUG_NO_BT
#define FUNC_START_DEBUG_NO_BT                \
  do {                                        \
    if ((cached_mask & LOG_MASK(LOG_INFO))) { \
      syslog2(LOG_INFO, "");                  \
    }                                         \
  } while (0)
#endif

int setlogmask2(int log_level);
void inline_pthread_set_name(const char *fname_str, size_t len);
void setup_syslog2(const char *ident, int log_level, bool set_log_syslog);
void syslog2_(int pri, const char *func, const char *filename, int line, const char *fmt, bool add_nl, ...);
void syslog2_printf_(int pri, const char *func, const char *filename, int line, const char *fmt, ...);
void debug(const char *fmt, ...);
void print_last_functions();

typedef struct {
  void *(*malloc_fn)(size_t);
  void (*free_fn)(void *);
  void (*log_fn)(const char *, ...);
} syslog2_mod_init_args_t;

void syslog2_mod_init(const syslog2_mod_init_args_t *args);

#define __FILENAME__ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

#ifndef syslog2
#define syslog2(pri, fmt, ...) syslog2_(pri, __func__, __FILENAME__, __LINE__, fmt, true, ##__VA_ARGS__)
#endif // syslog2

#ifndef syslog2_nonl
#define syslog2_nonl(pri, fmt, ...) syslog2_(pri, __func__, __FILENAME__, __LINE__, fmt, false, ##__VA_ARGS__)
#endif // syslog2

#ifndef syslog2_printf
#define syslog2_printf(pri, fmt, ...) syslog2_printf_(pri, __func__, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
#endif // syslog2_printf

#endif /* SYSLOG2_H_ */
