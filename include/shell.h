#pragma once

#include "disk.h"
#include "help.h"
#include "fat32_commands.h"
#include "error_code.h"

/**
 * Запускает интерактивную оболочку для работы с FAT32-разделом.
 * @param disk      Открытый диск.
 * @param start_lba Начальный LBA раздела.
 */
void run_shell(Disk *disk, uint64_t start_lba);