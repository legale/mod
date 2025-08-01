#include <fcntl.h>
#include <sys/stat.h> /* fchmod */
#include <sys/time.h> /* timeval_t struct */
#include <time.h>

#include "libnlarpcache.h"
#include <stdarg.h>

// logger and timeutil fallback
#ifdef IS_DYNAMIC_LIB
#include "../syslog2/syslog2.h"   // жёсткая зависимость
#include "../timeutil/timeutil.h" // жёсткая зависимость
#endif

#ifndef FUNC_START_DEBUG
#define FUNC_START_DEBUG syslog2(LOG_DEBUG, "START")
#endif

#ifndef IS_DYNAMIC_LIB
#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>
#include <time.h>

#define syslog2(pri, fmt, ...) syslog2_(pri, __func__, __FILE__, __LINE__, fmt, true, ##__VA_ARGS__)

// unit conversion macros
#define NS_PER_USEC 1000U
#define USEC_PER_MS 1000U
#define MS_PER_SEC 1000U
#define NS_PER_MS (USEC_PER_MS * NS_PER_USEC)
#define USEC_PER_SEC (MS_PER_SEC * USEC_PER_MS)
#define NS_PER_SEC (MS_PER_SEC * NS_PER_MS)

__attribute__((weak)) void *malloc(size_t size);
__attribute__((weak)) void *calloc(size_t nmemb, size_t size);
__attribute__((weak)) void *realloc(void *ptr, size_t size);
__attribute__((weak)) void free(void *ptr);

__attribute__((weak)) void setup_syslog2(const char *ident, int level, bool use_syslog);
void setup_syslog2(const char *ident, int level, bool use_syslog) {}

__attribute__((weak)) void syslog2_(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, ...);
void syslog2_(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, ...) {
  char buf[4096];
  size_t sz = sizeof(buf);
  va_list ap;

  va_start(ap, nl);
  int len = snprintf(buf, sz, "[%d] %s:%d %s: ", pri, file, line, func);
  len += vsnprintf(buf + len, sz - len, fmt, ap);
  va_end(ap);

  // ограничиваем длину если переполнено
  if (len >= (int)sz) len = sz - 1;

  // добавляем \n если нужно
  if (nl && len < (int)sz - 1) buf[len++] = '\n';

  ssize_t written = write(STDOUT_FILENO, buf, len);
  (void)written;
}

__attribute__((weak)) uint64_t tu_get_current_time_ms();
uint64_t tu_get_current_time_ms() {
  struct timespec ts;
  int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  if (ret != 0) {
    syslog2(LOG_INFO, "clock_gettime_fast failed: ret=%d", ret);
    return 0U;
  }
  return (uint64_t)ts.tv_sec * MS_PER_SEC + (uint64_t)(ts.tv_nsec / NS_PER_MS);
}
#endif // IS_DYNAMIC_LIB
// logger END

void parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, unsigned len) {

  /* loop over all rtattributes */
  while (RTA_OK(rta, len) && max--) {
    tb[rta->rta_type] = rta;  /* store attribute ptr to the tb array */
    rta = RTA_NEXT(rta, len); /* special rtnetlink.h macro to get next netlink route attribute ptr */
  }
}

/* warning malloc is used! free(recv_buf) REQUIRED!!!
 * return recv data size received */
