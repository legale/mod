/* minimal test runner with junit xml */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include "tap.h"

extern int run_unit_tests(void);
extern int run_thread_tests(void);
extern int run_stress_tests(void);

static const char *xml_path = NULL;

/* no punctuation in comments */
static void write_junit_xml(const char *name, int total, int failed){
  if(!xml_path) return;
  FILE *f = fopen(xml_path, "w");
  if(!f) return;
  fprintf(f, "<testsuite name=\"%s\" tests=\"%d\" failures=\"%d\">", name, total, failed);
  fprintf(f, "</testsuite>\n");
  fclose(f);
}

int main(int argc, char **argv){
  int i, is_tap=0;
  const char *suite="unit";
  for(i=1;i<argc;i++){
    if(!strcmp(argv[i],"--tap")) is_tap=1;
    else if(!strcmp(argv[i],"--junit") && i+1<argc) xml_path=argv[++i];
    else if(!strncmp(argv[i],"--suite=",8)) suite=argv[i]+8;
  }
  int total=0, failed=0, rc=0;
  if(!strcmp(suite,"unit")) rc=run_unit_tests();
  else if(!strcmp(suite,"thread")) rc=run_thread_tests();
  else if(!strcmp(suite,"stress")) rc=run_stress_tests();
  else return 2;
  if(rc<0) return 2;
  failed = rc;
  total = 0;
  write_junit_xml(suite, total, failed);
  return failed ? 1 : 0;
}
