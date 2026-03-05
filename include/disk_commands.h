#pragma once

#include "disk.h"
#include "error_code.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Структура для редактирования раздела
typedef struct {
    char path[MAX_PATH];           ///< Путь к файлу диска
    uint64_t size_mb;              ///< Размер диска в мегабайтах
    char partition_type[32];       ///< Тип раздела
} CreateDiskParams;

// 
ErrorCode cmd_create_disk(CreateDiskParams *params);
// 
ErrorCode cmd_disk_info(const char *path);
//
ErrorCode cmd_disk_read_sector(const char *path, uint64_t offset_sectors, uint64_t size_sectors);
