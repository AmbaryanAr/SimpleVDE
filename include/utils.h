#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "error_codes.h"

// Специальные значения, возвращаемые parse_part_index()
#define PART_INDEX_INVALID   -1   // строка не является ни числом, ни "raw"
#define PART_INDEX_RAW       -2   // образ без таблицы разделов (raw)

// Безопасное дублирование строки в динамической памяти.
// Возвращает копию строки или NULL при нехватке памяти.
char* my_strdup(const char *s);

// Преобразует строку с номером раздела (начиная с 1) в индекс (начиная с 0).
// При ошибке возвращает -1.
int parse_part_index(const char *str);

// Преобразует строку с размером (например, "128M", "4K", "1G") в количество байт.
// Поддерживает суффиксы K, M, G (регистронезависимо). Возвращает false при ошибке.
bool parse_size(const char *str, uint64_t *bytes);

// Преобразует строку в целое число (десятичное или шестнадцатеричное с 0x).
// Возвращает false при ошибке.
bool parse_integer(const char *str, uint64_t *result);

// Преобразует ASCII-строку в верхний регистр (изменяет на месте).
void str_toupper(char *str);

// Безопасное копирование строки с ограничением длины.
// Возвращает false, если строка-источник не помещается в буфер.
bool strlcpy_safe(char *dest, size_t dest_size, const char *src);

// Читает весь файл в динамический буфер.
// Вызывающий должен освободить *buffer через free().
ErrorCode read_whole_file(const char *path, uint8_t **buffer, size_t *size);

#endif