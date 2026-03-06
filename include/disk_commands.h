#pragma once

#include "disk.h"
#include "mbr_commands.h"
#include "gpt_commands.h"
#include "error_code.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define HEX_DUMP_WIDTH 16

/**
 * Параметры для создания виртуального диска.
 */
typedef struct {
    char path[MAX_PATH];      // Путь к файлу диска
    uint64_t size_mb;         // Размер диска в мегабайтах
    char partition_type[32];  // Тип таблицы разделов ("MBR" или "GPT")
} CreateDiskParams;

/**
 * Тип таблицы разделов, определённый по содержимому диска.
 */
typedef enum {
    PT_UNKNOWN,              // Не удалось определить или таблица отсутствует
    PT_MBR,                  // MBR (Master Boot Record)
    PT_GPT                   // GPT (GUID Partition Table)
} PartitionTableType;

/**
 * Создаёт новый виртуальный диск (файл) заданного размера.
 * При необходимости инициализирует таблицу разделов (MBR или GPT).
 *
 * @param params Параметры создания диска.
 * @return Код ошибки или ERR_OK.
 */
ErrorCode cmd_create_disk(CreateDiskParams *params);

/**
 * Выводит информацию о виртуальном диске: размер, тип таблицы разделов,
 * список разделов (если есть).
 *
 * @param path Путь к файлу диска.
 * @return Код ошибки или ERR_OK.
 */
ErrorCode cmd_disk_info(const char *path);

/**
 * Читает указанное количество секторов с диска и выводит их содержимое
 * в виде шестнадцатеричного дампа.
 *
 * @param path Путь к файлу диска.
 * @param offset_sectors Смещение от начала диска в секторах.
 * @param size_sectors Количество секторов для чтения.
 * @return Код ошибки или ERR_OK.
 */
ErrorCode cmd_disk_read_sector(const char *path, uint64_t offset_sectors, uint64_t size_sectors);

/**
 * Открывает файл диска и определяет тип таблицы разделов на нём.
 *
 * Функция объединяет вызовы disk_open и detect_partition_table:
 * - открывает диск по заданному пути;
 * - определяет тип таблицы разделов (MBR, GPT или неизвестно);
 * - возвращает диск открытым, если операция успешна.
 *
 * @param path Путь к файлу диска.
 * @param disk Указатель на структуру Disk, которая будет заполнена при успешном открытии.
 * @param type Указатель на переменную PartitionTableType, куда будет записан определённый тип.
 * @return Код ошибки:
 *         - ERR_OK – диск успешно открыт, тип определён;
 *         - ERR_DISK_OPEN – не удалось открыть файл;
 *         - ERR_DISK_SEEK – ошибка позиционирования при определении размера;
 *         - ERR_DISK_READ – ошибка чтения первого сектора для определения типа;
 *         - другие коды могут быть возвращены из disk_open.
 */
ErrorCode cmd_disk_open_and_detect(const char *path, Disk *disk, PartitionTableType *type);