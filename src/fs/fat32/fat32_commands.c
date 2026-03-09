#include "fat32_commands.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>

// Структура BPB для FAT32
#pragma pack(push, 1)
typedef struct {
    uint8_t  jump_boot[3];           // 0xEB 0x?? 0x90
    char     oem_name[8];            // "MSWIN4.1" или что-то подобное
    uint16_t bytes_per_sector;       // обычно 512
    uint8_t  sectors_per_cluster;    // должно быть степенью двойки (1,2,4,8,16,32,64,128)
    uint16_t reserved_sectors;       // обычно 32 для FAT32
    uint8_t  num_fats;               // обычно 2
    uint16_t root_entries;           // для FAT32 всегда 0
    uint16_t total_sectors_16;       // для FAT32 всегда 0
    uint8_t  media_descriptor;       // 0xF8 для жёсткого диска
    uint16_t fat_size_16;            // для FAT32 всегда 0
    uint16_t sectors_per_track;      // обычно 63
    uint16_t num_heads;              // обычно 255
    uint32_t hidden_sectors;         // количество секторов до начала раздела (start_lba)
    uint32_t total_sectors_32;       // общее количество секторов в разделе
    uint32_t fat_size_32;            // количество секторов на одну FAT
    uint16_t ext_flags;              // 0
    uint16_t fs_version;             // 0
    uint32_t root_cluster;           // первый кластер корневого каталога (обычно 2)
    uint16_t fs_info;                // сектор FSInfo (обычно 1)
    uint16_t backup_boot_sector;     // сектор резервной копии загрузочного сектора (обычно 6)
    uint8_t  reserved[12];           // зарезервировано
    uint8_t  drive_number;           // 0x00 или 0x80
    uint8_t  reserved1;              // 0
    uint8_t  boot_signature;         // 0x29
    uint32_t volume_id;              // серийный номер тома
    char     volume_label[11];       // "NO NAME    "
    char     fs_type[8];             // "FAT32   "
} BPB_FAT32;

typedef struct {
    uint32_t lead_signature;          // 0x41615252
    uint8_t  reserved1[480];
    uint32_t struct_signature;        // 0x61417272
    uint32_t free_count;              // 0xFFFFFFFF
    uint32_t next_free;               // 0xFFFFFFFF
    uint8_t  reserved2[12];
    uint32_t trail_signature;         // 0xAA550000
} FSInfo;
#pragma pack(pop)

// Структура для хранения параметров раздела – определяем до прототипов
typedef struct {
    uint32_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint32_t reserved_sectors;
    uint8_t  num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint64_t first_data_lba;
    uint64_t fat1_lba;
    uint64_t fat2_lba;
    uint32_t total_clusters;
} Fat32PartInfo;

// *** Вспомогательные функции ***
static uint32_t fat32_calc_fat_sectors(uint32_t total_sectors, uint32_t sectors_per_cluster);
static void fat32_format_name(const uint8_t name[11], char *out, size_t out_size);
static int is_long_name_entry(const FAT32_DirEntry *entry);
static void print_dir_entry(const FAT32_DirEntry *entry);
static ErrorCode fat32_read_cluster(Disk *disk, uint64_t cluster_lba, uint32_t sectors_per_cluster, uint8_t *buffer);
static ErrorCode fat32_read_fat_sector(Disk *disk, uint64_t fat_lba, uint32_t sector_num, uint8_t *buffer);
static ErrorCode fat32_write_fat_sector(Disk *disk, uint64_t fat_lba, uint32_t sector_num, const uint8_t *buffer);
static ErrorCode fat32_set_fat_entry(Disk *disk, uint64_t fat_lba, uint32_t cluster, uint32_t value);
static ErrorCode fat32_get_part_info(Disk *disk, uint64_t start_lba, Fat32PartInfo *info);
static ErrorCode fat32_find_free_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t *free_cluster);
static ErrorCode fat32_write_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, const uint8_t *data);
static ErrorCode fat32_free_cluster_chain(Disk *disk, const Fat32PartInfo *info, uint32_t first_cluster);
static ErrorCode fat32_init_dir_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, uint32_t parent_cluster);
static ErrorCode fat32_dir_is_empty(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, bool *empty);
static ErrorCode fat32_find_dir(Disk *disk, const Fat32PartInfo *info, const char *path, uint32_t *dir_cluster);
// static ErrorCode fat32_create_dir_entry(Disk *disk, const Fat32PartInfo *info, const char *name, uint32_t first_cluster, uint32_t file_size, uint8_t attr);

// Функция для вычисления размера FAT
static uint32_t fat32_calc_fat_sectors(uint32_t total_sectors, uint32_t sectors_per_cluster) {
    uint32_t data_sectors = total_sectors - 32; // резервные секторы (32)
    uint32_t clusters = data_sectors / sectors_per_cluster;
    // Каждый кластер занимает 4 байта в FAT
    uint32_t fat_bytes = clusters * 4;
    uint32_t fat_sectors = (fat_bytes + 511) / 512;
    // Округлим вверх, чтобы хватило
    return fat_sectors;
}

// Преобразование имени 8.3 в читаемую строку (удаляем пробелы, добавляем точку перед расширением)
static void fat32_format_name(const uint8_t name[11], char *out, size_t out_size) {
    char name_part[9] = {0};
    char ext_part[4] = {0};
    int i;

    // Основное имя (первые 8 символов)
    for (i = 0; i < 8 && name[i] && name[i] != ' '; i++) {
        name_part[i] = name[i];
    }
    name_part[i] = '\0';

    // Расширение (следующие 3 символа)
    for (i = 0; i < 3 && name[8+i] && name[8+i] != ' '; i++) {
        ext_part[i] = name[8+i];
    }
    ext_part[i] = '\0';

    if (ext_part[0]) {
        snprintf(out, out_size, "%s.%s", name_part, ext_part);
    } else {
        snprintf(out, out_size, "%s", name_part);
    }
}

