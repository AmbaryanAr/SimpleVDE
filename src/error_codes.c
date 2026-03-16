#include "error_codes.h"

const char* error_code_to_string(ErrorCode code) {
    switch (code) {
        case ERR_OK:                return "Success";
        case ERR_MISSING_ARGUMENT:  return "Missing required argument";
        case ERR_INVALID_ARGUMENT:  return "Invalid argument";
        case ERR_OUT_OF_MEMORY:     return "Out of memory";
        case ERR_DISK_OPEN:         return "Failed to open disk image";
        case ERR_DISK_CREATE:       return "Failed to create disk image";
        case ERR_DISK_READ:         return "Disk read error";
        case ERR_DISK_WRITE:        return "Disk write error";
        case ERR_DISK_SEEK:         return "Disk seek error";
        case ERR_DISK_CLOSE:        return "Disk close error";
        case ERR_NOT_FOUND:         return "File or directory not found";
        case ERR_ALREADY_EXISTS:    return "File or directory already exists";
        case ERR_NOT_SUPPORTED:     return "Operation not supported";
        case ERR_INVALID_SIGNATURE: return "Invalid signature (not a FAT32 volume)";
        case ERR_NO_FREE_SPACE:     return "No free space on disk";
        case ERR_INTERNAL:          return "Internal error";
        case ERR_NOT_IMPLEMENTED:   return "Not implemented yet";
        case ERR_DIR_NOT_EMPTY:     return "Directory not empty";
        default:                    return "Unknown error";
    }
}