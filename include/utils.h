#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "error_codes.h"

/**
 * @brief Безопасное дублирование строки (аналог strdup).
 * @param s Исходная строка.
 * @return Указатель на новую копию или NULL при ошибке.
 */
char* my_strdup(const char *s);

/**
 * @brief Преобразует строку с номером раздела (начиная с 1) в индекс (начиная с 0).
 * @param str Входная строка.
 * @return Индекс раздела (0..n-1) или -1 при ошибке.
 */
int parse_part_index(const char *str);

/**
 * @brief Преобразует строку с размером (например, "128M") в количество байт.
 * Поддерживает суффиксы K, M, G (1K = 1024, 1M = 1024^2, 1G = 1024^3).
 * @param str Входная строка.
 * @param bytes Указатель для записи результата.
 * @return true при успехе, false при ошибке.
 */
bool parse_size(const char *str, uint64_t *bytes);

/**
 * @brief Преобразует строку в целое число (десятичное или шестнадцатеричное с префиксом 0x).
 * @param str Входная строка.
 * @param result Указатель для записи результата.
 * @return true при успехе, false при ошибке.
 */
bool parse_integer(const char *str, uint64_t *result);

/**
 * @brief Преобразует строку в верхний регистр (на месте).
 * @param str Строка для преобразования.
 */
void str_toupper(char *str);

/**
 * @brief Безопасное копирование строки с ограничением длины.
 * @param dest Буфер назначения.
 * @param dest_size Размер буфера.
 * @param src Исходная строка.
 * @return true, если строка поместилась (включая завершающий нуль).
 */
bool strlcpy_safe(char *dest, size_t dest_size, const char *src);

/**
 * @brief Читает весь файл в динамический буфер.
 * @param path Путь к файлу.
 * @param buffer Указатель на указатель, куда будет выделен буфер (нужно освободить caller'ом).
 * @param size Указатель для записи размера файла.
 * @return ErrorCode.
 */
ErrorCode read_whole_file(const char *path, uint8_t **buffer, size_t *size);

#endif // UTILS_H