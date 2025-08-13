#ifndef TAP_H
#define TAP_H

#include <stdio.h>

static inline void tap_plan(int n)
{
  printf("1..%d\n", n);
}

static inline void tap_ok(int n, int ok, const char *msg)
{
  printf("%s %d - %s\n", ok ? "ok" : "not ok", n, msg);
}

#endif // TAP_H
