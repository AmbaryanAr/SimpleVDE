#ifndef MBR_H
#define MBR_H

#include <stdint.h>
#include <stdbool.h>

#include "disk.h"
#include "error_codes.h"

#define MBR_PARTITION_TABLE_OFFSET 446
#define MBR_PARTITION_ENTRY_SIZE 16
#define MBR_SIGNATURE_OFFSET 510
#define MBR_SIGNATURE 0xAA55

#pragma pack(push, 1)
typedef struct {
    uint8_t boot_flag;
    uint8_t start_head;
    uint8_t start_sector;
    uint8_t start_cylinder;
    uint8_t partition_type;
    uint8_t end_head;
    uint8_t end_sector;
    uint8_t end_cylinder;
    uint32_t lba_start;
    uint32_t sector_count;
} MbrPartitionEntry;

typedef struct {
    uint8_t bootstrap[MBR_PARTITION_TABLE_OFFSET];
    MbrPartitionEntry partitions[4];
    uint16_t signature;
} MbrSector;
#pragma pack(pop)

/** Инициализирует MBR (пустую). */
ErrorCode mbr_init(Disk *disk);

/** Создаёт новый раздел в MBR. */
ErrorCode mbr_create_partition(Disk *disk, int index, uint64_t size_sectors, uint8_t type);

/** Удаляет раздел по индексу. */
ErrorCode mbr_delete_partition(Disk *disk, int index);

/** Устанавливает активный флаг для раздела (сбрасывает у остальных). */
ErrorCode mbr_set_active(Disk *disk, int index);

/** Снимает активный флаг с раздела. */
ErrorCode mbr_set_inactive(Disk *disk, int index);

/** Изменяет тип раздела. */
ErrorCode mbr_set_partition_type(Disk *disk, int index, uint8_t type);

/** Получает информацию о разделе. */
ErrorCode mbr_get_partition_info(Disk *disk, int index, uint64_t *start_lba, uint64_t *size_sectors);

/** Выводит информацию о MBR на экран. */
void mbr_print_info(Disk *disk);

/** Преобразует имя типа в байт типа MBR. */
bool mbr_type_from_name(const char *name, uint8_t *type);

#endif