#include "pcap_dhcp.h"
#include "../syslog2/syslog2.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../test_util.h"

static void test_calc_vendor_specific_size(void) {
  PRINT_TEST_START("calc_vendor_specific_size");
  struct bootp sample;
  size_t expected = (size_t)&sample.bp_vend - (size_t)&sample;
  size_t got = calc_vendor_specific_size(&sample);
  assert(got == expected);
  PRINT_TEST_PASSED();
}

static void test_parse_vendor_specific_option_12(void) {
  PRINT_TEST_START("parse_vendor_specific_option_12");
  const nd_byte vend[] = {99, 130, 83, 99, 12, 4, 't', 'e', 's', 't', 255};
  char *hostname = parse_vendor_specific_option_12(vend, sizeof(vend));
  assert(hostname != NULL);
  assert(strcmp(hostname, "test") == 0);
  free(hostname);
  PRINT_TEST_PASSED();
}

static void test_tok2str(void) {
  PRINT_TEST_START("tok2str");
  const struct tok tokens[] = {{1, "one"}, {2, "two"}, {0, NULL}};
  const char *s1 = tok2str(tokens, "unknown", 2);
  assert(strcmp(s1, "two") == 0);
  const char *s2 = tok2str(tokens, "val-%d", 5);
  assert(strcmp(s2, "val-5") == 0);
  PRINT_TEST_PASSED();
}

int main(int argc, char **argv) {
  struct test_entry tests[] = {
      {"calc_vendor_specific_size", test_calc_vendor_specific_size},
      {"parse_vendor_specific_option_12", test_parse_vendor_specific_option_12},
      {"tok2str", test_tok2str}};

  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (!rc && argc == 1)
    printf(KGRN "====== All libpcap-dhcp-capture tests passed! ======\n" KNRM);
  return rc;
}
