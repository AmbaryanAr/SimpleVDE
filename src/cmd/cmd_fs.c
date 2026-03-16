#include "disk.h"
#include "cmd_fs.h"
#include "partition.h"
#include "fat32.h"
#include "utils.h"
#include "error_codes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Структура для хранения информации о дочернем элементе
typedef struct {
    uint32_t cluster;
    char *name;
    int is_dir;
    uint32_t size;
} child_entry_t;

// Вспомогательная функция для вывода ошибки с учётом кода
static void print_error(ErrorCode err, const char *context) {
    fprintf(stderr, "Error: %s - %s\n", context, error_code_to_string(err));
}

// Вспомогательная функция для преобразования SFN в читаемое имя (копия из других модулей)
static void sfn_to_display_name(const uint8_t sfn[11], char *out, size_t out_size) {
    if (out_size < 13) return;
    int pos = 0;
    for (int i = 0; i < 8 && sfn[i] != ' '; i++) {
        out[pos++] = (char)sfn[i];
    }
    if (sfn[8] != ' ') {
        out[pos++] = '.';
        for (int i = 0; i < 3 && sfn[8 + i] != ' '; i++) {
            out[pos++] = (char)sfn[8 + i];
        }
    }
    out[pos] = '\0';
}

// Вспомогательная функция для извлечения длинного имени (копия из fat32_ls.c)
static char* extract_lfn_name(const uint8_t *dir_buffer, uint32_t sfn_index) {
    uint32_t lfn_start = sfn_index;
    while (lfn_start > 0) {
        const uint8_t *e = dir_buffer + (lfn_start - 1) * 32;
        if (e[11] != FAT32_ATTR_LFN) break;
        if (e[0] & 0x40) {
            lfn_start--;
            break;
        }
        lfn_start--;
    }
    if (lfn_start == sfn_index) return NULL;

    uint32_t first_lfn = lfn_start;
    while (first_lfn > 0) {
        const uint8_t *e = dir_buffer + (first_lfn - 1) * 32;
        if (e[11] != FAT32_ATTR_LFN) break;
        first_lfn--;
    }

    uint16_t utf16[256];
    size_t pos = 0;
    for (uint32_t i = first_lfn; i < sfn_index; i++) {
        const Fat32LongEntry *lfn = (const Fat32LongEntry*)(dir_buffer + i * 32);
        if (lfn->attr != FAT32_ATTR_LFN) return NULL;
        for (int j = 0; j < 5; j++) {
            if (lfn->name1[j] != 0xFFFF) utf16[pos++] = lfn->name1[j];
        }
        for (int j = 0; j < 6; j++) {
            if (lfn->name2[j] != 0xFFFF) utf16[pos++] = lfn->name2[j];
        }
        for (int j = 0; j < 2; j++) {
            if (lfn->name3[j] != 0xFFFF) utf16[pos++] = lfn->name3[j];
        }
    }

    char *name = (char*)malloc(pos + 1);
    if (!name) return NULL;
    for (size_t i = 0; i < pos; i++) {
        name[i] = (char)(utf16[i] & 0x7F);
    }
    name[pos] = '\0';
    return name;
}

