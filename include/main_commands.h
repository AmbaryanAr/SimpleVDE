#pragma once

#include "help.h"

#include <stdbool.h>

#define MAX_ARG 256

// Коды возврата для функций
typedef enum {
    ERR_OK = 0,
    ERR_MISSING_ARGUMENT,
    ERR_INVALID_VALUE,
    ERR_GENERIC
} ErrorCode;

// Флаги для проверки наличия аргументов
typedef enum {
    ARG_DISK_PATH   = 1 << 0,  // -disk=
    ARG_PART_INDEX  = 1 << 1,  // -index=
    ARG_SIZE        = 1 << 2,  // -size=
    ARG_TYPE        = 1 << 3,  // -type=
    ARG_PATH        = 1 << 4,  // -path= (для диска)
    ARG_PATH_RAW    = 1 << 5,  // -path= (для ФС)
    ARG_FS_TYPE     = 1 << 6,  // -fs=
    ARG_NAME        = 1 << 7,  // -name=
    ARG_FILE        = 1 << 8,  // -file=
    ARG_SRC         = 1 << 9,  // -src=
    ARG_OP          = 1 << 10, // -op=
    ARG_OFFSET      = 1 << 11  // -offset=
} ArgFlag;

// Структура для хранения распарсенных аргументов
typedef struct {
    const char *command;        // основная команда (категория)
    
    // Флаги наличия параметров
    bool has_disk_path;
    bool has_part_index;
    bool has_size;
    bool has_type;
    bool has_path;
    bool has_path_raw;
    bool has_fs_type;
    bool has_name;
    bool has_file;
    bool has_src;
    bool has_op;
    bool has_offset;
    
    // Значения параметров
    char *disk_path;            // -disk=
    char *part_index_raw;       // -index=
    char *size_raw;             // -size=
    char *type_raw;             // -type=
    char *path;                 // -path= (для диска)
    char *path_raw;             // -path= (для файловых операций)
    char *fs_type;              // -fs=
    char *name_raw;             // -name=
    char *file_raw;             // -file=
    char *src_raw;              // -src=
    char *op_raw;               // -op=
    char *offset_raw;           // -offset=
} CommandArgs;

// Прототипы функций обработки команд
int process_create_disk(CommandArgs *args);
int process_disk_info(CommandArgs *args);
int process_disk_read(CommandArgs *args);

int process_create_partition(CommandArgs *args);
int process_delete_partition(CommandArgs *args);
int process_set_active(CommandArgs *args);
int process_set_type(CommandArgs *args);
int process_format(CommandArgs *args);
int process_write_mbr_loader(CommandArgs *args);
int process_write_bpb_loader(CommandArgs *args);

int process_ls(CommandArgs *args);
int process_copy(CommandArgs *args);
int process_rm(CommandArgs *args);
int process_mkdir(CommandArgs *args);
int process_rmdir(CommandArgs *args);

int process_map_file(CommandArgs *args);
int process_copy_special(CommandArgs *args);