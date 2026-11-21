#ifndef ZN_PLAN_H
#define ZN_PLAN_H

#include <stdbool.h>
#include <stddef.h>

#define ZN_PLAN_MAX 16
#define ZN_LIB_MAX 512

struct zn_plan_entry {
  char path[ZN_LIB_MAX];
  int fd;
  bool companion;
};

struct zn_plan {
  struct zn_plan_entry entries[ZN_PLAN_MAX];
  size_t count;
};

void zn_plan_close(struct zn_plan *plan);

size_t zn_plan_collect(struct zn_plan *plan, const char *modules_root,
                       const char *process_name, const char *exe_path);

#endif /* ZN_PLAN_H */
