#ifndef FAT32_UTIL_H
#define FAT32_UTIL_H

#include <stdint.h>
#include <stddef.h>

#include "fat32.h"

/** Сравнивает две ASCII-строки без учёта регистра. */
int strcasecmp_ascii(const char *a, const char *b);

/** Преобразует SFN-имя (11 байт) в читаемую строку "имя.расш". */
void sfn_to_display_name(const uint8_t sfn[11], char *out, size_t out_size);

/** Извлекает длинное имя из LFN-записей (динамическая память). */
char* extract_lfn_name(const uint8_t *dir_buffer, uint32_t sfn_index);

#endif