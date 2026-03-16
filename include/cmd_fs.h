#ifndef CMD_FS_H
#define CMD_FS_H

#include "cmd.h"
#include "fat32.h"
#include "utils.h"
#include "error_codes.h"



/**
 * @brief Выводит содержимое каталога внутри FAT32-раздела.
 * @param args Аргументы командной строки (ожидается file, part, path).
 * @return ErrorCode.
 */
ErrorCode cmd_fs_ls(CMDArgs *args);

/**
 * @brief Копирует файл из хост-системы в FAT32-раздел.
 * @param args Аргументы командной строки (ожидается file, part, src, dest).
 * @return ErrorCode.
 */
ErrorCode cmd_fs_copy(CMDArgs *args);

/**
 * @brief Создаёт новый каталог внутри FAT32-раздела.
 * @param args Аргументы командной строки (ожидается file, part, path).
 * @return ErrorCode.
 */
ErrorCode cmd_fs_mkdir(CMDArgs *args);

/**
 * @brief Удаляет файл внутри FAT32-раздела.
 * @param args Аргументы командной строки (ожидается file, part, path).
 * @return ErrorCode.
 */
ErrorCode cmd_fs_rm(CMDArgs *args);

/**
 * @brief Удаляет пустой каталог внутри FAT32-раздела.
 * @param args Аргументы командной строки (ожидается file, part, path).
 * @return ErrorCode.
 */
ErrorCode cmd_fs_rmdir(CMDArgs *args);


ErrorCode cmd_fs_tree(CMDArgs *args);

// Команды для работы с реестром
ErrorCode cmd_fs_reserve_init(CMDArgs *args);
ErrorCode cmd_fs_reserve_ls(CMDArgs *args);
ErrorCode cmd_fs_reserve_add(CMDArgs *args);
ErrorCode cmd_fs_reserve_rm(CMDArgs *args);
ErrorCode cmd_fs_reserve_clear(CMDArgs *args);
ErrorCode cmd_fs_reserve_dump(CMDArgs *args);
ErrorCode cmd_fs_reserve_info(CMDArgs *args);

#endif // CMD_FS_H