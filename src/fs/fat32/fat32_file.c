#include "fat32.h"
#include "fat32_util.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// Вспомогательная функция для поиска индекса записи в открытом каталоге по имени
static int find_entry_by_name(Disk *disk, const Fat32Info *info, Fat32Directory *dir,
                              const char *name, uint32_t *out_cluster_idx, uint32_t *out_entry_idx) {
    if (!disk || !info || !dir || !name || !out_cluster_idx || !out_entry_idx) return -1;

    // Читаем весь каталог в линейный буфер
    uint8_t *buffer = NULL;
    uint32_t entries_count = 0;
    ErrorCode err = fat32_read_dir(disk, info, dir->clusters[0].cluster_num, &buffer, &entries_count);
    if (err != ERR_OK) return -1;

    uint32_t entries_per_cluster = (info->sectors_per_cluster * info->bytes_per_sector) / 32;
    int result = -1;

    for (uint32_t i = 0; i < entries_count; i++) {
        const uint8_t *entry = buffer + i * 32;
        if (entry[0] == 0x00) break;
        if (entry[0] == 0xE5) continue;
        if (entry[11] == FAT32_ATTR_LFN) continue;

        const Fat32ShortEntry *se = (const Fat32ShortEntry*)entry;

        char *long_name = extract_lfn_name(buffer, i);
        char sfn_name[13];
        const char *cmp_name;

        if (long_name) {
            cmp_name = long_name;
        } else {
            sfn_to_display_name(se->name, sfn_name, sizeof(sfn_name));
            cmp_name = sfn_name;
        }

        if (strcasecmp_ascii(cmp_name, name) == 0) {
            *out_cluster_idx = i / entries_per_cluster;
            *out_entry_idx = i % entries_per_cluster;
            result = 0;
            free(long_name);
            break;
        }
        free(long_name);
    }

    free(buffer);
    return result;
}

// Пометка записи и всех связанных LFN как удалённых
static ErrorCode mark_entry_deleted(Disk *disk, const Fat32Info *info, Fat32Directory *dir,
                                    uint32_t cluster_idx, uint32_t entry_idx) {
    if (!disk || !info || !dir) return ERR_INVALID_ARGUMENT;
    if (cluster_idx >= dir->cluster_count) return ERR_INVALID_ARGUMENT;

    uint32_t entries_per_cluster = (info->sectors_per_cluster * info->bytes_per_sector) / 32;
    uint32_t global_entry = 0;
    // Вычислим глобальный индекс записи (для навигации по LFN назад)
    for (uint32_t ci = 0; ci < cluster_idx; ci++) {
        global_entry += entries_per_cluster;
    }
    global_entry += entry_idx;

    // Ищем начало LFN-набора для этой записи
    uint32_t lfn_start = global_entry;
    while (lfn_start > 0) {
        uint32_t prev_ci = (lfn_start - 1) / entries_per_cluster;
        uint32_t prev_ei = (lfn_start - 1) % entries_per_cluster;
        const uint8_t *prev = dir->clusters[prev_ci].data + prev_ei * 32;
        if (prev[11] != FAT32_ATTR_LFN) break;
        lfn_start--;
    }

    // Помечаем все LFN-записи от lfn_start до global_entry-1
    for (uint32_t g = lfn_start; g < global_entry; g++) {
        uint32_t ci = g / entries_per_cluster;
        uint32_t ei = g % entries_per_cluster;
        uint8_t *entry = dir->clusters[ci].data + ei * 32;
        entry[0] = 0xE5; // помечаем как удалённую
    }

    // Помечаем саму SFN-запись
    uint8_t *sfn_entry = dir->clusters[cluster_idx].data + entry_idx * 32;
    sfn_entry[0] = 0xE5;

    // Записываем изменённые кластеры на диск (те, которые были изменены)
    // Для простоты запишем все кластеры, которые мы трогали (от lfn_start до cluster_idx)
    uint32_t first_ci = lfn_start / entries_per_cluster;
    uint32_t last_ci = cluster_idx;
    for (uint32_t ci = first_ci; ci <= last_ci; ci++) {
        ErrorCode err = fat32_write_cluster(disk, info, dir->clusters[ci].cluster_num, dir->clusters[ci].data);
        if (err != ERR_OK) return err;
    }

    return ERR_OK;
}

