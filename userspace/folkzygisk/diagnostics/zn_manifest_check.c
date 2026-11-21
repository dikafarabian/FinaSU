#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool valid_library(const char *path) {
    for (const char *part = path; *part;) {
        size_t len = strcspn(part, "/");
        if (!len || (len == 1 && part[0] == '.') ||
            (len == 2 && memcmp(part, "..", 2) == 0)) return false;
        if (!part[len]) return len > 3 && strcmp(part + len - 3, ".so") == 0;
        part += len + 1;
    }
    return false;
}

static bool parse(char *line, size_t *entries, size_t *companions) {
    char *target = strtok(line, " \t\r");
    if (!target || target[0] == '#') return true;
    if (strncmp(target, "name=", 5) == 0) {
        if (!target[5] || strchr(target + 5, '/')) return false;
    } else if (strncmp(target, "path=/", 6) != 0 || !target[6]) {
        return false;
    }
    char *library = strtok(NULL, " \t\r");
    bool companion = library && strcmp(library, "companion") == 0;
    if (companion) library = strtok(NULL, " \t\r");
    if (!library || !valid_library(library) || strtok(NULL, " \t\r")) return false;
    ++*entries;
    *companions += companion;
    return true;
}

int main(int argc, char **argv) {
    if (argc != 1) {
        fprintf(stderr, "Usage: %s < zn_modules.txt\n", argv[0]);
        return 2;
    }
    char line[4096];
    size_t len = 0, number = 1, entries = 0, companions = 0;
    int c;
    while ((c = getchar()) != EOF) {
        if (c == '\n') {
            line[len] = '\0';
            if (!parse(line, &entries, &companions)) goto invalid;
            len = 0;
            ++number;
        } else {
            if (len == sizeof(line) - 1 || (c < 32 && c != '\t' && c != '\r') || c == 127)
                goto invalid;
            line[len++] = (char)c;
        }
    }
    if (ferror(stdin)) {
        perror("read manifest");
        return 1;
    }
    line[len] = '\0';
    if (!parse(line, &entries, &companions)) goto invalid;
    printf("entries=%zu companion=%zu\n", entries, companions);
    return 0;

invalid:
    fprintf(stderr, "Unsupported or malformed declaration at line %zu\n", number);
    return 1;
}
