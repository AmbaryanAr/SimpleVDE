#include "disk.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>

static bool disk_seek(FILE *file, uint64_t offset) {
    return fseek64(file, offset, SEEK_SET) == 0;
}

static size_t disk_read_at(FILE *file, void *buffer, uint64_t size, uint64_t offset) {
    if (!disk_seek(file, offset)) return 0;
    return fread(buffer, 1, size, file);
}

static size_t disk_write_at(FILE *file, const void *data, uint64_t size, uint64_t offset) {
    if (!disk_seek(file, offset)) return 0;
    return fwrite(data, 1, size, file);
}

ErrorCode disk_open(const char *path, Disk *disk) {
    if (!path || !disk) return ERR_DISK_OPEN;

    FILE *f = fopen(path, "rb+");
    if (!f) return ERR_DISK_OPEN;
setvbuf(f, NULL, _IONBF, 0);

    // Определяем размер файла
    if (fseek64(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ERR_DISK_SEEK;
    }
    int64_t sz = ftell64(f);
    if (sz == -1) {
        fclose(f);
        return ERR_DISK_SEEK;
    }
    rewind(f);

    strncpy(disk->path, path, MAX_PATH - 1);
    disk->path[MAX_PATH - 1] = '\0';
    disk->size = (uint64_t)sz;
    disk->file = f;
    disk->is_open = 1;
    return ERR_OK;
}

void disk_close(Disk *disk) {
    if (disk && disk->is_open && disk->file) {
        fclose(disk->file);
        disk->file = NULL;
        disk->is_open = 0;
    }
}

ErrorCode disk_create(const char *path, uint64_t size_bytes, Disk *disk) {
    if (!path || !disk) return ERR_DISK_CREATE;

    FILE *f = fopen(path, "wb+");
    if (!f) return ERR_DISK_OPEN;
setvbuf(f, NULL, _IONBF, 0);

    // Буфер для записи нулей (1 MB)
    size_t buf_size = 1024 * 1024;
    uint8_t *buffer = (uint8_t*)malloc(buf_size);
    if (!buffer) {
        fclose(f);
        return ERR_OUT_OF_MEMORY;
    }
    memset(buffer, 0, buf_size);

    uint64_t remaining = size_bytes;
    while (remaining > 0) {
        size_t to_write = (remaining < buf_size) ? (size_t)remaining : buf_size;
        size_t written = fwrite(buffer, 1, to_write, f);
        if (written != to_write) {
            free(buffer);
            fclose(f);
            return ERR_DISK_WRITE;
        }
        remaining -= written;
    }

    free(buffer);
    rewind(f);

    strncpy(disk->path, path, MAX_PATH - 1);
    disk->path[MAX_PATH - 1] = '\0';
    disk->size = size_bytes;
    disk->file = f;
    disk->is_open = 1;

    return ERR_OK;
}

ErrorCode disk_read(Disk *disk, void *buffer, uint64_t size, uint64_t offset) {
    if (!disk || !disk->is_open || !disk->file) return ERR_DISK_OPEN;
    if (offset + size > disk->size) return ERR_DISK_READ;
    size_t read_bytes = disk_read_at(disk->file, buffer, size, offset);
    if (read_bytes != size) return ERR_DISK_READ;
    return ERR_OK;
}

ErrorCode disk_write(Disk *disk, const void *data, uint64_t size, uint64_t offset) {
    if (!disk || !disk->is_open || !disk->file) return ERR_DISK_OPEN;
    if (offset + size > disk->size) return ERR_DISK_WRITE;
    size_t written = disk_write_at(disk->file, data, size, offset);
    if (written != size) return ERR_DISK_WRITE;
    return ERR_OK;
}