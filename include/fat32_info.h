#ifndef FAT32_INFO_H
#define FAT32_INFO_H

#include "disk.h"
#include "error_codes.h"

// Выводит подробную информацию о файловой системе FAT32 через svde_out
ErrorCode fat32_print_info(Disk *disk, uint64_t start_lba);

#endif