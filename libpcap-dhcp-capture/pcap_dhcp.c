#include "pcap_dhcp.h"

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <pcap.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// logger and timeutil fallback
#ifdef IS_DYNAMIC_LIB
#include "../syslog2/syslog2.h"   // жёсткая зависимость
#include "../timeutil/timeutil.h" // жёсткая зависимость
#endif

#include <time.h>
#include <unistd.h>
#ifndef FUNC_START_DEBUG
#define FUNC_START_DEBUG syslog2(LOG_DEBUG, "START")
#endif

#ifndef IS_DYNAMIC_LIB
#include <stdarg.h>
#include <syslog.h>

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

/*
 * Convert a token value to a string; use "fmt" if not found.
 */
static const char *tok2strbuf(const struct tok *lp, const char *fmt, const unsigned v, char *buf, const size_t bufsize) {
  if (lp != NULL) {
    while (lp->s != NULL) {
      if (lp->v == v)
        return (lp->s);
      ++lp;
    }
  }
  if (fmt == NULL)
    fmt = "#%d";

  (void)snprintf(buf, bufsize, fmt, v);
  return (const char *)buf;
}

/*
 * Convert a token value to a string; use "fmt" if not found.
 * Uses tok2strbuf() on one of four local static buffers of size TOKBUFSIZE
 * in round-robin fashion.
 */
const char *tok2str(const struct tok *lp, const char *fmt, const unsigned v) {
  static char buf[4][TOKBUFSIZE];
  static int idx = 0;
  char *ret;

  ret = buf[idx];
  idx = (idx + 1) & 3;
  return tok2strbuf(lp, fmt, v, ret, sizeof(buf[0]));
}

void dhcp_packet_handler(uint8_t *args, const struct pcap_pkthdr *h, const uint8_t *p) {

  /* Pointers to start point of various headers */
  const u_char *ip_h;
  // const u_char *udp_h; //for debug
  const u_char *payload;

  /* Header lengths in bytes */
  struct ether_header *eth_h = (struct ether_header *)p;
  int ethernet_h_len = 14; /* depends on ethernet type */

  /* if we have got 802.1Q frame */
  char *ether_type = (char *)&eth_h->ether_type;

  int cnt = 0;
  while (ntohs(*(uint16_t *)ether_type) == ETHERTYPE_VLAN) {
    if (cnt++ > 2) {
      // syslog2(LOG_DEBUG, "vlan headers > 2");
    }
    // struct vlan_header *vlan_h = (struct vlan_header *)eth_h;
    // //syslog2(LOG_DEBUG, "VLAN ID 0x%04X", ntohs(vlan_h->vlanid));
    ethernet_h_len += 4;
    ether_type += 4;
  }
  // //syslog2(LOG_DEBUG, "ether_type 0x%04X", ntohs(*ether_type));

  int ip_h_len;
  // int udp_len; //for debug
  const int udp_h_len = 8; /* fixed size 8 bytes */
  size_t payload_len;

  /* Find start of IP header */
  ip_h = p + ethernet_h_len;
  /* The second-half of the first byte in ip_header
     contains the IP header length (IHL). */
  ip_h_len = ((*ip_h) & 0x0F);
  ip_h_len = ip_h_len * 4;

  uint32_t total_headers_size = ethernet_h_len + ip_h_len + udp_h_len;
  // //syslog2(LOG_DEBUG, "Size of all headers combined: %d bytes", total_headers_size);
  if (total_headers_size > h->caplen) {
    // //syslog2(LOG_DEBUG, "Total headers size (%d) > packet captured size (%d). Skipping...", total_headers_size, h->caplen);
    return;
  }

  /* Now that we know where the IP header is, we can
     inspect the IP header for a protocol number to
     make sure it is UDP before going any further.
     Protocol is always the 10th byte of the IP header */
  u_char protocol = ip_h[9];
  if (protocol != IPPROTO_UDP) {
    // //syslog2(LOG_DEBUG, "%d Not a UDP packet. Skipping...", protocol);
    return;
  }

  /* Add the ethernet and ip header length to the start of the packet
     to find the beginning of the UDP header */
  // this is for debug
  // udp_h = p + ethernet_h_len + ip_h_len;
  /* The UDP header length is fixed size 8 bytes. Structure:
  | src port 2B | dst port 2B | len 2B | checksum 2B |
  packet data is little endian (aka network order), so for big endian system we need to
  swap data bytes
  */

  // udp_len = ntohs(*(uint16_t *)(udp_h + 4));
  // //syslog2(LOG_DEBUG, "UDP header + data length in bytes: %d", udp_len);

  /* Find the payload offset */
  payload_len = h->caplen -
                (ethernet_h_len + ip_h_len + udp_h_len);
  // //syslog2(LOG_DEBUG, "Payload size: %zu bytes", payload_len);
  payload = p + total_headers_size;
  if (payload_len < sizeof(struct bootp)) {
    // //syslog2(LOG_DEBUG, "payload size(%zu) < bootp structure size(%u). Skipping...", payload_len, (uint32_t)sizeof(struct bootp));
    return;
  }
  // syslog2(LOG_DEBUG, "Memory address where payload begins: %p", payload);

  pcap_dhcp_user_s *user = (pcap_dhcp_user_s *)args;
  (*user->callback)(p, *h, payload, user->callback_arg);
  return;
}

