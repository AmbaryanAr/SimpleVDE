#include "main_commands.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// *** Вспомогательная функция для вывода аргументов ***
static void print_args_summary(CommandArgs *args) {
    printf("Arguments received:\n");
    printf("  Command category: %s\n", args->command ? args->command : "NULL");
    
    if (args->has_disk_path) printf("  disk_path: %s\n", args->disk_path);
    if (args->has_part_index) printf("  part_index: %s\n", args->part_index_raw);
    if (args->has_size) printf("  size: %s\n", args->size_raw);
    if (args->has_type) printf("  type: %s\n", args->type_raw);
    if (args->has_path) printf("  path (disk): %s\n", args->path);
    if (args->has_path_raw) printf("  path (fs): %s\n", args->path_raw);
    if (args->has_fs_type) printf("  fs_type: %s\n", args->fs_type);
    if (args->has_name) printf("  name: %s\n", args->name_raw);
    if (args->has_file) printf("  file: %s\n", args->file_raw);
    if (args->has_src) printf("  src: %s\n", args->src_raw);
    if (args->has_op) printf("  op: %s\n", args->op_raw);
    if (args->has_offset) printf("  offset: %s\n", args->offset_raw);
}

// *** Статическая функция проверки обязательных аргументов ***
static ErrorCode check_required_args(CommandArgs *args, int required_flags, const char *action_name) {
    struct {
        int flag;
        bool has_field;
        const char *arg_name;
    } checks[] = {
        { ARG_DISK_PATH,  args->has_disk_path,  "-disk=" },
        { ARG_PART_INDEX, args->has_part_index, "-index=" },
        { ARG_SIZE,       args->has_size,       "-size=" },
        { ARG_TYPE,       args->has_type,       "-type=" },
        { ARG_PATH,       args->has_path,       "-path= (disk)" },
        { ARG_PATH_RAW,   args->has_path_raw,   "-path= (fs)" },
        { ARG_FS_TYPE,    args->has_fs_type,    "-fs=" },
        { ARG_NAME,       args->has_name,       "-name=" },
        { ARG_FILE,       args->has_file,       "-file=" },
        { ARG_SRC,        args->has_src,        "-src=" },
        { ARG_OP,         args->has_op,         "-op=" },
        { ARG_OFFSET,     args->has_offset,     "-offset=" }
    };

    for (size_t i = 0; i < sizeof(checks)/sizeof(checks[0]); i++) {
        if ((required_flags & checks[i].flag) && !checks[i].has_field) {
            printf("ERROR: %s is required for %s\n", checks[i].arg_name, action_name);
            return ERR_MISSING_ARGUMENT;
        }
    }
    return ERR_OK;
}
// ***