// Проверка, является ли запись длинным именем
static int is_long_name_entry(const FAT32_DirEntry *entry) {
    return (entry->attr == FAT32_ATTR_LONG_NAME);
}

// Вывод одной записи каталога
static void print_dir_entry(const FAT32_DirEntry *entry) {
    if (entry->name[0] == 0x00) return; // конец каталога
    if (entry->name[0] == 0xE5) return; // удалённая запись (помечена как 0xE5)
    if (is_long_name_entry(entry)) return; // длинные имена пропускаем (пока не поддерживаем)

    char name[13];
    fat32_format_name(entry->name, name, sizeof(name));

    if (entry->attr & FAT32_ATTR_DIRECTORY) {
        printf("DIR  %s\n", name);
    } else if (entry->attr & FAT32_ATTR_VOLUME_ID) {
        printf("VOL  %s  (size: %u bytes)\n", name, entry->file_size);
    } else {
        printf("FILE %s  (size: %u bytes)\n", name, entry->file_size);
    }
}

static ErrorCode fat32_read_cluster(Disk *disk, uint64_t cluster_lba, uint32_t sectors_per_cluster, uint8_t *buffer) {
    for (uint32_t i = 0; i < sectors_per_cluster; i++) {
        ErrorCode err = disk_read(disk, buffer + i * SECTOR_SIZE, SECTOR_SIZE, (cluster_lba + i) * SECTOR_SIZE);
        if (err != ERR_OK) return err;
    }
    return ERR_OK;
}

// Чтение сектора FAT (по номеру сектора относительно начала FAT)
static ErrorCode fat32_read_fat_sector(Disk *disk, uint64_t fat_lba, uint32_t sector_num, uint8_t *buffer) {
    return disk_read(disk, buffer, SECTOR_SIZE, (fat_lba + sector_num) * SECTOR_SIZE);
}

// Запись сектора FAT
static ErrorCode fat32_write_fat_sector(Disk *disk, uint64_t fat_lba, uint32_t sector_num, const uint8_t *buffer) {
    return disk_write(disk, buffer, SECTOR_SIZE, (fat_lba + sector_num) * SECTOR_SIZE);
}

// Запись значения в FAT по номеру кластера
static ErrorCode fat32_set_fat_entry(Disk *disk, uint64_t fat_lba, uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_num = fat_offset / SECTOR_SIZE;
    uint32_t byte_offset = fat_offset % SECTOR_SIZE;
    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = fat32_read_fat_sector(disk, fat_lba, sector_num, sector);
    if (err != ERR_OK) return err;
    // Сохраняем старшие 4 бита (зарезервированы) – оставляем как есть
    uint32_t old = *(uint32_t*)(sector + byte_offset);
    *(uint32_t*)(sector + byte_offset) = (old & 0xF0000000) | (value & 0x0FFFFFFF);
    return fat32_write_fat_sector(disk, fat_lba, sector_num, sector);
}

// Функцию для чтения параметров раздела.
static ErrorCode fat32_get_part_info(Disk *disk, uint64_t start_lba, Fat32PartInfo *info) {
    uint8_t bpb_sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, bpb_sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    info->bytes_per_sector = *(uint16_t*)(bpb_sector + 11);
    info->sectors_per_cluster = bpb_sector[13];
    info->reserved_sectors = *(uint16_t*)(bpb_sector + 14);
    info->num_fats = bpb_sector[16];
    info->fat_size_sectors = *(uint32_t*)(bpb_sector + 36);
    info->root_cluster = *(uint32_t*)(bpb_sector + 44);

    info->fat1_lba = start_lba + info->reserved_sectors;
    info->fat2_lba = info->fat1_lba + info->fat_size_sectors;
    info->first_data_lba = info->fat2_lba + info->fat_size_sectors; // после двух FAT

    // Вычисляем общее количество кластеров
    uint64_t total_sectors = *(uint32_t*)(bpb_sector + 32); // total_sectors_32
    uint64_t data_sectors = total_sectors - info->reserved_sectors - info->num_fats * info->fat_size_sectors;
    info->total_clusters = (uint32_t)(data_sectors / info->sectors_per_cluster);

    return ERR_OK;
}

// Поиск свободного кластера (используя info)
static ErrorCode fat32_find_free_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t *free_cluster) {
    // uint32_t fat_sectors = info->fat_size_sectors; delete
    uint32_t entries_per_sector = info->bytes_per_sector / 4; // 128
    uint32_t total_clusters = info->total_clusters;

    for (uint32_t c = 2; c < total_clusters; c++) {
        uint32_t fat_sec = c / entries_per_sector;
        uint32_t entry_off = (c % entries_per_sector) * 4;
        uint8_t sector[SECTOR_SIZE];
        ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, (info->fat1_lba + fat_sec) * SECTOR_SIZE);
        if (err != ERR_OK) return err;
        uint32_t val = *(uint32_t*)(sector + entry_off) & 0x0FFFFFFF;
        if (val == 0) {
            *free_cluster = c;
            return ERR_OK;
        }
    }
    return ERR_INVALID_VALUE; // нет свободных
}

//Запись кластера (данных)
static ErrorCode fat32_write_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, const uint8_t *data) {
    uint64_t cluster_lba = info->first_data_lba + (uint64_t)(cluster - 2) * info->sectors_per_cluster;
    size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
    return disk_write(disk, data, cluster_bytes, cluster_lba * SECTOR_SIZE);
}

