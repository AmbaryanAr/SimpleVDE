#ifndef FAT32_UTIL_H
#define FAT32_UTIL_H

#include <stdint.h>
#include <stddef.h>

#include "fat32.h"

// Сравнивает две ASCII-строки без учёта регистра.
// Возвращает 0 при равенстве, отрицательное/положительное при различии.
int strcasecmp_ascii(const char *a, const char *b);

// Преобразует короткое имя (11 байт, 8+3 без точки) в читаемую строку "имя.расш".
// out должен быть размером не менее 13 байт.
void sfn_to_display_name(const uint8_t sfn[11], char *out, size_t out_size);

// Извлекает длинное имя файла из LFN-записей, предшествующих SFN с индексом sfn_index.
// Возвращает строку в динамической памяти (вызывающий должен освободить) или NULL.
char* extract_lfn_name(const uint8_t *dir_buffer, uint32_t sfn_index);

#endif