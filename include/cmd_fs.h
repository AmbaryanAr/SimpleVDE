#ifndef CMD_FS_H
#define CMD_FS_H

#include "cmd.h"
#include "fat32.h"
#include "utils.h"
#include "error_codes.h"

// Выводит содержимое каталога (аналог ls)
ErrorCode cmd_fs_ls(CMDArgs *args);

// Копирует файл с хоста в образ (аналог cp)
ErrorCode cmd_fs_copy(CMDArgs *args);

// Создаёт каталог в образе (аналог mkdir)
ErrorCode cmd_fs_mkdir(CMDArgs *args);

// Удаляет файл из образа (аналог rm)
ErrorCode cmd_fs_rm(CMDArgs *args);

// Удаляет пустой каталог из образа (аналог rmdir)
ErrorCode cmd_fs_rmdir(CMDArgs *args);

// Выводит дерево каталогов (аналог tree)
ErrorCode cmd_fs_tree(CMDArgs *args);

// Проверяет целостность файловой системы (автоопределение типа ФС)
ErrorCode cmd_fs_check(CMDArgs *args);

// Читает или устанавливает метку тома
ErrorCode cmd_fs_label(CMDArgs *args);

// Выводит информацию о файловой системе
ErrorCode cmd_fs_info(CMDArgs *args);

// Инициализирует резервный кластер для реестра файлов
ErrorCode cmd_fs_reserve_init(CMDArgs *args);

// Выводит список записей в реестре
ErrorCode cmd_fs_reserve_ls(CMDArgs *args);

// Добавляет запись о файле в реестр
ErrorCode cmd_fs_reserve_add(CMDArgs *args);

// Удаляет запись из реестра по имени
ErrorCode cmd_fs_reserve_rm(CMDArgs *args);

// Очищает все записи реестра
ErrorCode cmd_fs_reserve_clear(CMDArgs *args);

// Выводит hex-дамп резервного кластера
ErrorCode cmd_fs_reserve_dump(CMDArgs *args);

// Выводит информацию о реестре (размер, занято/свободно)
ErrorCode cmd_fs_reserve_info(CMDArgs *args);

// Устанавливает файл из реестра как загрузочный
ErrorCode cmd_fs_reserve_boot_set(CMDArgs *args);

// Показывает текущий загрузочный файл из реестра
ErrorCode cmd_fs_reserve_boot_show(CMDArgs *args);

// Сбрасывает указатель загрузочного файла
ErrorCode cmd_fs_reserve_boot_clear(CMDArgs *args);

#endif