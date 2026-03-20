#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

#include "cmd.h"
#include "error_codes.h"

ErrorCode cmd_shell(CMDArgs *args);

#endif