// Создание записи каталога
ErrorCode fat32_create_dir(Disk *disk, uint64_t start_lba, const char *path) {
    // Разбираем путь на родительский и имя нового каталога
    char *path_copy = strdup(path);
    if (!path_copy) return ERR_GENERIC;
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        // нет слеша – создаём в корне
        last_slash = path_copy;
        // но нужно, чтобы путь начинался с '/'? Для простоты будем требовать абсолютный путь.
        free(path_copy);
        printf("Error: path must be absolute (start with /).\n");
        return ERR_INVALID_VALUE;
    }
    *last_slash = '\0';
    const char *parent_path = path_copy;
    const char *new_name = last_slash + 1;
    if (strlen(new_name) == 0) {
        free(path_copy);
        printf("Error: empty directory name.\n");
        return ERR_INVALID_VALUE;
    }

    // Получаем информацию о разделе
    Fat32PartInfo info;
    ErrorCode err = fat32_get_part_info(disk, start_lba, &info);
    if (err != ERR_OK) { free(path_copy); return err; }

    // Находим родительский каталог
    uint32_t parent_cluster;
    err = fat32_find_dir(disk, &info, parent_path, &parent_cluster);
    if (err != ERR_OK) {
        printf("Error: parent directory '%s' not found.\n", parent_path);
        free(path_copy);
        return err;
    }

    // Проверяем, нет ли уже в родительском каталоге записи с таким именем
    // (код аналогичен поиску в fat32_find_dir, но для одного имени)
    uint64_t parent_lba = info.first_data_lba + (parent_cluster - 2) * info.sectors_per_cluster;
    size_t cluster_bytes = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *parent_buf = (uint8_t*)malloc(cluster_bytes);
    if (!parent_buf) { free(path_copy); return ERR_GENERIC; }
    err = disk_read(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    if (err != ERR_OK) { free(parent_buf); free(path_copy); return err; }

    uint8_t short_name[11];
    memset(short_name, ' ', 11);
    size_t name_len = strlen(new_name);
    for (size_t i = 0; i < name_len && i < 8; i++) short_name[i] = toupper(new_name[i]);

    FAT32_DirEntry *parent_entries = (FAT32_DirEntry*)parent_buf;
    int found = 0;
    for (size_t i = 0; i < cluster_bytes / sizeof(FAT32_DirEntry); i++) {
        if (parent_entries[i].name[0] == 0x00) break;
        if (parent_entries[i].name[0] == 0xE5) continue;
        if (is_long_name_entry(&parent_entries[i])) continue;
        if (memcmp(parent_entries[i].name, short_name, 11) == 0) {
            found = 1;
            break;
        }
    }
	if (found) {
		printf("Error: '%s' already exists.\n", new_name);
		free(parent_buf);
		free(path_copy);
		return ERR_INVALID_VALUE;
	}

    // Ищем свободный кластер для нового каталога (как раньше)
    uint32_t new_cluster;
    err = fat32_find_free_cluster(disk, &info, &new_cluster);
    if (err != ERR_OK) {
        free(parent_buf);
        free(path_copy);
        printf("Error: no free clusters available.\n");
        return err;
    }

    // Инициализируем новый каталог записями "." и ".."
    err = fat32_init_dir_cluster(disk, &info, new_cluster, parent_cluster);
    if (err != ERR_OK) {
        free(parent_buf);
        free(path_copy);
        return err;
    }

    // Помечаем кластер в FAT как конец цепочки
    err = fat32_set_fat_entry(disk, info.fat1_lba, new_cluster, 0x0FFFFFFF);
    if (err != ERR_OK) { free(parent_buf); free(path_copy); return err; }
    err = fat32_set_fat_entry(disk, info.fat2_lba, new_cluster, 0x0FFFFFFF);
    if (err != ERR_OK) { free(parent_buf); free(path_copy); return err; }

    // Ищем свободное место в родительском каталоге для новой записи
    int free_index = -1;
    for (size_t i = 0; i < cluster_bytes / sizeof(FAT32_DirEntry); i++) {
        if (parent_entries[i].name[0] == 0x00 || parent_entries[i].name[0] == 0xE5) {
            free_index = i;
            break;
        }
    }
    if (free_index == -1) {
        free(parent_buf);
        free(path_copy);
        printf("Error: parent directory is full.\n");
        return ERR_INVALID_VALUE;
    }

    // Заполняем новую запись
    FAT32_DirEntry *new_entry = &parent_entries[free_index];
    memset(new_entry, 0, sizeof(FAT32_DirEntry));
    memcpy(new_entry->name, short_name, 11);
    new_entry->attr = FAT32_ATTR_DIRECTORY;
    new_entry->first_cluster_lo = new_cluster & 0xFFFF;
    new_entry->first_cluster_hi = (new_cluster >> 16) & 0xFFFF;

    // Записываем обновлённый родительский каталог
    err = disk_write(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    free(parent_buf);
    free(path_copy);
    if (err != ERR_OK) return err;

    printf("Directory '%s' created successfully.\n", path);
    return ERR_OK;
}

// Функция для освобождения цепочки кластеров
static ErrorCode fat32_free_cluster_chain(Disk *disk, const Fat32PartInfo *info, uint32_t first_cluster) {
    uint32_t current = first_cluster;
    while (current >= 2 && current < info->total_clusters + 2) {
        // Читаем значение текущего кластера из FAT
        uint32_t fat_sec = current / (info->bytes_per_sector / 4);
        uint32_t entry_off = (current % (info->bytes_per_sector / 4)) * 4;
        uint8_t sector[SECTOR_SIZE];
        ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, (info->fat1_lba + fat_sec) * SECTOR_SIZE);
        if (err != ERR_OK) return err;
        uint32_t next = *(uint32_t*)(sector + entry_off) & 0x0FFFFFFF;

        // Обнуляем в основной FAT
        uint32_t old = *(uint32_t*)(sector + entry_off);
        *(uint32_t*)(sector + entry_off) = (old & 0xF0000000); // оставляем только зарезервированные биты, обнуляем остальное
        err = disk_write(disk, sector, SECTOR_SIZE, (info->fat1_lba + fat_sec) * SECTOR_SIZE);
        if (err != ERR_OK) return err;

        // Обнуляем в резервной FAT
        err = disk_read(disk, sector, SECTOR_SIZE, (info->fat2_lba + fat_sec) * SECTOR_SIZE);
        if (err != ERR_OK) return err;
        *(uint32_t*)(sector + entry_off) = (old & 0xF0000000);
        err = disk_write(disk, sector, SECTOR_SIZE, (info->fat2_lba + fat_sec) * SECTOR_SIZE);
        if (err != ERR_OK) return err;

        if (next == 0x0FFFFFFF) break; // конец цепочки
        current = next;
    }
    return ERR_OK;
}

