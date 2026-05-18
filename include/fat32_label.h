#ifndef FAT32_LABEL_H
#define FAT32_LABEL_H

#include "disk.h"
#include "error_codes.h"

// Читает метку тома из BPB (11 символов, без '\0').
// out должен быть размером не менее 12 байт.
ErrorCode fat32_get_label(Disk *disk, uint64_t start_lba, char *out, size_t out_size);

// Устанавливает метку тома в BPB и резервный BPB (сектор 6).
// label — строка до 11 символов (лишнее обрезается, короткое дополняется пробелами).
ErrorCode fat32_set_label(Disk *disk, uint64_t start_lba, const char *label);

#endif