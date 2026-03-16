#ifndef CMD_COMMON_H
#define CMD_COMMON_H

#include "cmd.h"
#include "disk.h"
#include "fat32.h"
#include "partition.h"
#include "error_codes.h"

/**
 * Открывает диск и проверяет наличие таблицы разделов, но не требует существования конкретного раздела.
 * Диск остаётся открытым.
 */
ErrorCode open_disk_and_check_table(CMDArgs *args, Disk *disk);

/**
 * Открывает диск, проверяет таблицу разделов, получает информацию о разделе и проверяет FAT32.
 * В случае успеха возвращает ERR_OK, диск остаётся открытым, start_lba и info заполнены.
 * При ошибке диск закрывается, возвращается код ошибки.
 */
ErrorCode open_disk_and_prepare_fs(CMDArgs *args, Disk *disk, uint64_t *start_lba, Fat32Info *info);

/**
 * Открывает диск, проверяет таблицу разделов и получает информацию о разделе (start_lba и size_sectors).
 * Диск остаётся открытым, start_lba и size_sectors заполнены.
 */
ErrorCode open_disk_and_get_partition_info(CMDArgs *args, Disk *disk, uint64_t *start_lba, uint64_t *size_sectors);

/**
 * Выводит сообщение об ошибке с контекстом.
 */
void print_error(ErrorCode err, const char *context);

#endif