// Функция для инициализации кластера каталога
static ErrorCode fat32_init_dir_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, uint32_t parent_cluster) {
    size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
    uint8_t *cluster_buf = (uint8_t*)calloc(1, cluster_bytes);
    if (!cluster_buf) return ERR_GENERIC;

    FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;

    // Запись "." – текущий каталог
    memset(entries[0].name, ' ', 11);
    entries[0].name[0] = '.';
    entries[0].attr = FAT32_ATTR_DIRECTORY;
    entries[0].first_cluster_lo = cluster & 0xFFFF;
    entries[0].first_cluster_hi = (cluster >> 16) & 0xFFFF;

    // Запись ".." – родительский каталог
    memset(entries[1].name, ' ', 11);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT32_ATTR_DIRECTORY;
    entries[1].first_cluster_lo = parent_cluster & 0xFFFF;
    entries[1].first_cluster_hi = (parent_cluster >> 16) & 0xFFFF;

    // Запись кластера на диск
    uint64_t cluster_lba = info->first_data_lba + (uint64_t)(cluster - 2) * info->sectors_per_cluster;
    ErrorCode err = disk_write(disk, cluster_buf, cluster_bytes, cluster_lba * SECTOR_SIZE); // ???
    free(cluster_buf);
    return err;
}

// Функция для проверки пустоты каталога
static ErrorCode fat32_dir_is_empty(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, bool *empty) {
    uint64_t cluster_lba = info->first_data_lba + (uint64_t)(cluster - 2) * info->sectors_per_cluster;
    size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
    uint8_t *cluster_buf = (uint8_t*)malloc(cluster_bytes);
    if (!cluster_buf) return ERR_GENERIC;
    ErrorCode err = disk_read(disk, cluster_buf, cluster_bytes, cluster_lba * SECTOR_SIZE);
    if (err != ERR_OK) { free(cluster_buf); return err; }

    FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;
    // Проверяем первые две записи: должны быть "." и ".."
    if (entries[0].name[0] != '.' || entries[0].name[1] != ' ' || entries[0].name[2] != ' ') {
        // Проверим, что это действительно запись "." (имя может быть дополнено пробелами)
        // В FAT32 имя "." обычно записывается как ".          " (11 байт, точка и 10 пробелов)
        // Упрощённо: проверим, что первый байт '.', остальные пробелы или нули?
		//  - В записи "." остальные байты заполнены пробелами (0x20).
        // Для простоты просто проверим, что имя[0] == '.' и attr == DIRECTORY.
        // Не будем усложнять.
    }
    // Более надёжно: проверим, что во всём кластере кроме первых двух записей нет ненулевых записей.
    // Но записи "." и ".." могут быть не обязательно первыми, если каталог когда-то содержал файлы, а потом они были удалены.
    // В FAT32 удалённые файлы помечаются 0xE5 в первом байте имени, но они могут оставаться.
    // Поэтому пустым будем считать каталог, в котором нет ни одной активной записи, кроме "." и "..".
    int active_entries = 0;
    for (size_t i = 0; i < cluster_bytes / sizeof(FAT32_DirEntry); i++) {
        if (entries[i].name[0] == 0x00) break; // конец каталога
        if (entries[i].name[0] == 0xE5) continue; // удалённая запись
        // Проверим, не является ли эта запись "." или ".."
        if (entries[i].name[0] == '.' &&
            (entries[i].name[1] == ' ' || entries[i].name[1] == '.') &&
            entries[i].attr == FAT32_ATTR_DIRECTORY) {
            // Это "." или ".."
            continue;
        }
        active_entries++;
    }
    *empty = (active_entries == 0);
    free(cluster_buf);
    return ERR_OK;
}

