#include "main_commands.h"
#include "help.h"
#include "version.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// *** Реализация вспомогательных функций парсинга ***
static bool is_option(const char *arg) {
    return arg && arg[0] == '-';
}

static bool extract_value(const char *arg, const char *prefix, char *value, int max_len) {
    if (!arg || !prefix || !value) return false;
    size_t prefix_len = strlen(prefix);
    if (strncmp(arg, prefix, prefix_len) != 0) return false;
    const char *val_start = arg + prefix_len;
    strncpy(value, val_start, max_len - 1);
    value[max_len - 1] = '\0';
    return true;
}
// ***

int main(int argc, char *argv[]) {
    if (argc == 1) {
        print_short_help();
        return 0;
    }

    // Отдельная обработка глобальных опций
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        print_version();
        return 0;
    }

    // Определяем категорию команды (первый аргумент)
    const char *category = argv[1];
    if (strcmp(category, "--disk") != 0 &&
        strcmp(category, "--partition") != 0 &&
        strcmp(category, "--copy") != 0 &&
        strcmp(category, "--ls") != 0 &&
        strcmp(category, "--mkdir") != 0 &&
        strcmp(category, "--rmdir") != 0 &&
        strcmp(category, "--rm") != 0 &&
        strcmp(category, "--map_file") != 0) {
        printf("Unknown command category: %s\n", category);
		printf("Try '%s --help' for more information.\n", PROGRAM_EXECUTABLE_NAME);
        return 1;
    }

    CommandArgs args = {0};
    args.command = category;   // сохраняем категорию

    // Парсим остальные аргументы (начиная с индекса 2)
    for (int i = 2; i < argc; i++) {
        char value[MAX_ARG];

        if (extract_value(argv[i], "-disk=", value, sizeof(value))) {
            args.disk_path = strdup(value);
            args.has_disk_path = true;
        }
        else if (extract_value(argv[i], "-index=", value, sizeof(value))) {
            args.part_index_raw = strdup(value);
            args.has_part_index = true;
        }
        else if (extract_value(argv[i], "-size=", value, sizeof(value))) {
            args.size_raw = strdup(value);
            args.has_size = true;
        }
        else if (extract_value(argv[i], "-type=", value, sizeof(value))) {
            args.type_raw = strdup(value);
            args.has_type = true;
        }
        else if (extract_value(argv[i], "-file=", value, sizeof(value))) {
            args.file_raw = strdup(value);
            args.has_file = true;
        }
        else if (extract_value(argv[i], "-name=", value, sizeof(value))) {
            args.name_raw = strdup(value);
            args.has_name = true;
        }
        else if (extract_value(argv[i], "-src=", value, sizeof(value))) {
            args.src_raw = strdup(value);
            args.has_src = true;
        }
        else if (extract_value(argv[i], "-op=", value, sizeof(value))) {
            args.op_raw = strdup(value);
            args.has_op = true;
        }
        else if (extract_value(argv[i], "-offset=", value, sizeof(value))) {
            // для операции read диска
            args.offset_raw = strdup(value);
            args.has_offset = true;
        }
        else if (extract_value(argv[i], "-path=", value, sizeof(value))) {
            // Путь интерпретируется в зависимости от категории
            if (strcmp(category, "--disk") == 0) {
                // для дисковых операций это путь к образу диска
                args.path = strdup(value);
                args.has_path = true;
            } else {
                // для остальных это путь внутри файловой системы
                args.path_raw = strdup(value);
                args.has_path_raw = true;
            }
        }
        else if (is_option(argv[i])) {
            printf("Warning: Unknown option '%s' ignored\n", argv[i]);
        }
    }

    // Диспетчеризация по категориям
    if (strcmp(category, "--disk") == 0) {
        if (!args.has_op) {
            printf("Error: --disk requires -op parameter.\n");
            return 1;
        }
        if (strcmp(args.op_raw, "create") == 0) {
            return process_create_disk(&args);
        } else if (strcmp(args.op_raw, "info") == 0) {
            return process_disk_info(&args);
        } else if (strcmp(args.op_raw, "read") == 0) {
            // Предполагается наличие функции process_disk_read
            return process_disk_read(&args);
        } else {
            printf("Error: Unknown operation '%s' for --disk\n", args.op_raw);
            return 1;
        }
    }
    else if (strcmp(category, "--partition") == 0) {
        if (!args.has_op) {
            printf("Error: --partition requires -op parameter.\n");
            return 1;
        }
        if (strcmp(args.op_raw, "create") == 0) {
            return process_create_partition(&args);
        } else if (strcmp(args.op_raw, "delete") == 0) {
            return process_delete_partition(&args);
        } else if (strcmp(args.op_raw, "active") == 0 || strcmp(args.op_raw, "inactive") == 0) {
            // Предполагается, что process_set_active использует args.op_raw
            return process_set_active(&args);
        } else if (strcmp(args.op_raw, "set_type") == 0) {
            return process_set_type(&args);
        } else if (strcmp(args.op_raw, "format") == 0) {
            return process_format(&args);
        } else if (strcmp(args.op_raw, "write_mbr") == 0) {
            return process_write_mbr_loader(&args);
        } else if (strcmp(args.op_raw, "write_bpb") == 0) {
            return process_write_bpb_loader(&args);
        } else {
            printf("Error: Unknown operation '%s' for --partition\n", args.op_raw);
            return 1;
        }
    }
    else if (strcmp(category, "--copy") == 0) {
        // операция копирования без дополнительной опции
        return process_copy(&args);
    }
    else if (strcmp(category, "--ls") == 0) {
        return process_ls(&args);
    }
    else if (strcmp(category, "--mkdir") == 0) {
        return process_mkdir(&args);
    }
    else if (strcmp(category, "--rmdir") == 0) {
        return process_rmdir(&args);
    }
    else if (strcmp(category, "--rm") == 0) {
        return process_rm(&args);
    }
    else if (strcmp(category, "--map_file") == 0) {
        if (!args.has_op) {
            printf("Error: --map_file requires -op parameter.\n");
            return 1;
        }
        if (strcmp(args.op_raw, "list") == 0) {
            return process_map_file(&args);
        } else if (strcmp(args.op_raw, "delete") == 0) {
            return process_map_file(&args);
        } else if (strcmp(args.op_raw, "copy") == 0) {
            return process_copy_special(&args);
        } else {
            printf("Error: Unknown operation '%s' for --map_file\n", args.op_raw);
            return 1;
        }
    }

    printf("Unknown command category: %s\n", category);
    return 1;
}