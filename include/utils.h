#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "error_codes.h"

/** Безопасное дублирование строки. */
char* my_strdup(const char *s);

/** Преобразует строку с номером раздела (начиная с 1) в индекс (начиная с 0). */
int parse_part_index(const char *str);

/** Преобразует строку с размером (например, "128M") в количество байт. */
bool parse_size(const char *str, uint64_t *bytes);

/** Преобразует строку в целое число (десятичное или шестнадцатеричное). */
bool parse_integer(const char *str, uint64_t *result);

/** Преобразует строку в верхний регистр (на месте). */
void str_toupper(char *str);

/** Безопасное копирование строки с ограничением длины. */
bool strlcpy_safe(char *dest, size_t dest_size, const char *src);

/** Читает весь файл в динамический буфер. */
ErrorCode read_whole_file(const char *path, uint8_t **buffer, size_t *size);

#endif