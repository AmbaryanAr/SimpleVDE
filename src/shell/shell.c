#include "disk.h"
#include "shell.h"
#include "fat32.h"
#include "utils.h"
#include "output.h"
#include "partition.h"
#include "fat32_util.h"
#include "cmd_common.h"
#include "error_codes.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static struct {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    char current_path[MAX_PATH];
    int is_open;
    char part_str[16];
} shell_state = { .is_open = 0 };

// Приводит абсолютный путь к каноническому виду: убирает ".", "..", двойные слеши
static char* normalize_path(const char *path);

// Преобразует относительный или абсолютный путь в абсолютный на основе current_path
static char* resolve_path(const char *input);

// Разбирает строку на аргументы (аналог argv). Поддерживает кавычки.
// Возвращает количество аргументов или -1 при ошибке.
static int parse_line(char *line, char **argv, int max_args);

// Выводит список команд интерактивной оболочки
static void print_help(void);

// Обработчик команды ls: выводит содержимое каталога
static int cmd_ls(int argc, char **argv);

// Обработчик команды cd: меняет текущий каталог
static int cmd_cd(int argc, char **argv);

// Обработчик команды pwd: выводит текущий каталог
static int cmd_pwd(int argc, char **argv);

// Обработчик команды mkdir: создаёт каталог
static int cmd_mkdir(int argc, char **argv);

// Обработчик команды rmdir: удаляет пустой каталог
static int cmd_rmdir(int argc, char **argv);

// Обработчик команды rm: удаляет файл
static int cmd_rm(int argc, char **argv);

// Обработчик команды copy: копирует файл с хоста в образ
static int cmd_copy(int argc, char **argv);

// Обработчик команды reserve: работает с реестром (init/ls/add/rm/clear/dump/info/boot)
static int cmd_reserve(int argc, char **argv);

// Обработчик команды tree: выводит дерево каталогов
static int cmd_tree(int argc, char **argv);

// Рекурсивно выводит дерево каталога (дублирует логику из cmd_fs.c)
static void print_tree(Disk *disk, const Fat32Info *info, uint32_t cluster,
                       const char *name, const char *prefix, int is_last);

// Инициализирует состояние оболочки: открывает диск, проверяет раздел и FAT32
static ErrorCode shell_init(const char *file, const char *part) {
    if (shell_state.is_open) {
        return ERR_OK;
    }

    ErrorCode err = disk_open(file, &shell_state.disk);
    if (err != ERR_OK) {
        svde_err( "Error: cannot open disk image: %s\n", file);
        return err;
    }

    int part_index = parse_part_index(part);
    if (part_index == PART_INDEX_INVALID) {
        disk_close(&shell_state.disk);
        svde_err( "Error: invalid partition number '%s'.\n", part);
        return ERR_INVALID_ARGUMENT;
    }

    if (part_index == PART_INDEX_RAW) {
        // Raw-образ: весь диск как один FAT32-раздел
        shell_state.start_lba = 0;
    } else {
        PartitionTableType table_type;
        err = partition_detect_type(&shell_state.disk, &table_type);
        if (err != ERR_OK || table_type == PT_UNKNOWN) {
            disk_close(&shell_state.disk);
            svde_err( "Error: invalid partition table.\n");
            return err != ERR_OK ? err : ERR_INVALID_SIGNATURE;
        }

        uint64_t size_sectors;
        err = partition_get_info(&shell_state.disk, part_index, &shell_state.start_lba, &size_sectors);
        if (err != ERR_OK) {
            disk_close(&shell_state.disk);
            if (err == ERR_NOT_FOUND) {
                svde_err( "Error: partition %d does not exist.\n", part_index + 1);
            } else {
                svde_err( "Error: cannot get partition info.\n");
            }
            return err;
        }
    }

    err = fat32_get_info(&shell_state.disk, shell_state.start_lba, &shell_state.info);
    if (err != ERR_OK) {
        disk_close(&shell_state.disk);
        svde_err( "Error: partition is not a valid FAT32 filesystem.\n");
        return ERR_INVALID_SIGNATURE;
    }

    strcpy(shell_state.current_path, "/");
    strncpy(shell_state.part_str, part, sizeof(shell_state.part_str) - 1);
    shell_state.part_str[sizeof(shell_state.part_str) - 1] = '\0';
    shell_state.is_open = 1;
    return ERR_OK;
}

