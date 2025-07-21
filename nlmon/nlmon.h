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
#include <linux/rtnetlink.h> // RTM_NEWLINK, RTM_DELADDR, RTM_NEWROUTE и т.д.

#ifndef EXPORT_API
#define EXPORT_API __attribute__((visibility("default")))
#endif


//максимальное число фильтров
#define NLMON_MAX_FILTERS 5
#define NLMON_MAX_IFACES 5


// События интерфейса
#define NLMON_EVENT_LINK_UP     IFF_UP
#define NLMON_EVENT_LINK_DOWN   (!IFF_UP | !IFF_RUNNING)
#define NLMON_EVENT_BROADCAST   IFF_BROADCAST
#define NLMON_EVENT_DEBUG       IFF_DEBUG
#define NLMON_EVENT_LOOPBACK    IFF_LOOPBACK
#define NLMON_EVENT_POINTOPOINT IFF_POINTOPOINT
#define NLMON_EVENT_NOTRAILERS  IFF_NOTRAILERS
#define NLMON_EVENT_RUNNING     IFF_RUNNING
#define NLMON_EVENT_NOARP       IFF_NOARP
#define NLMON_EVENT_PROMISC     IFF_PROMISC
#define NLMON_EVENT_ALLMULTI    IFF_ALLMULTI
#define NLMON_EVENT_MASTER      IFF_MASTER
#define NLMON_EVENT_SLAVE       IFF_SLAVE
#define NLMON_EVENT_MULTICAST   IFF_MULTICAST
#define NLMON_EVENT_PORTSEL     IFF_PORTSEL
#define NLMON_EVENT_AUTOMEDIA   IFF_AUTOMEDIA
#define NLMON_EVENT_DYNAMIC     IFF_DYNAMIC
#define NLMON_EVENT_LOWER_UP    IFF_LOWER_UP
#define NLMON_EVENT_DORMANT     IFF_DORMANT
#define NLMON_EVENT_ECHO        IFF_ECHO
#define NLMON_EVENT_ALL         0xFFFFFFFF


typedef void (*nl_cb_func_t)(const char *ifname, uint32_t events, void *arg);

typedef struct {
    int idx[NLMON_MAX_IFACES];  // NULL-terminated array, may be NULL/empty
    uint32_t events;       // bit mask of monitored events
    nl_cb_func_t cb;
    void *arg;
} nlmon_filter_t;

// API для работы с фильтрами и мониторингом
EXPORT_API int nlmon_add_filter(const nlmon_filter_t *filter);
EXPORT_API int nlmon_remove_filter(const nlmon_filter_t *filter);
EXPORT_API int nlmon_list_filters(void);
EXPORT_API void nlmon_clear_filters(void);
EXPORT_API int nlmon_run(void);    // запускает мониторинг в фоне
EXPORT_API void nlmon_stop(void);  // останавливает мониторинг

#endif // NL_MON_H
