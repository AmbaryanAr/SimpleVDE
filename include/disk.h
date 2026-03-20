#ifndef DISK_H
#define DISK_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "error_codes.h"

#define MAX_ARGS 10
#define MAX_PATH 512
#define SECTOR_SIZE 512
#define MAX_CMD_LINE 1024

#ifdef _WIN32
    #define fseek64 _fseeki64
    #define ftell64 _ftelli64
#else
    #define fseek64 fseeko
    #define ftell64 ftello
#endif

typedef struct {
    char path[MAX_PATH];
    uint64_t size;
    FILE *file;
    int is_open;
} Disk;

/** Открывает существующий файл диска. */
ErrorCode disk_open(const char *path, Disk *disk);

/** Закрывает диск. */
void disk_close(Disk *disk);

/** Создаёт новый файл диска заданного размера, заполняя нулями. */
ErrorCode disk_create(const char *path, uint64_t size_bytes, Disk *disk);

/** Читает данные с диска. */
ErrorCode disk_read(Disk *disk, void *buffer, uint64_t size, uint64_t offset);

/** Записывает данные на диск. */
ErrorCode disk_write(Disk *disk, const void *data, uint64_t size, uint64_t offset);

#endif