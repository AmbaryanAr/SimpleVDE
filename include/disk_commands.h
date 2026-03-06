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