static ErrorCode fat32_add_dir_entry(Disk *disk, const Fat32PartInfo *info, uint32_t parent_cluster, const char *name, uint32_t first_cluster, uint32_t file_size, uint8_t attr) {
    // Формируем имя 8.3
    uint8_t short_name[11];
    memset(short_name, ' ', 11);
    const char *dot = strchr(name, '.');
    size_t name_len = (dot) ? (size_t)(dot - name) : strlen(name);
    size_t ext_len = (dot) ? strlen(dot + 1) : 0;

    for (size_t i = 0; i < name_len && i < 8; i++) short_name[i] = toupper(name[i]);
    for (size_t i = 0; i < ext_len && i < 3; i++) short_name[8 + i] = toupper(dot[i+1]);

    // Читаем родительский кластер
    uint64_t parent_lba = info->first_data_lba + (parent_cluster - 2) * info->sectors_per_cluster;
    size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
    uint8_t *cluster_buf = (uint8_t*)malloc(cluster_bytes);
    if (!cluster_buf) return ERR_GENERIC;
    ErrorCode err = disk_read(disk, cluster_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    if (err != ERR_OK) { free(cluster_buf); return err; }

    FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;
    int free_index = -1;
    for (size_t i = 0; i < cluster_bytes / sizeof(FAT32_DirEntry); i++) {
        if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
            free_index = i;
            break;
        }
    }
    if (free_index == -1) {
        free(cluster_buf);
        return ERR_INVALID_VALUE; // нет места в родительском каталоге
    }

    FAT32_DirEntry *entry = &entries[free_index];
    memset(entry, 0, sizeof(FAT32_DirEntry));
    memcpy(entry->name, short_name, 11);
    entry->attr = attr;
    entry->first_cluster_lo = first_cluster & 0xFFFF;
    entry->first_cluster_hi = (first_cluster >> 16) & 0xFFFF;
    entry->file_size = file_size;

    err = disk_write(disk, cluster_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    free(cluster_buf);
    return err;
}

static ErrorCode fat32_find_dir(Disk *disk, const Fat32PartInfo *info, const char *path, uint32_t *dir_cluster) {
    // Начинаем с корневого кластера
    uint32_t current = info->root_cluster;
    
    // Пропускаем ведущий '/'
    if (path[0] == '/') path++;
    if (path[0] == '\0') {
        // Путь "/" – корень
        *dir_cluster = current;
        return ERR_OK;
    }

    // Копируем путь, чтобы разбить на компоненты
    char *path_copy = strdup(path);
    if (!path_copy) return ERR_GENERIC;

    char *saveptr;
    char *token = strtok_r(path_copy, "/", &saveptr);
    while (token != NULL) {
        // Читаем текущий каталог (кластер current)
        uint64_t cluster_lba = info->first_data_lba + (current - 2) * info->sectors_per_cluster;
        size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
        uint8_t *cluster_buf = (uint8_t*)malloc(cluster_bytes);
        if (!cluster_buf) { free(path_copy); return ERR_GENERIC; }
        ErrorCode err = disk_read(disk, cluster_buf, cluster_bytes, cluster_lba * SECTOR_SIZE);
        if (err != ERR_OK) { free(cluster_buf); free(path_copy); return err; }

        // Ищем запись с именем token (в формате 8.3)
        uint8_t short_name[11];
        memset(short_name, ' ', 11);
        size_t name_len = strlen(token);
        for (size_t i = 0; i < name_len && i < 8; i++) short_name[i] = toupper(token[i]);

        FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;
        int found = 0;
        uint32_t next_cluster = 0;
        for (size_t i = 0; i < cluster_bytes / sizeof(FAT32_DirEntry); i++) {
            if (entries[i].name[0] == 0x00) break;
            if (entries[i].name[0] == 0xE5) continue;
            if (is_long_name_entry(&entries[i])) continue;
            if (memcmp(entries[i].name, short_name, 11) == 0) {
                if (!(entries[i].attr & FAT32_ATTR_DIRECTORY)) {
                    free(cluster_buf);
                    free(path_copy);
                    return ERR_INVALID_VALUE; // это не каталог
                }
                next_cluster = (entries[i].first_cluster_hi << 16) | entries[i].first_cluster_lo;
                found = 1;
                break;
            }
        }
        free(cluster_buf);
        if (!found) {
            free(path_copy);
            return ERR_INVALID_VALUE; // компонент не найден
        }
        current = next_cluster;
        token = strtok_r(NULL, "/", &saveptr);
    }
    free(path_copy);
    *dir_cluster = current;
    return ERR_OK;
}
// ***

ErrorCode fat32_format(Disk *disk, uint64_t start_lba, uint64_t total_sectors, uint8_t drive_number) {
    if (total_sectors > UINT32_MAX) {
        printf("Error: FAT32 partition too large (max 2TB)\n");
        return ERR_INVALID_VALUE;
    }
    uint32_t total_sec = (uint32_t)total_sectors;

    // Выбираем размер кластера в зависимости от размера раздела
    uint8_t sectors_per_cluster;
    if (total_sec <= 512 * 1024) // 256 МБ
        sectors_per_cluster = 1;
    else if (total_sec <= 1024 * 1024) // 512 МБ
        sectors_per_cluster = 2;
    else if (total_sec <= 2048 * 1024) // 1 ГБ
        sectors_per_cluster = 4;
    else if (total_sec <= 4096 * 1024) // 2 ГБ
        sectors_per_cluster = 8;
    else if (total_sec <= 8192 * 1024) // 4 ГБ
        sectors_per_cluster = 16;
    else
        sectors_per_cluster = 32; // до 2 ТБ

    uint32_t fat_sectors = fat32_calc_fat_sectors(total_sec, sectors_per_cluster);

    // Заполняем BPB
    BPB_FAT32 bpb;
    memset(&bpb, 0, sizeof(bpb));
    bpb.jump_boot[0] = 0xEB;
    bpb.jump_boot[1] = 0x58;
    bpb.jump_boot[2] = 0x90;
    memcpy(bpb.oem_name, "MSWIN4.1", 8);
    bpb.bytes_per_sector = 512;
    bpb.sectors_per_cluster = sectors_per_cluster;
    bpb.reserved_sectors = 32;
    bpb.num_fats = 2;
    bpb.root_entries = 0;
    bpb.total_sectors_16 = 0;
    bpb.media_descriptor = 0xF8;
    bpb.fat_size_16 = 0;
    bpb.sectors_per_track = 63;
    bpb.num_heads = 255;
    bpb.hidden_sectors = (uint32_t)start_lba;
    bpb.total_sectors_32 = total_sec;
    bpb.fat_size_32 = fat_sectors;
    bpb.ext_flags = 0;
    bpb.fs_version = 0;
    bpb.root_cluster = 2;
    bpb.fs_info = 1;
    bpb.backup_boot_sector = 6;
    bpb.drive_number = drive_number;
    bpb.boot_signature = 0x29;
    // Генерируем случайный volume_id (можно использовать rand)
    srand(time(NULL));
    bpb.volume_id = (uint32_t)rand();
    memcpy(bpb.volume_label, "NO NAME    ", 11);
    memcpy(bpb.fs_type, "FAT32   ", 8);

	// Создаём буфер для целого сектора
	uint8_t boot_sector[SECTOR_SIZE];
	memset(boot_sector, 0, SECTOR_SIZE);
	memcpy(boot_sector, &bpb, sizeof(bpb));
	boot_sector[510] = 0x55;
	boot_sector[511] = 0xAA;

    // Запись BPB в первый сектор раздела
    ErrorCode err = disk_write(disk, boot_sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Запись резервной копии BPB в сектор 6 (backup boot sector)
    err = disk_write(disk, boot_sector, SECTOR_SIZE, (start_lba + 6) * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Заполнение FSInfo
    FSInfo fsi;
    memset(&fsi, 0, sizeof(fsi));
    fsi.lead_signature = 0x41615252;
    fsi.struct_signature = 0x61417272;
    fsi.free_count = 0xFFFFFFFF; // неизвестно
    fsi.next_free = 0xFFFFFFFF;
    fsi.trail_signature = 0xAA550000;

    // Запись FSInfo в сектор 1
    err = disk_write(disk, &fsi, sizeof(fsi), (start_lba + 1) * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Резервная копия FSInfo в сектор 7
    err = disk_write(disk, &fsi, sizeof(fsi), (start_lba + 7) * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Инициализация FAT (таблицы размещения файлов)
    uint32_t fat_size_bytes = fat_sectors * SECTOR_SIZE;
    uint8_t *fat_buffer = (uint8_t*)malloc(fat_size_bytes);
    if (!fat_buffer) return ERR_GENERIC;
    memset(fat_buffer, 0, fat_size_bytes);

    // Первые два кластера зарезервированы
    // Кластер 0: обычно 0x0FFFFFF8 (конец цепочки для зарезервированных)
    // Кластер 1: 0x0FFFFFFF (конец цепочки)
    // Кластер 2: 0x0FFFFFFF (корневой каталог)
    uint32_t *fat = (uint32_t*)fat_buffer;
    fat[0] = 0x0FFFFFF8; // зарезервированный кластер
    fat[1] = 0x0FFFFFFF; // конец цепочки для корневого каталога? Обычно в FAT32 первый кластер корня = 2, и в FAT[2] ставится 0x0FFFFFFF. Но по спецификации FAT[0] и FAT[1] зарезервированы.
    // Установим кластер 2 как конец цепочки
    fat[2] = 0x0FFFFFFF;

    // Запись основной FAT (начинается с сектора 32)
    err = disk_write(disk, fat_buffer, fat_size_bytes, (start_lba + 32) * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(fat_buffer);
        return err;
    }

    // Запись второй FAT (сразу за первой)
    err = disk_write(disk, fat_buffer, fat_size_bytes, (start_lba + 32 + fat_sectors) * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(fat_buffer);
        return err;
    }
    free(fat_buffer);

    // Инициализация корневого каталога (кластер 2) – заполняем нулями
    uint32_t root_sectors = sectors_per_cluster; // размер одного кластера
    uint8_t *root_buffer = (uint8_t*)calloc(1, root_sectors * SECTOR_SIZE);
    if (!root_buffer) return ERR_GENERIC;
    uint64_t root_lba = start_lba + 32 + 2 * fat_sectors; // после двух FAT
    err = disk_write(disk, root_buffer, root_sectors * SECTOR_SIZE, root_lba * SECTOR_SIZE);
    free(root_buffer);
    if (err != ERR_OK) return err;

    // Всё готово
    return ERR_OK;
}

ErrorCode fat32_list_dir(Disk *disk, uint64_t start_lba, const char *path) {
    Fat32PartInfo info;
    ErrorCode err = fat32_get_part_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    uint32_t dir_cluster;
    err = fat32_find_dir(disk, &info, path, &dir_cluster);
    if (err != ERR_OK) {
        printf("Error: path '%s' not found.\n", path);
        return err;
    }

    uint64_t cluster_lba = info.first_data_lba + (dir_cluster - 2) * info.sectors_per_cluster;
    size_t cluster_bytes = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *cluster_buf = (uint8_t*)malloc(cluster_bytes);
    if (!cluster_buf) return ERR_GENERIC;
    err = fat32_read_cluster(disk, cluster_lba, info.sectors_per_cluster, cluster_buf);
    if (err != ERR_OK) { free(cluster_buf); return err; }

    printf("Contents of %s:\n", path);
    printf("------------------------------------------------\n");
    FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;
    for (size_t i = 0; i < cluster_bytes / sizeof(FAT32_DirEntry); i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (is_long_name_entry(&entries[i])) continue;
        print_dir_entry(&entries[i]);
    }
    free(cluster_buf);
    return ERR_OK;
}

ErrorCode fat32_copy_file(Disk *disk, uint64_t start_lba, const char *host_path, const char *dest_path) {
    // Разбираем путь на родительский и имя файла
    char *path_copy = strdup(dest_path);
    if (!path_copy) return ERR_GENERIC;
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        free(path_copy);
        printf("Error: path must be absolute (start with /).\n");
        return ERR_INVALID_VALUE;
    }
    *last_slash = '\0';
    const char *parent_path = path_copy;
    const char *file_name = last_slash + 1;
    if (strlen(file_name) == 0) {
        free(path_copy);
        printf("Error: empty file name.\n");
        return ERR_INVALID_VALUE;
    }

    // Получаем информацию о разделе
    Fat32PartInfo info;
    ErrorCode err = fat32_get_part_info(disk, start_lba, &info);
    if (err != ERR_OK) { free(path_copy); return err; }

    // Находим родительский каталог
    uint32_t parent_cluster;
    err = fat32_find_dir(disk, &info, parent_path, &parent_cluster);
    if (err != ERR_OK) {
        printf("Error: parent directory '%s' not found.\n", parent_path);
        free(path_copy);
        return err;
    }

    // Проверяем, нет ли уже файла с таким именем в родительском каталоге
    uint64_t parent_lba = info.first_data_lba + (parent_cluster - 2) * info.sectors_per_cluster;
    size_t cluster_bytes = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *parent_buf = (uint8_t*)malloc(cluster_bytes);
    if (!parent_buf) { free(path_copy); return ERR_GENERIC; }
    err = disk_read(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    if (err != ERR_OK) { free(parent_buf); free(path_copy); return err; }

    uint8_t short_name[11];
    memset(short_name, ' ', 11);
    const char *dot = strchr(file_name, '.');
    size_t name_len = (dot) ? (size_t)(dot - file_name) : strlen(file_name);
    size_t ext_len = (dot) ? strlen(dot + 1) : 0;
    for (size_t i = 0; i < name_len && i < 8; i++) short_name[i] = toupper(file_name[i]);
    for (size_t i = 0; i < ext_len && i < 3; i++) short_name[8 + i] = toupper(dot[i+1]);

    FAT32_DirEntry *parent_entries = (FAT32_DirEntry*)parent_buf;
    int found = 0;
    for (size_t i = 0; i < cluster_bytes / sizeof(FAT32_DirEntry); i++) {
        if (parent_entries[i].name[0] == 0x00) break;
        if (parent_entries[i].name[0] == 0xE5) continue;
        if (is_long_name_entry(&parent_entries[i])) continue;
        if (memcmp(parent_entries[i].name, short_name, 11) == 0) {
            found = 1;
            break;
        }
    }
    free(parent_buf);
    if (found) {
        printf("Error: file '%s' already exists.\n", file_name);
        free(path_copy);
        return ERR_INVALID_VALUE;
    }

    // Открываем хост-файл
    FILE *f = fopen(host_path, "rb");
    if (!f) {
        printf("Error: cannot open host file '%s'\n", host_path);
        free(path_copy);
        return ERR_GENERIC;
    }

    // Получаем размер файла
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size == 0) {
        fclose(f);
        free(path_copy);
        printf("Warning: file is empty.\n");
        return ERR_OK; // или создать пустую запись?
    }

    // Вычисляем необходимое количество кластеров
    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint32_t needed_clusters = (file_size + cluster_size - 1) / cluster_size;

    // Выделяем первый свободный кластер
    uint32_t first_cluster;
    err = fat32_find_free_cluster(disk, &info, &first_cluster);
    if (err != ERR_OK) {
        fclose(f);
        free(path_copy);
        printf("Error: no free clusters available.\n");
        return err;
    }

    // Для каждого кластера: читаем из файла, записываем на диск, обновляем FAT
    uint32_t current_cluster = first_cluster;
    uint8_t *cluster_buf = (uint8_t*)malloc(cluster_size);
    if (!cluster_buf) {
        fclose(f);
        free(path_copy);
        return ERR_GENERIC;
    }

    for (uint32_t i = 0; i < needed_clusters; i++) {
        size_t bytes_to_read = (i == needed_clusters - 1) ? (file_size - i * cluster_size) : cluster_size;
        size_t read_bytes = fread(cluster_buf, 1, bytes_to_read, f);
        if (read_bytes != bytes_to_read) {
            free(cluster_buf);
            fclose(f);
            free(path_copy);
            printf("Error: failed to read host file.\n");
            return ERR_GENERIC;
        }
        if (bytes_to_read < cluster_size) {
            memset(cluster_buf + bytes_to_read, 0, cluster_size - bytes_to_read);
        }

        err = fat32_write_cluster(disk, &info, current_cluster, cluster_buf);
        if (err != ERR_OK) {
            free(cluster_buf);
            fclose(f);
            free(path_copy);
            return err;
        }

        uint32_t next_cluster;
        if (i == needed_clusters - 1) {
            next_cluster = 0x0FFFFFFF;
        } else {
            err = fat32_find_free_cluster(disk, &info, &next_cluster);
            if (err != ERR_OK) {
                free(cluster_buf);
                fclose(f);
                free(path_copy);
                printf("Error: not enough free clusters for whole file.\n");
                return err;
            }
        }

        err = fat32_set_fat_entry(disk, info.fat1_lba, current_cluster, next_cluster);
        if (err != ERR_OK) { free(cluster_buf); fclose(f); free(path_copy); return err; }
        err = fat32_set_fat_entry(disk, info.fat2_lba, current_cluster, next_cluster);
        if (err != ERR_OK) { free(cluster_buf); fclose(f); free(path_copy); return err; }

        current_cluster = next_cluster;
    }

    free(cluster_buf);
    fclose(f);

    // Создаём запись в родительском каталоге
    err = fat32_add_dir_entry(disk, &info, parent_cluster, file_name, first_cluster, file_size, FAT32_ATTR_ARCHIVE);
    free(path_copy);
    if (err != ERR_OK) {
        printf("Error: failed to create directory entry.\n");
        return err;
    }

    printf("File '%s' copied successfully (%ld bytes).\n", dest_path, file_size);
    return ERR_OK;
}

ErrorCode fat32_delete_file(Disk *disk, uint64_t start_lba, const char *path) {
    // Разбираем путь на родительский и имя файла
    char *path_copy = strdup(path);
    if (!path_copy) return ERR_GENERIC;
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        free(path_copy);
        printf("Error: path must be absolute (start with /).\n");
        return ERR_INVALID_VALUE;
    }
    *last_slash = '\0';
    const char *parent_path = path_copy;
    const char *file_name = last_slash + 1;
    if (strlen(file_name) == 0) {
        free(path_copy);
        printf("Error: empty file name.\n");
        return ERR_INVALID_VALUE;
    }

    Fat32PartInfo info;
    ErrorCode err = fat32_get_part_info(disk, start_lba, &info);
    if (err != ERR_OK) { free(path_copy); return err; }

    // Находим родительский каталог
    uint32_t parent_cluster;
    err = fat32_find_dir(disk, &info, parent_path, &parent_cluster);
    if (err != ERR_OK) {
        printf("Error: parent directory '%s' not found.\n", parent_path);
        free(path_copy);
        return err;
    }

    // Читаем родительский каталог
    uint64_t parent_lba = info.first_data_lba + (parent_cluster - 2) * info.sectors_per_cluster;
    size_t cluster_bytes = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *parent_buf = (uint8_t*)malloc(cluster_bytes);
    if (!parent_buf) { free(path_copy); return ERR_GENERIC; }
    err = disk_read(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    if (err != ERR_OK) { free(parent_buf); free(path_copy); return err; }

    // Ищем запись с именем file_name
    uint8_t short_name[11];
    memset(short_name, ' ', 11);
    const char *dot = strchr(file_name, '.');
    size_t name_len = (dot) ? (size_t)(dot - file_name) : strlen(file_name);
    size_t ext_len = (dot) ? strlen(dot + 1) : 0;
    for (size_t i = 0; i < name_len && i < 8; i++) short_name[i] = toupper(file_name[i]);
    for (size_t i = 0; i < ext_len && i < 3; i++) short_name[8 + i] = toupper(dot[i+1]);

    FAT32_DirEntry *parent_entries = (FAT32_DirEntry*)parent_buf;
    int found_index = -1;
    uint32_t first_cluster = 0;
    for (size_t i = 0; i < cluster_bytes / sizeof(FAT32_DirEntry); i++) {
        if (parent_entries[i].name[0] == 0x00) break;
        if (parent_entries[i].name[0] == 0xE5) continue;
        if (is_long_name_entry(&parent_entries[i])) continue;
        if (memcmp(parent_entries[i].name, short_name, 11) == 0) {
            if (parent_entries[i].attr & FAT32_ATTR_DIRECTORY) {
                printf("Error: '%s' is a directory. Use rmdir.\n", file_name);
                free(parent_buf);
                free(path_copy);
                return ERR_INVALID_VALUE;
            }
            found_index = i;
            first_cluster = (parent_entries[i].first_cluster_hi << 16) | parent_entries[i].first_cluster_lo;
            break;
        }
    }

    if (found_index == -1) {
        printf("Error: file '%s' not found.\n", path);
        free(parent_buf);
        free(path_copy);
        return ERR_INVALID_VALUE;
    }

    // Помечаем запись как удалённую
    parent_entries[found_index].name[0] = 0xE5;

    // Записываем обновлённый родительский каталог
    err = disk_write(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    free(parent_buf);
    if (err != ERR_OK) { free(path_copy); return err; }

    // Освобождаем кластеры файла
    if (first_cluster != 0) {
        err = fat32_free_cluster_chain(disk, &info, first_cluster);
        if (err != ERR_OK) { free(path_copy); return err; }
    }

    free(path_copy);
    printf("File '%s' deleted successfully.\n", path);
    return ERR_OK;
}

ErrorCode fat32_remove_dir(Disk *disk, uint64_t start_lba, const char *path) {
    // Разбираем путь на родительский и имя удаляемого каталога
    char *path_copy = strdup(path);
    if (!path_copy) return ERR_GENERIC;
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        free(path_copy);
        printf("Error: path must be absolute (start with /).\n");
        return ERR_INVALID_VALUE;
    }
    *last_slash = '\0';
    const char *parent_path = path_copy;
    const char *dir_name = last_slash + 1;
    if (strlen(dir_name) == 0) {
        free(path_copy);
        printf("Error: empty directory name.\n");
        return ERR_INVALID_VALUE;
    }

    // Получаем информацию о разделе
    Fat32PartInfo info;
    ErrorCode err = fat32_get_part_info(disk, start_lba, &info);
    if (err != ERR_OK) { free(path_copy); return err; }

    // Находим родительский каталог
    uint32_t parent_cluster;
    err = fat32_find_dir(disk, &info, parent_path, &parent_cluster);
    if (err != ERR_OK) {
        printf("Error: parent directory '%s' not found.\n", parent_path);
        free(path_copy);
        return err;
    }

    // Читаем родительский каталог
    uint64_t parent_lba = info.first_data_lba + (parent_cluster - 2) * info.sectors_per_cluster;
    size_t cluster_bytes = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *parent_buf = (uint8_t*)malloc(cluster_bytes);
    if (!parent_buf) { free(path_copy); return ERR_GENERIC; }
    err = disk_read(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    if (err != ERR_OK) { free(parent_buf); free(path_copy); return err; }

    // Ищем запись с именем dir_name в родительском каталоге
    uint8_t short_name[11];
    memset(short_name, ' ', 11);
    size_t name_len = strlen(dir_name);
    for (size_t i = 0; i < name_len && i < 8; i++) short_name[i] = toupper(dir_name[i]);

    FAT32_DirEntry *parent_entries = (FAT32_DirEntry*)parent_buf;
    int found_index = -1;
    uint32_t dir_cluster = 0;
    for (size_t i = 0; i < cluster_bytes / sizeof(FAT32_DirEntry); i++) {
        if (parent_entries[i].name[0] == 0x00) break;
        if (parent_entries[i].name[0] == 0xE5) continue;
        if (is_long_name_entry(&parent_entries[i])) continue;
        if (memcmp(parent_entries[i].name, short_name, 11) == 0) {
            if (!(parent_entries[i].attr & FAT32_ATTR_DIRECTORY)) {
                printf("Error: '%s' is not a directory.\n", dir_name);
                free(parent_buf);
                free(path_copy);
                return ERR_INVALID_VALUE;
            }
            found_index = i;
            dir_cluster = (parent_entries[i].first_cluster_hi << 16) | parent_entries[i].first_cluster_lo;
            break;
        }
    }

    if (found_index == -1) {
        printf("Error: directory '%s' not found.\n", path);
        free(parent_buf);
        free(path_copy);
        return ERR_INVALID_VALUE;
    }

    // Проверяем, пуст ли удаляемый каталог
    bool empty;
    err = fat32_dir_is_empty(disk, &info, dir_cluster, &empty);
    if (err != ERR_OK) {
        free(parent_buf);
        free(path_copy);
        return err;
    }
    if (!empty) {
        printf("Error: directory '%s' is not empty.\n", path);
        free(parent_buf);
        free(path_copy);
        return ERR_INVALID_VALUE;
    }

    // Помечаем запись в родительском каталоге как удалённую
    parent_entries[found_index].name[0] = 0xE5;

    // Записываем обновлённый родительский каталог
    err = disk_write(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    free(parent_buf);
    if (err != ERR_OK) { free(path_copy); return err; }

    // Освобождаем кластеры удаляемого каталога
    err = fat32_free_cluster_chain(disk, &info, dir_cluster);
    free(path_copy);
    if (err != ERR_OK) return err;

    printf("Directory '%s' removed successfully.\n", path);
    return ERR_OK;
}