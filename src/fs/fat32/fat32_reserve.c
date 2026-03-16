#include "fat32.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#define RESERVED_SLOT_SIZE 128
#define RESERVED_NAME_SIZE 124
#define RESERVED_FIELD_OFFSET 52

// Вспомогательные функции (скопированы из fat32_file.c, можно вынести в общий модуль позже)
static int strcasecmp_ascii(const char *a, const char *b) {
    while (*a && *b) {
        char ca = tolower((unsigned char)*a);
        char cb = tolower((unsigned char)*b);
        if (ca != cb) return (int)(ca - cb);
        a++;
        b++;
    }
    return (int)((unsigned char)*a - (unsigned char)*b);
}

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

/**
 * Извлекает длинное имя из LFN-записей, предшествующих SFN-записи с индексом sfn_index.
 * Возвращает динамически выделенную строку в UTF-8 (только ASCII), или NULL, если LFN нет.
 * Вызывающий должен освободить память free().
 */
static char* extract_lfn_name(const uint8_t *dir_buffer, uint32_t sfn_index) {
    // Ищем последнюю LFN перед SFN (ту, у которой order имеет бит 0x40)
    uint32_t last_lfn = sfn_index;
    while (last_lfn > 0) {
        const uint8_t *e = dir_buffer + (last_lfn - 1) * 32;
        if (e[11] != FAT32_ATTR_LFN) break;
        if (e[0] & 0x40) {
            // Это последняя LFN в наборе (первая при записи)
            last_lfn--; // теперь last_lfn указывает на эту запись
            break;
        }
        last_lfn--;
    }
    if (last_lfn == sfn_index) return NULL; // нет LFN

    // Теперь last_lfn - индекс последней LFN (той, что ближе к SFN)
    // Нам нужно собрать все LFN от last_lfn назад до первой, но проще собрать от last_lfn до sfn_index-1
    uint32_t first_lfn = last_lfn;
    // Идём назад до первой LFN (где order без бита 0x40)
    while (first_lfn > 0) {
        const uint8_t *e = dir_buffer + (first_lfn - 1) * 32;
        if (e[11] != FAT32_ATTR_LFN) break;
        first_lfn--;
    }
    // Теперь first_lfn - индекс первой LFN в наборе
    // Собираем имена от first_lfn до sfn_index-1
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

// Найти слот в реестре по имени (или по пустому месту)
static int find_slot_by_name(const uint8_t *cluster_data, uint32_t slots, const char *name, uint32_t *cluster_num) {
    for (uint32_t i = 0; i < slots; i++) {
        const uint8_t *slot = cluster_data + i * RESERVED_SLOT_SIZE;
        const char *slot_name = (const char*)slot;
        if (slot_name[0] == '\0') continue; // пустой слот
        // Сравниваем имя
        if (strcmp(slot_name, name) == 0) {
            *cluster_num = *(uint32_t*)(slot + RESERVED_NAME_SIZE);
            return i;
        }
    }
    return -1;
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
    // Копируем логику из fat32_delete_file: разбираем путь, находим родительский каталог, ищем запись
    // ... для краткости предполагаем, что есть функция find_entry_by_name (как в fat32_dir.c)
    // Но мы можем использовать готовую fat32_find_dir для родителя, а затем просканировать записи.
    // Упростим: используем fat32_find_dir для поиска кластера самого файла? Нет, fat32_find_dir ищет каталоги.
    // Придётся реализовать поиск записи по имени в каталоге.
    // Возьмём код из fat32_delete_file, но адаптируем.
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

    // Открываем родительский каталог
    Fat32Directory parent_dir;
    err = fat32_dir_open(disk, info, parent_cluster, &parent_dir);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    // Читаем весь каталог в буфер
    uint8_t *buffer = NULL;
    uint32_t entries = 0;
    err = fat32_read_dir(disk, info, parent_cluster, &buffer, &entries);
    if (err != ERR_OK) {
        fat32_dir_close(&parent_dir);
        free(path_copy);
        return err;
    }

    uint32_t entries_per_cluster = (info->sectors_per_cluster * info->bytes_per_sector) / 32;
    int found = -1;
    for (uint32_t i = 0; i < entries; i++) {
        const uint8_t *entry = buffer + i * 32;
        if (entry[0] == 0x00) break;
        if (entry[0] == 0xE5) continue;
        if (entry[11] == FAT32_ATTR_LFN) continue;
        const Fat32ShortEntry *se = (const Fat32ShortEntry*)entry;
        // Пропускаем каталоги
        if (se->attr & FAT32_ATTR_DIRECTORY) continue;

        // Получаем длинное имя
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

    // Проверяем, не инициализирован ли уже
    uint32_t existing;
    err = get_reserve_cluster(disk, start_lba, &existing);
    if (err != ERR_OK) return err;
    if (existing != 0) {
        return ERR_ALREADY_EXISTS; // реестр уже есть
    }

    // Выделяем кластер
    uint32_t cluster = fat32_alloc_cluster(disk, &info);
    if (cluster == 0) return ERR_NO_FREE_SPACE;

    // Помечаем его как EOC (уже сделано в alloc_cluster)
    // Но alloc_cluster помечает как EOC? В нашей реализации да, он устанавливает 0x0FFFFFFF.
    // Обнуляем кластер
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

    // Сохраняем номер в BPB
    err = set_reserve_cluster(disk, start_lba, cluster);
    if (err != ERR_OK) {
        fat32_free_cluster_chain(disk, &info, cluster);
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
        fprintf(stderr, "Reserve cluster not initialized.\n");
        return ERR_NOT_FOUND;
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

    printf("Reserve cluster %u (%u bytes, %u slots):\n", reserve_cluster, cluster_size, slots);
    for (uint32_t i = 0; i < slots; i++) {
        const uint8_t *slot = buffer + i * RESERVED_SLOT_SIZE;
        const char *name = (const char*)slot;
        if (name[0] != '\0') {
            uint32_t cl = *(uint32_t*)(slot + RESERVED_NAME_SIZE);
            printf("  [%u] %s (cluster %u)\n", i, name, cl);
        }
    }
    free(buffer);
    return ERR_OK;
}

ErrorCode fat32_reserve_add(Disk *disk, uint64_t start_lba, const char *path) {
    if (!is_valid_name(path)) {
        // но path - это путь к файлу, а не имя для реестра. Имя будет извлечено из пути.
        // Поэтому нужно получить базовое имя файла.
        return ERR_INVALID_ARGUMENT;
    }
    // Извлекаем имя файла из пути
    const char *fname = strrchr(path, '/');
    if (fname) fname++; else fname = path;
    if (!is_valid_name(fname)) {
        fprintf(stderr, "File name contains invalid characters or is too long.\n");
        return ERR_INVALID_ARGUMENT;
    }

    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        fprintf(stderr, "Reserve cluster not initialized. Use reserve-init first.\n");
        return ERR_NOT_FOUND;
    }

    // Получаем первый кластер файла
    uint32_t file_cluster, file_size;
    err = get_file_cluster(disk, &info, path, &file_cluster, &file_size);
    if (err != ERR_OK) {
        if (err == ERR_NOT_FOUND)
            fprintf(stderr, "File not found.\n");
        else if (err == ERR_INVALID_ARGUMENT)
            fprintf(stderr, "Path is not a file (maybe a directory).\n");
        return err;
    }

    // Читаем кластер реестра
    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint32_t slots = cluster_size / RESERVED_SLOT_SIZE;
    uint8_t *buffer = (uint8_t*)malloc(cluster_size);
    if (!buffer) return ERR_OUT_OF_MEMORY;

    err = fat32_read_cluster(disk, &info, reserve_cluster, buffer);
    if (err != ERR_OK) {
        free(buffer);
        return err;
    }

    // Проверяем уникальность имени и кластера
    for (uint32_t i = 0; i < slots; i++) {
        const uint8_t *slot = buffer + i * RESERVED_SLOT_SIZE;
        const char *name = (const char*)slot;
        if (name[0] != '\0') {
            uint32_t cl = *(uint32_t*)(slot + RESERVED_NAME_SIZE);
            if (strcmp(name, fname) == 0) {
                free(buffer);
                fprintf(stderr, "Entry with name '%s' already exists.\n", fname);
                return ERR_ALREADY_EXISTS;
            }
            if (cl == file_cluster) {
                free(buffer);
                fprintf(stderr, "Cluster %u already used by entry '%s'.\n", file_cluster, name);
                return ERR_ALREADY_EXISTS;
            }
        }
    }

    // Ищем пустой слот
    int empty = find_empty_slot(buffer, slots);
    if (empty < 0) {
        free(buffer);
        fprintf(stderr, "No free slots in reserve cluster.\n");
        return ERR_NO_FREE_SPACE;
    }

    // Записываем
    uint8_t *slot = buffer + empty * RESERVED_SLOT_SIZE;
    strncpy((char*)slot, fname, RESERVED_NAME_SIZE);
    slot[RESERVED_NAME_SIZE - 1] = '\0'; // гарантия
    *(uint32_t*)(slot + RESERVED_NAME_SIZE) = file_cluster;

    // Сохраняем обратно
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
        fprintf(stderr, "Reserve cluster not initialized.\n");
        return ERR_NOT_FOUND;
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
    for (uint32_t i = 0; i < slots; i++) {
        const uint8_t *slot = buffer + i * RESERVED_SLOT_SIZE;
        if (slot[0] != '\0' && strcmp((const char*)slot, name) == 0) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        free(buffer);
        fprintf(stderr, "Entry '%s' not found.\n", name);
        return ERR_NOT_FOUND;
    }

    // Обнуляем слот
    memset(buffer + found * RESERVED_SLOT_SIZE, 0, RESERVED_SLOT_SIZE);
    err = fat32_write_cluster(disk, &info, reserve_cluster, buffer);
    free(buffer);
    return err;
}

ErrorCode fat32_reserve_clear(Disk *disk, uint64_t start_lba) {
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        fprintf(stderr, "Reserve cluster not initialized.\n");
        return ERR_NOT_FOUND;
    }

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *zero = (uint8_t*)calloc(1, cluster_size);
    if (!zero) return ERR_OUT_OF_MEMORY;

    err = fat32_write_cluster(disk, &info, reserve_cluster, zero);
    free(zero);
    return err;
}

ErrorCode fat32_reserve_dump(Disk *disk, uint64_t start_lba) {
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t reserve_cluster;
    err = get_reserve_cluster(disk, start_lba, &reserve_cluster);
    if (err != ERR_OK) return err;
    if (reserve_cluster == 0) {
        fprintf(stderr, "Reserve cluster not initialized.\n");
        return ERR_NOT_FOUND;
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
        printf("Reserve cluster not initialized.\n");
        return ERR_OK; // не ошибка, а просто информация
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

    int used = 0;
    for (uint32_t i = 0; i < slots; i++) {
        if (buffer[i * RESERVED_SLOT_SIZE] != 0) used++;
    }
    free(buffer);

    printf("Reserve cluster: %u\n", reserve_cluster);
    printf("Cluster size: %u bytes\n", cluster_size);
    printf("Slot size: %u bytes\n", RESERVED_SLOT_SIZE);
    printf("Total slots: %u\n", slots);
    printf("Used slots: %u\n", used);
    printf("Free slots: %u\n", slots - used);
    return ERR_OK;
}