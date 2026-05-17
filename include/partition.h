#ifndef PARTITION_H
#define PARTITION_H

#include <stdint.h>
#include <stdbool.h>

#include "disk.h"
#include "error_codes.h"

// Тип таблицы разделов на диске
typedef enum {
    PT_UNKNOWN = 0,  // таблица разделов не обнаружена
    PT_MBR,          // классическая MBR (DOS)
    PT_GPT           // GUID Partition Table
} PartitionTableType;

// Определяет тип таблицы разделов на открытом диске (MBR, GPT или UNKNOWN)
ErrorCode partition_detect_type(Disk *disk, PartitionTableType *type);

// Создаёт таблицу разделов указанного типа (mbr_init или gpt_init)
ErrorCode partition_create_table(Disk *disk, PartitionTableType type);

// Создаёт раздел с указанным индексом, размером в байтах и типом (имя или код/GUID).
// Тип таблицы определяется автоматически.
ErrorCode partition_create(Disk *disk, int part_index, uint64_t size_bytes, const char *type_str);

// Удаляет раздел. Тип таблицы определяется автоматически.
ErrorCode partition_delete(Disk *disk, int part_index);

// Изменяет тип раздела. Тип таблицы определяется автоматически.
ErrorCode partition_set_type(Disk *disk, int part_index, const char *type_str);

// Устанавливает (active=true) или снимает (active=false) флаг активности.
// Работает только для MBR.
ErrorCode partition_set_active(Disk *disk, int part_index, bool active);

// Возвращает начальный LBA и размер раздела в секторах.
// Тип таблицы определяется автоматически.
ErrorCode partition_get_info(Disk *disk, int part_index, uint64_t *start_lba, uint64_t *size_sectors);

// Выводит информацию о таблице разделов (MBR или GPT) через svde_out
void partition_print_info(Disk *disk);

#endif