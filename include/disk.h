#ifndef DISK_H
#define DISK_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "error_codes.h"

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
 * @brief Структура, представляющая открытый виртуальный диск.
 */
typedef struct {
    char path[MAX_PATH];      /**< Путь к файлу диска */
    uint64_t size;            /**< Размер диска в байтах */
    FILE *file;               /**< Указатель на открытый файл (NULL, если диск не открыт) */
    int is_open;              /**< Флаг: 1 – диск открыт, 0 – закрыт */
} Disk;

/**
 * @brief Открывает существующий файл диска для чтения/записи.
 * @param path Путь к файлу диска.
 * @param disk Указатель на структуру Disk, которая будет заполнена.
 * @return ErrorCode: ERR_OK при успехе, иначе ERR_DISK_OPEN или ERR_DISK_SEEK.
 */
ErrorCode disk_open(const char *path, Disk *disk);

/**
 * @brief Закрывает открытый диск (если он был открыт).
 * @param disk Указатель на структуру Disk.
 */
void disk_close(Disk *disk);

/**
 * @brief Создаёт новый файл диска заданного размера, заполняя его нулями.
 * @param path Путь для нового файла.
 * @param size_bytes Размер в байтах.
 * @param disk Указатель на структуру Disk (после создания диск остаётся открытым).
 * @return ErrorCode: ERR_OK при успехе, иначе ERR_DISK_CREATE, ERR_DISK_WRITE, ERR_OUT_OF_MEMORY.
 */
ErrorCode disk_create(const char *path, uint64_t size_bytes, Disk *disk);

/**
 * @brief Читает данные с диска.
 * @param disk Указатель на открытый диск.
 * @param buffer Буфер для чтения.
 * @param size Количество байт для чтения.
 * @param offset Смещение от начала диска в байтах.
 * @return ErrorCode: ERR_OK при успехе, иначе ERR_DISK_OPEN, ERR_DISK_READ.
 */
ErrorCode disk_read(Disk *disk, void *buffer, uint64_t size, uint64_t offset);

/**
 * @brief Записывает данные на диск.
 * @param disk Указатель на открытый диск.
 * @param data Буфер с данными для записи.
 * @param size Количество байт для записи.
 * @param offset Смещение от начала диска в байтах.
 * @return ErrorCode: ERR_OK при успехе, иначе ERR_DISK_OPEN, ERR_DISK_WRITE.
 */
ErrorCode disk_write(Disk *disk, const void *data, uint64_t size, uint64_t offset);

#endif