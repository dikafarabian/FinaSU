#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/limits.h>

#include "zn_plan.h"

#if defined(__aarch64__)
  #define ZN_ELF_CLASS ELFCLASS64
  #define ZN_ELF_MACHINE EM_AARCH64
#elif defined(__arm__)
  #define ZN_ELF_CLASS ELFCLASS32
  #define ZN_ELF_MACHINE EM_ARM
#elif defined(__x86_64__)
  #define ZN_ELF_CLASS ELFCLASS64
  #define ZN_ELF_MACHINE EM_X86_64
#elif defined(__i386__)
  #define ZN_ELF_CLASS ELFCLASS32
  #define ZN_ELF_MACHINE EM_386
#else
  #error "Unsupported architecture"
#endif

static bool join_path(char *out, size_t capacity, const char *directory, const char *leaf) {
  size_t directory_len = strlen(directory);
  size_t leaf_len = strlen(leaf);
  if (directory_len + leaf_len + 2 > capacity) return false;
  memcpy(out, directory, directory_len);
  out[directory_len] = '/';
  memcpy(out + directory_len + 1, leaf, leaf_len + 1);
  return true;
}

struct zn_declaration {
  const char *target;
  const char *library;
  bool companion;
};

static bool is_separator(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Returns false for a declaration this loader does not claim to understand. */
static bool parse_declaration(char *line, struct zn_declaration *decl) {
  char *tokens[3];
  size_t count = 0;

  char *cursor = line;
  while (*cursor != '\0') {
    while (is_separator(*cursor)) ++cursor;
    if (*cursor == '\0') break;
    if (count == 0 && *cursor == '#') return true;
    if (count == 3) return false;
    tokens[count++] = cursor;
    while (*cursor != '\0' && !is_separator(*cursor)) ++cursor;
    if (*cursor != '\0') *cursor++ = '\0';
  }

  if (count == 0) return true;

  const char *target = tokens[0];
  if (strncmp(target, "name=", 5) == 0) {
    if (target[5] == '\0' || strchr(target + 5, '/') != NULL) return false;
  } else if (strncmp(target, "path=", 5) == 0) {
    if (target[5] != '/') return false;
  } else {
    return false;
  }

  size_t library_index = 1;
  bool companion = false;
  if (count == 3) {
    if (strcmp(tokens[1], "companion") != 0) return false;
    companion = true;
    library_index = 2;
  } else if (count != 2) {
    return false;
  }

  decl->target = target;
  decl->library = tokens[library_index];
  decl->companion = companion;
  return true;
}

static bool library_path_ok(const char *path) {
  size_t len = strlen(path);
  if (len < 4 || len >= ZN_LIB_MAX) return false;
  if (path[0] == '/' || strcmp(path + len - 3, ".so") != 0) return false;

  const char *part = path;
  while (*part != '\0') {
    size_t span = strcspn(part, "/");
    if (span == 0) return false;
    if ((span == 1 && part[0] == '.') ||
        (span == 2 && memcmp(part, "..", 2) == 0)) return false;
    part += span;
    if (*part == '/') ++part;
  }

  return true;
}

static bool target_matches(const char *target, const char *process_name,
                           const char *exe_path) {
  if (strncmp(target, "name=", 5) == 0)
    return strcmp(target + 5, process_name) == 0;
  return exe_path != NULL && strcmp(target + 5, exe_path) == 0;
}

static bool contained_by(const char *path, const char *directory) {
  size_t len = strlen(directory);
  return strncmp(path, directory, len) == 0 && path[len] == '/';
}

/* Rejects a swapped or replaced file after open, and anything outside the module. */
static bool fd_is_usable(int fd, const char *module_dir) {
  char resolved[PATH_MAX];
  char proc_path[64];
  snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);

  ssize_t len = readlink(proc_path, resolved, sizeof(resolved) - 1);
  if (len <= 0 || (size_t)len >= sizeof(resolved) - 1) return false;
  resolved[len] = '\0';
  if (!contained_by(resolved, module_dir)) return false;

  struct stat st;
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 52) return false;

  unsigned char ident[EI_NIDENT + 4];
  ssize_t got = pread(fd, ident, sizeof(ident), 0);
  if (got != (ssize_t)sizeof(ident)) return false;
  if (memcmp(ident, ELFMAG, SELFMAG) != 0) return false;
  if (ident[EI_CLASS] != ZN_ELF_CLASS || ident[EI_DATA] != ELFDATA2LSB) return false;

  uint16_t type = 0;
  uint16_t machine = 0;
  memcpy(&type, ident + EI_NIDENT, sizeof(type));
  memcpy(&machine, ident + EI_NIDENT + sizeof(type), sizeof(machine));
  return type == ET_DYN && machine == ZN_ELF_MACHINE;
}

