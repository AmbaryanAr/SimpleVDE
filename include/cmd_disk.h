#ifndef CMD_DISK_H
#define CMD_DISK_H

#include "cmd.h"
#include "fat32.h"
#include "error_codes.h"

/** Создаёт новый образ диска. */
ErrorCode cmd_disk_create(CMDArgs *args);

/** Выводит информацию о диске и таблице разделов. */
ErrorCode cmd_disk_info(CMDArgs *args);

/** Читает байты с диска и выводит hex-дамп. */
ErrorCode cmd_disk_read(CMDArgs *args);

/** Читает секторы с диска и выводит hex-дамп. */
ErrorCode cmd_disk_read_s(CMDArgs *args);

/** Записывает загрузочный код в MBR (первые 446 байт). */
ErrorCode cmd_mbr_write(CMDArgs *args);

/** Записывает загрузочный код в BPB-сектор (смещение 90). */
ErrorCode cmd_bpb_write(CMDArgs *args);

#endif