#pragma once

/**
 * @file help.h
 * @brief Функции для отображения справки по программе.
 */

/**
 * @brief Выводит краткую однострочную справку с предложением использовать --help.
 */
void print_short_help(void);

/**
 * @brief Выводит общую справку по всем категориям команд.
 *
 * Эквивалентно вызову справки для всех категорий последовательно.
 */
void print_general_help(void);

/**
 * @brief Выводит справку по операциям с дисками (категория --disk).
 */
void print_disk_help(void);

/**
 * @brief Выводит справку по операциям с разделами (категория --partition).
 */
void print_partition_help(void);

/**
 * @brief Выводит справку по операциям с файловыми системами (категории --copy, --ls, --mkdir, --rmdir, --rm).
 */
void print_fs_help(void);

/**
 * @brief Выводит справку по операциям с картой специальных файлов (категория --map_file).
 */
void print_map_help(void);

/**
 * @brief Выводит справку по глобальным опциям (--help, --version).
 */
void print_global_help(void);

/**
 * @brief Выводит информацию о версии программы.
 */
void print_version(void);