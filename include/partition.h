#ifndef PARTITION_H
#define PARTITION_H

#include <stdint.h>
#include <stdbool.h>

#include "disk.h"
#include "error_codes.h"

typedef enum {
    PT_UNKNOWN = 0,
    PT_MBR,
    PT_GPT
} PartitionTableType;

/** Определяет тип таблицы разделов на открытом диске. */
ErrorCode partition_detect_type(Disk *disk, PartitionTableType *type);

/** Создаёт (инициализирует) таблицу разделов указанного типа. */
ErrorCode partition_create_table(Disk *disk, PartitionTableType type);

/** Создаёт новый раздел. */
ErrorCode partition_create(Disk *disk, int part_index, uint64_t size_bytes, const char *type_str);

/** Удаляет раздел. */
ErrorCode partition_delete(Disk *disk, int part_index);

/** Изменяет тип раздела. */
ErrorCode partition_set_type(Disk *disk, int part_index, const char *type_str);

/** Устанавливает или снимает активный флаг (только MBR). */
ErrorCode partition_set_active(Disk *disk, int part_index, bool active);

/** Получает информацию о разделе. */
ErrorCode partition_get_info(Disk *disk, int part_index, uint64_t *start_lba, uint64_t *size_sectors);

/** Выводит информацию о таблице разделов на экран. */
void partition_print_info(Disk *disk);

#endif