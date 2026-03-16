#ifndef FAT32_H
#define FAT32_H

#include "disk.h"
#include "utils.h"
#include "error_codes.h"
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

// ==================== Атрибуты записей каталога ====================
#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_ID    0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20
#define FAT32_ATTR_LFN          0x0F

// ==================== Константы для FSInfo-сектора ====================
#define FAT32_FSINFO_LEAD_SIG         0x41615252
#define FAT32_FSINFO_STRUCT_SIG       0x61417272
#define FAT32_FSINFO_TRAIL_SIG        0xAA550000

#define FAT32_FSINFO_LEAD_OFFSET      0
#define FAT32_FSINFO_STRUCT_OFFSET    484
#define FAT32_FSINFO_FREE_OFFSET      488
#define FAT32_FSINFO_NEXT_FREE_OFFSET 492
#define FAT32_FSINFO_TRAIL_OFFSET     508

// ==================== Специальные значения кластеров в FAT-таблице ====================
#define FAT32_CLUSTER_FREE       0x00000000
#define FAT32_CLUSTER_RESERVED   0x00000001
#define FAT32_CLUSTER_BAD        0x0FFFFFF7
#define FAT32_CLUSTER_LAST_MIN   0x0FFFFFF8
#define FAT32_CLUSTER_LAST_MAX   0x0FFFFFFF
#define FAT32_CLUSTER_EOC        0x0FFFFFF8

// ==================== Специальные байты в записях каталога ====================
#define FAT32_DIRENT_FREE        0x00
#define FAT32_DIRENT_DELETED     0xE5
#define FAT32_DIRENT_LFN_LAST    0x40

// ==================== Размеры и ограничения ====================
#define FAT32_DIRENT_SIZE        32
#define FAT32_SFN_LENGTH         11
#define FAT32_MAX_LFN_ENTRIES    20

// ==================== Маски для проверки типа записи ====================
#define FAT32_ATTR_FILE_MASK     (FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | FAT32_ATTR_SYSTEM | FAT32_ATTR_ARCHIVE)
#define FAT32_ATTR_DIR_MASK      (FAT32_ATTR_DIRECTORY)

// ==================== Коды ошибок FAT32 ====================
typedef enum {
    FAT32_SUCCESS = 0,
    FAT32_ERR_INVALID_PARAM,
    FAT32_ERR_OUT_OF_MEMORY,
    FAT32_ERR_IO_READ,
    FAT32_ERR_IO_WRITE,
    FAT32_ERR_BAD_BPB,
    FAT32_ERR_FSINFO_CORRUPT,
    FAT32_ERR_VOLUME_NOT_MOUNTED,
    FAT32_ERR_NO_FREE_CLUSTER,
    FAT32_ERR_BAD_CLUSTER,
    FAT32_ERR_FAT_CORRUPT,
    FAT32_ERR_DIR_NO_FREE_ENTRY,
    FAT32_ERR_DIR_NOT_FOUND,
    FAT32_ERR_DIR_ALREADY_EXISTS,
    FAT32_ERR_DIR_IS_NOT_DIRECTORY,
    FAT32_ERR_NAME_TOO_LONG,
    FAT32_ERR_NAME_INVALID,
    FAT32_ERR_UTF16_CONVERSION,
    FAT32_ERR_LFN_CHECKSUM,
    FAT32_ERR_TOO_MANY_LFN_ENTRIES,
    FAT32_ERR_SFN_SUFFIX_OVERFLOW,
} Fat32ErrorCode;

#pragma pack(push, 1)
typedef struct {
    uint8_t  jump_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_descriptor;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} Fat32BPB;

typedef struct {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} Fat32ShortEntry;

typedef struct {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t first_cluster;
    uint16_t name3[2];
} Fat32LongEntry;
#pragma pack(pop)

typedef struct {
    uint64_t first_data_lba;
    uint64_t fat1_lba;
    uint64_t fat2_lba;
    uint32_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint32_t reserved_sectors;
    uint8_t  num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint32_t total_clusters;
    uint32_t free_clusters;
    uint32_t next_free_cluster;
} Fat32Info;

typedef struct {
    uint32_t    cluster_num;
    uint32_t    offset_bytes;
    uint8_t*    data;
} Fat32DirCluster;

typedef struct {
    Fat32Info*         volume;
    Fat32DirCluster*   clusters;
    uint32_t           cluster_count;
} Fat32Directory;

typedef struct {
    const char*      name;
    uint8_t          sfn[FAT32_SFN_LENGTH];
    uint16_t*        utf16_name;
    size_t           utf16_len;
    Fat32LongEntry*  lfn_entries;
    uint8_t          lfn_count;
    uint8_t          total_entries;
    uint8_t          attr;
    uint16_t         crt_time;
    uint16_t         crt_date;
    uint16_t         wrt_time;
    uint16_t         wrt_date;
    uint16_t         lst_acc_date;
    uint8_t          crt_time_tenth;
    uint32_t         first_cluster;
    uint32_t         file_size;
} fat32_entry_info_t;