// Закрывает диск и сбрасывает состояние оболочки
static void shell_shutdown(void) {
    if (shell_state.is_open) {
        disk_close(&shell_state.disk);
        shell_state.is_open = 0;
    }
}

static char* normalize_path(const char *path) {
    if (!path || path[0] != '/') {
        return NULL;
    }
    char *result = malloc(MAX_PATH);
    if (!result) {
        return NULL;
    }
    char *out = result;
    const char *in = path;
    *out++ = '/';
    in++;

    while (*in) {
        while (*in == '/') {
            in++;
        }
        if (*in == '\0') {
            break;
        }
        const char *start = in;
        while (*in && *in != '/') {
            in++;
        }
        size_t len = in - start;

        if (len == 1 && start[0] == '.') {
            // ничего
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (out > result + 1) {
                out--;
                while (out > result && *(out - 1) != '/') {
                    out--;
                }
            }
        } else {
            if (*(out - 1) != '/') {
                *out++ = '/';
            }
            memcpy(out, start, len);
            out += len;
        }
    }
    *out = '\0';
    if (out == result + 1 && result[0] == '/') {
        result[1] = '\0';
    }
    return result;
}

static char* resolve_path(const char *input) {
    if (!input) return NULL;
    char *full;
    if (input[0] == '/') {
        full = my_strdup(input);
    } else {
        // Динамическое выделение буфера нужного размера
        size_t needed = strlen(shell_state.current_path) + strlen(input) + 2; // +1 для '/' и +1 для '\0'
        char *temp = (char*)malloc(needed);
        if (!temp) return NULL;
        snprintf(temp, needed, "%s/%s", shell_state.current_path, input);
        full = my_strdup(temp);
        free(temp);
    }
    if (!full) return NULL;
    char *norm = normalize_path(full);
    free(full);
    return norm;
}

static int parse_line(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (argc >= max_args) {
            return -1;
        }
        if (*p == '"') {
            p++;
            argv[argc] = p;
            while (*p && *p != '"') {
                p++;
            }
            if (*p == '"') {
                *p++ = '\0';
            } else {
                return -1;
            }
        } else {
            argv[argc] = p;
            while (*p && !isspace((unsigned char)*p)) {
                p++;
            }
            if (*p) {
                *p++ = '\0';
            }
        }
        argc++;
    }
    return argc;
}

static void print_help(void) {
    svde_out("Available commands:\n");
    svde_out("  ls [path]                 - list directory contents\n");
    svde_out("  cd [path]                 - change current directory (default: root)\n");
    svde_out("  pwd                       - print current directory\n");
    svde_out("  mkdir <path>              - create directory\n");
    svde_out("  rmdir <path>              - remove empty directory\n");
    svde_out("  rm <path>                 - delete file\n");
    svde_out("  copy <host-file> <dest>   - copy file from host to image\n");
    svde_out("  tree [path]               - display directory tree\n");
    svde_out("\n - A common command for working with the file registry -\n");
    svde_out("  reserve init              - initialize reserve cluster\n");
    svde_out("  reserve ls                - list reserve entries\n");
    svde_out("  reserve add <path>        - add file entry to reserve\n");
    svde_out("  reserve rm <name>         - remove entry by name\n");
    svde_out("  reserve clear             - clear all entries\n");
    svde_out("  reserve dump              - hex dump of reserve cluster\n");
    svde_out("  reserve info              - show reserve info\n\n");
	svde_out("  reserve boot set <name>     - set file as boot entry\n");
    svde_out("  reserve boot show           - show current boot entry\n");
    svde_out("  reserve boot clear          - clear boot entry\n");
    svde_out("  exit                      - exit shell\n");
    svde_out("  help, ?                   - show this help\n");
}

