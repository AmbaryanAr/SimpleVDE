#include "fat32.h"
#include "fat32_util.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Размер записи каталога в байтах
#define DIR_ENTRY_SIZE 32

// ==================== Вспомогательные функции ====================

// Проверяет, свободна ли запись каталога (первый байт 0x00 или 0xE5)
static bool is_entry_free(const uint8_t *entry) {
    return entry[0] == 0x00 || entry[0] == 0xE5;
}

// Ищет запись в открытом каталоге по имени (с учётом LFN). Возвращает индекс кластера и записи.
// При отсутствии возвращает -1.
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

        // Получаем длинное имя, если есть
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
            // Вычисляем индекс кластера и смещение по глобальному индексу i
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

// Помечает запись каталога и все связанные с ней LFN как удалённые (байт 0xE5)
static ErrorCode mark_entry_deleted_in_dir(Disk *disk, const Fat32Info *info,
                                           Fat32Directory *dir, uint32_t cluster_idx,
                                           uint32_t entry_idx) {
    if (!disk || !info || !dir) return ERR_INVALID_ARGUMENT;
    if (cluster_idx >= dir->cluster_count) return ERR_INVALID_ARGUMENT;

    uint32_t entries_per_cluster = (info->sectors_per_cluster * info->bytes_per_sector) / 32;
    uint32_t global_entry = 0;
    for (uint32_t ci = 0; ci < cluster_idx; ci++) {
        global_entry += entries_per_cluster;
    }
    global_entry += entry_idx;

    // Ищем начало LFN-набора
    uint32_t lfn_start = global_entry;
    while (lfn_start > 0) {
        uint32_t prev_ci = (lfn_start - 1) / entries_per_cluster;
        uint32_t prev_ei = (lfn_start - 1) % entries_per_cluster;
        const uint8_t *prev = dir->clusters[prev_ci].data + prev_ei * 32;
        if (prev[11] != FAT32_ATTR_LFN) break;
        lfn_start--;
    }

    // Помечаем все LFN от lfn_start до global_entry-1
    for (uint32_t g = lfn_start; g < global_entry; g++) {
        uint32_t ci = g / entries_per_cluster;
        uint32_t ei = g % entries_per_cluster;
        dir->clusters[ci].data[ei * 32] = 0xE5;
    }
    // Помечаем саму SFN
    dir->clusters[cluster_idx].data[entry_idx * 32] = 0xE5;

    // Записываем изменённые кластеры на диск
    uint32_t first_ci = lfn_start / entries_per_cluster;
    uint32_t last_ci = cluster_idx;
    for (uint32_t ci = first_ci; ci <= last_ci; ci++) {
        ErrorCode err = fat32_write_cluster(disk, info, dir->clusters[ci].cluster_num, dir->clusters[ci].data);
        if (err != ERR_OK) return err;
    }
    return ERR_OK;
}

// Проверяет, пуст ли каталог (содержит только "." и "..")
static bool is_directory_empty(Disk *disk, const Fat32Info *info, uint32_t cluster) {
    Fat32Directory dir;
    ErrorCode err = fat32_dir_open(disk, info, cluster, &dir);
    if (err != ERR_OK) return false; // ошибка чтения – считаем непустым для безопасности

    uint32_t entries_per_cluster = (info->sectors_per_cluster * info->bytes_per_sector) / 32;
    uint32_t count = 0;
    uint32_t entry_index = 0;
    for (uint32_t ci = 0; ci < dir.cluster_count; ci++) {
        const uint8_t *data = dir.clusters[ci].data;
        for (uint32_t ei = 0; ei < entries_per_cluster; ei++) {
            const uint8_t *entry = data + ei * 32;
            if (entry[0] == 0x00) break; // конец
            if (entry[0] == 0xE5) continue;
            if (entry[11] == FAT32_ATTR_LFN) continue;
            // Это SFN-запись
            const Fat32ShortEntry *se = (const Fat32ShortEntry*)entry;
            char name[13];
            sfn_to_display_name(se->name, name, sizeof(name));
            // Проверяем, что это "." или ".."
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                // допустимо
            } else {
                fat32_dir_close(&dir);
                return false; // есть посторонняя запись
            }
            count++;
            entry_index++;
        }
    }
    fat32_dir_close(&dir);
    // Должно быть ровно две записи: "." и ".."
    return (count == 2);
}

