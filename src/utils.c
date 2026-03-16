#include "utils.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

char* my_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *dup = (char*)malloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

int parse_part_index(const char *str) {
    if (!str) {
        return -1;
    }
    char *endptr;
    long val = strtol(str, &endptr, 10);
    if (*endptr != '\0' || val < 1 || val > 1000) {
        return -1;
    }
    return (int)(val - 1);
}

bool parse_size(const char *str, uint64_t *bytes) {
    if (!str || !*str) {
        return false;
    }
    char *endptr;
    errno = 0;
    uint64_t val = strtoull(str, &endptr, 0);
    if (errno != 0 || endptr == str) {
        return false;
    }

    while (*endptr == ' ') {
        endptr++;
    }

    char suffix = *endptr;
    if (suffix == '\0') {
        *bytes = val;
        return true;
    }

    if (suffix >= 'a' && suffix <= 'z') {
        suffix -= 32;
    }

    uint64_t multiplier = 1;
    switch (suffix) {
        case 'K':
            multiplier = 1024ULL;
            break;
        case 'M':
            multiplier = 1024ULL * 1024;
            break;
        case 'G':
            multiplier = 1024ULL * 1024 * 1024;
            break;
        default:
            return false;
    }

    if (val > UINT64_MAX / multiplier) {
        return false;
    }
    *bytes = val * multiplier;

    endptr++;
    while (*endptr == ' ') {
        endptr++;
    }
    if (*endptr != '\0') {
        return false;
    }
    return true;
}

bool parse_integer(const char *str, uint64_t *result) {
    if (!str || !*str) {
        return false;
    }
    char *endptr;
    errno = 0;
    uint64_t val = strtoull(str, &endptr, 0);
    if (errno != 0 || endptr == str || *endptr != '\0') {
        return false;
    }
    *result = val;
    return true;
}

void str_toupper(char *str) {
    for (; *str; ++str) {
        *str = toupper((unsigned char)*str);
    }
}

bool strlcpy_safe(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0 || !src) {
        return false;
    }
    size_t src_len = strlen(src);
    if (src_len >= dest_size) {
        return false;
    }
    memcpy(dest, src, src_len + 1);
    return true;
}

ErrorCode read_whole_file(const char *path, uint8_t **buffer, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ERR_DISK_OPEN;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ERR_DISK_READ;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return ERR_DISK_READ;
    }
    rewind(f);
    *buffer = (uint8_t*)malloc(sz);
    if (!*buffer) {
        fclose(f);
        return ERR_OUT_OF_MEMORY;
    }
    size_t read = fread(*buffer, 1, sz, f);
    fclose(f);
    if (read != (size_t)sz) {
        free(*buffer);
        return ERR_DISK_READ;
    }
    *size = sz;
    return ERR_OK;
}