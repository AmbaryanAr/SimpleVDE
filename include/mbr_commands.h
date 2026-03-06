#pragma once

#include "disk.h"
#include "error_code.h"
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>

#define MBR_SIZE 512
#define PARTITION_ENTRY_SIZE 16
#define PARTITION_TABLE_OFFSET 446
#define PARTITION_BOOTABLE     0x80
#define PARTITION_NON_BOOTABLE 0x00
#define MBR_SIGNATURE 0xAA55

#pragma pack(push, 1)
typedef struct {
    uint8_t  boot_flag;
    uint8_t  start_head;
    uint8_t  start_sector;
    uint8_t  start_cylinder;
    uint8_t  partition_type;
    uint8_t  end_head;
    uint8_t  end_sector;
    uint8_t  end_cylinder;
    uint32_t lba_start;
    uint32_t sector_count;
} PartitionEntry;

typedef struct {
    uint8_t bootstrap[PARTITION_TABLE_OFFSET];
    PartitionEntry partitions[4];
    uint16_t signature;
} MBR;
#pragma pack(pop)

// Параметры для создания раздела
typedef struct {
    char disk_path[MAX_PATH];      // Путь к файлу диска
    int index;                     // Номер раздела (1-4)
    uint32_t size_sectors;         // Размер в секторах
    uint8_t type;                  // Тип раздела (по умолчанию 0x83 - Linux)
    bool type_specified;           // Был ли указан тип явно
} CreatePartitionParams;

/**
 * Создаёт MBR (Master Boot Record) на открытом диске.
 * Записывает в первый сектор стандартную загрузочную запись и пустую таблицу разделов.
 * Диск должен быть открыт для записи.
 *
 * @param disk Указатель на открытый диск (структура Disk).
 * @return Код ошибки:
 *         - ERR_OK – MBR успешно создана;
 *         - ERR_DISK_OPEN – диск не открыт или передан нулевой указатель;
 *         - ERR_DISK_WRITE – ошибка записи в первый сектор.
 */
ErrorCode mbr_create(Disk *disk);

// ErrorCode cmd_mbr_create_partition( ... );
// ErrorCode cmd_mbr_delete_partition( ... );
// ErrorCode cmd_mbr_set_partition_type( ... );
// ErrorCode cmd_mbr_set_active_partition( ... );

/**
 * Выводит информацию о MBR-таблице разделов на открытом диске.
 * Читает MBR из первого сектора и отображает:
 * - количество первичных разделов,
 * - для каждого непустого раздела: тип, статус (активный/неактивный),
 *   начальный LBA и размер в секторах.
 *
 * @param disk Указатель на открытый диск (должен быть корректен и содержать MBR).
 */
void mbr_print_info(Disk *disk);
