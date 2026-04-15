#include "fat32.h"
#include "fat32_util.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

ErrorCode fat32_read_dir(Disk *disk, const Fat32Info *info, uint32_t start_cluster,
                         uint8_t **buffer, uint32_t *entries_count) {
    if (!disk || !info || !buffer || !entries_count) return ERR_INVALID_ARGUMENT;
    if (start_cluster < 2 || start_cluster > info->total_clusters + 1)
        return ERR_INVALID_ARGUMENT;

    *buffer = NULL;
    *entries_count = 0;

    uint32_t cluster_size = info->sectors_per_cluster * info->bytes_per_sector;
    uint32_t current_cluster = start_cluster;
    size_t total_size = 0;
    uint8_t *tmp_buf = NULL;

    while (1) {
        uint8_t *cluster_data = (uint8_t*)malloc(cluster_size);
        if (!cluster_data) {
            free(tmp_buf);
            return ERR_OUT_OF_MEMORY;
        }

        ErrorCode err = fat32_read_cluster(disk, info, current_cluster, cluster_data);
        if (err != ERR_OK) {
            free(cluster_data);
            free(tmp_buf);
            return err;
        }

        uint8_t *new_buf = (uint8_t*)realloc(tmp_buf, total_size + cluster_size);
        if (!new_buf) {
            free(cluster_data);
            free(tmp_buf);
            return ERR_OUT_OF_MEMORY;
        }
        tmp_buf = new_buf;
        memcpy(tmp_buf + total_size, cluster_data, cluster_size);
        total_size += cluster_size;
        free(cluster_data);

        uint32_t next_cluster;
        err = fat32_get_next_cluster(disk, info, current_cluster, &next_cluster);
        if (err != ERR_OK) {
            free(tmp_buf);
            return err;
        }

        if (next_cluster >= 0x0FFFFFF8) break;
        current_cluster = next_cluster;
    }

    *buffer = tmp_buf;
    *entries_count = (uint32_t)(total_size / 32);
    return ERR_OK;
}

ErrorCode fat32_find_dir(Disk *disk, const Fat32Info *info, const char *path, uint32_t *dir_cluster) {
    if (!disk || !info || !path || !dir_cluster) return ERR_INVALID_ARGUMENT;
    if (*path != '/') return ERR_INVALID_ARGUMENT;

    uint32_t current_cluster = info->root_cluster;
    if (path[1] == '\0') {
        *dir_cluster = current_cluster;
        return ERR_OK;
    }

    char *path_copy = my_strdup(path);
    if (!path_copy) return ERR_OUT_OF_MEMORY;

    char *token = strtok(path_copy + 1, "/");
    ErrorCode err = ERR_OK;

    while (token != NULL) {
        uint8_t *dir_buffer = NULL;
        uint32_t entries_count = 0;
        err = fat32_read_dir(disk, info, current_cluster, &dir_buffer, &entries_count);
        if (err != ERR_OK) break;

        uint32_t found_cluster = 0;
        uint32_t i = 0;
        while (i < entries_count) {
            const uint8_t *entry = dir_buffer + i * 32;
            if (entry[0] == 0x00) break;
            if (entry[0] == 0xE5) { i++; continue; }

            if (entry[11] == FAT32_ATTR_LFN) {
                i++;
                continue;
            }

            Fat32ShortEntry *se = (Fat32ShortEntry*)entry;

            if (!(se->attr & FAT32_ATTR_DIRECTORY)) {
                i++;
                continue;
            }

            char *long_name = extract_lfn_name(dir_buffer, i);
            char sfn_name[13];

            if (long_name) {
                if (strcasecmp_ascii(long_name, token) == 0) {
                    found_cluster = ((uint32_t)se->first_cluster_hi << 16) | se->first_cluster_lo;
                    free(long_name);
                    break;
                }
                free(long_name);
            } else {
                if (strcasecmp_ascii(sfn_name, token) == 0) {
                    found_cluster = ((uint32_t)se->first_cluster_hi << 16) | se->first_cluster_lo;
                    break;
                }
            }

            i++;
        }

        free(dir_buffer);

        if (found_cluster == 0) {
            err = ERR_NOT_FOUND;
            break;
        }

        current_cluster = found_cluster;
        token = strtok(NULL, "/");
    }

    free(path_copy);
    if (err == ERR_OK) {
        *dir_cluster = current_cluster;
    }
    return err;
}

ErrorCode fat32_list_dir(Disk *disk, uint64_t start_lba, const char *path) {
    if (!disk || !path) return ERR_INVALID_ARGUMENT;

    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t dir_cluster;
    err = fat32_find_dir(disk, &info, path, &dir_cluster);
    if (err != ERR_OK) return err;

    uint8_t *dir_buffer = NULL;
    uint32_t entries_count = 0;
    err = fat32_read_dir(disk, &info, dir_cluster, &dir_buffer, &entries_count);
    if (err != ERR_OK) return err;

    printf("Directory listing of %s:\n", path);
    uint32_t count = 0;
    for (uint32_t i = 0; i < entries_count; i++) {
        const uint8_t *entry = dir_buffer + i * 32;
        if (entry[0] == 0x00) break;
        if (entry[0] == 0xE5) continue;
        if (entry[11] == FAT32_ATTR_LFN) continue;

        Fat32ShortEntry *se = (Fat32ShortEntry*)entry;
        count++;

        char *long_name = extract_lfn_name(dir_buffer, i);
        const char *display_name;
        char sfn_name[13];

        if (long_name) {
            display_name = long_name;
        } else {
            sfn_to_display_name(se->name, sfn_name, sizeof(sfn_name));
            display_name = sfn_name;
        }

        const char *type = (se->attr & FAT32_ATTR_DIRECTORY) ? "DIR" : "FILE";
        uint32_t first_cluster = ((uint32_t)se->first_cluster_hi << 16) | se->first_cluster_lo;
        printf("  %s %10u bytes  cluster %u  %s\n",
               type, se->file_size, first_cluster, display_name);

        free(long_name);
    }

    if (count == 0) {
        printf("  (empty)\n");
    }

    free(dir_buffer);
    return ERR_OK;
}