// Рекурсивная печать дерева с красивыми линиями
static void print_tree(Disk *disk, const Fat32Info *info, uint32_t cluster,
                       const char *name, const char *prefix, int is_last) {
    // Печатаем текущий узел с префиксом
    printf("%s%s%s\n", prefix, (is_last ? "'-- " : "|-- "), name);

    // Читаем содержимое каталога
    uint8_t *buffer = NULL;
    uint32_t entries = 0;
    if (fat32_read_dir(disk, info, cluster, &buffer, &entries) != ERR_OK) {
        return;
    }

    // Собираем все дочерние элементы (кроме "." и "..")
    child_entry_t *children = NULL;
    uint32_t child_count = 0;

    for (uint32_t i = 0; i < entries; i++) {
        const uint8_t *entry = buffer + i * 32;
        if (entry[0] == 0x00) break;
        if (entry[0] == 0xE5) continue;
        if (entry[11] == FAT32_ATTR_LFN) continue;

        Fat32ShortEntry *se = (Fat32ShortEntry*)entry;
        char display_name[256];

        // Пытаемся получить длинное имя
        char *long_name = extract_lfn_name(buffer, i);
        if (long_name) {
            strcpy(display_name, long_name);
            free(long_name);
        } else {
            sfn_to_display_name(se->name, display_name, sizeof(display_name));
        }

        // Пропускаем "." и ".."
        if (strcmp(display_name, ".") == 0 || strcmp(display_name, "..") == 0)
            continue;

        // Добавляем элемент в массив
        children = realloc(children, (child_count + 1) * sizeof(child_entry_t));
        children[child_count].name = strdup(display_name);
        children[child_count].cluster = ((uint32_t)se->first_cluster_hi << 16) | se->first_cluster_lo;
        children[child_count].is_dir = (se->attr & FAT32_ATTR_DIRECTORY) != 0;
        children[child_count].size = se->file_size;
        child_count++;
    }

    free(buffer);

    // Формируем новый префикс для детей
    char new_prefix[512];
    snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix, (is_last ? "    " : "|   "));

    // Выводим всех детей
    for (uint32_t i = 0; i < child_count; i++) {
        child_entry_t *child = &children[i];
        int last = (i == child_count - 1);

        if (child->is_dir) {
            // Рекурсивно вызываем для подкаталога
            print_tree(disk, info, child->cluster, child->name, new_prefix, last);
        } else {
            // Для файла просто печатаем строку с размером
            char file_info[256];
            snprintf(file_info, sizeof(file_info), "%s (%u bytes)", child->name, child->size);
            printf("%s%s%s\n", new_prefix, (last ? "'-- " : "|-- "), file_info);
        }
        free(child->name);
    }
    free(children);
}

// ==================== Вспомогательная функция для открытия и подготовки ФС ====================
static ErrorCode open_disk_and_prepare_fs(CMDArgs *args, Disk *disk, uint64_t *start_lba, Fat32Info *info) {
    ErrorCode err = disk_open(args->file, disk);
    if (err != ERR_OK) {
        print_error(err, "cannot open disk image");
        return err;
    }

    PartitionTableType table_type;
    err = partition_detect_type(disk, &table_type);
    if (err != ERR_OK || table_type == PT_UNKNOWN) {
        disk_close(disk);
        print_error(err != ERR_OK ? err : ERR_INVALID_SIGNATURE, "invalid partition table");
        return err != ERR_OK ? err : ERR_INVALID_SIGNATURE;
    }

    int part_index = parse_part_index(args->part);
    if (part_index < 0) {
        disk_close(disk);
        fprintf(stderr, "Error: invalid partition number '%s'.\n", args->part);
        return ERR_INVALID_ARGUMENT;
    }

    uint64_t size_sectors; // не используется
    err = partition_get_info(disk, part_index, start_lba, &size_sectors);
    if (err != ERR_OK) {
        disk_close(disk);
        if (err == ERR_NOT_FOUND)
            fprintf(stderr, "Error: partition %d does not exist.\n", part_index + 1);
        else
            print_error(err, "cannot get partition info");
        return err;
    }

    err = fat32_get_info(disk, *start_lba, info);
    if (err != ERR_OK) {
        disk_close(disk);
        fprintf(stderr, "Error: partition %d is not a valid FAT32 filesystem.\n", part_index + 1);
        return ERR_INVALID_SIGNATURE;
    }

    return ERR_OK;
}

// ==================== Команды ====================

ErrorCode cmd_fs_ls(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    const char *path = args->path ? args->path : "/";
    err = fat32_list_dir(&disk, start_lba, path);
    if (err != ERR_OK) print_error(err, "cannot list directory");

    disk_close(&disk);
    return err;
}

