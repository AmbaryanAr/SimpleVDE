#include "fat32.h"
#include "fat32_util.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#define RESERVED_SLOT_SIZE 128
#define RESERVED_NAME_SIZE 124
#define RESERVED_FIELD_OFFSET 52

// Получить номер резервного кластера из BPB
static ErrorCode get_reserve_cluster(Disk *disk, uint64_t start_lba, uint32_t *cluster) {
    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;
    *cluster = *(uint32_t*)(sector + RESERVED_FIELD_OFFSET);
    return ERR_OK;
}

// Записать номер резервного кластера в BPB
static ErrorCode set_reserve_cluster(Disk *disk, uint64_t start_lba, uint32_t cluster) {
    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;
    *(uint32_t*)(sector + RESERVED_FIELD_OFFSET) = cluster;
    err = disk_write(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;
    // также обновить резервный BPB (сектор 6)
    err = disk_write(disk, sector, SECTOR_SIZE, (start_lba + 6) * SECTOR_SIZE);
    return err;
}

// Получить кластер загрузочного файла из BPB
static ErrorCode get_boot_cluster(Disk *disk, uint64_t start_lba, uint32_t *cluster) {
    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;
    *cluster = *(uint32_t*)(sector + RESERVED_FIELD_OFFSET + 4);
    return ERR_OK;
}

// Записать кластер загрузочного файла в BPB
static ErrorCode set_boot_cluster(Disk *disk, uint64_t start_lba, uint32_t cluster) {
    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;
    *(uint32_t*)(sector + RESERVED_FIELD_OFFSET + 4) = cluster;
    err = disk_write(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;
    // также обновить резервный BPB (сектор 6)
    err = disk_write(disk, sector, SECTOR_SIZE, (start_lba + 6) * SECTOR_SIZE);
    return err;
}

// Найти пустой слот
static int find_empty_slot(const uint8_t *cluster_data, uint32_t slots) {
    for (uint32_t i = 0; i < slots; i++) {
        const uint8_t *slot = cluster_data + i * RESERVED_SLOT_SIZE;
        if (slot[0] == '\0') return i;
    }
    return -1;
}

// Проверка, что имя допустимо (ASCII, длина <= 123)
static bool is_valid_name(const char *name) {
    size_t len = strlen(name);
    if (len == 0 || len > RESERVED_NAME_SIZE - 1) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = name[i];
        if (c < 0x20 || c > 0x7E) return false; // только печатные ASCII
    }
    return true;
}

// Получить первый кластер файла по пути (проверка, что это файл)
static ErrorCode get_file_cluster(Disk *disk, const Fat32Info *info, const char *path, uint32_t *cluster, uint32_t *size) {
    char *path_copy = my_strdup(path);
    if (!path_copy) return ERR_OUT_OF_MEMORY;
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        free(path_copy);
        return ERR_INVALID_ARGUMENT;
    }
    *last_slash = '\0';
    char *parent_path = path_copy;
    const char *file_name = last_slash + 1;
    if (parent_path[0] == '\0') parent_path = "/";

    uint32_t parent_cluster;
    ErrorCode err = fat32_find_dir(disk, info, parent_path, &parent_cluster);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    Fat32Directory parent_dir;
    err = fat32_dir_open(disk, info, parent_cluster, &parent_dir);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    uint8_t *buffer = NULL;
    uint32_t entries = 0;
    err = fat32_read_dir(disk, info, parent_cluster, &buffer, &entries);
    if (err != ERR_OK) {
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return err;
    }
    
    int found = -1;
    for (uint32_t i = 0; i < entries; i++) {
        const uint8_t *entry = buffer + i * 32;
        if (entry[0] == 0x00) break;
        if (entry[0] == 0xE5) continue;
        if (entry[11] == FAT32_ATTR_LFN) continue;
        const Fat32ShortEntry *se = (const Fat32ShortEntry*)entry;
        if (se->attr & FAT32_ATTR_DIRECTORY) continue;

        char *long_name = extract_lfn_name(buffer, i);
        const char *cmp;
        char sfn_name[13];
        if (long_name) {
            cmp = long_name;
        } else {
            sfn_to_display_name(se->name, sfn_name, sizeof(sfn_name));
            cmp = sfn_name;
        }
        if (strcasecmp_ascii(cmp, file_name) == 0) {
            *cluster = ((uint32_t)se->first_cluster_hi << 16) | se->first_cluster_lo;
            *size = se->file_size;
            found = 0;
            free(long_name);
            break;
        }
        free(long_name);
    }
    free(buffer);
    fat32_dir_close(&parent_dir);
    free(path_copy);
    return found == 0 ? ERR_OK : ERR_NOT_FOUND;
}

