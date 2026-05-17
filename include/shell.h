#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

#include "cmd.h"
#include "error_codes.h"

// Запускает интерактивную оболочку для работы с FAT32-разделом.
// Принимает путь к образу и номер раздела. Работает до команды "exit".
ErrorCode cmd_shell(CMDArgs *args);

#endif