ErrorCode fat32_copy_file(Disk *disk, uint64_t start_lba, const char *host_path, const char *dest_path) {
    if (!disk || !host_path || !dest_path) return ERR_INVALID_ARGUMENT;

    // 1. Получаем информацию о томе
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    // 2. Разбираем путь назначения на родительский каталог и имя файла
    char *dest_copy = my_strdup(dest_path);
    char *last_slash = strrchr(dest_copy, '/');
	if (!last_slash) {
		free(dest_copy);
		return ERR_INVALID_ARGUMENT;
	}
	*last_slash = '\0';
	char *parent_path = dest_copy;
	const char *file_name = last_slash + 1;
	if (parent_path[0] == '\0') {
		parent_path = "/";
	}

    // 3. Находим родительский каталог и открываем его
    uint32_t parent_cluster;
    err = fat32_find_dir(disk, &info, parent_path, &parent_cluster);
    if (err != ERR_OK) {
        free(dest_copy);
        return err;
    }

    Fat32Directory parent_dir;
    err = fat32_dir_open(disk, &info, parent_cluster, &parent_dir);
    if (err != ERR_OK) {
        free(dest_copy);
        return err;
    }

    // 4. Проверяем, нет ли уже файла с таким именем (просканируем родительский каталог)
    uint8_t *parent_buffer = NULL;
    uint32_t parent_entries = 0;
    err = fat32_read_dir(disk, &info, parent_cluster, &parent_buffer, &parent_entries);
    if (err != ERR_OK) {
        fat32_dir_close(&parent_dir);
        free(dest_copy);
        return err;
    }

    bool already_exists = false;
    for (uint32_t i = 0; i < parent_entries; i++) {
        const uint8_t *entry = parent_buffer + i * 32;
        if (entry[0] == 0x00) break;
        if (entry[0] == 0xE5) continue;
        if (entry[11] == FAT32_ATTR_LFN) continue;

        char *long_name = extract_lfn_name(parent_buffer, i);
        const char *cmp_name = long_name ? long_name : (const char*)entry;
        char sfn_name[13];
        if (!long_name) {
            sfn_to_display_name(entry, sfn_name, sizeof(sfn_name));
            cmp_name = sfn_name;
        }

        if (strcasecmp_ascii(cmp_name, file_name) == 0) {
            already_exists = true;
            free(long_name);
            break;
        }
        free(long_name);
    }
    free(parent_buffer);

    if (already_exists) {
        fat32_dir_close(&parent_dir);
        free(dest_copy);
        return ERR_ALREADY_EXISTS;
    }

    // 5. Открываем хост-файл и узнаём размер
    FILE *host_file = fopen(host_path, "rb");
    if (!host_file) {
        fat32_dir_close(&parent_dir);
        free(dest_copy);
        return ERR_DISK_OPEN;
    }

    fseek(host_file, 0, SEEK_END);
    long host_size = ftell(host_file);
    fseek(host_file, 0, SEEK_SET);
    if (host_size < 0) {
        fclose(host_file);
        fat32_dir_close(&parent_dir);
        free(dest_copy);
        return ERR_DISK_READ;
    }
    uint32_t file_size = (uint32_t)host_size;

    // 6. Рассчитываем необходимое количество кластеров
    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint32_t needed_clusters = (file_size + cluster_size - 1) / cluster_size;

    // 7. Выделяем цепочку кластеров
    uint32_t first_cluster = 0;
    uint32_t prev_cluster = 0;
    uint8_t *cluster_buf = (uint8_t*)malloc(cluster_size);
    if (!cluster_buf) {
        fclose(host_file);
        fat32_dir_close(&parent_dir);
        free(dest_copy);
        return ERR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < needed_clusters; i++) {
        uint32_t new_cluster = fat32_alloc_cluster(disk, &info);
        if (new_cluster == 0) {
            if (first_cluster != 0) {
                fat32_free_cluster_chain(disk, &info, first_cluster);
            }
            free(cluster_buf);
            fclose(host_file);
            fat32_dir_close(&parent_dir);
            free(dest_copy);
            return ERR_NO_FREE_SPACE;
        }

        if (i == 0) {
            first_cluster = new_cluster;
        } else {
            err = fat32_set_fat_entry(disk, &info, prev_cluster, new_cluster);
            if (err != ERR_OK) {
                fat32_free_cluster_chain(disk, &info, first_cluster);
                free(cluster_buf);
                fclose(host_file);
                fat32_dir_close(&parent_dir);
                free(dest_copy);
                return err;
            }
        }
        prev_cluster = new_cluster;
    }
    if (needed_clusters > 0) {
        err = fat32_set_fat_entry(disk, &info, prev_cluster, FAT32_CLUSTER_EOC);
        if (err != ERR_OK) {
            fat32_free_cluster_chain(disk, &info, first_cluster);
            free(cluster_buf);
            fclose(host_file);
            fat32_dir_close(&parent_dir);
            free(dest_copy);
            return err;
        }
    }

    // 8. Записываем данные в кластеры
    uint32_t cluster = first_cluster;
    uint32_t remaining = file_size;
    while (cluster != 0 && remaining > 0) {
        size_t to_read = (remaining < cluster_size) ? remaining : cluster_size;
        size_t read = fread(cluster_buf, 1, to_read, host_file);
        if (read != to_read) {
            fat32_free_cluster_chain(disk, &info, first_cluster);
            free(cluster_buf);
            fclose(host_file);
            fat32_dir_close(&parent_dir);
            free(dest_copy);
            return ERR_DISK_READ;
        }
        err = fat32_write_cluster(disk, &info, cluster, cluster_buf);
        if (err != ERR_OK) {
            fat32_free_cluster_chain(disk, &info, first_cluster);
            free(cluster_buf);
            fclose(host_file);
            fat32_dir_close(&parent_dir);
            free(dest_copy);
            return err;
        }
        remaining -= (uint32_t)to_read;
        uint32_t next;
        err = fat32_get_next_cluster(disk, &info, cluster, &next);
        if (err != ERR_OK) {
            fat32_free_cluster_chain(disk, &info, first_cluster);
            free(cluster_buf);
            fclose(host_file);
            fat32_dir_close(&parent_dir);
            free(dest_copy);
            return err;
        }
        cluster = next;
    }
    free(cluster_buf);
    fclose(host_file);

    // 9. Подготавливаем запись для нового файла
    uint8_t *parent_dir_buffer = NULL;
    uint32_t parent_dir_entries = 0;
    err = fat32_read_dir(disk, &info, parent_cluster, &parent_dir_buffer, &parent_dir_entries);
    if (err != ERR_OK) {
        fat32_free_cluster_chain(disk, &info, first_cluster);
        fat32_dir_close(&parent_dir);
        free(dest_copy);
        return err;
    }

    fat32_entry_info_t entry_info;
    ErrorCode ferr = fat32_prepare_entry(file_name, FAT32_ATTR_ARCHIVE,
                                              parent_dir_buffer, parent_dir_entries,
                                              &entry_info);
    free(parent_dir_buffer);
    if (ferr != ERR_OK) {
        fat32_free_cluster_chain(disk, &info, first_cluster);
        fat32_dir_close(&parent_dir);
        free(dest_copy);
        return (ErrorCode)ferr;
    }

    // 10. Заполняем первый кластер, размер и текущее время
    entry_info.first_cluster = first_cluster;
    entry_info.file_size = file_size;
    fat32_set_current_time(&entry_info);

    // 11. Ищем свободное место в родительском каталоге
    uint32_t cluster_idx, entry_off;
    bool found = fat32_find_free_entries(&parent_dir, entry_info.total_entries,
                                          &cluster_idx, &entry_off);
    if (!found) {
        err = fat32_append_cluster_to_dir(disk, &info, &parent_dir);
        if (err != ERR_OK) {
            fat32_free_cluster_chain(disk, &info, first_cluster);
            fat32_free_entry_info(&entry_info);
            fat32_dir_close(&parent_dir);
            free(dest_copy);
            return err;
        }
        found = fat32_find_free_entries(&parent_dir, entry_info.total_entries,
                                         &cluster_idx, &entry_off);
        if (!found) {
            fat32_free_cluster_chain(disk, &info, first_cluster);
            fat32_free_entry_info(&entry_info);
            fat32_dir_close(&parent_dir);
            free(dest_copy);
            return ERR_NO_FREE_SPACE;
        }
    }

    // 12. Записываем записи в родительский каталог
    err = fat32_write_entries_to_dir(disk, &info, &parent_dir,
                                      cluster_idx, entry_off, &entry_info);
    if (err != ERR_OK) {
        fat32_free_cluster_chain(disk, &info, first_cluster);
        fat32_free_entry_info(&entry_info);
        fat32_dir_close(&parent_dir);
        free(dest_copy);
        return err;
    }

    fat32_free_entry_info(&entry_info);
    fat32_dir_close(&parent_dir);
    free(dest_copy);
    return ERR_OK;
}

