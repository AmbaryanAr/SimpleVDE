#pragma once

#include "help.h"
#include "disk_commands.h"
#include "mbr_commands.h"
#include "gpt_commands.h"
#include "fat32_commands.h"
#include "error_code.h"
#include <stdbool.h>

#define MAX_ARG 256

/**
 * Флаги для проверки наличия аргументов командной строки.
 * Используются в битовой маске (например, ARG_DISK_PATH | ARG_SIZE).
 */
typedef enum {
    ARG_DISK_PATH   = 1 << 0,  // -disk= (путь к диску)
    ARG_PART_INDEX  = 1 << 1,  // -index= (номер раздела)
    ARG_SIZE        = 1 << 2,  // -size= (размер)
    ARG_TYPE        = 1 << 3,  // -type= (тип таблицы разделов или ФС)
    ARG_PATH        = 1 << 4,  // -path= (путь к файлу диска, используется только в --disk)
    ARG_PATH_RAW    = 1 << 5,  // -path= (путь внутри файловой системы, используется во всех остальных командах)
    ARG_FS_TYPE     = 1 << 6,  // -fs= (тип файловой системы)
    ARG_NAME        = 1 << 7,  // -name= (имя записи в карте файлов)
    ARG_FILE        = 1 << 8,  // -file= (путь к файлу для загрузчика MBR/BPB)
    ARG_SRC         = 1 << 9,  // -src= (исходный файл в хостовой ОС)
    ARG_OP          = 1 << 10, // -op= (операция: create, delete, ...)
    ARG_OFFSET      = 1 << 11  // -offset= (смещение в секторах)
} ArgFlag;

/**
 * Структура для хранения всех распарсенных аргументов командной строки.
 * Поля типа char* указывают на динамически выделенную память (strdup).
 */
typedef struct {
    const char *command;        // Категория команды: --disk, --partition, ... (ссылается на argv[1], не требует освобождения)

    // Флаги наличия параметров (true – параметр присутствовал в командной строке)
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

    // Значения параметров (строки, выделенные через strdup)
    char *disk_path;            // -disk= (путь к файлу диска)
    char *part_index_raw;       // -index= (сырая строка, например "1")
    char *size_raw;             // -size= (сырая строка, например "128M")
    char *type_raw;             // -type= (сырая строка, например "MBR")
    char *path;                 // -path= для --disk (путь к образу диска)
    char *path_raw;             // -path= для всех остальных команд (путь внутри ФС)
    char *fs_type;              // -fs= (тип файловой системы)
    char *name_raw;             // -name= (имя для операций с картой файлов)
    char *file_raw;             // -file= (путь к файлу для загрузчика)
    char *src_raw;              // -src= (исходный файл в ОС)
    char *op_raw;               // -op= (операция)
    char *offset_raw;           // -offset= (смещение в секторах)
} CommandArgs;

// Прототипы функций обработки команд.
// Каждая функция принимает распарсенные аргументы, проверяет их наличие.
// Возвращает код ошибки (ERR_OK при успехе).

ErrorCode process_create_disk(CommandArgs *args);
ErrorCode process_disk_info(CommandArgs *args);
ErrorCode process_disk_read(CommandArgs *args);

ErrorCode process_create_partition(CommandArgs *args);
ErrorCode process_delete_partition(CommandArgs *args);
ErrorCode process_set_active(CommandArgs *args);
ErrorCode process_set_type(CommandArgs *args);
ErrorCode process_format(CommandArgs *args);
ErrorCode process_write_mbr_loader(CommandArgs *args);
ErrorCode process_write_bpb_loader(CommandArgs *args);

ErrorCode process_ls(CommandArgs *args);
ErrorCode process_copy(CommandArgs *args);
ErrorCode process_rm(CommandArgs *args);
ErrorCode process_mkdir(CommandArgs *args);
ErrorCode process_rmdir(CommandArgs *args);

ErrorCode process_map_file(CommandArgs *args);
ErrorCode process_copy_special(CommandArgs *args);