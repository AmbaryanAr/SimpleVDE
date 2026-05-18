#ifndef FAT32_EXTRACT_H
#define FAT32_EXTRACT_H

#include "disk.h"
#include "error_codes.h"

// Извлекает файл из образа FAT32 на хост.
// src_path — абсолютный путь к файлу внутри ФС.
// dest_path — путь для сохранения на хосте.
// overwrite — true: перезаписать существующий файл, false: ошибка если существует.
ErrorCode fat32_extract_file(Disk *disk, uint64_t start_lba,
                             const char *src_path, const char *dest_path,
                             bool overwrite);

#endif