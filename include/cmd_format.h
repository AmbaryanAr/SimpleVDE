#ifndef CMD_FORMAT_H
#define CMD_FORMAT_H

#include "cmd.h"
#include "disk.h"
#include "fat32.h"
#include "error_codes.h"

ErrorCode cmd_format(CMDArgs *args);

#endif