ErrorCode cmd_fs_mkdir(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_create_dir(&disk, start_lba, args->path);
    if (err != ERR_OK) {
        print_error(err, "cannot create directory");
        disk_close(&disk);
        return err;
    }

    printf("Directory '%s' created successfully.\n", args->path);
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_copy(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_copy_file(&disk, start_lba, args->src, args->dest);
    if (err != ERR_OK) {
        print_error(err, "cannot copy file");
        if (err == ERR_INVALID_ARGUMENT)
            fprintf(stderr, "Note: Destination path must be absolute, e.g., /dir/file.txt\n");
        else if (err == ERR_NOT_FOUND)
            fprintf(stderr, "Note: Check that source file exists and is readable.\n");
        else if (err == ERR_ALREADY_EXISTS)
            fprintf(stderr, "Note: A file with the same name already exists.\n");
        disk_close(&disk);
        return err;
    }

    printf("File '%s' copied to '%s' successfully.\n", args->src, args->dest);
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_rm(CMDArgs *args) {
    if (strcmp(args->path, "/") == 0) {
        fprintf(stderr, "Error: cannot delete root directory.\n");
        return ERR_INVALID_ARGUMENT;
    }

    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_delete_file(&disk, start_lba, args->path);
    if (err != ERR_OK) {
        print_error(err, "cannot delete file");
        if (err == ERR_INVALID_ARGUMENT)
            fprintf(stderr, "Note: Path must be absolute and point to a file, not a directory.\n");
        disk_close(&disk);
        return err;
    }

    printf("File '%s' deleted successfully.\n", args->path);
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_rmdir(CMDArgs *args) {
    if (strcmp(args->path, "/") == 0) {
        fprintf(stderr, "Error: cannot remove root directory.\n");
        return ERR_INVALID_ARGUMENT;
    }

    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_remove_dir(&disk, start_lba, args->path);
    if (err != ERR_OK) {
        print_error(err, "cannot remove directory");
        if (err == ERR_INVALID_ARGUMENT)
            fprintf(stderr, "Note: Path must be absolute and point to a directory.\n");
        else if (err == ERR_DIR_NOT_EMPTY)
            fprintf(stderr, "Note: Directory is not empty.\n");
        disk_close(&disk);
        return err;
    }

    printf("Directory '%s' removed successfully.\n", args->path);
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_tree(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t start_cluster = info.root_cluster;
    const char *start_name = "/";
    if (args->path) {
        err = fat32_find_dir(&disk, &info, args->path, &start_cluster);
        if (err != ERR_OK) {
            disk_close(&disk);
            print_error(err, "cannot find path");
            return err;
        }
        start_name = args->path;
    }

    printf("Directory tree of %s:\n", start_name);
    print_tree(&disk, &info, start_cluster, start_name, "", 1);

    disk_close(&disk);
    return ERR_OK;
}

// ==================== Команды реестра (reserve) ====================

ErrorCode cmd_fs_reserve_init(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_reserve_init(&disk, start_lba);
    if (err != ERR_OK) {
        print_error(err, "cannot initialize reserve cluster");
        disk_close(&disk);
        return err;
    }

    printf("Reserve cluster initialized successfully.\n");
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_reserve_ls(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_reserve_list(&disk, start_lba);
    disk_close(&disk);
    return err;
}

ErrorCode cmd_fs_reserve_add(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_reserve_add(&disk, start_lba, args->path);
    if (err != ERR_OK) {
        print_error(err, "cannot add entry to reserve");
        disk_close(&disk);
        return err;
    }

    printf("Entry added to reserve successfully.\n");
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_reserve_rm(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_reserve_remove(&disk, start_lba, args->name);
    if (err != ERR_OK) {
        print_error(err, "cannot remove entry from reserve");
        disk_close(&disk);
        return err;
    }

    printf("Entry removed from reserve successfully.\n");
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_reserve_clear(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_reserve_clear(&disk, start_lba);
    if (err != ERR_OK) {
        print_error(err, "cannot clear reserve");
        disk_close(&disk);
        return err;
    }

    printf("Reserve cleared successfully.\n");
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_reserve_dump(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_reserve_dump(&disk, start_lba);
    disk_close(&disk);
    return err;
}

ErrorCode cmd_fs_reserve_info(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_reserve_info(&disk, start_lba);
    disk_close(&disk);
    return err;
}