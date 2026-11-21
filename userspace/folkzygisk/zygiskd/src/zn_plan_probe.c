#include <stdio.h>

#include "zn_plan.h"

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <modules-root> <process-name> <exe-path>\n", argv[0]);
    return 2;
  }

  struct zn_plan plan;
  size_t count = zn_plan_collect(&plan, argv[1], argv[2], argv[3]);
  printf("count=%zu\n", count);
  for (size_t i = 0; i < count; ++i)
    printf("module=%s companion=%d\n", plan.entries[i].path, plan.entries[i].companion ? 1 : 0);

  zn_plan_close(&plan);
  return 0;
}
