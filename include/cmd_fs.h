#ifndef CMD_FS_H
#define CMD_FS_H

#include "cmd.h"
#include "fat32.h"
#include "utils.h"
#include "error_codes.h"

/** Выводит содержимое каталога. */
ErrorCode cmd_fs_ls(CMDArgs *args);

/** Копирует файл с хоста в образ. */
ErrorCode cmd_fs_copy(CMDArgs *args);

/** Создаёт каталог. */
ErrorCode cmd_fs_mkdir(CMDArgs *args);

/** Удаляет файл. */
ErrorCode cmd_fs_rm(CMDArgs *args);

/** Удаляет пустой каталог. */
ErrorCode cmd_fs_rmdir(CMDArgs *args);

/** Выводит дерево каталогов. */
ErrorCode cmd_fs_tree(CMDArgs *args);

/* Команды для работы с реестром */
ErrorCode cmd_fs_reserve_init(CMDArgs *args);
ErrorCode cmd_fs_reserve_ls(CMDArgs *args);
ErrorCode cmd_fs_reserve_add(CMDArgs *args);
ErrorCode cmd_fs_reserve_rm(CMDArgs *args);
ErrorCode cmd_fs_reserve_clear(CMDArgs *args);
ErrorCode cmd_fs_reserve_dump(CMDArgs *args);
ErrorCode cmd_fs_reserve_info(CMDArgs *args);

ErrorCode cmd_fs_reserve_boot_set(CMDArgs *args);
ErrorCode cmd_fs_reserve_boot_show(CMDArgs *args);
ErrorCode cmd_fs_reserve_boot_clear(CMDArgs *args);

#endif