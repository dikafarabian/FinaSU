#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zygisk_next_api.h"

static FILE *output;

static void report(const char *format, int value) {
  if (output) fprintf(output, format, value);
}

static void on_loaded(void *handle, const struct ZygiskNextAPI *api) {
  report("loaded=%d\n", 1);
  report("api=%d\n", api != NULL);
  report("runtime=%d\n", api->getRuntime != NULL && api->getRuntime() != NULL);
  report("inline=%d\n", api->inlineHook == NULL ? -1 : api->inlineHook(NULL, NULL, NULL));

  struct ZnSymbolResolver *resolver = api->newSymbolResolver("libc.so", NULL);
  report("resolver=%d\n", resolver != NULL);
  if (resolver) {
    size_t size = 0;
    void *exact = api->symbolLookup(resolver, "getpid", false, &size);
    report("getpid=%d\n", exact != NULL && exact == (void *)getpid);
    report("size=%d\n", size > 0);
    report("prefix=%d\n", api->symbolLookup(resolver, "getp", true, NULL) != NULL);
    api->freeSymbolResolver(resolver);
  }

  int fd = api->connectCompanion(handle);
  report("companion_fd=%d\n", fd >= 0);
  if (fd >= 0) {
    char byte = 0;
    report("companion_io=%d\n", write(fd, "Q", 1) == 1 && read(fd, &byte, 1) == 1 && byte == 'A');
    close(fd);
  }
}

static void companion_loaded(void) {
  report("companion_loaded=%d\n", 1);
}

static void companion_connected(int fd) {
  char byte = 0;
  if (read(fd, &byte, 1) == 1 && byte == 'Q') write(fd, "A", 1);
  close(fd);
}

struct ZygiskNextCompanionModule zn_companion_module = {
    .target_api_version = ZYGISK_NEXT_API_VERSION,
    .onCompanionLoaded = companion_loaded,
    .onModuleConnected = companion_connected,
};

struct ZygiskNextModule zn_module = {
    .target_api_version = ZYGISK_NEXT_API_VERSION,
    .onModuleLoaded = on_loaded,
};

__attribute__((constructor)) static void open_output(void) {
  const char *path = getenv("ZN_TEST_OUT");
  output = fopen(path ? path : "/data/local/tmp/zn-injected.out", "a");
}
