#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <unistd.h>

#include "misc.h"
#include "utils.h"

#include "remote_csoloader.h"
#include "zn_spawner.h"

#define ATTACH_TIMEOUT_MS 2000
#define ATTACH_POLL_MS 20

static bool maps_have_libc(int pid) {
  char pid_str[11];
  snprintf(pid_str, sizeof(pid_str), "%d", pid);

  struct maps_info *map = parse_maps(pid_str);
  if (!map) return false;

  bool found = find_module_return_addr(map, "libc.so") != NULL;
  free_maps(map);

  return found;
}

/* The monitor hands over the pid at exec, before the linker mapped libc. */
static bool wait_for_loaded_image(int pid) {
  for (int waited = 0; waited < ATTACH_TIMEOUT_MS; waited += ATTACH_POLL_MS) {
    if (maps_have_libc(pid)) return true;
    if (kill(pid, 0) == -1 && errno == ESRCH) return false;

    usleep(ATTACH_POLL_MS * 1000);
  }

  return false;
}

static bool wait_stopped(int pid, int *status) {
  for (int waited = 0; waited < ATTACH_TIMEOUT_MS; waited += ATTACH_POLL_MS) {
    pid_t waited_pid;
    do {
      waited_pid = waitpid(pid, status, __WALL | WNOHANG);
    } while (waited_pid == -1 && errno == EINTR);

    if (waited_pid == pid) return true;
    if (waited_pid == -1) return false;

    usleep(ATTACH_POLL_MS * 1000);
  }

  return false;
}

bool zn_spawner_inject(int pid, const char *lib_path) {
  if (pid <= 0 || !lib_path) return false;

  if (!wait_for_loaded_image(pid)) {
    LOGE("spawner %d has no usable image", pid);

    return false;
  }

  if (ptrace(PTRACE_ATTACH, pid, 0, 0) == -1) {
    PLOGE("attach %d", pid);

    return false;
  }

  int status = 0;
  if (!wait_stopped(pid, &status) || !WIFSTOPPED(status)) {
    LOGE("spawner %d did not stop", pid);

    ptrace(PTRACE_DETACH, pid, 0, 0);

    return false;
  }

  struct user_regs_struct regs = { 0 };
  struct user_regs_struct base = { 0 };
  struct maps_info *map = NULL;
  struct maps_info *local_map = NULL;
  bool injected = false;

  if (!get_regs(pid, &regs)) {
    LOGE("cannot read registers of %d", pid);

    goto done;
  }
  base = regs;

  char pid_str[11];
  snprintf(pid_str, sizeof(pid_str), "%d", pid);

  map = parse_maps(pid_str);
  local_map = parse_maps("self");
  if (!map || !local_map) {
    LOGE("cannot parse maps of %d", pid);

    goto done;
  }

  uintptr_t remote_base = 0, entry = 0;
  size_t remote_size = 0;

  if (!remote_csoloader_load_and_resolve_symbol(pid, &regs, map, local_map, lib_path, "zn_entry",
                                                &remote_base, &remote_size, &entry)) {
    LOGE("loading %s into %d failed", lib_path, pid);

    goto done;
  }

  uintptr_t return_addr = (uintptr_t)find_module_return_addr(map, "libc.so");
  if (!return_addr) {
    LOGE("no return address in %d", pid);

    goto done;
  }

  long args[2] = { (long)remote_base, (long)remote_size };
  remote_call(pid, &regs, entry, return_addr, args, 2);

  #if defined(__arm__)
    injected = (((uintptr_t)regs.REG_IP & ~1u) == (return_addr & ~1u));
  #else
    injected = ((uintptr_t)regs.REG_IP == return_addr);
  #endif

  if (!injected) LOGE("zn_entry faulted in %d at %p", pid, (void *)regs.REG_IP);

done:
  free_maps(map);
  free_maps(local_map);

  /* A remote call leaves the tracee at the sentinel; rewind before resuming. */
  set_regs(pid, &base);
  if (ptrace(PTRACE_DETACH, pid, 0, 0) == -1) PLOGE("detach %d", pid);

  return injected;
}