// ---------- Заглушки для дисковых операций ----------
int process_create_disk(CommandArgs *args) {
    printf("\n>>> STUB: process_create_disk\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_PATH | ARG_SIZE | ARG_TYPE, "disk creation") != ERR_OK)
        return 1;

    printf("ACTION: Would create disk at '%s' with size %s and type %s\n", 
           args->path, args->size_raw, args->type_raw);
    return 0;
}

int process_disk_info(CommandArgs *args) {
    printf("\n>>> STUB: process_disk_info\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_PATH, "disk info") != ERR_OK)
        return 1;

    printf("ACTION: Would show information for disk at '%s'\n", args->path);
    return 0;
}

int process_disk_read(CommandArgs *args) {
    printf("\n>>> STUB: process_disk_read\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_PATH | ARG_OFFSET | ARG_SIZE, "disk read") != ERR_OK)
        return 1;

    printf("ACTION: Would read %s bytes from disk '%s' at offset %s\n", 
           args->size_raw, args->path, args->offset_raw);
    return 0;
}

// ---------- Заглушки для операций с разделами ----------
int process_create_partition(CommandArgs *args) {
    printf("\n>>> STUB: process_create_partition\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_SIZE | ARG_TYPE, "partition creation") != ERR_OK)
        return 1;

    printf("ACTION: Would create %s partition at index %s on disk '%s' with size %s\n", 
           args->type_raw, args->part_index_raw, args->disk_path, args->size_raw);
    return 0;
}

int process_delete_partition(CommandArgs *args) {
    printf("\n>>> STUB: process_delete_partition\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX, "partition deletion") != ERR_OK)
        return 1;

    printf("ACTION: Would delete partition at index %s on disk '%s'\n", 
           args->part_index_raw, args->disk_path);
    return 0;
}

int process_set_active(CommandArgs *args) {
    printf("\n>>> STUB: process_set_active\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_OP, "set active") != ERR_OK)
        return 1;

    if (strcmp(args->op_raw, "active") != 0 && strcmp(args->op_raw, "inactive") != 0) {
        printf("ERROR: -op= must be 'active' or 'inactive' for set active\n");
        return 1;
    }

    const char *state = (strcmp(args->op_raw, "active") == 0) ? "active" : "inactive";
    printf("ACTION: Would set partition at index %s on disk '%s' as %s\n", 
           args->part_index_raw, args->disk_path, state);
    return 0;
}

int process_set_type(CommandArgs *args) {
    printf("\n>>> STUB: process_set_type\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_TYPE, "set type") != ERR_OK)
        return 1;

    printf("ACTION: Would set type of partition at index %s on disk '%s' to %s\n", 
           args->part_index_raw, args->disk_path, args->type_raw);
    return 0;
}

int process_format(CommandArgs *args) {
    printf("\n>>> STUB: process_format\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_TYPE, "format") != ERR_OK)
        return 1;

    printf("ACTION: Would format partition at index %s on disk '%s' as %s\n", 
           args->part_index_raw, args->disk_path, args->type_raw);
    return 0;
}

int process_write_mbr_loader(CommandArgs *args) {
    printf("\n>>> STUB: process_write_mbr_loader\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_FILE, "write MBR") != ERR_OK)
        return 1;

    printf("ACTION: Would write MBR loader from file '%s' to partition at index %s on disk '%s'\n", 
           args->file_raw, args->part_index_raw, args->disk_path);
    return 0;
}

int process_write_bpb_loader(CommandArgs *args) {
    printf("\n>>> STUB: process_write_bpb_loader\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_FILE, "write BPB") != ERR_OK)
        return 1;

    printf("ACTION: Would write BPB loader from file '%s' to partition at index %s on disk '%s'\n", 
           args->file_raw, args->part_index_raw, args->disk_path);
    return 0;
}

// ---------- Заглушки для файловых операций ----------
int process_ls(CommandArgs *args) {
    printf("\n>>> STUB: process_ls\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX, "ls") != ERR_OK)
        return 1;

    const char *path = args->has_path_raw ? args->path_raw : "/";
    printf("ACTION: Would list contents of '%s' on partition %s of disk '%s'\n", 
           path, args->part_index_raw, args->disk_path);
    return 0;
}

int process_copy(CommandArgs *args) {
    printf("\n>>> STUB: process_copy\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_SRC | ARG_DISK_PATH | ARG_PART_INDEX | ARG_PATH_RAW, "copy") != ERR_OK)
        return 1;

    printf("ACTION: Would copy host file '%s' to '%s' on partition %s of disk '%s'\n", 
           args->src_raw, args->path_raw, args->part_index_raw, args->disk_path);
    return 0;
}

int process_rm(CommandArgs *args) {
    printf("\n>>> STUB: process_rm\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_PATH_RAW, "rm") != ERR_OK)
        return 1;

    printf("ACTION: Would remove file '%s' on partition %s of disk '%s'\n", 
           args->path_raw, args->part_index_raw, args->disk_path);
    return 0;
}

int process_mkdir(CommandArgs *args) {
    printf("\n>>> STUB: process_mkdir\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_PATH_RAW, "mkdir") != ERR_OK)
        return 1;

    printf("ACTION: Would create directory '%s' on partition %s of disk '%s'\n", 
           args->path_raw, args->part_index_raw, args->disk_path);
    return 0;
}

int process_rmdir(CommandArgs *args) {
    printf("\n>>> STUB: process_rmdir\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_PATH_RAW, "rmdir") != ERR_OK)
        return 1;

    printf("ACTION: Would remove directory '%s' on partition %s of disk '%s'\n", 
           args->path_raw, args->part_index_raw, args->disk_path);
    return 0;
}

// ---------- Заглушки для карты специальных файлов ----------
int process_map_file(CommandArgs *args) {
    printf("\n>>> STUB: process_map_file\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_OP, "map_file") != ERR_OK)
        return 1;

    if (strcmp(args->op_raw, "delete") == 0 && !args->has_name) {
        printf("ERROR: -name= is required for delete operation\n");
        return 1;
    }

    if (strcmp(args->op_raw, "list") == 0) {
        printf("ACTION: Would list map file entries on partition %s of disk '%s'\n", 
               args->part_index_raw, args->disk_path);
    } else if (strcmp(args->op_raw, "delete") == 0) {
        printf("ACTION: Would delete map file entry '%s' on partition %s of disk '%s'\n", 
               args->name_raw, args->part_index_raw, args->disk_path);
    } else {
        printf("ERROR: Unsupported operation '%s' for map_file\n", args->op_raw);
        return 1;
    }

    return 0;
}

int process_copy_special(CommandArgs *args) {
    printf("\n>>> STUB: process_copy_special\n");
    print_args_summary(args);

    if (check_required_args(args, ARG_DISK_PATH | ARG_PART_INDEX | ARG_SRC | ARG_PATH_RAW, "copy_special") != ERR_OK)
        return 1;

    printf("ACTION: Would copy host file '%s' to '%s' on partition %s of disk '%s' with map file entry\n", 
           args->src_raw, args->path_raw, args->part_index_raw, args->disk_path);
    return 0;
}