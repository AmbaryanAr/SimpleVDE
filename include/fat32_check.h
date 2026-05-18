#ifndef FAT32_CHECK_H
#define FAT32_CHECK_H

#include "disk.h"
#include "error_codes.h"

#define FSCHECK_LEVEL_QUICK  0   // быстрая проверка: BPB, FSInfo
#define FSCHECK_LEVEL_FULL   1   // полная проверка: FAT, цепочки, каталоги

// Проверяет целостность FAT32 на разделе, начиная с start_lba.
// level: FSCHECK_LEVEL_QUICK или FSCHECK_LEVEL_FULL.
// Возвращает ERR_OK, если ошибок нет, или код первой обнаруженной ошибки.
ErrorCode fat32_check(Disk *disk, uint64_t start_lba, int level);

#endif