// ==================== fat32_dir_open ====================
ErrorCode fat32_dir_open(Disk *disk, const Fat32Info *info, uint32_t start_cluster, Fat32Directory *dir) {
    if (!disk || !info || !dir) return ERR_INVALID_ARGUMENT;
    if (start_cluster < 2) return ERR_INVALID_ARGUMENT;

    dir->volume = (Fat32Info*)info; // (const cast, но мы не будем менять info)
    dir->clusters = NULL;
    dir->cluster_count = 0;

    uint32_t current = start_cluster;
    uint32_t cluster_size = info->sectors_per_cluster * info->bytes_per_sector;

    while (1) {
        // Выделяем память под новый элемент массива
        Fat32DirCluster *new_clusters = (Fat32DirCluster*)realloc(dir->clusters,
                                          (dir->cluster_count + 1) * sizeof(Fat32DirCluster));
        if (!new_clusters) {
            // Очищаем уже выделенное
            for (uint32_t i = 0; i < dir->cluster_count; i++) {
                free(dir->clusters[i].data);
            }
            free(dir->clusters);
            dir->clusters = NULL;
            return ERR_OUT_OF_MEMORY;
        }
        dir->clusters = new_clusters;

        Fat32DirCluster *cl = &dir->clusters[dir->cluster_count];
        cl->cluster_num = current;
        cl->data = (uint8_t*)malloc(cluster_size);
        if (!cl->data) {
            // Очистка
            for (uint32_t i = 0; i < dir->cluster_count; i++) {
                free(dir->clusters[i].data);
            }
            free(dir->clusters);
            dir->clusters = NULL;
            return ERR_OUT_OF_MEMORY;
        }

        ErrorCode err = fat32_read_cluster(disk, info, current, cl->data);
        if (err != ERR_OK) {
            free(cl->data);
            for (uint32_t i = 0; i < dir->cluster_count; i++) {
                free(dir->clusters[i].data);
            }
            free(dir->clusters);
            dir->clusters = NULL;
            return err;
        }

        dir->cluster_count++;

        // Получаем следующий кластер
        uint32_t next;
        err = fat32_get_next_cluster(disk, info, current, &next);
        if (err != ERR_OK) {
            // Ошибка чтения FAT
            for (uint32_t i = 0; i < dir->cluster_count; i++) {
                free(dir->clusters[i].data);
            }
            free(dir->clusters);
            dir->clusters = NULL;
            return err;
        }

        if (next >= 0x0FFFFFF8) break; // конец цепочки
        current = next;
    }

    return ERR_OK;
}

// ==================== fat32_dir_close ====================
void fat32_dir_close(Fat32Directory *dir) {
    if (!dir) return;
    for (uint32_t i = 0; i < dir->cluster_count; i++) {
        free(dir->clusters[i].data);
    }
    free(dir->clusters);
    dir->clusters = NULL;
    dir->cluster_count = 0;
}

// ==================== fat32_find_free_entries ====================
bool fat32_find_free_entries(const Fat32Directory *dir, uint8_t needed,
                             uint32_t *out_cluster_idx, uint32_t *out_entry_off) {
    if (!dir || needed == 0) return false;

    uint32_t entries_per_cluster = (dir->volume->sectors_per_cluster * dir->volume->bytes_per_sector) / 32;

    uint32_t consecutive = 0;

    for (uint32_t ci = 0; ci < dir->cluster_count; ci++) {
        const uint8_t *data = dir->clusters[ci].data;
        for (uint32_t ei = 0; ei < entries_per_cluster; ei++) {
            const uint8_t *entry = data + ei * 32;
            if (is_entry_free(entry)) {
                consecutive++;
                if (consecutive == needed) {
                    // Нашли нужный блок. Возвращаем индекс первого кластера и смещение первой записи.
                    *out_cluster_idx = ci;
                    *out_entry_off = ei - (needed - 1);
                    return true;
                }
            } else {
                consecutive = 0;
            }
        }
    }
    return false; // недостаточно свободных записей
}

