#ifndef PARTITION_H
#define PARTITION_H

#include <stdint.h>
#include <stdbool.h>
#include "disk.h"
#include "error_codes.h"

/**
 * @file partition.h
 * @brief Абстрактный интерфейс для работы с таблицами разделов (MBR/GPT).
 */

/** Типы таблиц разделов */
typedef enum {
    PT_UNKNOWN = 0,    /**< Не удалось определить или таблица отсутствует */
    PT_MBR,            /**< Master Boot Record */
    PT_GPT             /**< GUID Partition Table */
} PartitionTableType;

/**
 * @brief Определяет тип таблицы разделов на открытом диске.
 * @param disk Открытый диск.
 * @param type Указатель для записи типа.
 * @return ErrorCode.
 */
ErrorCode partition_detect_type(Disk *disk, PartitionTableType *type);

/**
 * @brief Создаёт (инициализирует) таблицу разделов указанного типа.
 * @param disk Открытый диск.
 * @param type Тип таблицы (PT_MBR или PT_GPT).
 * @return ErrorCode.
 */
ErrorCode partition_create_table(Disk *disk, PartitionTableType type);

/**
 * @brief Создаёт новый раздел.
 * @param disk Открытый диск.
 * @param part_index Индекс раздела (0..n-1, зависит от таблицы).
 * @param size_bytes Размер раздела в байтах (0 = использовать всё свободное место).
 * @param type_str Строковое представление типа (например, "linux", "0x83" или GUID).
 * @return ErrorCode.
 */
ErrorCode partition_create(Disk *disk, int part_index, uint64_t size_bytes, const char *type_str);

/**
 * @brief Удаляет раздел.
 * @param disk Открытый диск.
 * @param part_index Индекс раздела.
 * @return ErrorCode.
 */
ErrorCode partition_delete(Disk *disk, int part_index);

/**
 * @brief Изменяет тип раздела.
 * @param disk Открытый диск.
 * @param part_index Индекс раздела.
 * @param type_str Новый тип (строка).
 * @return ErrorCode.
 */
ErrorCode partition_set_type(Disk *disk, int part_index, const char *type_str);

/**
 * @brief Устанавливает активный флаг (только для MBR).
 * @param disk Открытый диск.
 * @param part_index Индекс раздела.
 * @param active true – сделать активным, false – снять активность.
 * @return ErrorCode (ERR_NOT_SUPPORTED для GPT).
 */
ErrorCode partition_set_active(Disk *disk, int part_index, bool active);

/**
 * @brief Получает информацию о разделе.
 * @param disk Открытый диск.
 * @param part_index Индекс раздела.
 * @param start_lba Указатель для записи начального LBA.
 * @param size_sectors Указатель для записи размера в секторах.
 * @return ErrorCode (ERR_NOT_FOUND, если раздел не существует).
 */
ErrorCode partition_get_info(Disk *disk, int part_index, uint64_t *start_lba, uint64_t *size_sectors);

/**
 * @brief Выводит информацию о таблице разделов на экран.
 * @param disk Открытый диск.
 */
void partition_print_info(Disk *disk);

#endif