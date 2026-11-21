#ifndef DAEMON_H
#define DAEMON_H

#include <stdbool.h>

#include <unistd.h>

enum rezygiskd_actions {
  ZygoteInjected,
  GetProcessFlags,
  GetInfo,
  ReadModules,
  RequestCompanionSocket,
  GetModuleDir,
  ZygoteRestart,
  UpdateMountNamespace,
  RemoveModule,
  ReadZnModules,
  RequestZnCompanionSocket
};

#define ZN_LIB_MAX 512

struct zn_module_ref {
  char path[ZN_LIB_MAX];
  int fd;
  bool companion;
};

struct zygisk_modules {
  char **modules;
  size_t modules_count;
};

enum root_impl {
  ROOT_IMPL_NONE,
  ROOT_IMPL_APATCH,
  ROOT_IMPL_KERNELSU,
  ROOT_IMPL_MAGISK
};

struct rezygisk_info {
  struct zygisk_modules modules;
  enum root_impl root_impl;
  pid_t pid;
  bool running;
};

enum mount_namespace_state {
  Clean,
  Mounted
};

#include "folkzygisk_paths.h"
#define TMP_PATH FOLKZYGISK_RUNTIME_DIR

static inline const char *rezygiskd_get_path() {
  return TMP_PATH;
}

bool rezygiskd_zygote_injected();

uint32_t rezygiskd_get_process_flags(uid_t uid, const char *const process);

void rezygiskd_get_info(struct rezygisk_info *info);

void free_rezygisk_info(struct rezygisk_info *info);

bool rezygiskd_read_modules(struct zygisk_modules *modules);

void free_modules(struct zygisk_modules *modules);

int rezygiskd_connect_companion(size_t index);

int rezygiskd_get_module_dir(size_t index);

size_t rezygiskd_read_zn_modules(const char *process, const char *exe,
                                 struct zn_module_ref *refs, size_t capacity);

int rezygiskd_request_zn_companion(const char *path);

void rezygiskd_zygote_restart();

bool rezygiskd_update_mns(enum mount_namespace_state nms_state, char *buf, size_t buf_size);

bool rezygiskd_remove_module(size_t index);

#endif /* DAEMON_H */