ErrorCode fat32_delete_file(Disk *disk, uint64_t start_lba, const char *path) {
    if (!disk || !path) return ERR_INVALID_ARGUMENT;

    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    char *path_copy = my_strdup(path);
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        free(path_copy);
        return ERR_INVALID_ARGUMENT;
    }
    *last_slash = '\0';
    char *parent_path = path_copy;
    const char *file_name = last_slash + 1;
    if (parent_path[0] == '\0') {
        parent_path = "/";
    }

    uint32_t parent_cluster;
    err = fat32_find_dir(disk, &info, parent_path, &parent_cluster);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    Fat32Directory parent_dir;
    err = fat32_dir_open(disk, &info, parent_cluster, &parent_dir);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    uint32_t cluster_idx, entry_idx;
    if (find_entry_by_name(disk, &info, &parent_dir, file_name, &cluster_idx, &entry_idx) != 0) {
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return ERR_NOT_FOUND;
    }

    uint8_t *entry = parent_dir.clusters[cluster_idx].data + entry_idx * 32;
    Fat32ShortEntry *se = (Fat32ShortEntry*)entry;
    if (se->attr & FAT32_ATTR_DIRECTORY) {
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return ERR_INVALID_ARGUMENT;
    }

    uint32_t first_cluster = ((uint32_t)se->first_cluster_hi << 16) | se->first_cluster_lo;
    if (first_cluster != 0) {
        err = fat32_free_cluster_chain(disk, &info, first_cluster);
        if (err != ERR_OK) {
            fat32_dir_close(&parent_dir);
            free(path_copy);
            return err;
        }
    }

    err = mark_entry_deleted(disk, &info, &parent_dir, cluster_idx, entry_idx);
    if (err != ERR_OK) {
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return err;
    }

    fat32_dir_close(&parent_dir);
    free(path_copy);
    return ERR_OK;
}