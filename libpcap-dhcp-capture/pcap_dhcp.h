#ifndef __PCAP_DHCP_H
#define __PCAP_DHCP_H 1

#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <pcap.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * an IPv4 address from a packet data buffer; it was introduced in reaction
 * to somebody who *had* done that.
 */
typedef unsigned char nd_ipv4[4];

/*
 * Use this for blobs of bytes; make them arrays of nd_byte.
 */
typedef unsigned char nd_byte;

/*
 * Data types corresponding to multi-byte integral values within data
 * structures.  These are defined as arrays of octets, so that they're
 * not aligned on their "natural" boundaries, and so that you *must*
 * use the EXTRACT_ macros to extract them (which you should be doing
 * *anyway*, so as not to assume a particular byte order or alignment
 * in your code).
 *
 * We even want EXTRACT_U_1 used for 8-bit integral values, so we
 * define nd_uint8_t and nd_int8_t as arrays as well.
 */
typedef unsigned char nd_uint8_t[1];
typedef unsigned char nd_uint16_t[2];
typedef unsigned char nd_uint24_t[3];
typedef unsigned char nd_uint32_t[4];
typedef unsigned char nd_uint40_t[5];
typedef unsigned char nd_uint48_t[6];
typedef unsigned char nd_uint56_t[7];
typedef unsigned char nd_uint64_t[8];

typedef signed char nd_int8_t[1];

/*
 * Bootstrap Protocol (BOOTP).  RFC951 and RFC1048.
 *
 * This file specifies the "implementation-independent" BOOTP protocol
 * information which is common to both client and server.
 *
 * Copyright 1988 by Carnegie Mellon.
 *
 * Permission to use, copy, modify, and distribute this program for any
 * purpose and without fee is hereby granted, provided that this copyright
 * and permission notice appear on all copies and supporting documentation,
 * the name of Carnegie Mellon not be used in advertising or publicity
 * pertaining to distribution of the program without specific prior
 * permission, and notice be given in supporting documentation that copying
 * and distribution is by permission of Carnegie Mellon and Stanford
 * University.  Carnegie Mellon makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 */

struct bootp {
  nd_uint8_t bp_op;            /* packet opcode type */
  nd_uint8_t bp_htype;         /* hardware addr type */
  nd_uint8_t bp_hlen;          /* hardware addr length */
  nd_uint8_t bp_hops;          /* gateway hops */
  nd_uint32_t bp_xid;          /* transaction ID */
  nd_uint16_t bp_secs;         /* seconds since boot began */
  nd_uint16_t bp_flags;        /* flags - see bootp_flag_values[]
                                  in print-bootp.c */
  nd_ipv4 bp_ciaddr;           /* client IP address */
  nd_ipv4 bp_yiaddr;           /* 'your' IP address */
  nd_ipv4 bp_siaddr;           /* server IP address */
  nd_ipv4 bp_giaddr;           /* gateway IP address */
  nd_byte bp_chaddr[16];       /* client hardware address */
  nd_byte bp_sname[64];        /* server host name */
  nd_byte bp_file[128];        /* boot file name */
  nd_byte bp_vend[64];         /* vendor-specific area */
};



struct vlan_header {
  uint16_t ether_type; /* packet type ID field	*/
  uint16_t vlanid;     /* packet type ID field	*/
} __attribute__((__packed__));

#define BOOTPREPLY 2
#define BOOTPREQUEST 1
#define TOKBUFSIZE 128

struct tok {
  u_int v;       /* value */
  const char *s; /* string */
};

typedef struct pcap_dhcp_user {
  void (*callback)(); /* user callback function which called by dhcp_packet_handler */
  void *callback_arg; /* callback_arg */
} pcap_dhcp_user_s;

pcap_t *dhcp_pcap_open_live(const char *device);

const char *tok2str(const struct tok *lp, const char *fmt, const u_int v);

void dhcp_packet_handler(u_char *args, const struct pcap_pkthdr *h, const uint8_t *p);
char *parse_vendor_specific_option_12(const nd_byte *vend_data, size_t vend_len);
size_t calc_vendor_specific_size(struct bootp *sample);
#endif /* pcap_dhcp.h */