static int cmd_ls(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";
    char *abs_path = resolve_path(path);
    if (!abs_path) {
        svde_err( "Error: invalid path.\n");
        return 1;
    }
    ErrorCode err = fat32_list_dir(&shell_state.disk, shell_state.start_lba, abs_path);
    free(abs_path);
    if (err != ERR_OK) {
        svde_err( "Error: %s\n", error_code_to_string(err));
        return 1;
    }
    return 0;
}

static int cmd_cd(int argc, char **argv) {
    const char *target = (argc > 1) ? argv[1] : "/";
    char *abs_path = resolve_path(target);
    if (!abs_path) {
        svde_err( "Error: invalid path.\n");
        return 1;
    }
    uint32_t cluster;
    ErrorCode err = fat32_find_dir(&shell_state.disk, &shell_state.info, abs_path, &cluster);
    if (err != ERR_OK) {
        svde_err( "Error: directory not found.\n");
        free(abs_path);
        return 1;
    }
    strcpy(shell_state.current_path, abs_path);
    free(abs_path);
    return 0;
}

static int cmd_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    svde_out("%s\n", shell_state.current_path);
    return 0;
}

static int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        svde_err( "Usage: mkdir <path>\n");
        return 1;
    }
    char *abs_path = resolve_path(argv[1]);
    if (!abs_path) {
        svde_err( "Error: invalid path.\n");
        return 1;
    }
    ErrorCode err = fat32_create_dir(&shell_state.disk, shell_state.start_lba, abs_path);
    free(abs_path);
    if (err != ERR_OK) {
        svde_err( "Error: %s\n", error_code_to_string(err));
        return 1;
    }
    return 0;
}

static int cmd_rmdir(int argc, char **argv) {
    if (argc < 2) {
        svde_err( "Usage: rmdir <path>\n");
        return 1;
    }
    char *abs_path = resolve_path(argv[1]);
    if (!abs_path) {
        svde_err( "Error: invalid path.\n");
        return 1;
    }
    ErrorCode err = fat32_remove_dir(&shell_state.disk, shell_state.start_lba, abs_path);
    free(abs_path);
    if (err != ERR_OK) {
        svde_err( "Error: %s\n", error_code_to_string(err));
        return 1;
    }
    return 0;
}

static int cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        svde_err( "Usage: rm <path>\n");
        return 1;
    }
    char *abs_path = resolve_path(argv[1]);
    if (!abs_path) {
        svde_err( "Error: invalid path.\n");
        return 1;
    }
    ErrorCode err = fat32_delete_file(&shell_state.disk, shell_state.start_lba, abs_path);
    free(abs_path);
    if (err != ERR_OK) {
        svde_err( "Error: %s\n", error_code_to_string(err));
        return 1;
    }
    return 0;
}

static int cmd_copy(int argc, char **argv) {
    if (argc < 3) {
        svde_err( "Usage: copy <host-file> <dest>\n");
        return 1;
    }
    char *abs_dest = resolve_path(argv[2]);
    if (!abs_dest) {
        svde_err( "Error: invalid destination path.\n");
        return 1;
    }
    ErrorCode err = fat32_copy_file(&shell_state.disk, shell_state.start_lba, argv[1], abs_dest);
    free(abs_dest);
    if (err != ERR_OK) {
        svde_err( "Error: %s\n", error_code_to_string(err));
        return 1;
    }
    return 0;
}