pcap_t *dhcp_pcap_open_live(const char *device) {
  char error_buffer[PCAP_ERRBUF_SIZE];
  pcap_t *handle;
  const int buf_size = 2048;                            /* interested packet size range 292 - 512 */
  int timeout_limit = 1000;                             /* In milliseconds */
  struct bpf_program fp;                                /* The compiled filter */
  char filter_exp[] = "len >= 292 && udp && (port 67)"; /* The filter expression to catch bootp packets */
  bpf_u_int32 net = 0;                                  /* Our netmask */

  /* Create pcap handle */
  handle = pcap_create(device, error_buffer);
  if (handle == NULL) {
    syslog2(LOG_CRIT, "error: Could not create pcap handle for device %s: %s", device, error_buffer);
    return NULL;
  }

  /* Set snapshot length */
  if (pcap_set_snaplen(handle, buf_size) != 0) {
    syslog2(LOG_CRIT, "error: Could not set snapshot length for device %s: %s", device, pcap_geterr(handle));
    pcap_close(handle);
    return NULL;
  }

  /* Set promiscuous mode */
  if (pcap_set_promisc(handle, 1) != 0) {
    syslog2(LOG_CRIT, "error: Could not set promiscuous mode for device %s: %s", device, pcap_geterr(handle));
    pcap_close(handle);
    return NULL;
  }

  /* Set read timeout */
  if (pcap_set_timeout(handle, timeout_limit) != 0) {
    syslog2(LOG_CRIT, "error: Could not set timeout for device %s: %s", device, pcap_geterr(handle));
    pcap_close(handle);
    return NULL;
  }

  // /* Set buffer size for DHCP packets */
  if (pcap_set_buffer_size(handle, 32 * 1024) != 0) {
    syslog2(LOG_ALERT, "Unable to set buffer size for %s", device);
    pcap_close(handle);
    return NULL;
  }

  // this mode is not working with small buffer size error: can't mmap rx ring: Invalid argument
  /* Set immediate mode */
  if (pcap_set_immediate_mode(handle, 1) != 0) {
    syslog2(LOG_ALERT, "Unable to set immediate mode for %s", device);
    pcap_close(handle);
    return NULL;
  }

  // Set non-blocking mode
  if (pcap_setnonblock(handle, 1, error_buffer) == -1) {
    syslog2(LOG_CRIT, "error: Could not set non-blocking mode for device %s: %s", device, error_buffer);
    pcap_close(handle);
    return NULL;
  }

  /* Activate the pcap handle */
  if (pcap_activate(handle) != 0) {
    syslog2(LOG_CRIT, "error: Could not activate pcap handle for device %s: %s", device, pcap_geterr(handle));
    pcap_close(handle);
    return NULL;
  }

  /* Compile and apply the filter */
  if (pcap_compile(handle, &fp, filter_exp, 1, net) == -1) {
    syslog2(LOG_CRIT, "error: Couldn't parse filter %s: %s", filter_exp, pcap_geterr(handle));
    pcap_close(handle);
    return NULL;
  }

  if (pcap_setfilter(handle, &fp) == -1) {
    syslog2(LOG_CRIT, "error: Couldn't install filter %s: %s", filter_exp, pcap_geterr(handle));
    pcap_close(handle);
    return NULL;
  }

  return handle;
}

/*
 * Vendor magic cookie (v_magic) for RFC1048
 */
#define VM_RFC1048 \
  {99, 130, 83, 99}

static const uint8_t vm_rfc1048[4] = VM_RFC1048;

char *parse_vendor_specific_option_12(const nd_byte *vend_data, size_t vend_len) {
  size_t index = 0;
  if (memcmp((const char *)vend_data, vm_rfc1048, sizeof(int)) != 0) {
    perror("Vendor Magic Cookie not found");
    return NULL;
  }
  vend_data += sizeof(vm_rfc1048);
  vend_len -= sizeof(vm_rfc1048);

  while (index < vend_len) {
    uint8_t option = vend_data[index++];
    uint8_t option_len = vend_data[index++];
    // printf("Vendor option: %u, Data length: %u\n", option, option_len);
    //  end field code
    if (option == 255) break;

    if (option == 12) {
      char *buf = malloc(option_len + 1);
      const char *data = (const char *)&vend_data[index];
      memcpy(buf, data, option_len);
      buf[option_len] = '\0';
      return buf;
    }

    index += option_len; // Move the index to the next option
  }
  return NULL;
}

size_t calc_vendor_specific_size(struct bootp *sample) {
  return (size_t)&sample->bp_vend - (size_t)sample;
}