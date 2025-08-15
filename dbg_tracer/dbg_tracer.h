/* dbg_tracer.h
 * public api for use from any module
 */

#ifndef DBG_TRACER_H
#define DBG_TRACER_H

#include <stddef.h>

#ifndef EXPORT_API
#define EXPORT_API __attribute__((visibility("default")))
#endif

/* init registry only */
EXPORT_API void tracer_setup(void);

/* optional segv install */
EXPORT_API void tracer_install_segv(void);

/* attach current thread to ring registry */
EXPORT_API void trace_reg_attach(void);

/* attach with custom name */
EXPORT_API void trace_reg_attach_name(const char *name);

/* set pthread name and slot name */
EXPORT_API void trace_set_thread_name(const char *name);

/* safe snprintf that aborts on overflow */
EXPORT_API int safe_snprintf(char *dst, size_t cap, const char *fmt, ...);

/* dump state to stderr or fd */
EXPORT_API void tracer_dump_now(void);
EXPORT_API void tracer_dump_now_fd(int fd);

/* low level writer called by macro */
EXPORT_API void tracer_mark_point(const char *file, int line, const char *func);

/* macro visible to users of the lib */
#define TRACE() tracer_mark_point(__FILE__, __LINE__, __func__)


// macro to set thread name
#define PTHREAD_SET_NAME(name)                 \
  do {                                         \
    char __buf[16];                            \
    size_t __len = strlen(name);               \
    if (__len > 15) __len = 15;                \
    memcpy(__buf, name, __len);                \
    __buf[__len] = '\0';                       \
    pthread_setname_np(pthread_self(), __buf); \
  } while (0)


#endif
