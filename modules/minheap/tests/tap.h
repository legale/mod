/* tiny tap output */
#ifndef TAP_MINI_H
#define TAP_MINI_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int tap_cnt, tap_fail;
static void tap_plan(int n){ printf("1..%d\n", n); }
static void tap_ok(int ok, const char *name){
  tap_cnt++;
  if(ok) printf("ok %d - %s\n", tap_cnt, name);
  else { printf("not ok %d - %s\n", tap_cnt, name); tap_fail++; }
}
static int tap_status(void){ return tap_fail ? 1 : 0; }
#endif
