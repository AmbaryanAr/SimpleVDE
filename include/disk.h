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

// Кроссплатформенные макросы для 64-битного позиционирования в файле
#if defined(__MINGW32__) || defined(_WIN32)
    #define fseek64 _fseeki64
    #define ftell64 _ftelli64
#else
    #define fseek64 fseeko
    #define ftell64 ftello
#endif

// Представление открытого файла образа диска
typedef struct {
    char path[MAX_PATH];    // путь к файлу образа
    uint64_t size;          // размер образа в байтах
    FILE *file;             // файловый дескриптор
    int is_open;            // флаг: 1 — открыт, 0 — закрыт
} Disk;

// Открывает существующий файл образа для чтения и записи (rb+).
// При успехе заполняет поля size и is_open.
ErrorCode disk_open(const char *path, Disk *disk);

// Закрывает файл образа и сбрасывает флаг is_open.
void disk_close(Disk *disk);

// Создаёт новый файл образа заданного размера, заполняя нулями.
// При успехе файл остаётся открытым, поля структуры заполнены.
ErrorCode disk_create(const char *path, uint64_t size_bytes, Disk *disk);

// Читает size байт с диска, начиная со смещения offset, в буфер buffer.
ErrorCode disk_read(Disk *disk, void *buffer, uint64_t size, uint64_t offset);

// Записывает size байт из data на диск, начиная со смещения offset.
ErrorCode disk_write(Disk *disk, const void *data, uint64_t size, uint64_t offset);

#endif