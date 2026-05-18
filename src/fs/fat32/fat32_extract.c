#include "fat32_extract.h"
#include "fat32.h"
#include "fat32_util.h"
#include "output.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

ErrorCode fat32_extract_file(Disk *disk, uint64_t start_lba,
                             const char *src_path, const char *dest_path,
                             bool overwrite) {
    if (!disk || !src_path || !dest_path) return ERR_INVALID_ARGUMENT;

    // 1. Получить информацию о томе
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    // 2. Разобрать src_path на родительский каталог и имя файла
    char *path_copy = my_strdup(src_path);
    if (!path_copy) {
        fat32_free_cache(&info);
        return ERR_OUT_OF_MEMORY;
    }

    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        free(path_copy);
        fat32_free_cache(&info);
        return ERR_INVALID_ARGUMENT;
    }
    *last_slash = '\0';
    char *parent_path = path_copy;
    const char *file_name = last_slash + 1;
    if (parent_path[0] == '\0') parent_path = "/";

    // 3. Найти родительский каталог
    uint32_t parent_cluster;
    err = fat32_find_dir(disk, &info, parent_path, &parent_cluster);
    if (err != ERR_OK) {
        free(path_copy);
        fat32_free_cache(&info);
        return err;
    }

    // 4. Найти файл в родительском каталоге
    uint8_t *dir_buffer = NULL;
    uint32_t entries_count = 0;
    err = fat32_read_dir(disk, &info, parent_cluster, &dir_buffer, &entries_count);
    if (err != ERR_OK) {
        free(path_copy);
        fat32_free_cache(&info);
        return err;
    }

    uint32_t file_cluster = 0;
    uint32_t file_size = 0;
    bool found = false;

    for (uint32_t i = 0; i < entries_count; i++) {
        const uint8_t *entry = dir_buffer + i * 32;
        if (entry[0] == 0x00) break;
        if (entry[0] == 0xE5) continue;
        if (entry[11] == FAT32_ATTR_LFN) continue;

        const Fat32ShortEntry *se = (const Fat32ShortEntry*)entry;

        // Проверяем, что это файл, а не каталог
        if (se->attr & FAT32_ATTR_DIRECTORY) {
            // Проверим имя — может, это искомый каталог (ошибка)
            char *long_name = extract_lfn_name(dir_buffer, i);
            char sfn_name[13];
            const char *cmp_name;
            if (long_name) {
                cmp_name = long_name;
            } else {
                sfn_to_display_name(se->name, sfn_name, sizeof(sfn_name));
                cmp_name = sfn_name;
            }

            if (strcasecmp_ascii(cmp_name, file_name) == 0) {
                free(long_name);
                free(dir_buffer);
                free(path_copy);
                fat32_free_cache(&info);
                return ERR_INVALID_ARGUMENT; // это каталог, не файл
            }
            free(long_name);
            continue;
        }

        char *long_name = extract_lfn_name(dir_buffer, i);
        char sfn_name[13];
        const char *cmp_name;
        if (long_name) {
            cmp_name = long_name;
        } else {
            sfn_to_display_name(se->name, sfn_name, sizeof(sfn_name));
            cmp_name = sfn_name;
        }

        if (strcasecmp_ascii(cmp_name, file_name) == 0) {
            file_cluster = ((uint32_t)se->first_cluster_hi << 16) | se->first_cluster_lo;
            file_size = se->file_size;
            found = true;
            free(long_name);
            break;
        }
        free(long_name);
    }
    free(dir_buffer);
    free(path_copy);

    if (!found) {
        fat32_free_cache(&info);
        return ERR_NOT_FOUND;
    }

    // 5. Проверить, существует ли dest_path
    FILE *host_file = fopen(dest_path, "rb");
    if (host_file) {
        fclose(host_file);
        if (!overwrite) {
            fat32_free_cache(&info);
            return ERR_ALREADY_EXISTS;
        }
    }

    // 6. Открыть хост-файл для записи
    host_file = fopen(dest_path, "wb");
    if (!host_file) {
        fat32_free_cache(&info);
        return ERR_DISK_OPEN;
    }

    // 7. Прочитать цепочку кластеров и записать на хост
    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *buffer = (uint8_t*)malloc(cluster_size);
    if (!buffer) {
        fclose(host_file);
        fat32_free_cache(&info);
        return ERR_OUT_OF_MEMORY;
    }

    uint32_t remaining = file_size;
    uint32_t current_cluster = file_cluster;

    while (remaining > 0 && current_cluster >= 2 && current_cluster <= info.total_clusters + 1) {
        err = fat32_read_cluster(disk, &info, current_cluster, buffer);
        if (err != ERR_OK) {
            free(buffer);
            fclose(host_file);
            fat32_free_cache(&info);
            return err;
        }

        size_t to_write = (remaining < cluster_size) ? remaining : cluster_size;
        size_t written = fwrite(buffer, 1, to_write, host_file);
        if (written != to_write) {
            free(buffer);
            fclose(host_file);
            fat32_free_cache(&info);
            return ERR_DISK_WRITE;
        }

        remaining -= (uint32_t)to_write;

        if (remaining > 0) {
            err = fat32_get_next_cluster(disk, &info, current_cluster, &current_cluster);
            if (err != ERR_OK) {
                free(buffer);
                fclose(host_file);
                fat32_free_cache(&info);
                return err;
            }
        }
    }

    free(buffer);
    fclose(host_file);
    fat32_free_cache(&info);
    return ERR_OK;
}