// ==================== fat32_append_cluster_to_dir ====================
ErrorCode fat32_append_cluster_to_dir(Disk *disk, const Fat32Info *info, Fat32Directory *dir) {
    if (!disk || !info || !dir) return ERR_INVALID_ARGUMENT;

    uint32_t new_cluster = fat32_alloc_cluster(disk, info);
    if (new_cluster == 0) return ERR_NO_FREE_SPACE;

    // Обновляем FAT: связываем последний кластер с новым
    if (dir->cluster_count > 0) {
        uint32_t last_cluster = dir->clusters[dir->cluster_count - 1].cluster_num;
        ErrorCode err = fat32_set_fat_entry(disk, info, last_cluster, new_cluster);
        if (err != ERR_OK) {
            // Освобождаем выделенный кластер
            fat32_free_cluster_chain(disk, info, new_cluster);
            return err;
        }
    } else {
        // Каталог пуст – такого не должно быть, но на всякий случай
        return ERR_INTERNAL;
    }

    // Обнуляем новый кластер
    uint32_t cluster_size = info->sectors_per_cluster * info->bytes_per_sector;
    uint8_t *zero_buf = (uint8_t*)calloc(1, cluster_size);
    if (!zero_buf) {
        // Откатываем FAT
        fat32_free_cluster_chain(disk, info, new_cluster);
        return ERR_OUT_OF_MEMORY;
    }

    ErrorCode err = fat32_write_cluster(disk, info, new_cluster, zero_buf);
    free(zero_buf);
    if (err != ERR_OK) {
        fat32_free_cluster_chain(disk, info, new_cluster);
        return err;
    }

    // Добавляем в структуру каталога
    Fat32DirCluster *new_clusters = (Fat32DirCluster*)realloc(dir->clusters,
                                      (dir->cluster_count + 1) * sizeof(Fat32DirCluster));
    if (!new_clusters) {
        fat32_free_cluster_chain(disk, info, new_cluster);
        return ERR_OUT_OF_MEMORY;
    }
    dir->clusters = new_clusters;

    Fat32DirCluster *cl = &dir->clusters[dir->cluster_count];
    cl->cluster_num = new_cluster;
    cl->data = (uint8_t*)malloc(cluster_size);
    if (!cl->data) {
        fat32_free_cluster_chain(disk, info, new_cluster);
        return ERR_OUT_OF_MEMORY;
    }
    memset(cl->data, 0, cluster_size); // уже обнулили, но для памяти в структуре тоже обнулим
    dir->cluster_count++;

    return ERR_OK;
}

// ==================== fat32_write_entries_to_dir ====================
ErrorCode fat32_write_entries_to_dir(Disk *disk, const Fat32Info *info,
                                     Fat32Directory *dir, uint32_t cluster_idx,
                                     uint32_t entry_off, const fat32_entry_info_t *entry) {
    if (!disk || !info || !dir || !entry) return ERR_INVALID_ARGUMENT;
    if (cluster_idx >= dir->cluster_count) return ERR_INVALID_ARGUMENT;

    uint32_t cluster_size = info->sectors_per_cluster * info->bytes_per_sector;
    uint32_t entries_per_cluster = cluster_size / 32;

    // Проверяем, что запись помещается в кластер
    if (entry_off + entry->total_entries > entries_per_cluster) {
        return ERR_INVALID_ARGUMENT; // или можно расширить? но по логике поиск гарантирует
    }

    Fat32DirCluster *cl = &dir->clusters[cluster_idx];
    uint8_t *base = cl->data + entry_off * 32;

    // Сначала копируем LFN-записи (если есть)
    for (uint8_t i = 0; i < entry->lfn_count; i++) {
        memcpy(base + i * 32, &entry->lfn_entries[i], 32);
    }

    // Затем SFN-запись
    Fat32ShortEntry sfn_entry;
    memcpy(sfn_entry.name, entry->sfn, 11);
    sfn_entry.attr = entry->attr;
    sfn_entry.nt_reserved = 0;
    sfn_entry.create_time_tenth = entry->crt_time_tenth;
    sfn_entry.create_time = entry->crt_time;
    sfn_entry.create_date = entry->crt_date;
    sfn_entry.last_access_date = entry->lst_acc_date;
    sfn_entry.first_cluster_hi = (uint16_t)(entry->first_cluster >> 16);
    sfn_entry.write_time = entry->wrt_time;
    sfn_entry.write_date = entry->wrt_date;
    sfn_entry.first_cluster_lo = (uint16_t)(entry->first_cluster & 0xFFFF);
    sfn_entry.file_size = entry->file_size;

    memcpy(base + entry->lfn_count * 32, &sfn_entry, 32);

    // Записываем обновлённый кластер на диск
    ErrorCode err = fat32_write_cluster(disk, info, cl->cluster_num, cl->data);
    return err;
}

