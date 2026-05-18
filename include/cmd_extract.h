#ifndef CMD_EXTRACT_H
#define CMD_EXTRACT_H

#include "cmd.h"
#include "error_codes.h"

// Копирует раздел в отдельный raw-файл (раздел остаётся на месте)
ErrorCode cmd_extract_copy(CMDArgs *args);

#endif