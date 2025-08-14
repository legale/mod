#include "dbg_tracer.h"
#include <assert.h>
#include <string.h>

int main(void) {
  char buf[8];
  int n = safe_snprintf(buf, sizeof(buf), "%s", "abc");
  assert(n == 3);
  assert(strcmp(buf, "abc") == 0);
  tracer_setup();
  trace_reg_attach();
  return 0;
}
