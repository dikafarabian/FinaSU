#ifndef ZN_API_H
#define ZN_API_H

#include <stdbool.h>

/* Loads the Zygisk Next modules declared for this process, if any. */
void zn_load_modules(void);

/* False for the zygote image, which ZN manifests never target. */
bool zn_process_is_target(void);

#endif /* ZN_API_H */