ssize_t send_recv(const void *send_buf, size_t send_buf_len, void **buf) {

  ssize_t status;                                       /* to store send() recv() return value  */
  int sd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE); /* open socket */
  if (sd < 0) {
    perror("socket failed");
    return -1;
  }

  fchmod(sd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  // set socket nonblocking flag
  // int flags = fcntl(sd, F_GETFL, 0);
  // fcntl(sd, F_SETFL, flags | O_NONBLOCK);

  // set socket timeout 100ms
  struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
  if (setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("setsockopt");
    return -2;
  }

  /* send message */
  status = send(sd, send_buf, send_buf_len, 0);
  if (status < 0) {
    syslog2(LOG_ERR, "send %zd %d", status, errno);
  }
  /* get an answer */
  /*first we need to find out buffer size needed */
  ssize_t expected_buf_size = 512;  /* initial buffer size */
  ssize_t buf_size = 0;             /* real buffer size */
  *buf = malloc(expected_buf_size); /* alloc memory for a buffer */

  // checking for incoming data
  fd_set readset;
  FD_ZERO(&readset);
  FD_SET(sd, &readset);
  struct timeval timeout = {.tv_sec = 1, .tv_usec = 0}; // 1 sec. timeout
  int ret = select(sd + 1, &readset, NULL, NULL, &timeout);
  if (ret == 0) {
    close(sd);
    return ret;
  } else if (ret < 0) {
    if (errno == EINTR) {

      syslog2(LOG_WARNING, "select EINTR");
      close(sd);
      return ret;
    }

    syslog2(LOG_ERR, "select error=%s", strerror(errno));
    close(sd);
    return ret;
  }

  /*
   * MSG_TRUNC will return data size even if buffer is smaller than data
   * MSG_PEEK will receive queue without removing that data from the queue.
   * Thus, a subsequent receive call will return the same data.
   * MSG_DONTWAIT will set non-blocking mode
   */
  status = recv(sd, *buf, expected_buf_size, MSG_PEEK | MSG_TRUNC | MSG_DONTWAIT);
  if (status < 0) {
    syslog2(LOG_ERR, "recv %zd %d", status, errno);
  }
  if (status > expected_buf_size) {
    expected_buf_size = status;              /* this is real size */
    *buf = realloc(*buf, expected_buf_size); /* increase buffer size */

    status = recv(sd, *buf, expected_buf_size, MSG_DONTWAIT); /* now we get the full message */
    buf_size = status;                                        /* save real buffer bsize */
    if (status < 0) {
      syslog2(LOG_ERR, "recv %zd %d", status, errno);
    }
  }

  status = close(sd); /* close socket */
  if (status < 0) {
    syslog2(LOG_ERR, "recv %zd %d", status, errno);
  }
  return buf_size;
}

/* malloc is used! don't forget free(buf) */
ssize_t get_arp_cache(void **buf_ptr) {
  /* construct arp cache request */
  struct {
    struct nlmsghdr n;
    struct ndmsg ndm;
    char buf[0];
  } req = {
      .n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ndmsg)),
      .n.nlmsg_type = RTM_GETNEIGH,                /* to get arp cache */
      .n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT, /* to get all table instead of a single entry */
                                                   //.ndm.ndm_family = AF_INET, /* IP protocol family. AF_INET/AF_INET6 or both if not set */
  };
  ssize_t status = send_recv(&req, req.n.nlmsg_len, buf_ptr);

  return status;
}

/* parse arp cache netlink messages */
ssize_t parse_arp_cache(void *buf, ssize_t buf_size, arp_cache cache[]) {
  void *p, *lp;        /* just a ptrs to work with netlink answer data */
  lp = buf + buf_size; /* answer last byte ptr */
  p = buf;             /* set p to start of an answer */
  int cnt = 0;         /* nl message counter */

  for (int i = 0; p < lp; i++) {         /* loop till the end */
    struct nlmsghdr *hdr = p;            /* netlink header structure */
    uint32_t len = hdr->nlmsg_len;       /* netlink message length including header */
    struct ndmsg *msg = NLMSG_DATA(hdr); /* macro to get a ptr right after header */

    cache[i].nl_hdr = hdr;
    cache[i].ndm_family = msg->ndm_family;
    cache[i].ndm_state = msg->ndm_state;
    /*
     * Given the payload length, len, this macro returns the aligned
     * length to store in the nlmsg_len field of the nlmsghdr.
     */
    uint32_t msg_len = NLMSG_LENGTH(sizeof(*msg));
    len -= msg_len; /* count message length left */
    p += msg_len;   /* move ptr forward */

    /* this is very first rtnetlink attribute */
    struct rtattr *rta = p;
    parse_rtattr(cache[i].tb, NDA_MAX, rta, len); /* fill tb attribute buffer */

    p += len; /* move ptr to the next netlink message */
    cnt++;
  }
  return p == lp ? cnt : -1;
}
