#ifndef CMD_PART_H
#define CMD_PART_H

#include "cmd.h"
#include "error_codes.h"

/** Создаёт новый раздел. */
ErrorCode cmd_part_create(CMDArgs *args);

/** Удаляет раздел. */
ErrorCode cmd_part_delete(CMDArgs *args);

/** Изменяет тип раздела. */
ErrorCode cmd_part_set_type(CMDArgs *args);

/** Устанавливает активный флаг (только MBR). */
ErrorCode cmd_part_set_active(CMDArgs *args);

/** Снимает активный флаг (только MBR). */
ErrorCode cmd_part_set_inactive(CMDArgs *args);

#endif