// ==================== Экспортируемые функции ====================

ErrorCode fat32_reserve_init(Disk *disk, uint64_t start_lba) {
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t existing;
    err = get_reserve_cluster(disk, start_lba, &existing);
    if (err != ERR_OK) return err;
    if (existing != 0) {
        return ERR_ALREADY_EXISTS;
    }

    uint32_t cluster = fat32_alloc_cluster(disk, &info);
    if (cluster == 0) return ERR_NO_FREE_SPACE;

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *zero = (uint8_t*)calloc(1, cluster_size);
    if (!zero) {
        fat32_free_cluster_chain(disk, &info, cluster);
        return ERR_OUT_OF_MEMORY;
    }
    err = fat32_write_cluster(disk, &info, cluster, zero);
    free(zero);
    if (err != ERR_OK) {
        fat32_free_cluster_chain(disk, &info, cluster);
        return err;
    }

    err = set_reserve_cluster(disk, start_lba, cluster);
    if (err != ERR_OK) {
        fat32_free_cluster_chain(disk, &info, cluster);
        return err;
    }

    // При инициализации также обнуляем загрузочный кластер
    err = set_boot_cluster(disk, start_lba, 0);
    if (err != ERR_OK) {
        // не фатально, но сообщим
        return err;
    }

    return ERR_OK;
}

ErrorCode fat32_reserve_list(Disk *disk, uint64_t start_lba) {
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        return ERR_RESERVE_NOT_INIT;
    }

    uint32_t boot_cluster = 0;
    get_boot_cluster(disk, start_lba, &boot_cluster);

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint32_t slots = cluster_size / RESERVED_SLOT_SIZE;
    uint8_t *buffer = (uint8_t*)malloc(cluster_size);
    if (!buffer) return ERR_OUT_OF_MEMORY;

    err = fat32_read_cluster(disk, &info, reserve_cluster, buffer);
    if (err != ERR_OK) {
        free(buffer);
        return err;
    }

    printf("Reserve cluster %u (%u bytes, %u slots):\n", reserve_cluster, cluster_size, slots);
    for (uint32_t i = 0; i < slots; i++) {
        const uint8_t *slot = buffer + i * RESERVED_SLOT_SIZE;
        const char *name = (const char*)slot;
        if (name[0] != '\0') {
            uint32_t cl = *(uint32_t*)(slot + RESERVED_NAME_SIZE);
            printf("  [%u] %s (cluster %u)%s\n", i, name, cl,
                   (cl == boot_cluster) ? " *" : "");
        }
    }
    free(buffer);
    return ERR_OK;
}


ErrorCode fat32_reserve_add(Disk *disk, uint64_t start_lba, const char *path) {
    const char *fname = strrchr(path, '/');
    if (fname) fname++; else fname = path;
    if (!is_valid_name(fname)) {
        return ERR_FAT32_NAME_INVALID; // или ERR_INVALID_ARGUMENT
    }

    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        return ERR_RESERVE_NOT_INIT;
    }

    uint32_t file_cluster, file_size;
    err = get_file_cluster(disk, &info, path, &file_cluster, &file_size);
    if (err != ERR_OK) return err;

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint32_t slots = cluster_size / RESERVED_SLOT_SIZE;
    uint8_t *buffer = (uint8_t*)malloc(cluster_size);
    if (!buffer) return ERR_OUT_OF_MEMORY;

    err = fat32_read_cluster(disk, &info, reserve_cluster, buffer);
    if (err != ERR_OK) {
        free(buffer);
        return err;
    }

    for (uint32_t i = 0; i < slots; i++) {
        const uint8_t *slot = buffer + i * RESERVED_SLOT_SIZE;
        const char *name = (const char*)slot;
        if (name[0] != '\0') {
            uint32_t cl = *(uint32_t*)(slot + RESERVED_NAME_SIZE);
            if (strcmp(name, fname) == 0) {
                free(buffer);
                return ERR_ALREADY_EXISTS;
            }
            if (cl == file_cluster) {
                free(buffer);
                return ERR_ALREADY_EXISTS;
            }
        }
    }

    int empty = find_empty_slot(buffer, slots);
    if (empty < 0) {
        free(buffer);
        return ERR_NO_FREE_SPACE;
    }

    uint8_t *slot = buffer + empty * RESERVED_SLOT_SIZE;
    strncpy((char*)slot, fname, RESERVED_NAME_SIZE);
    slot[RESERVED_NAME_SIZE - 1] = '\0';
    *(uint32_t*)(slot + RESERVED_NAME_SIZE) = file_cluster;

    err = fat32_write_cluster(disk, &info, reserve_cluster, buffer);
    free(buffer);
    return err;
}

