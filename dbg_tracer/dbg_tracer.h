#ifndef DBG_TRACER_H
#define DBG_TRACER_H

#include <stddef.h>

#ifndef EXPORT_API
#define EXPORT_API __attribute__((visibility("default")))
#endif

EXPORT_API void tracer_setup(void);
EXPORT_API void trace_reg_attach(void);
EXPORT_API int safe_snprintf(char *dst, size_t cap, const char *fmt, ...);

#endif // DBG_TRACER_H
