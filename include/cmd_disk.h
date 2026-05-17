#ifndef CMD_DISK_H
#define CMD_DISK_H

#include "cmd.h"
#include "fat32.h"
#include "error_codes.h"

// Создаёт новый образ диска с указанным размером и типом таблицы разделов
ErrorCode cmd_disk_create(CMDArgs *args);

// Выводит информацию о диске: размер, тип таблицы разделов, список разделов
ErrorCode cmd_disk_info(CMDArgs *args);

// Читает байты с диска по смещению и выводит hex-дамп
ErrorCode cmd_disk_read(CMDArgs *args);

// Читает секторы с диска по смещению и выводит hex-дамп
ErrorCode cmd_disk_read_s(CMDArgs *args);

// Записывает загрузочный код в MBR (первые 446 байт сектора 0)
ErrorCode cmd_mbr_write(CMDArgs *args);

// Записывает загрузочный код в BPB-сектор раздела FAT32 (смещение 90, после BPB)
ErrorCode cmd_bpb_write(CMDArgs *args);

#endif