// ==================== fat32_init_new_dir ====================
ErrorCode fat32_init_new_dir(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t parent_cluster) {
    if (!disk || !info || cluster < 2) return ERR_INVALID_ARGUMENT;

    uint32_t cluster_size = info->sectors_per_cluster * info->bytes_per_sector;
    uint8_t *buf = (uint8_t*)calloc(1, cluster_size);
    if (!buf) return ERR_OUT_OF_MEMORY;

    // Запись "." – указывает на себя
    Fat32ShortEntry *dot = (Fat32ShortEntry*)buf;
    memset(dot->name, ' ', 11);
    dot->name[0] = '.';
    dot->attr = FAT32_ATTR_DIRECTORY;
    dot->first_cluster_hi = (uint16_t)(cluster >> 16);
    dot->first_cluster_lo = (uint16_t)(cluster & 0xFFFF);
    // Время можно установить позже, но для инициализации оставим 0
    // dot->create_time и т.д.

    // Запись ".." – указывает на родителя
    Fat32ShortEntry *dotdot = (Fat32ShortEntry*)(buf + 32);
    memset(dotdot->name, ' ', 11);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attr = FAT32_ATTR_DIRECTORY;
    dotdot->first_cluster_hi = (uint16_t)(parent_cluster >> 16);
    dotdot->first_cluster_lo = (uint16_t)(parent_cluster & 0xFFFF);

    ErrorCode err = fat32_write_cluster(disk, info, cluster, buf);
    free(buf);
    return err;
}