static int cmd_reserve(int argc, char **argv) {
    if (argc < 2) {
        svde_err( "Usage: reserve <subcommand> [args]\n");
        return 1;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "init") == 0) {
        ErrorCode err = fat32_reserve_init(&shell_state.disk, shell_state.start_lba);
        if (err != ERR_OK) {
            svde_err( "Error: %s\n", error_code_to_string(err));
            return 1;
        }
        svde_out("Reserve cluster initialized.\n");
    } else if (strcmp(sub, "ls") == 0) {
        ErrorCode err = fat32_reserve_list(&shell_state.disk, shell_state.start_lba);
        if (err != ERR_OK) {
            svde_err( "Error: %s\n", error_code_to_string(err));
            return 1;
        }
    } else if (strcmp(sub, "add") == 0) {
        if (argc < 3) {
            svde_err( "Usage: reserve add <path>\n");
            return 1;
        }
        char *abs_path = resolve_path(argv[2]);
        if (!abs_path) {
            svde_err( "Error: invalid path.\n");
            return 1;
        }
        ErrorCode err = fat32_reserve_add(&shell_state.disk, shell_state.start_lba, abs_path);
        free(abs_path);
        if (err != ERR_OK) {
            svde_err( "Error: %s\n", error_code_to_string(err));
            return 1;
        }
        svde_out("Entry added.\n");
    } else if (strcmp(sub, "rm") == 0) {
        if (argc < 3) {
            svde_err( "Usage: reserve rm <name>\n");
            return 1;
        }
        ErrorCode err = fat32_reserve_remove(&shell_state.disk, shell_state.start_lba, argv[2]);
        if (err != ERR_OK) {
            svde_err( "Error: %s\n", error_code_to_string(err));
            return 1;
        }
        svde_out("Entry removed.\n");
    } else if (strcmp(sub, "clear") == 0) {
        ErrorCode err = fat32_reserve_clear(&shell_state.disk, shell_state.start_lba);
        if (err != ERR_OK) {
            svde_err( "Error: %s\n", error_code_to_string(err));
            return 1;
        }
        svde_out("Reserve cleared.\n");
    } else if (strcmp(sub, "dump") == 0) {
        ErrorCode err = fat32_reserve_dump(&shell_state.disk, shell_state.start_lba);
        if (err != ERR_OK) {
            svde_err( "Error: %s\n", error_code_to_string(err));
            return 1;
        }
    } else if (strcmp(sub, "info") == 0) {
        ErrorCode err = fat32_reserve_info(&shell_state.disk, shell_state.start_lba);
        if (err != ERR_OK) {
            svde_err( "Error: %s\n", error_code_to_string(err));
            return 1;
        }
    } else if (strcmp(sub, "boot") == 0) {
        if (argc < 3) {
            svde_err( "Usage: reserve boot set <name> | reserve boot show | reserve boot clear\n");
            return 1;
        }
        const char *subsub = argv[2];
        if (strcmp(subsub, "set") == 0) {
            if (argc < 4) {
                svde_err( "Usage: reserve boot set <name>\n");
                return 1;
            }
            ErrorCode err = fat32_reserve_boot_set(&shell_state.disk, shell_state.start_lba, argv[3]);
            if (err != ERR_OK) {
                svde_err( "Error: %s\n", error_code_to_string(err));
                return 1;
            }
            svde_out("Boot entry set.\n");
        } else if (strcmp(subsub, "show") == 0) {
            ErrorCode err = fat32_reserve_boot_show(&shell_state.disk, shell_state.start_lba);
            if (err != ERR_OK) {
                svde_err( "Error: %s\n", error_code_to_string(err));
                return 1;
            }
        } else if (strcmp(subsub, "clear") == 0) {
            ErrorCode err = fat32_reserve_boot_clear(&shell_state.disk, shell_state.start_lba);
            if (err != ERR_OK) {
                svde_err( "Error: %s\n", error_code_to_string(err));
                return 1;
            }
            svde_out("Boot entry cleared.\n");
        } else {
            svde_err( "Unknown boot subcommand: %s\n", subsub);
            return 1;
        }
    }
	return 0;
}

