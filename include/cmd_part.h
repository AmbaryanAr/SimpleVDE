#ifndef CMD_PART_H
#define CMD_PART_H

#include "cmd.h"
#include "error_codes.h"

// Создаёт новый раздел. Тип таблицы (MBR/GPT) определяется автоматически.
ErrorCode cmd_part_create(CMDArgs *args);

// Удаляет раздел по номеру.
ErrorCode cmd_part_delete(CMDArgs *args);

// Изменяет тип раздела (код для MBR, GUID для GPT).
ErrorCode cmd_part_set_type(CMDArgs *args);

// Устанавливает флаг активности для раздела (только MBR).
ErrorCode cmd_part_set_active(CMDArgs *args);

// Снимает флаг активности с раздела (только MBR).
ErrorCode cmd_part_set_inactive(CMDArgs *args);

#endif