// ==================== fat32_create_dir ====================
ErrorCode fat32_create_dir(Disk *disk, uint64_t start_lba, const char *path) {
    if (!disk || !path) return ERR_INVALID_ARGUMENT;

    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    char *path_copy = my_strdup(path);
    if (!path_copy) return ERR_OUT_OF_MEMORY;

    char *last_slash = strrchr(path_copy, '/');
    char *parent_path;
    const char *new_name;

    if (!last_slash) {
        free(path_copy);
        return ERR_INVALID_ARGUMENT;
    }

    if (last_slash == path_copy) {
        parent_path = "/";
        new_name = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent_path = path_copy;
        new_name = last_slash + 1;
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

    uint32_t parent_entries_count = 0;
    for (uint32_t i = 0; i < parent_dir.cluster_count; i++) {
        parent_entries_count += (info.sectors_per_cluster * info.bytes_per_sector) / 32;
    }

    uint8_t *parent_buffer = (uint8_t*)malloc(parent_entries_count * 32);
    if (!parent_buffer) {
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return ERR_OUT_OF_MEMORY;
    }

    uint32_t offset = 0;
    for (uint32_t i = 0; i < parent_dir.cluster_count; i++) {
        uint32_t cluster_entries = (info.sectors_per_cluster * info.bytes_per_sector) / 32;
        memcpy(parent_buffer + offset * 32, parent_dir.clusters[i].data, cluster_entries * 32);
        offset += cluster_entries;
    }

    fat32_entry_info_t entry_info;
    err = fat32_prepare_entry(new_name, FAT32_ATTR_DIRECTORY,
                                              parent_buffer, parent_entries_count, &entry_info);
    free(parent_buffer);
    if (err != ERR_OK) {
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return err;
    }

    fat32_set_current_time(&entry_info);
    entry_info.first_cluster = 0;

    uint32_t cluster_idx, entry_off;
    bool found = fat32_find_free_entries(&parent_dir, entry_info.total_entries, &cluster_idx, &entry_off);

    if (!found) {
        err = fat32_append_cluster_to_dir(disk, &info, &parent_dir);
        if (err != ERR_OK) {
            fat32_free_entry_info(&entry_info);
            fat32_dir_close(&parent_dir);
            free(path_copy);
            return err;
        }
        found = fat32_find_free_entries(&parent_dir, entry_info.total_entries, &cluster_idx, &entry_off);
        if (!found) {
            fat32_free_entry_info(&entry_info);
            fat32_dir_close(&parent_dir);
            free(path_copy);
            return ERR_NO_FREE_SPACE;
        }
    }

    uint32_t new_cluster = fat32_alloc_cluster(disk, &info);
    if (new_cluster == 0) {
        fat32_free_entry_info(&entry_info);
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return ERR_NO_FREE_SPACE;
    }

    err = fat32_init_new_dir(disk, &info, new_cluster, parent_cluster);
    if (err != ERR_OK) {
        fat32_free_cluster_chain(disk, &info, new_cluster);
        fat32_free_entry_info(&entry_info);
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return err;
    }

    entry_info.first_cluster = new_cluster;
    entry_info.file_size = 0;

    err = fat32_write_entries_to_dir(disk, &info, &parent_dir, cluster_idx, entry_off, &entry_info);
    if (err != ERR_OK) {
        fat32_free_cluster_chain(disk, &info, new_cluster);
        fat32_free_entry_info(&entry_info);
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return err;
    }

    fat32_free_entry_info(&entry_info);
    fat32_dir_close(&parent_dir);
    free(path_copy);

    return ERR_OK;
}

// Основная функция удаления пустого каталога
ErrorCode fat32_remove_dir(Disk *disk, uint64_t start_lba, const char *path) {
    if (!disk || !path) return ERR_INVALID_ARGUMENT;
    if (strcmp(path, "/") == 0) return ERR_INVALID_ARGUMENT;

    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    char *path_copy = my_strdup(path);
    if (!path_copy) return ERR_OUT_OF_MEMORY;

    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        free(path_copy);
        return ERR_INVALID_ARGUMENT;
    }

    *last_slash = '\0';
    char *parent_path = path_copy;
    const char *dir_name = last_slash + 1;

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
	if (find_entry_by_name(disk, &info, &parent_dir, dir_name, &cluster_idx, &entry_idx) != 0) {
		fat32_dir_close(&parent_dir);
		free(path_copy);
		return ERR_NOT_FOUND;
	}

    uint8_t *entry = parent_dir.clusters[cluster_idx].data + entry_idx * 32;
    Fat32ShortEntry *se = (Fat32ShortEntry*)entry;
    if (!(se->attr & FAT32_ATTR_DIRECTORY)) {
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return ERR_INVALID_ARGUMENT;
    }

    uint32_t target_cluster = ((uint32_t)se->first_cluster_hi << 16) | se->first_cluster_lo;

    if (!is_directory_empty(disk, &info, target_cluster)) {
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return ERR_DIR_NOT_EMPTY;
    }

    if (target_cluster != 0) {
        err = fat32_free_cluster_chain(disk, &info, target_cluster);
        if (err != ERR_OK) {
            fat32_dir_close(&parent_dir);
            free(path_copy);
            return err;
        }
    }

    err = mark_entry_deleted_in_dir(disk, &info, &parent_dir, cluster_idx, entry_idx);
    if (err != ERR_OK) {
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return err;
    }

    fat32_dir_close(&parent_dir);
    free(path_copy);
    return ERR_OK;
}