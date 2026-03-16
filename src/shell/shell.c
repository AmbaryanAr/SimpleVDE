#include "shell.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// Нормализация пути: преобразует относительный путь в абсолютный с учётом текущего каталога
static char *resolve_path(const char *cwd, const char *input) {
    char *work;
    if (input[0] == '/') {
        work = strdup(input);
    } else {
        if (strcmp(cwd, "/") == 0) {
            work = malloc(strlen(input) + 2);
            if (work) sprintf(work, "/%s", input);
        } else {
            work = malloc(strlen(cwd) + strlen(input) + 2);
            if (work) sprintf(work, "%s/%s", cwd, input);
        }
    }
    if (!work) return NULL;

    char *components[256];
    int comp_count = 0;
    char *token = strtok(work, "/");
    while (token) {
        if (strcmp(token, ".") == 0) {
            // ничего
        } else if (strcmp(token, "..") == 0) {
            if (comp_count > 0) comp_count--;
        } else {
            components[comp_count++] = token;
        }
        token = strtok(NULL, "/");
    }

    // Сборка нового пути
    size_t new_len = 1; // начальный '/'
    for (int i = 0; i < comp_count; i++) {
        new_len += strlen(components[i]) + 1;
    }
    char *result = malloc(new_len);
    if (!result) {
        free(work);
        return NULL;
    }
    result[0] = '/';
    size_t pos = 1;
    for (int i = 0; i < comp_count; i++) {
        if (i > 0) result[pos++] = '/';
        strcpy(result + pos, components[i]);
        pos += strlen(components[i]);
    }
    if (comp_count == 0) {
        result[1] = '\0'; // путь стал корнем
    } else {
        result[pos] = '\0';
    }
    free(work);
    return result;
}

// Разбор командной строки (до двух аргументов)
static int parse_line(char *line, char **cmd, char **arg1, char **arg2) {
    *cmd = *arg1 = *arg2 = NULL;
    char *p = line;

    // Пропустить начальные пробелы
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') return 0;

    // Команда (без кавычек, т.к. команды не содержат пробелов)
    *cmd = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = '\0';

    // Пропустить пробелы после команды
    while (*p && isspace((unsigned char)*p)) p++;

    // Вспомогательная функция для разбора одного аргумента
    char *parse_argument(char **pos) {
        char *start = *pos;
        if (*start == '"') {
            // Аргумент в двойных кавычках
            start++; // пропускаем открывающую кавычку
            char *end = strchr(start, '"');
            if (!end) return NULL; // нет закрывающей кавычки
            *end = '\0'; // завершаем строку
            *pos = end + 1; // позиция после кавычки
            return start;
        } else {
            // Обычный аргумент до пробела
            char *end = start;
            while (*end && !isspace((unsigned char)*end)) end++;
            if (*end) *end++ = '\0';
            *pos = end;
            return start;
        }
    };

    if (*p) {
        *arg1 = parse_argument(&p);
        if (!*arg1) return 0;
        // Пропустить пробелы после первого аргумента
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p) {
            *arg2 = parse_argument(&p);
        }
    }
    return 1;
}

void run_shell(Disk *disk, uint64_t start_lba) {
    // Получаем информацию о разделе один раз при старте
    Fat32PartInfo info;
    ErrorCode err = fat32_get_part_info(disk, start_lba, &info);
    if (err != ERR_OK) {
        printf("Error: cannot read FAT32 filesystem info.\n");
        return;
    }

    char *cwd = strdup("/");
    char line[1024];

    printf("Entering interactive shell. Type 'help' for commands.\n");

    while (1) {
        printf("%s> ", cwd);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        char *cmd, *arg1, *arg2;
        if (!parse_line(line, &cmd, &arg1, &arg2)) continue;

        if (strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
            print_shell_help();
        } else if (strcmp(cmd, "pwd") == 0) {
            printf("%s\n", cwd);
        } else if (strcmp(cmd, "cd") == 0) {
            if (!arg1) {
                free(cwd);
                cwd = strdup("/");
                continue;
            }
            char *new_path = resolve_path(cwd, arg1);
            if (!new_path) {
                printf("Error: cannot resolve path\n");
                continue;
            }
            uint32_t cluster;
            ErrorCode err = fat32_find_dir(disk, &info, new_path, &cluster);
            if (err != ERR_OK) {
                printf("Error: directory '%s' not found\n", arg1);
                free(new_path);
                continue;
            }
            free(cwd);
            cwd = new_path;
        } else if (strcmp(cmd, "ls") == 0) {
            const char *target = arg1 ? arg1 : ".";
            char *full_path = resolve_path(cwd, target);
            if (!full_path) {
                printf("Error: cannot resolve path\n");
                continue;
            }
            err = fat32_list_dir(disk, start_lba, full_path);
            free(full_path);
        } else if (strcmp(cmd, "mkdir") == 0) {
            if (!arg1) {
                printf("Usage: mkdir <path>\n");
                continue;
            }
            char *full_path = resolve_path(cwd, arg1);
            if (!full_path) {
                printf("Error: cannot resolve path\n");
                continue;
            }
            err = fat32_create_dir(disk, start_lba, full_path);
            free(full_path);
        } else if (strcmp(cmd, "rmdir") == 0) {
            if (!arg1) {
                printf("Usage: rmdir <path>\n");
                continue;
            }
            char *full_path = resolve_path(cwd, arg1);
            if (!full_path) {
                printf("Error: cannot resolve path\n");
                continue;
            }
            err = fat32_remove_dir(disk, start_lba, full_path);
            free(full_path);
        } else if (strcmp(cmd, "rm") == 0) {
            if (!arg1) {
                printf("Usage: rm <path>\n");
                continue;
            }
            char *full_path = resolve_path(cwd, arg1);
            if (!full_path) {
                printf("Error: cannot resolve path\n");
                continue;
            }
            err = fat32_delete_file(disk, start_lba, full_path);
            free(full_path);
        } else if (strcmp(cmd, "copy") == 0) {
            if (!arg1 || !arg2) {
                printf("Usage: copy <host_file> <dest_path>\n");
                continue;
            }
            char *full_dest = resolve_path(cwd, arg2);
            if (!full_dest) {
                printf("Error: cannot resolve destination path\n");
                continue;
            }
            err = fat32_copy_file(disk, start_lba, arg1, full_dest);
            free(full_dest);
        } else if (strcmp(cmd, "reserve") == 0) {
			uint32_t cluster = fat32_alloc_cluster(disk, &info);
			if (cluster == 0) {
				printf("Error: no free clusters.\n");
			} else {
				ErrorCode err = fat32_write_reserved_cluster(disk, start_lba, cluster);
				if (err == ERR_OK) {
					printf("Reserved cluster %u written to BPB reserved area.\n", cluster);
				} else {
					printf("Error writing to BPB (code %d).\n", err);
				}
			}
		} else {
            printf("Unknown command: %s\n", cmd);
        }
    }

    free(cwd);
    printf("Exiting shell.\n");
}