ErrorCode fat32_reserve_remove(Disk *disk, uint64_t start_lba, const char *name) {
    if (!is_valid_name(name)) return ERR_INVALID_ARGUMENT;

    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        return ERR_RESERVE_NOT_INIT;
    }

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint32_t slots = cluster_size / RESERVED_SLOT_SIZE;
    uint8_t *buffer = (uint8_t*)malloc(cluster_size);
    if (!buffer) return ERR_OUT_OF_MEMORY;

    err = fat32_read_cluster(disk, &info, reserve_cluster, buffer);
    if (err != ERR_OK) {
        free(buffer);
        return err;
    }

    int found = -1;
    uint32_t found_cluster = 0;
    for (uint32_t i = 0; i < slots; i++) {
        const uint8_t *slot = buffer + i * RESERVED_SLOT_SIZE;
        if (slot[0] != '\0' && strcmp((const char*)slot, name) == 0) {
            found = i;
            found_cluster = *(uint32_t*)(slot + RESERVED_NAME_SIZE);
            break;
        }
    }

    if (found < 0) {
        free(buffer);
        return ERR_NOT_FOUND;
    }

    // Если удаляемый файл является загрузочным, очищаем загрузочный кластер
    uint32_t boot_cluster;
    get_boot_cluster(disk, start_lba, &boot_cluster);
    if (found_cluster == boot_cluster) {
        set_boot_cluster(disk, start_lba, 0);
        // можно не выводить сообщение здесь; пусть команда сама решит
    }

    memset(buffer + found * RESERVED_SLOT_SIZE, 0, RESERVED_SLOT_SIZE);
    err = fat32_write_cluster(disk, &info, reserve_cluster, buffer);
    free(buffer);
    return err;
}

// fat32_reserve_clear
ErrorCode fat32_reserve_clear(Disk *disk, uint64_t start_lba) {
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        return ERR_RESERVE_NOT_INIT;
    }

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *zero = (uint8_t*)calloc(1, cluster_size);
    if (!zero) return ERR_OUT_OF_MEMORY;

    err = fat32_write_cluster(disk, &info, reserve_cluster, zero);
    free(zero);
    if (err != ERR_OK) return err;

    // Также очищаем загрузочный кластер
    set_boot_cluster(disk, start_lba, 0);
    return ERR_OK;
}


