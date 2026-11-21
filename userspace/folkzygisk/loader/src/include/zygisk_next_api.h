#ifndef ZYGISK_NEXT_API_H
#define ZYGISK_NEXT_API_H

/*
  Compatibility surface for native modules built against the Zygisk Next module
  ABI. Identifiers and field order are the interoperability contract; only the
  declarations a module needs to compile and bind are present here.
*/

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZYGISK_NEXT_API_VERSION 4
#define ZYGISK_NEXT_HYOS_API_VERSION 1

#define ZN_SUCCESS 0
#define ZN_FAILED 1

struct ZnSymbolResolver;
struct ZygiskNextRuntime;

struct ZygiskNextAPI {
  int (*pltHook)(void *base_addr, const char *symbol, void *hook_handler, void **original);
  int (*inlineHook)(void *target, void *addr, void **original);
  int (*inlineUnhook)(void *target);
  struct ZnSymbolResolver *(*newSymbolResolver)(const char *path, void *base_addr);
  void (*freeSymbolResolver)(struct ZnSymbolResolver *resolver);
  void *(*getBaseAddress)(struct ZnSymbolResolver *resolver);
  void *(*symbolLookup)(struct ZnSymbolResolver *resolver, const char *name, bool prefix,
                        size_t *size);
  void (*forEachSymbols)(struct ZnSymbolResolver *resolver,
                         bool (*callback)(const char *name, void *addr, size_t size, void *data),
                         void *data);
  int (*connectCompanion)(void *handle);
  const struct ZygiskNextRuntime *(*getRuntime)(void);
};

struct ZygiskNextModule {
  int target_api_version;
  void (*onModuleLoaded)(void *self_handle, const struct ZygiskNextAPI *api);
};

enum ZygiskNextRuntimeType {
  ZN_RUNTIME_HYOS = 1,
};

struct ZnHyosAppSpecializeArgs {
  const char *process_name;
  const char *package_name;
  const char *se_info;
};

struct ZygiskNextHyosModule {
  int target_api_version;
  void (*onAppSpecialized)(const struct ZnHyosAppSpecializeArgs *args);
};

struct ZygiskNextRuntime {
  enum ZygiskNextRuntimeType type;
  int api_version;
  int (*registerModule)(const void *module);
};

struct ZygiskNextCompanionModule {
  int target_api_version;
  void (*onCompanionLoaded)(void);
  void (*onModuleConnected)(int fd);
};

#ifdef __cplusplus
}
#endif

#endif /* ZYGISK_NEXT_API_H */
