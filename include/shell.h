#ifndef SHELL_H
#define SHELL_H

#include "cmd.h"
#include "error_codes.h"
#include <stdint.h>

ErrorCode cmd_shell(CMDArgs *args);

#endif