ErrorCode fat32_reserve_dump(Disk *disk, uint64_t start_lba) {
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        return ERR_RESERVE_NOT_INIT;
    }

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *buffer = (uint8_t*)malloc(cluster_size);
    if (!buffer) return ERR_OUT_OF_MEMORY;

    err = fat32_read_cluster(disk, &info, reserve_cluster, buffer);
    if (err != ERR_OK) {
        free(buffer);
        return err;
    }

    printf("Reserve cluster %u dump (%u bytes):\n", reserve_cluster, cluster_size);
    for (uint32_t i = 0; i < cluster_size; i++) {
        if (i % 16 == 0) printf("%08x: ", i);
        printf("%02x ", buffer[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (cluster_size % 16 != 0) printf("\n");
    free(buffer);
    return ERR_OK;
}

ErrorCode fat32_reserve_info(Disk *disk, uint64_t start_lba) {
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        return ERR_RESERVE_NOT_INIT;
    }

    uint32_t boot_cluster;
    get_boot_cluster(disk, start_lba, &boot_cluster);

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint32_t slots = cluster_size / RESERVED_SLOT_SIZE;
    uint8_t *buffer = (uint8_t*)malloc(cluster_size);
    if (!buffer) return ERR_OUT_OF_MEMORY;

    err = fat32_read_cluster(disk, &info, reserve_cluster, buffer);
    if (err != ERR_OK) {
        free(buffer);
        return err;
    }

    int used = 0;
    for (uint32_t i = 0; i < slots; i++) {
        if (buffer[i * RESERVED_SLOT_SIZE] != 0) used++;
    }
    free(buffer);

    printf("Reserve cluster: %u\n", reserve_cluster);
    printf("Boot cluster: %u\n", boot_cluster);
    printf("Cluster size: %u bytes\n", cluster_size);
    printf("Slot size: %u bytes\n", RESERVED_SLOT_SIZE);
    printf("Total slots: %u\n", slots);
    printf("Used slots: %u\n", used);
    printf("Free slots: %u\n", slots - used);
    return ERR_OK;
}

// ==================== Функции для работы с загрузчиком ====================

ErrorCode fat32_reserve_boot_set(Disk *disk, uint64_t start_lba, const char *name) {
    if (!is_valid_name(name)) return ERR_INVALID_ARGUMENT;

    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        return ERR_RESERVE_NOT_INIT;
    }

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint32_t slots = cluster_size / RESERVED_SLOT_SIZE;
    uint8_t *buffer = (uint8_t*)malloc(cluster_size);
    if (!buffer) return ERR_OUT_OF_MEMORY;

    err = fat32_read_cluster(disk, &info, reserve_cluster, buffer);
    if (err != ERR_OK) {
        free(buffer);
        return err;
    }

    uint32_t found_cluster = 0;
    int found = -1;
    for (uint32_t i = 0; i < slots; i++) {
        const uint8_t *slot = buffer + i * RESERVED_SLOT_SIZE;
        if (slot[0] != '\0' && strcmp((const char*)slot, name) == 0) {
            found_cluster = *(uint32_t*)(slot + RESERVED_NAME_SIZE);
            found = i;
            break;
        }
    }
    free(buffer);

    if (found < 0) {
        return ERR_NOT_FOUND;
    }

    err = set_boot_cluster(disk, start_lba, found_cluster);
    if (err != ERR_OK) return err;

    printf("File '%s' (cluster %u) set as boot entry.\n", name, found_cluster);
    return ERR_OK;
}

ErrorCode fat32_reserve_boot_show(Disk *disk, uint64_t start_lba) {
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        return ERR_RESERVE_NOT_INIT;
    }

    uint32_t boot_cluster;
    err = get_boot_cluster(disk, start_lba, &boot_cluster);
    if (err != ERR_OK) return err;
    if (boot_cluster == 0) {
        printf("No boot file set.\n");
        return ERR_OK;
    }

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint32_t slots = cluster_size / RESERVED_SLOT_SIZE;
    uint8_t *buffer = (uint8_t*)malloc(cluster_size);
    if (!buffer) return ERR_OUT_OF_MEMORY;

    err = fat32_read_cluster(disk, &info, reserve_cluster, buffer);
    if (err != ERR_OK) {
        free(buffer);
        return err;
    }

    // Ищем имя и выводим его сразу, пока буфер не освобождён
    for (uint32_t i = 0; i < slots; i++) {
        const uint8_t *slot = buffer + i * RESERVED_SLOT_SIZE;
        if (slot[0] != '\0') {
            uint32_t cl = *(uint32_t*)(slot + RESERVED_NAME_SIZE);
            if (cl == boot_cluster) {
                printf("Boot file: %s (cluster %u)\n", (const char*)slot, boot_cluster);
                free(buffer);
                return ERR_OK;
            }
        }
    }
    free(buffer);
    printf("Boot file cluster %u not found in reserve.\n", boot_cluster);
    return ERR_OK;
}

ErrorCode fat32_reserve_boot_clear(Disk *disk, uint64_t start_lba) {
    return set_boot_cluster(disk, start_lba, 0);
}