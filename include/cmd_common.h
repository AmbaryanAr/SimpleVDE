#ifndef CMD_COMMON_H
#define CMD_COMMON_H

#include "cmd.h"
#include "disk.h"
#include "fat32.h"
#include "partition.h"
#include "error_codes.h"

// Открывает диск и проверяет наличие таблицы разделов (MBR или GPT).
// Диск остаётся открытым. При ошибке диск закрывается.
ErrorCode open_disk_and_check_table(CMDArgs *args, Disk *disk);

// Открывает диск, проверяет таблицу разделов, получает информацию о разделе
// и проверяет, что раздел содержит корректную FAT32. Диск остаётся открытым.
// При ошибке диск закрывается.
ErrorCode open_disk_and_prepare_fs(CMDArgs *args, Disk *disk, uint64_t *start_lba, Fat32Info *info);

// Открывает диск, проверяет таблицу разделов и получает начальный LBA и размер раздела.
// Диск остаётся открытым. При ошибке диск закрывается.
ErrorCode open_disk_and_get_partition_info(CMDArgs *args, Disk *disk, uint64_t *start_lba, uint64_t *size_sectors);

// Выводит сообщение об ошибке с контекстом через svde_err.
// Формат: "Error: <context> - <текст ошибки>"
void print_error(ErrorCode err, const char *context);

#endif