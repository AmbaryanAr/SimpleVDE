#pragma once

#include "disk.h"
#include "error_code.h"
#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t  name[11];            // 8.3 format, space-padded
    uint8_t  attr;                // атрибуты
    uint8_t  nt_reserved;         // для Windows NT
    uint8_t  create_time_tenth;   // десятые доли секунды создания
    uint16_t create_time;         // время создания
    uint16_t create_date;         // дата создания
    uint16_t last_access_date;    // дата последнего доступа
    uint16_t first_cluster_hi;    // старшие 16 бит первого кластера (FAT32)
    uint16_t write_time;          // время последней записи
    uint16_t write_date;          // дата последней записи
    uint16_t first_cluster_lo;    // младшие 16 бит первого кластера
    uint32_t file_size;           // размер файла в байтах
} FAT32_DirEntry;

// Атрибуты
#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LONG_NAME  0x0F  // комбинация для длинных имён
#pragma pack(pop)

/**
 * Форматирует раздел в FAT32.
 *
 * @param disk           Открытый диск.
 * @param start_lba      Начальный LBA раздела (абсолютный на диске).
 * @param total_sectors  Размер раздела в секторах.
 * @param drive_number   Номер диска (0x00 для сменных, 0x80 для жёстких). Обычно 0x80.
 * @return               Код ошибки или ERR_OK.
 */
ErrorCode fat32_format(Disk *disk, uint64_t start_lba, uint64_t total_sectors, uint8_t drive_number);

/**
 * Выводит список содержимого указанного каталога на FAT32-разделе.
 *
 * Функция читает параметры раздела из BPB, определяет расположение корневого
 * или заданного каталога и выводит список файлов и подкаталогов.
 * В текущей реализации поддерживается только корневой каталог (путь "/").
 *
 * @param disk         Открытый диск.
 * @param start_lba    Абсолютный LBA начала раздела.
 * @param root_cluster Номер первого кластера корневого каталога (обычно 2).
 *                     Если передан 0, значение берётся из BPB.
 * @param path         Путь к каталогу внутри раздела (пока только "/").
 * @return Код ошибки или ERR_OK.
 */
ErrorCode fat32_list_dir(Disk *disk, uint64_t start_lba, const char *path);

/**
 * Копирует файл с хоста в FAT32-раздел.
 *
 * @param disk         Открытый диск.
 * @param start_lba    Абсолютный LBA начала раздела.
 * @param root_cluster Номер кластера корневого каталога (обычно 2).
 * @param host_path    Путь к файлу в хост-системе.
 * @param dest_path    Путь назначения внутри раздела (пока только имя файла в корне).
 * @return Код ошибки или ERR_OK.
 */
ErrorCode fat32_copy_file(Disk *disk, uint64_t start_lba, const char *host_path, const char *dest_path);

/**
 * Удаляет файл из FAT32-раздела.
 *
 * @param disk         Открытый диск.
 * @param start_lba    Абсолютный LBA начала раздела.
 * @param path         Путь к файлу внутри раздела (пока только имя в корне).
 * @return Код ошибки или ERR_OK.
 */
ErrorCode fat32_delete_file(Disk *disk, uint64_t start_lba, const char *path);

/**
 * Создаёт каталог в FAT32-разделе.
 *
 * @param disk         Открытый диск.
 * @param start_lba    Абсолютный LBA начала раздела.
 * @param path         Путь к создаваемому каталогу (пока только имя в корне).
 * @return Код ошибки или ERR_OK.
 */
ErrorCode fat32_create_dir(Disk *disk, uint64_t start_lba, const char *path);

/**
 * Удаляет пустой каталог из FAT32-раздела.
 *
 * @param disk         Открытый диск.
 * @param start_lba    Абсолютный LBA начала раздела.
 * @param path         Путь к удаляемому каталогу (пока только имя в корне).
 * @return Код ошибки или ERR_OK.
 */
ErrorCode fat32_remove_dir(Disk *disk, uint64_t start_lba, const char *path);