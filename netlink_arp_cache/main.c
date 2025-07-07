#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "libnlarpcache.h"
#include "../syslog2/syslog2.h"

#define NEXT_ARG() do { argv++; if (--argc <= 0) incomplete_command(); } while(0)
#define NEXT_ARG_OK() (argc - 1 > 0)
#define PREV_ARG() do { argv--; argc++; } while(0)

static char *argv0;

static void usage(void) {
  fprintf(stdout,
          "Usage:   %s [option] [value]\n"
          "            option: ip | mac | help\n"
          "\n"
          "Example: %s ip 192.168.1.1\n"
          "         %s mac 00:ff:12:a3:e3\n"
          "\n",
          argv0,
          argv0,
          argv0);
  exit(-1);
}

static bool matches(const char *prefix, const char *string) {
  if (!*prefix) return false;
  while (*string && *prefix == *string) {
    prefix++;
    string++;
  }
  return !*prefix;
}

static void incomplete_command(void) {
  fprintf(stderr, "Command line is not complete. Try option \"help\"\n");
  exit(-1);
}

int main(int argc, char **argv) {
  const char *val_search = NULL;
  int mode = 0;

  argv0 = *argv;
  while (argc > 1) {
    NEXT_ARG();
    if (matches(*argv, "ip")) {
      mode = 1;
      NEXT_ARG();
      val_search = *argv;
    } else if (matches(*argv, "mac")) {
      mode = 2;
      NEXT_ARG();
      val_search = *argv;
    } else if (matches(*argv, "help")) {
      usage();
    } else {
      usage();
    }
    argc--;
    argv++;
  }

  void *buf;
  int64_t status = get_arp_cache(&buf);
  if (status < 0) {
    syslog2(LOG_ERR, "get_arp_cache %ld %d", status, errno);
    return status;
  }

  int64_t buf_size = status;
  uint32_t expected_entries = buf_size < 50 ? 1 : buf_size / 50;
  arp_cache cache[expected_entries];
  memset(cache, 0, sizeof(cache));

  status = parse_arp_cache(buf, buf_size, cache);
  int64_t cnt = status;
  if (status < 0) {
    syslog2(LOG_ERR, "parse_arp_cache %ld %d", status, errno);
  }

  while (cnt--) {
    uint8_t ndm_family = cache[cnt].ndm_family;
    struct rtattr **tb = cache[cnt].tb;
    if (cache[cnt].ndm_state & NUD_NOARP) {
      continue;
    }

    char ip[INET6_ADDRSTRLEN] = {0};
    if (tb[NDA_DST]) {
      const char *ip_raw = RTA_DATA(tb[NDA_DST]);
      inet_ntop(ndm_family, ip_raw, ip, INET6_ADDRSTRLEN);
    }

    char addr[sizeof("00:00:00:00:00:00") + 1] = {0};
    if (tb[NDA_LLADDR]) {
      const uint8_t *addr_raw = RTA_DATA(tb[NDA_LLADDR]);
      sprintf(addr,
              "%02x:%02x:%02x:%02x:%02x:%02x",
              addr_raw[0],
              addr_raw[1],
              addr_raw[2],
              addr_raw[3],
              addr_raw[4],
              addr_raw[5]);
    }

    switch (mode) {
      case 0:
        fprintf(stdout, "%s lladdr %s\n", ip, addr);
        break;
      case 1:
        if (strcmp(val_search, ip) == 0) {
          fprintf(stdout, "%s laddr %s\n", ip, addr);
          return 0;
        }
        break;
      case 2:
        if (strcmp(val_search, addr) == 0) {
          fprintf(stdout, "%s lladdr %s\n", ip, addr);
          return 0;
        }
        break;
      default:
        break;
    }
  }
  free(buf);
  return 0;
}
