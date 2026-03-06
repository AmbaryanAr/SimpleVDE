#pragma once

#include "error_code.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define SECTOR_SIZE 512
#define MAX_PATH 260

#ifdef _WIN32
    #define fseek64 _fseeki64
    #define ftell64 _ftelli64
#else
    #define fseek64 fseeko
    #define ftell64 ftello
#endif

/**
 * Структура, представляющая открытый виртуальный диск.
 */
typedef struct {
    char path[MAX_PATH];      ///< Путь к файлу диска
    uint64_t size;            ///< Размер диска в байтах
    FILE *file;               ///< Указатель на открытый файл (NULL, если диск не открыт)
    int is_open;              ///< Флаг: 1 – диск открыт, 0 – закрыт
} Disk;

/**
 * Открывает существующий файл диска для чтения/записи.
 *
 * @param path Путь к файлу диска.
 * @param disk Указатель на структуру Disk, которая будет заполнена.
 * @return Код ошибки: ERR_OK при успехе, иначе ERR_DISK_OPEN или ERR_DISK_SEEK.
 */
ErrorCode disk_open(const char *path, Disk *disk);

/**
 * Закрывает открытый диск (если он был открыт).
 *
 * @param disk Указатель на структуру Disk.
 */
void disk_close(Disk *disk);

/**
 * Создаёт новый файл диска заданного размера, заполняя его нулями.
 *
 * @param path Путь для нового файла.
 * @param size_mb Размер в мегабайтах.
 * @param disk Указатель на структуру Disk (после создания диск остаётся открытым).
 * @return Код ошибки: ERR_OK при успехе, иначе ERR_DISK_CREATE, ERR_DISK_WRITE.
 */
ErrorCode disk_create(const char *path, uint64_t size_mb, Disk *disk);

/**
 * Читает данные с диска.
 *
 * @param disk Указатель на открытый диск.
 * @param value Буфер для чтения.
 * @param size_value Количество байт для чтения.
 * @param offset Смещение от начала диска в байтах.
 * @return Код ошибки: ERR_OK при успехе, иначе ERR_DISK_OPEN, ERR_DISK_READ.
 */
ErrorCode disk_read(Disk *disk, void *value, uint32_t size_value, uint64_t offset);

/**
 * Записывает данные на диск.
 *
 * @param disk Указатель на открытый диск.
 * @param value Буфер с данными для записи.
 * @param size_value Количество байт для записи.
 * @param offset Смещение от начала диска в байтах.
 * @return Код ошибки: ERR_OK при успехе, иначе ERR_DISK_OPEN, ERR_DISK_WRITE.
 */
ErrorCode disk_write(Disk *disk, const void *value, uint32_t size_value, uint64_t offset);