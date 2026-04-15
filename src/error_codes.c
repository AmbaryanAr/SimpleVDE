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
        case ERR_UNKNOWN:           return "Unknown error";
        // FAT32-specific
        case ERR_FAT32_BAD_BPB:                return "Bad BPB (not a FAT32 volume)";
        case ERR_FAT32_FSINFO_CORRUPT:         return "FSInfo sector corrupted";
        case ERR_FAT32_VOLUME_NOT_MOUNTED:     return "Volume not mounted";
        case ERR_FAT32_NO_FREE_CLUSTER:        return "No free cluster";
        case ERR_FAT32_BAD_CLUSTER:            return "Bad cluster detected";
        case ERR_FAT32_FAT_CORRUPT:            return "FAT table corrupted";
        case ERR_FAT32_DIR_NO_FREE_ENTRY:      return "No free entry in directory";
        case ERR_FAT32_DIR_IS_NOT_DIRECTORY:   return "Entry is not a directory";
        case ERR_FAT32_NAME_TOO_LONG:          return "Name too long";
        case ERR_FAT32_NAME_INVALID:           return "Invalid characters in name";
        case ERR_FAT32_UTF16_CONVERSION:       return "UTF-16 conversion error";
        case ERR_FAT32_LFN_CHECKSUM:           return "LFN checksum mismatch";
        case ERR_FAT32_TOO_MANY_LFN_ENTRIES:   return "Too many LFN entries";
        case ERR_FAT32_SFN_SUFFIX_OVERFLOW:    return "SFN suffix overflow";
		case ERR_RESERVE_NOT_INIT:             return "Reserve cluster not initialized";
        default: return "Unknown error";
    }
}