// ==================== Основные функции тома (fat32_core.c) ====================
ErrorCode fat32_format(Disk *disk, uint64_t start_lba, uint64_t total_sectors,
                       uint8_t drive_number, uint32_t volume_id, const char *volume_label);
ErrorCode fat32_get_info(Disk *disk, uint64_t start_lba, Fat32Info *info);
uint32_t fat32_alloc_cluster(Disk *disk, const Fat32Info *info);
ErrorCode fat32_free_cluster_chain(Disk *disk, const Fat32Info *info, uint32_t first_cluster);
ErrorCode fat32_read_cluster(Disk *disk, const Fat32Info *info, uint32_t cluster, uint8_t *buffer);
ErrorCode fat32_write_cluster(Disk *disk, const Fat32Info *info, uint32_t cluster, const uint8_t *buffer);
ErrorCode fat32_get_next_cluster(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t *next);
ErrorCode fat32_set_fat_entry(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t value);

// ==================== Функции для работы с каталогами (fat32_dir.c) ====================
ErrorCode fat32_dir_open(Disk *disk, const Fat32Info *info, uint32_t start_cluster, Fat32Directory *dir);
void fat32_dir_close(Fat32Directory *dir);
bool fat32_find_free_entries(const Fat32Directory *dir, uint8_t needed,
                             uint32_t *out_cluster_idx, uint32_t *out_entry_off);
ErrorCode fat32_append_cluster_to_dir(Disk *disk, const Fat32Info *info, Fat32Directory *dir);
ErrorCode fat32_write_entries_to_dir(Disk *disk, const Fat32Info *info,
                                     Fat32Directory *dir, uint32_t cluster_idx,
                                     uint32_t entry_off, const fat32_entry_info_t *entry);
ErrorCode fat32_init_new_dir(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t parent_cluster);
ErrorCode fat32_create_dir(Disk *disk, uint64_t start_lba, const char *path);

// ==================== Функции чтения/поиска каталогов (fat32_ls.c) ====================
ErrorCode fat32_read_dir(Disk *disk, const Fat32Info *info, uint32_t start_cluster,
                         uint8_t **buffer, uint32_t *entries_count);
ErrorCode fat32_find_dir(Disk *disk, const Fat32Info *info, const char *path, uint32_t *dir_cluster);
ErrorCode fat32_list_dir(Disk *disk, uint64_t start_lba, const char *path);

// ==================== Функции подготовки записей (fat32_entry.c) ====================
Fat32ErrorCode fat32_prepare_entry(const char *utf8_name, uint8_t attr,
                                   const uint8_t *dir_buffer, uint32_t entry_count,
                                   fat32_entry_info_t *info);
void fat32_free_entry_info(fat32_entry_info_t *info);
void fat32_set_current_time(fat32_entry_info_t *info);


ErrorCode fat32_delete_file(Disk *disk, uint64_t start_lba, const char *path);
ErrorCode fat32_copy_file(Disk *disk, uint64_t start_lba, const char *host_path, const char *dest_path);
ErrorCode fat32_remove_dir(Disk *disk, uint64_t start_lba, const char *path);

// ==================== Функции для работы с реестром (reserve) ====================
/**
 * @brief Инициализирует реестр: выделяет кластер, помечает его в FAT, сохраняет номер в BPB.
 * @param disk Открытый диск.
 * @param start_lba Начало раздела.
 * @return ErrorCode.
 */
ErrorCode fat32_reserve_init(Disk *disk, uint64_t start_lba);

/**
 * @brief Добавляет запись о файле в реестр.
 * @param disk Открытый диск.
 * @param start_lba Начало раздела.
 * @param path Путь к файлу внутри образа (абсолютный).
 * @return ErrorCode.
 */
ErrorCode fat32_reserve_add(Disk *disk, uint64_t start_lba, const char *path);

/**
 * @brief Выводит список записей из реестра.
 * @param disk Открытый диск.
 * @param start_lba Начало раздела.
 * @return ErrorCode.
 */
ErrorCode fat32_reserve_list(Disk *disk, uint64_t start_lba);

/**
 * @brief Удаляет запись из реестра по имени.
 * @param disk Открытый диск.
 * @param start_lba Начало раздела.
 * @param name Имя файла (как сохранено в реестре).
 * @return ErrorCode.
 */
ErrorCode fat32_reserve_remove(Disk *disk, uint64_t start_lba, const char *name);

/**
 * @brief Очищает весь реестр (обнуляет кластер).
 * @param disk Открытый диск.
 * @param start_lba Начало раздела.
 * @return ErrorCode.
 */
ErrorCode fat32_reserve_clear(Disk *disk, uint64_t start_lba);

/**
 * @brief Выводит hex-дамп кластера реестра.
 * @param disk Открытый диск.
 * @param start_lba Начало раздела.
 * @return ErrorCode.
 */
ErrorCode fat32_reserve_dump(Disk *disk, uint64_t start_lba);

/**
 * @brief Выводит информацию о реестре: размер, занято, свободно.
 * @param disk Открытый диск.
 * @param start_lba Начало раздела.
 * @return ErrorCode.
 */
ErrorCode fat32_reserve_info(Disk *disk, uint64_t start_lba);
#endif