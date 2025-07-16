#ifndef NL_MON_H
#define NL_MON_H

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif //_POSIX_SOURCE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <net/if.h>


#ifndef EXPORT_API
#define EXPORT_API __attribute__((visibility("default")))
#endif

// События интерфейса
#define NLMON_EVENT_LINK_UP   0x1
#define NLMON_EVENT_LINK_DOWN 0x2

typedef void (*nl_cb_func_t)(const char *ifname, uint32_t events, void *arg);

typedef struct {
    const char **ifnames;  // NULL-terminated array, may be NULL/empty
    uint32_t events;       // bit mask of monitored events
    nl_cb_func_t cb;
    void *arg;
} nlmon_filter_t;

// API для работы с фильтрами и мониторингом
EXPORT_API int nlmon_add_filter(const nlmon_filter_t *filter);
EXPORT_API int nlmon_remove_filter(const nlmon_filter_t *filter);
EXPORT_API int nlmon_list_filters(void);
EXPORT_API int nlmon_run(int timeout_sec);

#endif // NL_MON_H