void zn_plan_close(struct zn_plan *plan) {
  for (size_t i = 0; i < plan->count; ++i) {
    if (plan->entries[i].fd >= 0) close(plan->entries[i].fd);
    plan->entries[i].fd = -1;
  }
  plan->count = 0;
}

static bool add_entry(struct zn_plan *plan, const char *module_dir,
                      const char *path, bool companion) {
  for (size_t i = 0; i < plan->count; ++i) {
    if (strcmp(plan->entries[i].path, path) == 0) {
      plan->entries[i].companion |= companion;
      return true;
    }
  }
  if (plan->count >= ZN_PLAN_MAX) return false;

  struct zn_plan_entry *entry = &plan->entries[plan->count];
  if (strlen(path) >= sizeof(entry->path)) return false;
  entry->fd = open(path, O_RDONLY | O_CLOEXEC);
  if (entry->fd == -1) return false;
  if (!fd_is_usable(entry->fd, module_dir)) {
    close(entry->fd);
    entry->fd = -1;
    return false;
  }
  strcpy(entry->path, path);
  entry->companion = companion;
  ++plan->count;
  return true;
}

static void scan_manifest(struct zn_plan *plan, const char *modules_root,
                          const char *module_name, const char *process_name,
                          const char *exe_path) {
  char module_dir[PATH_MAX];
  char marker[PATH_MAX];
  char manifest[PATH_MAX];
  if (!join_path(module_dir, sizeof(module_dir), modules_root, module_name) ||
      !join_path(marker, sizeof(marker), module_dir, "disable") ||
      !join_path(manifest, sizeof(manifest), module_dir, "zn_modules.txt"))
    return;

  struct stat st;
  if (stat(marker, &st) == 0) return;
  if (!join_path(marker, sizeof(marker), module_dir, "remove") ||
      stat(marker, &st) == 0)
    return;

  FILE *file = fopen(manifest, "re");
  if (file == NULL) return;

  size_t first = plan->count;
  char line[2048];
  bool readable = true;
  while (fgets(line, sizeof(line), file) != NULL) {
    size_t len = strlen(line);
    if (len == sizeof(line) - 1 && line[len - 1] != '\n') {
      readable = false;
      break;
    }
    struct zn_declaration decl = { 0 };
    if (!parse_declaration(line, &decl)) {
      readable = false;
      break;
    }
    if (decl.target == NULL || !target_matches(decl.target, process_name, exe_path))
      continue;
    if (!library_path_ok(decl.library)) {
      readable = false;
      break;
    }

    char library[PATH_MAX];
    if (!join_path(library, sizeof(library), module_dir, decl.library) ||
        !add_entry(plan, module_dir, library, decl.companion)) {
      readable = false;
      break;
    }
  }

  if (!readable || ferror(file)) {
    while (plan->count > first) close(plan->entries[--plan->count].fd);
  }
  fclose(file);
}

size_t zn_plan_collect(struct zn_plan *plan, const char *modules_root,
                       const char *process_name, const char *exe_path) {
  plan->count = 0;
  for (size_t i = 0; i < ZN_PLAN_MAX; ++i) plan->entries[i].fd = -1;

  DIR *dir = opendir(modules_root);
  if (dir == NULL) {
    plan->count = 0;
    return 0;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') continue;
    if (entry->d_type != DT_DIR && entry->d_type != DT_LNK && entry->d_type != DT_UNKNOWN)
      continue;
    scan_manifest(plan, modules_root, entry->d_name, process_name, exe_path);
  }

  closedir(dir);

  return plan->count;
}