static void print_tree(Disk *disk, const Fat32Info *info, uint32_t cluster,
                       const char *name, const char *prefix, int is_last) {
    svde_out("%s%s%s\n", prefix, (is_last ? "'-- " : "|-- "), name);

    uint8_t *buffer = NULL;
    uint32_t entries = 0;
    if (fat32_read_dir(disk, info, cluster, &buffer, &entries) != ERR_OK) {
        return;
    }

    typedef struct {
        uint32_t cluster;
        char *name;
        int is_dir;
        uint32_t size;
    } child_entry_t;

    child_entry_t *children = NULL;
    uint32_t child_count = 0;

    for (uint32_t i = 0; i < entries; i++) {
        const uint8_t *entry = buffer + i * 32;
        if (entry[0] == 0x00) {
            break;
        }
        if (entry[0] == 0xE5) {
            continue;
        }
        if (entry[11] == FAT32_ATTR_LFN) {
            continue;
        }

        Fat32ShortEntry *se = (Fat32ShortEntry*)entry;
        char display_name[256];

        char *long_name = extract_lfn_name(buffer, i);
        if (long_name) {
            strcpy(display_name, long_name);
            free(long_name);
        } else {
            sfn_to_display_name(se->name, display_name, sizeof(display_name));
        }

        if (strcmp(display_name, ".") == 0 || strcmp(display_name, "..") == 0) {
            continue;
        }

        children = realloc(children, (child_count + 1) * sizeof(child_entry_t));
        children[child_count].name = my_strdup(display_name);
        children[child_count].cluster = ((uint32_t)se->first_cluster_hi << 16) | se->first_cluster_lo;
        children[child_count].is_dir = (se->attr & FAT32_ATTR_DIRECTORY) != 0;
        children[child_count].size = se->file_size;
        child_count++;
    }

    free(buffer);

    char new_prefix[512];
    snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix, (is_last ? "    " : "|   "));

    for (uint32_t i = 0; i < child_count; i++) {
        child_entry_t *child = &children[i];
        int last = (i == child_count - 1);

        if (child->is_dir) {
            print_tree(disk, info, child->cluster, child->name, new_prefix, last);
        } else {
            char file_info[256];
            snprintf(file_info, sizeof(file_info), "%s (%u bytes)", child->name, child->size);
            svde_out("%s%s%s\n", new_prefix, (last ? "'-- " : "|-- "), file_info);
        }
        free(child->name);
    }
    free(children);
}

static int cmd_tree(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";
    char *abs_path = resolve_path(path);
    if (!abs_path) {
        svde_err( "Error: invalid path.\n");
        return 1;
    }

    uint32_t cluster;
    ErrorCode err = fat32_find_dir(&shell_state.disk, &shell_state.info, abs_path, &cluster);
    if (err != ERR_OK) {
        svde_err( "Error: cannot find path.\n");
        free(abs_path);
        return 1;
    }

    svde_out("Directory tree of %s:\n", abs_path);
    print_tree(&shell_state.disk, &shell_state.info, cluster, abs_path, "", 1);
    free(abs_path);
    return 0;
}

ErrorCode cmd_shell(CMDArgs *args) {
    ErrorCode err = shell_init(args->file, args->part);
    if (err != ERR_OK) {
        return err;
    }

    svde_out("FAT32 Shell. Type 'help' for commands.\n");

    char line[MAX_CMD_LINE];
    while (1) {
        svde_out("");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        line[strcspn(line, "\n")] = '\0';

        if (line[0] == '\0') {
            continue;
        }

        char *argv[MAX_ARGS];
        int argc = parse_line(line, argv, MAX_ARGS);
        if (argc < 0) {
            svde_err( "Error: invalid command line (too many args or unmatched quotes).\n");
            continue;
        }
        if (argc == 0) {
            continue;
        }

        const char *cmd = argv[0];
        if (strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
            print_help();
        } else if (strcmp(cmd, "ls") == 0) {
            cmd_ls(argc, argv);
        } else if (strcmp(cmd, "cd") == 0) {
            cmd_cd(argc, argv);
        } else if (strcmp(cmd, "pwd") == 0) {
            cmd_pwd(argc, argv);
        } else if (strcmp(cmd, "mkdir") == 0) {
            cmd_mkdir(argc, argv);
        } else if (strcmp(cmd, "rmdir") == 0) {
            cmd_rmdir(argc, argv);
        } else if (strcmp(cmd, "rm") == 0) {
            cmd_rm(argc, argv);
        } else if (strcmp(cmd, "copy") == 0) {
            cmd_copy(argc, argv);
        } else if (strcmp(cmd, "tree") == 0) {
            cmd_tree(argc, argv);
        } else if (strcmp(cmd, "reserve") == 0) {
            cmd_reserve(argc, argv);
        } else {
            svde_err( "Unknown command: %s\n", cmd);
        }
    }

    shell_shutdown();
    return ERR_OK;
}