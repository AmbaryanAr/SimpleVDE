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

// Структура для длинного имени (LFN)
typedef struct {
    uint8_t  order;          // порядковый номер (0x40 для последней)
    uint16_t name1[5];       // первые 5 символов (UTF-16LE)
    uint8_t  attr;           // всегда 0x0F
    uint8_t  type;           // всегда 0
    uint8_t  checksum;       // контрольная сумма короткого имени
    uint16_t name2[6];       // следующие 6 символов
    uint16_t first_cluster;  // всегда 0
    uint16_t name3[2];       // последние 2 символа
} FAT32_LongDirEntry;
#pragma pack(pop)

// Структура для хранения параметров раздела
typedef struct {
    uint32_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint32_t reserved_sectors;
    uint8_t  num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint64_t first_data_lba;
    uint64_t fat1_lba;
    uint64_t fat2_lba;
    uint32_t total_clusters;
} Fat32PartInfo;

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

ErrorCode fat32_get_part_info(Disk *disk, uint64_t start_lba, Fat32PartInfo *info);
ErrorCode fat32_find_dir(Disk *disk, const Fat32PartInfo *info, const char *path, uint32_t *dir_cluster);

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

/**
 * Находит свободный кластер и резервирует его (помечает как 0x0FFFFFFF в FAT).
 * @param disk Открытый диск.
 * @param info Информация о FAT32-разделе.
 * @return Номер кластера (>=2) или 0, если нет свободных кластеров или произошла ошибка.
 */
uint32_t fat32_alloc_cluster(Disk *disk, const Fat32PartInfo *info);

/**
 * Записывает номер кластера в зарезервированную область BPB (смещение 52).
 * @param disk      Открытый диск.
 * @param start_lba Начальный LBA раздела.
 * @param cluster   Номер кластера для записи.
 * @return Код ошибки или ERR_OK.
 */
ErrorCode fat32_write_reserved_cluster(Disk *disk, uint64_t start_lba, uint32_t cluster);

// Новые функции для работы с длинными именами (LFN)
/**
 * Генерирует короткое имя (8.3) из длинного.
 * @param long_name Исходное длинное имя (UTF-8).
 * @param short_name Буфер для 11-байтового короткого имени (заполняется пробелами).
 * @return 0 при успехе, -1 если имя не может быть преобразовано.
 */
int lfn_generate_short_name(const char *long_name, uint8_t *short_name);

/**
 * Записывает цепочку LFN-записей для длинного имени в указанный каталог.
 * @param disk         Открытый диск.
 * @param info         Информация о FAT32-разделе.
 * @param dir_cluster  Кластер каталога, куда добавляется запись.
 * @param long_name    Длинное имя (UTF-8).
 * @param short_name   Соответствующее короткое имя (8.3).
 * @param first_cluster Первый кластер файла/каталога.
 * @param file_size    Размер файла (для каталога 0).
 * @param attr         Атрибуты (FAT32_ATTR_DIRECTORY и т.д.).
 * @return Код ошибки или ERR_OK.
 */
ErrorCode lfn_write_entries(Disk *disk, const Fat32PartInfo *info,
                            uint32_t dir_cluster, const char *long_name,
                            const uint8_t *short_name, uint32_t first_cluster,
                            uint32_t file_size, uint8_t attr);

/**
 * Удаляет цепочку LFN-записей, связанных с основной записью в каталоге.
 * @param disk         Открытый диск.
 * @param info         Информация о FAT32-разделе.
 * @param dir_cluster  Кластер каталога.
 * @param entry_offset Смещение основной записи в байтах от начала кластера.
 * @return Код ошибки или ERR_OK.
 */
ErrorCode lfn_remove_entries(Disk *disk, const Fat32PartInfo *info, uint32_t dir_cluster, size_t entry_offset);