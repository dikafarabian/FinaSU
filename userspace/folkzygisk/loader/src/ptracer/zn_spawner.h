#ifndef ZN_SPAWNER_H
#define ZN_SPAWNER_H

#include <stdbool.h>

/* Injects libzygisk.so into a running HyperOS spawner and calls zn_entry. */
bool zn_spawner_inject(int pid, const char *lib_path);

#endif /* ZN_SPAWNER_H */
