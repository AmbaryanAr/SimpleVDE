#ifndef MBR_H
#define MBR_H

#include "disk.h"
#include "error_codes.h"
#include <stdint.h>
#include <stdbool.h>

#define MBR_PARTITION_TABLE_OFFSET 446
#define MBR_PARTITION_ENTRY_SIZE 16
#define MBR_SIGNATURE_OFFSET 510
#define MBR_SIGNATURE 0xAA55

#pragma pack(push, 1)
/**
 * @brief Структура одной записи раздела в MBR.
 */
typedef struct {
    uint8_t boot_flag;          /**< Флаг загрузки: 0x80 = активный раздел, 0x00 = неактивный */
    uint8_t start_head;         /**< Номер головки для начала раздела (CHS) */
    uint8_t start_sector;       /**< Номер сектора для начала раздела (CHS): биты 0-5 – номер сектора (1-63), биты 6-7 – старшие биты цилиндра */
    uint8_t start_cylinder;     /**< Младшие 8 бит номера цилиндра для начала раздела (CHS) */
    uint8_t partition_type;     /**< Тип раздела (например, 0x83 – Linux, 0x0B – FAT32, 0xEE – GPT protective) */
    uint8_t end_head;           /**< Номер головки для конца раздела (CHS) */
    uint8_t end_sector;         /**< Номер сектора для конца раздела (CHS): биты 0-5 – номер сектора, биты 6-7 – старшие биты цилиндра */
    uint8_t end_cylinder;       /**< Младшие 8 бит номера цилиндра для конца раздела (CHS) */
    uint32_t lba_start;         /**< Абсолютный номер первого сектора раздела в режиме LBA (little-endian) */
    uint32_t sector_count;      /**< Количество секторов в разделе (little-endian) */
} MbrPartitionEntry;

/**
 * @brief Структура, представляющая весь 512-байтный сектор MBR.
 */
typedef struct {
    uint8_t bootstrap[MBR_PARTITION_TABLE_OFFSET];  /**< Код начального загрузчика (первые 446 байт) */
    MbrPartitionEntry partitions[4];                /**< Четыре записи таблицы разделов (по 16 байт каждая) */
    uint16_t signature;                             /**< Сигнатура загрузочной записи: всегда 0xAA55 (little-endian) */
} MbrSector;
#pragma pack(pop)

/**
 * @brief Инициализирует MBR (пустую, с защитным или стандартным кодом) на диске.
 * @param disk Открытый диск.
 * @return ErrorCode.
 */
ErrorCode mbr_init(Disk *disk);

/**
 * @brief Создаёт новый раздел в MBR.
 * @param disk Открытый диск.
 * @param index Индекс раздела (0-3).
 * @param size_sectors Размер в секторах (0 = использовать всё свободное место до конца диска).
 * @param type Тип раздела (например, 0x83).
 * @return ErrorCode.
 */
ErrorCode mbr_create_partition(Disk *disk, int index, uint64_t size_sectors, uint8_t type);

/**
 * @brief Удаляет раздел по индексу.
 * @param disk Открытый диск.
 * @param index Индекс раздела (0-3).
 * @return ErrorCode (ERR_NOT_FOUND, если раздел не существует).
 */
ErrorCode mbr_delete_partition(Disk *disk, int index);

/**
 * @brief Устанавливает активный флаг для указанного раздела (сбрасывает у остальных).
 * @param disk Открытый диск.
 * @param index Индекс раздела (0-3).
 * @return ErrorCode.
 */
ErrorCode mbr_set_active(Disk *disk, int index);

/**
 * @brief Снимает активный флаг с указанного раздела (не изменяя остальные).
 * @param disk Открытый диск.
 * @param index Индекс раздела (0-3).
 * @return ErrorCode.
 */
ErrorCode mbr_set_inactive(Disk *disk, int index);

/**
 * @brief Изменяет тип раздела.
 * @param disk Открытый диск.
 * @param index Индекс раздела (0-3).
 * @param type Новый тип.
 * @return ErrorCode.
 */
ErrorCode mbr_set_partition_type(Disk *disk, int index, uint8_t type);

/**
 * @brief Получает информацию о разделе.
 * @param disk Открытый диск.
 * @param index Индекс раздела (0-3).
 * @param start_lba Указатель для записи начального LBA.
 * @param size_sectors Указатель для записи размера в секторах.
 * @return ErrorCode (ERR_NOT_FOUND, если раздел не существует).
 */
ErrorCode mbr_get_partition_info(Disk *disk, int index, uint64_t *start_lba, uint64_t *size_sectors);

/**
 * @brief Выводит информацию о MBR на экран.
 * @param disk Открытый диск.
 */
void mbr_print_info(Disk *disk);

/**
 * @brief Преобразует имя типа (например, "linux") в байт типа MBR.
 * @param name Имя типа (регистронезависимо).
 * @param type Указатель для записи байта.
 * @return true, если имя найдено, иначе false.
 */
bool mbr_type_from_name(const char *name, uint8_t *type);

#endif // MBR_H