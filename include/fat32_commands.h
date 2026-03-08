#pragma once

#include "disk.h"
#include "error_code.h"

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