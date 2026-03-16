#include "fat32_commands.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>

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
    uint32_t lead_signature;
    uint8_t  reserved1[480];
    uint32_t struct_signature;
    uint32_t free_count;
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_signature;
} FSInfo;
#pragma pack(pop)

// Вспомогательные функции
static uint32_t fat32_calc_fat_sectors(uint32_t total_sectors, uint32_t sectors_per_cluster);
static void fat32_format_name(const uint8_t name[11], char *out, size_t out_size);
static int is_long_name_entry(const FAT32_DirEntry *entry);
static void print_dir_entry(const FAT32_DirEntry *entry);
static ErrorCode fat32_read_cluster(Disk *disk, uint64_t cluster_lba, uint32_t sectors_per_cluster, uint8_t *buffer);
static ErrorCode fat32_read_fat_sector(Disk *disk, uint64_t fat_lba, uint32_t sector_num, uint8_t *buffer);
static ErrorCode fat32_write_fat_sector(Disk *disk, uint64_t fat_lba, uint32_t sector_num, const uint8_t *buffer);
static ErrorCode fat32_set_fat_entry(Disk *disk, uint64_t fat_lba, uint32_t cluster, uint32_t value);
static ErrorCode fat32_find_free_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t *free_cluster);
static ErrorCode fat32_write_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, const uint8_t *data);
static ErrorCode fat32_free_cluster_chain(Disk *disk, const Fat32PartInfo *info, uint32_t first_cluster);
static ErrorCode fat32_init_dir_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, uint32_t parent_cluster);
static ErrorCode fat32_dir_is_empty(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, bool *empty);

// Преобразование UTF-8 символа в UTF-16LE (упрощённо)
static uint16_t utf8_to_utf16le(const char **str) {
    // Для простоты поддерживаем только ASCII
    uint8_t c = (uint8_t)**str;
    (*str)++;
    return (c < 0x80) ? c : '?';
}

// Генерация короткого имени (8.3) из длинного
int lfn_generate_short_name(const char *long_name, uint8_t *short_name) {
    // Инициализируем пробелами
    memset(short_name, ' ', 11);

    // Разделяем на имя и расширение
    const char *dot = strrchr(long_name, '.');
    const char *name_part = long_name;
    const char *ext_part = NULL;
    size_t name_len, ext_len = 0;

    if (dot && dot > long_name) {
        name_len = dot - long_name;
        ext_part = dot + 1;
        ext_len = strlen(ext_part);
    } else {
        name_len = strlen(long_name);
    }

    // Основное имя (до 8 символов)
    for (size_t i = 0; i < name_len && i < 8; i++) {
        short_name[i] = toupper((unsigned char)name_part[i]);
    }

    // Расширение (до 3 символов)
    for (size_t i = 0; i < ext_len && i < 3; i++) {
        short_name[8 + i] = toupper((unsigned char)ext_part[i]);
    }

    // TODO: Проверка на коллизию и добавление ~1, ~2 и т.д.
    // Пока просто возвращаем как есть.

    return 0;
}

// Вычисление контрольной суммы для короткого имени (8.3)
static uint8_t lfn_checksum(const uint8_t *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_name[i];
    }
    return sum;
}

// Исправленная функция вычисления FAT: учитываем две зарезервированные записи
static uint32_t fat32_calc_fat_sectors(uint32_t total_sectors, uint32_t sectors_per_cluster) {
    uint32_t data_sectors = total_sectors - 32;
    uint32_t clusters = data_sectors / sectors_per_cluster;
    // FAT содержит записи для каждого кластера данных плюс две зарезервированные
    uint32_t fat_bytes = (clusters + 2) * 4;
    uint32_t fat_sectors = (fat_bytes + 511) / 512;
    return fat_sectors;
}

static void fat32_format_name(const uint8_t name[11], char *out, size_t out_size) {
    char name_part[9] = {0};
    char ext_part[4] = {0};
    int i;

    for (i = 0; i < 8 && name[i] && name[i] != ' '; i++) {
        name_part[i] = name[i];
    }
    name_part[i] = '\0';

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

static int is_long_name_entry(const FAT32_DirEntry *entry) {
    return (entry->attr == FAT32_ATTR_LONG_NAME);
}

// Сборка длинного имени из цепочки LFN-записей, расположенных перед основной записью.
// Параметры:
//   disk         - открытый диск
//   info         - информация о FAT32-разделе
//   dir_cluster  - кластер каталога, где находится основная запись
//   entry_index  - индекс основной записи в кластере (смещение в байтах)
//   long_name    - буфер для длинного имени (минимум 256 * 2 + 1 байт)
//   max_len      - размер буфера в символах (не байтах)
// Возвращает ERR_OK, если имя успешно собрано, иначе код ошибки.
static ErrorCode lfn_collect_name(Disk *disk, const Fat32PartInfo *info, 
                                   uint32_t dir_cluster, size_t entry_index,
                                   char *long_name, size_t max_len) {
    // Определяем LBA каталога
    uint64_t cluster_lba = info->first_data_lba + (uint64_t)(dir_cluster - 2) * info->sectors_per_cluster;
    size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
    uint8_t *cluster_buf = (uint8_t*)malloc(cluster_bytes);
    if (!cluster_buf) return ERR_GENERIC;

    ErrorCode err = disk_read(disk, cluster_buf, cluster_bytes, cluster_lba * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(cluster_buf);
        return err;
    }

    FAT32_DirEntry *main_entry = (FAT32_DirEntry*)(cluster_buf + entry_index);
    uint8_t expected_checksum = lfn_checksum(main_entry->name);

    // Сборка имени: идём назад от основной записи
    // LFN-записи идут в обратном порядке: последняя часть имени (order & 0x3F = 1) — самая близкая к основной.
    // Порядковые номера: 1,2,3,...; у последней записи установлен старший бит (0x40).
    uint16_t utf16_name[256]; // максимум 255 символов (LFN поддерживает до 255)
    int utf16_len = 0;
    int expected_order = 0; // будем ожидать сначала порядковый номер 1 (после обработки последней)

    // Проходим в обратном порядке, начиная с entry_index-1, пока не встретим запись с attr=0x0F
    int lfn_found = 0;
    int i = entry_index / sizeof(FAT32_DirEntry) - 1; // индекс записи как элемента массива
    while (i >= 0) {
        FAT32_DirEntry *e = (FAT32_DirEntry*)(cluster_buf + i * sizeof(FAT32_DirEntry));
        if (e->name[0] == 0x00) break; // конец каталога, дальше нет записей
        if (e->attr != FAT32_ATTR_LONG_NAME) break; // не LFN, значит цепочка закончилась

        FAT32_LongDirEntry *lfn = (FAT32_LongDirEntry*)e;
        if (lfn->checksum != expected_checksum) {
            // Контрольная сумма не совпадает — это не наш LFN, прерываем
            break;
        }

        uint8_t order = lfn->order & 0x3F; // младшие 6 бит
        int is_last = (lfn->order & 0x40) != 0;

        if (is_last) {
            // Это последняя запись в цепочке (содержит первую часть имени)
            expected_order = order; // ожидаем, что дальше пойдут order-1, order-2...
            lfn_found = 1;
        }

        if (!lfn_found || order != expected_order) {
            // Нарушение порядка — прерываем
            break;
        }

        // Копируем имя из этой записи
        // name1[5], name2[6], name3[2] — всего 13 символов
        uint16_t *name_parts[] = { lfn->name1, lfn->name2, lfn->name3 };
        int part_sizes[] = { 5, 6, 2 };
        for (int part = 0; part < 3; part++) {
            for (int j = 0; j < part_sizes[part]; j++) {
                utf16_name[utf16_len++] = name_parts[part][j];
            }
        }

        expected_order--;
        i--;
    }

    free(cluster_buf);

    if (!lfn_found) {
        return ERR_INVALID_VALUE; // нет длинного имени
    }

    // Преобразуем UTF-16LE в UTF-8 (упрощённо: только ASCII)
    // В реальной программе лучше использовать iconv или аналоги.
    size_t out_pos = 0;
    for (int j = 0; j < utf16_len && out_pos < max_len - 1; j++) {
        uint16_t wc = utf16_name[j];
        if (wc < 0x80) {
            long_name[out_pos++] = (char)wc;
        } else {
            long_name[out_pos++] = '?'; // не-ASCII символ
        }
    }
    long_name[out_pos] = '\0';
    return ERR_OK;
}

static void print_dir_entry(const FAT32_DirEntry *entry) {
    if (entry->name[0] == 0x00) return;
    if (entry->name[0] == 0xE5) return;
    if (is_long_name_entry(entry)) return;

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

static ErrorCode fat32_read_fat_sector(Disk *disk, uint64_t fat_lba, uint32_t sector_num, uint8_t *buffer) {
    return disk_read(disk, buffer, SECTOR_SIZE, (fat_lba + sector_num) * SECTOR_SIZE);
}

static ErrorCode fat32_write_fat_sector(Disk *disk, uint64_t fat_lba, uint32_t sector_num, const uint8_t *buffer) {
    return disk_write(disk, buffer, SECTOR_SIZE, (fat_lba + sector_num) * SECTOR_SIZE);
}

static ErrorCode fat32_set_fat_entry(Disk *disk, uint64_t fat_lba, uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_num = fat_offset / SECTOR_SIZE;
    uint32_t byte_offset = fat_offset % SECTOR_SIZE;
    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = fat32_read_fat_sector(disk, fat_lba, sector_num, sector);
    if (err != ERR_OK) return err;
    uint32_t old = *(uint32_t*)(sector + byte_offset);
    *(uint32_t*)(sector + byte_offset) = (old & 0xF0000000) | (value & 0x0FFFFFFF);
    return fat32_write_fat_sector(disk, fat_lba, sector_num, sector);
}

// Исправленный поиск свободного кластера: теперь до total_clusters + 2
static ErrorCode fat32_find_free_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t *free_cluster) {
    uint32_t entries_per_sector = info->bytes_per_sector / 4;
    uint32_t total_entries = info->total_clusters + 2;  // всего записей в FAT

    for (uint32_t c = 2; c < total_entries; c++) {
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
    return ERR_INVALID_VALUE;
}

static ErrorCode fat32_write_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, const uint8_t *data) {
    uint64_t cluster_lba = info->first_data_lba + (uint64_t)(cluster - 2) * info->sectors_per_cluster;
    size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
    return disk_write(disk, data, cluster_bytes, cluster_lba * SECTOR_SIZE);
}

static ErrorCode fat32_free_cluster_chain(Disk *disk, const Fat32PartInfo *info, uint32_t first_cluster) {
    uint32_t current = first_cluster;
    while (current >= 2 && current < info->total_clusters + 2) {
        uint32_t fat_sec = current / (info->bytes_per_sector / 4);
        uint32_t entry_off = (current % (info->bytes_per_sector / 4)) * 4;
        uint8_t sector[SECTOR_SIZE];
        ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, (info->fat1_lba + fat_sec) * SECTOR_SIZE);
        if (err != ERR_OK) return err;
        uint32_t next = *(uint32_t*)(sector + entry_off) & 0x0FFFFFFF;

        uint32_t old = *(uint32_t*)(sector + entry_off);
        *(uint32_t*)(sector + entry_off) = (old & 0xF0000000);
        err = disk_write(disk, sector, SECTOR_SIZE, (info->fat1_lba + fat_sec) * SECTOR_SIZE);
        if (err != ERR_OK) return err;

        err = disk_read(disk, sector, SECTOR_SIZE, (info->fat2_lba + fat_sec) * SECTOR_SIZE);
        if (err != ERR_OK) return err;
        *(uint32_t*)(sector + entry_off) = (old & 0xF0000000);
        err = disk_write(disk, sector, SECTOR_SIZE, (info->fat2_lba + fat_sec) * SECTOR_SIZE);
        if (err != ERR_OK) return err;

        if (next == 0x0FFFFFFF) break;
        current = next;
    }
    return ERR_OK;
}

static ErrorCode fat32_init_dir_cluster(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, uint32_t parent_cluster) {
    size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
    uint8_t *cluster_buf = (uint8_t*)calloc(1, cluster_bytes);
    if (!cluster_buf) return ERR_GENERIC;

    FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;
    memset(entries[0].name, ' ', 11);
    entries[0].name[0] = '.';
    entries[0].attr = FAT32_ATTR_DIRECTORY;
    entries[0].first_cluster_lo = cluster & 0xFFFF;
    entries[0].first_cluster_hi = (cluster >> 16) & 0xFFFF;

    memset(entries[1].name, ' ', 11);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT32_ATTR_DIRECTORY;
    entries[1].first_cluster_lo = parent_cluster & 0xFFFF;
    entries[1].first_cluster_hi = (parent_cluster >> 16) & 0xFFFF;

    uint64_t cluster_lba = info->first_data_lba + (uint64_t)(cluster - 2) * info->sectors_per_cluster;
    ErrorCode err = disk_write(disk, cluster_buf, cluster_bytes, cluster_lba * SECTOR_SIZE);
    free(cluster_buf);
    return err;
}

static ErrorCode fat32_dir_is_empty(Disk *disk, const Fat32PartInfo *info, uint32_t cluster, bool *empty) {
    uint64_t cluster_lba = info->first_data_lba + (uint64_t)(cluster - 2) * info->sectors_per_cluster;
    size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
    uint8_t *cluster_buf = (uint8_t*)malloc(cluster_bytes);
    if (!cluster_buf) return ERR_GENERIC;
    ErrorCode err = disk_read(disk, cluster_buf, cluster_bytes, cluster_lba * SECTOR_SIZE);
    if (err != ERR_OK) { free(cluster_buf); return err; }

    FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;
    int active_entries = 0;
    for (size_t i = 0; i < cluster_bytes / sizeof(FAT32_DirEntry); i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (entries[i].name[0] == '.' && (entries[i].name[1] == ' ' || entries[i].name[1] == '.') &&
            entries[i].attr == FAT32_ATTR_DIRECTORY) {
            continue;
        }
        active_entries++;
    }
    *empty = (active_entries == 0);
    free(cluster_buf);
    return ERR_OK;
}

static ErrorCode fat32_add_dir_entry(Disk *disk, const Fat32PartInfo *info,
                                     uint32_t parent_cluster, const char *name,
                                     uint32_t first_cluster, uint32_t file_size,
                                     uint8_t attr) {
    // Генерируем короткое имя (8.3) из любого имени
    uint8_t short_name[11];
    lfn_generate_short_name(name, short_name);

    // Определяем, является ли имя длинным (требует LFN)
    int is_long = 0;
    size_t len = strlen(name);
    // Если имя длиннее 12 символов (8.3 + точка) или содержит символы вне допустимого набора для 8.3
    if (len > 12) {
        is_long = 1;
    } else {
        for (size_t i = 0; i < len; i++) {
            char c = name[i];
            if (c == '.') continue; // точка разрешена
            // Допустимые символы: буквы, цифры, '_', '-', возможно '$', '%', etc.
            // Упростим: если не буква/цифра и не '_' и не '-', считаем длинным
            if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
                is_long = 1;
                break;
            }
        }
    }

    if (is_long) {
        // Используем LFN
        return lfn_write_entries(disk, info, parent_cluster, name, short_name,
                                 first_cluster, file_size, attr);
    } else {
        // Короткое имя: записываем только основную запись
        // Читаем кластер родительского каталога
        uint64_t parent_lba = info->first_data_lba + (parent_cluster - 2) * info->sectors_per_cluster;
        size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
        uint8_t *cluster_buf = (uint8_t*)malloc(cluster_bytes);
        if (!cluster_buf) return ERR_GENERIC;

        ErrorCode err = disk_read(disk, cluster_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
        if (err != ERR_OK) {
            free(cluster_buf);
            return err;
        }

        FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;
        int max_entries = cluster_bytes / sizeof(FAT32_DirEntry);
        int free_index = -1;

        for (int i = 0; i < max_entries; i++) {
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                free_index = i;
                break;
            }
        }

        if (free_index == -1) {
            free(cluster_buf);
            return ERR_INVALID_VALUE; // нет места
        }

        // Заполняем запись
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
}

// ----------------------------------------------------------------------
// Публичные функции
// ----------------------------------------------------------------------

// Публичная функция: выделить и зарезервировать кластер
uint32_t fat32_alloc_cluster(Disk *disk, const Fat32PartInfo *info) {
    uint32_t cluster;
    ErrorCode err = fat32_find_free_cluster(disk, info, &cluster);
    if (err != ERR_OK) {
        return 0;
    }

    err = fat32_set_fat_entry(disk, info->fat1_lba, cluster, 0x0FFFFFFF);
    if (err != ERR_OK) {
        return 0;
    }
    err = fat32_set_fat_entry(disk, info->fat2_lba, cluster, 0x0FFFFFFF);
    if (err != ERR_OK) {
        // При ошибке пытаемся откатить первую FAT
        fat32_set_fat_entry(disk, info->fat1_lba, cluster, 0);
        return 0;
    }
    return cluster;
}

// Публичная функция: записать кластер в зарезервированную область BPB (смещение 52)
ErrorCode fat32_write_reserved_cluster(Disk *disk, uint64_t start_lba, uint32_t cluster) {
    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    sector[52] = cluster & 0xFF;
    sector[53] = (cluster >> 8) & 0xFF;
    sector[54] = (cluster >> 16) & 0xFF;
    sector[55] = (cluster >> 24) & 0xFF;

    err = disk_write(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Записать также в резервную копию BPB (сектор 6)
    err = disk_write(disk, sector, SECTOR_SIZE, (start_lba + 6) * SECTOR_SIZE);
    return err;
}

// Запись цепочки LFN-записей для длинного имени
ErrorCode lfn_write_entries(Disk *disk, const Fat32PartInfo *info,
                            uint32_t dir_cluster, const char *long_name,
                            const uint8_t *short_name, uint32_t first_cluster,
                            uint32_t file_size, uint8_t attr) {
    // Вычисляем длину имени в символах UTF-16
    int name_len = 0;
    const char *p = long_name;
    while (*p) {
        // Для простоты считаем, что каждый символ ASCII занимает 1 позицию
        name_len++;
        p++;
    }

    // Количество необходимых LFN-записей (13 символов на запись)
    int num_lfn = (name_len + 12) / 13;
    if (num_lfn == 0) num_lfn = 1; // минимум одна запись для длинного имени

    // Вычисляем контрольную сумму короткого имени
    uint8_t checksum = lfn_checksum(short_name);

    // Читаем текущий кластер каталога (родительский)
    uint64_t dir_lba = info->first_data_lba + (dir_cluster - 2) * info->sectors_per_cluster;
    size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
    uint8_t *cluster_buf = (uint8_t*)malloc(cluster_bytes);
    if (!cluster_buf) return ERR_GENERIC;

    ErrorCode err = disk_read(disk, cluster_buf, cluster_bytes, dir_lba * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(cluster_buf);
        return err;
    }

    // Ищем свободное место для num_lfn + 1 записей
    FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;
    int max_entries = cluster_bytes / sizeof(FAT32_DirEntry);
    int free_index = -1;
    int free_count = 0;
    for (int i = 0; i < max_entries; i++) {
        if (entries[i].name[0] == 0x00) {
            // Конец каталога, всё остальное свободно
            free_index = i;
            break;
        }
        if (entries[i].name[0] == 0xE5) {
            // Удалённая запись, можно использовать
            free_count++;
            if (free_count >= num_lfn + 1) {
                free_index = i - (num_lfn + 1) + 1;
                break;
            }
        } else {
            free_count = 0;
        }
    }

    if (free_index == -1) {
        // Нет места в текущем кластере – нужно расширять каталог (не реализовано)
        free(cluster_buf);
        printf("Error: directory full, cannot add LFN entries.\n");
        return ERR_INVALID_VALUE;
    }

    // Заполняем LFN-записи (они идут перед основной записью)
    int lfn_start = free_index;
    int main_index = free_index + num_lfn;

    // Проходим по LFN-записям от последней к первой (порядок в цепочке обратный)
    for (int i = 0; i < num_lfn; i++) {
        FAT32_LongDirEntry *lfn = (FAT32_LongDirEntry*)&entries[lfn_start + i];
        memset(lfn, 0, sizeof(FAT32_LongDirEntry));

        // Порядковый номер: от 1 до num_lfn, у последней (в цепочке) старший бит = 0x40
        // В цепочке первая запись (ближайшая к основной) имеет номер 1, последняя (дальняя) имеет номер num_lfn с флагом LAST.
        // Но в FAT32 LFN-записи располагаются в обратном порядке: сначала последняя часть имени (самая дальняя от основной), потом предпоследняя и т.д.
        // То есть в нашем массиве entries[lfn_start] будет самой дальней (содержит первые символы имени).
        // Правильно: для имени "ABCDEFGHIJKLM" (13 символов) одна LFN-запись содержит все символы, order=0x41 (последняя).
        // Если символов больше, то первая LFN-запись (самая дальняя) содержит символы 1-13, order=0x01? Нет, спецификация:
        // LFN-записи располагаются в обратном порядке: запись с order=1 находится непосредственно перед основной записью,
        // запись с order=2 перед ней и т.д., а запись с order=N (последняя) имеет установленный бит 0x40 и содержит первую часть имени.
        // То есть при записи мы должны идти от конца имени к началу.
        int order = num_lfn - i; // для i=0 (самая дальняя) order = num_lfn
        if (i == 0) {
            order |= 0x40; // последняя в цепочке (содержит первую часть имени)
        }

        lfn->order = (uint8_t)order;
        lfn->attr = FAT32_ATTR_LONG_NAME;
        lfn->type = 0;
        lfn->checksum = checksum;
        lfn->first_cluster = 0;

        // Заполняем имя из long_name, начиная с нужной позиции
        int start_pos = (num_lfn - i - 1) * 13; // для i=0 (самая дальняя) start_pos = 0 (первые символы)
        int pos = start_pos;
        uint16_t *name_fields[] = { lfn->name1, lfn->name2, lfn->name3 };
        int field_sizes[] = { 5, 6, 2 };
        int field_idx = 0;
        int char_idx = 0;
        const char *np = long_name;
        // Пропускаем до start_pos
        for (int k = 0; k < start_pos; k++) {
            if (!*np) break;
            utf8_to_utf16le(&np);
        }
        // Заполняем поля
        for (int f = 0; f < 3; f++) {
            for (int c = 0; c < field_sizes[f]; c++) {
                if (pos < name_len && *np) {
                    name_fields[f][c] = utf8_to_utf16le(&np);
                    pos++;
                } else {
                    name_fields[f][c] = 0xFFFF; // признак конца
                }
            }
        }
    }

    // Заполняем основную запись
    FAT32_DirEntry *main_entry = &entries[main_index];
    memset(main_entry, 0, sizeof(FAT32_DirEntry));
    memcpy(main_entry->name, short_name, 11);
    main_entry->attr = attr;
    main_entry->first_cluster_lo = first_cluster & 0xFFFF;
    main_entry->first_cluster_hi = (first_cluster >> 16) & 0xFFFF;
    main_entry->file_size = file_size;

    // Записываем обратно в каталог
    err = disk_write(disk, cluster_buf, cluster_bytes, dir_lba * SECTOR_SIZE);
    free(cluster_buf);
    return err;
}

// Удаление цепочки LFN-записей по смещению основной записи
ErrorCode lfn_remove_entries(Disk *disk, const Fat32PartInfo *info,
                             uint32_t dir_cluster, size_t entry_offset) {
    uint64_t dir_lba = info->first_data_lba + (dir_cluster - 2) * info->sectors_per_cluster;
    size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
    uint8_t *cluster_buf = (uint8_t*)malloc(cluster_bytes);
    if (!cluster_buf) return ERR_GENERIC;

    ErrorCode err = disk_read(disk, cluster_buf, cluster_bytes, dir_lba * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(cluster_buf);
        return err;
    }

    FAT32_DirEntry *main_entry = (FAT32_DirEntry*)(cluster_buf + entry_offset);
    uint8_t checksum = lfn_checksum(main_entry->name);

    // Идём назад от основной записи
    int entry_index = entry_offset / sizeof(FAT32_DirEntry);
    int i = entry_index - 1;
    while (i >= 0) {
        FAT32_DirEntry *e = (FAT32_DirEntry*)(cluster_buf + i * sizeof(FAT32_DirEntry));
        if (e->name[0] == 0x00) break; // конец каталога
        if (e->attr != FAT32_ATTR_LONG_NAME) break; // не LFN

        FAT32_LongDirEntry *lfn = (FAT32_LongDirEntry*)e;
        if (lfn->checksum != checksum) break; // не наш LFN

        // Помечаем как удалённую
        e->name[0] = 0xE5;
        i--;
    }

    err = disk_write(disk, cluster_buf, cluster_bytes, dir_lba * SECTOR_SIZE);
    free(cluster_buf);
    return err;
}

ErrorCode fat32_format(Disk *disk, uint64_t start_lba, uint64_t total_sectors, uint8_t drive_number) {
    if (total_sectors > UINT32_MAX) {
        printf("Error: FAT32 partition too large (max 2TB)\n");
        return ERR_INVALID_VALUE;
    }
    uint32_t total_sec = (uint32_t)total_sectors;

    uint8_t sectors_per_cluster;
    if (total_sec <= 512 * 1024) sectors_per_cluster = 1;
    else if (total_sec <= 1024 * 1024) sectors_per_cluster = 2;
    else if (total_sec <= 2048 * 1024) sectors_per_cluster = 4;
    else if (total_sec <= 4096 * 1024) sectors_per_cluster = 8;
    else if (total_sec <= 8192 * 1024) sectors_per_cluster = 16;
    else sectors_per_cluster = 32;

    uint32_t fat_sectors = fat32_calc_fat_sectors(total_sec, sectors_per_cluster);

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
    bpb.volume_id = (uint32_t)rand();
    memcpy(bpb.volume_label, "NO NAME    ", 11);
    memcpy(bpb.fs_type, "FAT32   ", 8);

    uint8_t boot_sector[SECTOR_SIZE];
    memset(boot_sector, 0, SECTOR_SIZE);
    memcpy(boot_sector, &bpb, sizeof(bpb));
    boot_sector[510] = 0x55;
    boot_sector[511] = 0xAA;

    ErrorCode err = disk_write(disk, boot_sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;
    err = disk_write(disk, boot_sector, SECTOR_SIZE, (start_lba + 6) * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    FSInfo fsi;
    memset(&fsi, 0, sizeof(fsi));
    fsi.lead_signature = 0x41615252;
    fsi.struct_signature = 0x61417272;
    fsi.free_count = 0xFFFFFFFF;
    fsi.next_free = 0xFFFFFFFF;
    fsi.trail_signature = 0xAA550000;

    err = disk_write(disk, &fsi, sizeof(fsi), (start_lba + 1) * SECTOR_SIZE);
    if (err != ERR_OK) return err;
    err = disk_write(disk, &fsi, sizeof(fsi), (start_lba + 7) * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    uint32_t fat_size_bytes = fat_sectors * SECTOR_SIZE;
    uint8_t *fat_buffer = (uint8_t*)malloc(fat_size_bytes);
    if (!fat_buffer) return ERR_GENERIC;
    memset(fat_buffer, 0, fat_size_bytes);
    uint32_t *fat = (uint32_t*)fat_buffer;
    fat[0] = 0x0FFFFFF8;
    fat[1] = 0x0FFFFFFF;
    fat[2] = 0x0FFFFFFF;

    err = disk_write(disk, fat_buffer, fat_size_bytes, (start_lba + 32) * SECTOR_SIZE);
    if (err != ERR_OK) { free(fat_buffer); return err; }
    err = disk_write(disk, fat_buffer, fat_size_bytes, (start_lba + 32 + fat_sectors) * SECTOR_SIZE);
    free(fat_buffer);
    if (err != ERR_OK) return err;

    uint32_t root_sectors = sectors_per_cluster;
    uint8_t *root_buffer = (uint8_t*)calloc(1, root_sectors * SECTOR_SIZE);
    if (!root_buffer) return ERR_GENERIC;
    uint64_t root_lba = start_lba + 32 + 2 * fat_sectors;
    err = disk_write(disk, root_buffer, root_sectors * SECTOR_SIZE, root_lba * SECTOR_SIZE);
    free(root_buffer);
    return err;
}

ErrorCode fat32_get_part_info(Disk *disk, uint64_t start_lba, Fat32PartInfo *info) {
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
    info->first_data_lba = info->fat2_lba + info->fat_size_sectors;

    uint64_t total_sectors = *(uint32_t*)(bpb_sector + 32);
    uint64_t data_sectors = total_sectors - info->reserved_sectors - info->num_fats * info->fat_size_sectors;
    info->total_clusters = (uint32_t)(data_sectors / info->sectors_per_cluster);

    return ERR_OK;
}

ErrorCode fat32_find_dir(Disk *disk, const Fat32PartInfo *info, const char *path, uint32_t *dir_cluster) {
    uint32_t current = info->root_cluster;

    if (path[0] == '/') path++;
    if (path[0] == '\0') {
        *dir_cluster = current;
        return ERR_OK;
    }

    char *path_copy = strdup(path);
    if (!path_copy) return ERR_GENERIC;

    char *saveptr;
    char *token = strtok_r(path_copy, "/", &saveptr);
    while (token != NULL) {
        uint64_t cluster_lba = info->first_data_lba + (current - 2) * info->sectors_per_cluster;
        size_t cluster_bytes = info->sectors_per_cluster * info->bytes_per_sector;
        uint8_t *cluster_buf = (uint8_t*)malloc(cluster_bytes);
        if (!cluster_buf) { free(path_copy); return ERR_GENERIC; }
        ErrorCode err = disk_read(disk, cluster_buf, cluster_bytes, cluster_lba * SECTOR_SIZE);
        if (err != ERR_OK) { free(cluster_buf); free(path_copy); return err; }

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
                    return ERR_INVALID_VALUE;
                }
                next_cluster = (entries[i].first_cluster_hi << 16) | entries[i].first_cluster_lo;
                found = 1;
                break;
            }
        }
        free(cluster_buf);
        if (!found) {
            free(path_copy);
            return ERR_INVALID_VALUE;
        }
        current = next_cluster;
        token = strtok_r(NULL, "/", &saveptr);
    }
    free(path_copy);
    *dir_cluster = current;
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
    if (err != ERR_OK) {
        free(cluster_buf);
        return err;
    }

    printf("Contents of %s:\n", path);
    printf("------------------------------------------------\n");

    FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;
    size_t max_entries = cluster_bytes / sizeof(FAT32_DirEntry);

    for (size_t i = 0; i < max_entries; i++) {
        if (entries[i].name[0] == 0x00) break; // конец каталога
        if (entries[i].name[0] == 0xE5) continue; // удалённая запись
        if (is_long_name_entry(&entries[i])) continue; // пропускаем сами LFN-записи

        // Это основная запись (файл/каталог)
        // Пытаемся получить длинное имя
        char long_name[512] = {0}; // достаточно для 255 UTF-16 символов
        ErrorCode lfn_err = lfn_collect_name(disk, &info, dir_cluster, i * sizeof(FAT32_DirEntry), long_name, sizeof(long_name)/sizeof(long_name[0]));

        char display_name[13];
        fat32_format_name(entries[i].name, display_name, sizeof(display_name));

        if (lfn_err == ERR_OK) {
            // Есть длинное имя
            if (entries[i].attr & FAT32_ATTR_DIRECTORY) {
                printf("DIR  %s  [%s]\n", long_name, display_name);
            } else if (entries[i].attr & FAT32_ATTR_VOLUME_ID) {
                printf("VOL  %s  (size: %u bytes) [%s]\n", long_name, entries[i].file_size, display_name);
            } else {
                printf("FILE %s  (size: %u bytes) [%s]\n", long_name, entries[i].file_size, display_name);
            }
        } else {
            // Только короткое имя
            if (entries[i].attr & FAT32_ATTR_DIRECTORY) {
                printf("DIR  %s\n", display_name);
            } else if (entries[i].attr & FAT32_ATTR_VOLUME_ID) {
                printf("VOL  %s  (size: %u bytes)\n", display_name, entries[i].file_size);
            } else {
                printf("FILE %s  (size: %u bytes)\n", display_name, entries[i].file_size);
            }
        }
    }

    free(cluster_buf);
    return ERR_OK;
}

ErrorCode fat32_copy_file(Disk *disk, uint64_t start_lba, const char *host_path, const char *dest_path) {
    // Разбираем путь на родительский каталог и имя файла
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

    // Получаем информацию о FAT32-разделе
    Fat32PartInfo info;
    ErrorCode err = fat32_get_part_info(disk, start_lba, &info);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    // Находим кластер родительского каталога
    uint32_t parent_cluster;
    err = fat32_find_dir(disk, &info, parent_path, &parent_cluster);
    if (err != ERR_OK) {
        printf("Error: parent directory '%s' not found.\n", parent_path);
        free(path_copy);
        return err;
    }

    // Проверяем, существует ли уже файл с таким именем (по короткому имени)
    uint64_t parent_lba = info.first_data_lba + (parent_cluster - 2) * info.sectors_per_cluster;
    size_t cluster_bytes = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *parent_buf = (uint8_t*)malloc(cluster_bytes);
    if (!parent_buf) {
        free(path_copy);
        return ERR_GENERIC;
    }

    err = disk_read(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(parent_buf);
        free(path_copy);
        return err;
    }

    // Генерируем короткое имя для проверки существования
    uint8_t short_name[11];
    lfn_generate_short_name(file_name, short_name);

    FAT32_DirEntry *parent_entries = (FAT32_DirEntry*)parent_buf;
    int found = 0;
    size_t max_entries = cluster_bytes / sizeof(FAT32_DirEntry);
    for (size_t i = 0; i < max_entries; i++) {
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

    // Получаем размер файла (64 бита)
    fseek64(f, 0, SEEK_END);
    int64_t file_size_s64 = ftell64(f);
    fseek64(f, 0, SEEK_SET);
    if (file_size_s64 < 0) {
        fclose(f);
        free(path_copy);
        printf("Error: cannot determine file size.\n");
        return ERR_GENERIC;
    }
    uint64_t file_size = (uint64_t)file_size_s64;

    // Вычисляем необходимое количество кластеров
    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;
    uint32_t needed_clusters = (file_size + cluster_size - 1) / cluster_size;

    // Буфер для чтения/записи одного кластера
    uint8_t *cluster_buf = (uint8_t*)malloc(cluster_size);
    if (!cluster_buf) {
        fclose(f);
        free(path_copy);
        return ERR_GENERIC;
    }

    // Выделяем первый кластер
    uint32_t first_cluster = fat32_alloc_cluster(disk, &info);
    if (first_cluster == 0) {
        printf("Error: no free clusters available.\n");
        free(cluster_buf);
        fclose(f);
        free(path_copy);
        return ERR_INVALID_VALUE;
    }

    uint32_t prev_cluster = first_cluster;
    uint32_t current_cluster = first_cluster;

    // Цикл по кластерам
    for (uint32_t i = 0; i < needed_clusters; i++) {
        // Чтение данных из файла
        size_t bytes_to_read = (i == needed_clusters - 1) ? (size_t)(file_size - i * cluster_size) : cluster_size;
        size_t read_bytes = fread(cluster_buf, 1, bytes_to_read, f);
        if (read_bytes != bytes_to_read) {
            printf("Error: failed to read host file.\n");
            goto error;
        }
        // Дополняем нулями, если последний кластер не полный
        if (bytes_to_read < cluster_size) {
            memset(cluster_buf + bytes_to_read, 0, cluster_size - bytes_to_read);
        }

        // Запись кластера на диск
        err = fat32_write_cluster(disk, &info, current_cluster, cluster_buf);
        if (err != ERR_OK) goto error;

        // Если это не последний кластер, выделяем следующий
        if (i < needed_clusters - 1) {
            uint32_t next_cluster = fat32_alloc_cluster(disk, &info);
            if (next_cluster == 0) {
                printf("Error: not enough free clusters.\n");
                goto error;
            }
            // Связываем текущий кластер со следующим в FAT
            err = fat32_set_fat_entry(disk, info.fat1_lba, current_cluster, next_cluster);
            if (err != ERR_OK) goto error;
            err = fat32_set_fat_entry(disk, info.fat2_lba, current_cluster, next_cluster);
            if (err != ERR_OK) goto error;

            prev_cluster = current_cluster;
            current_cluster = next_cluster;
        } else {
            // Последний кластер помечаем как конец цепочки
            err = fat32_set_fat_entry(disk, info.fat1_lba, current_cluster, 0x0FFFFFFF);
            if (err != ERR_OK) goto error;
            err = fat32_set_fat_entry(disk, info.fat2_lba, current_cluster, 0x0FFFFFFF);
            if (err != ERR_OK) goto error;
        }
    }

    fclose(f);
    free(cluster_buf);

    // Добавляем запись в родительский каталог (с поддержкой длинных имён)
    err = fat32_add_dir_entry(disk, &info, parent_cluster, file_name,
                              first_cluster, (uint32_t)file_size, FAT32_ATTR_ARCHIVE);
    free(path_copy);

    if (err != ERR_OK) {
        // При ошибке создания записи освобождаем все выделенные кластеры
        fat32_free_cluster_chain(disk, &info, first_cluster);
        printf("Error: failed to create directory entry. Clusters freed.\n");
        return err;
    }

    printf("File '%s' copied successfully (%" PRIu64 " bytes).\n", dest_path, file_size);
    return ERR_OK;

error:
    // Очистка при ошибке: освобождаем все выделенные кластеры
    if (first_cluster != 0) {
        fat32_free_cluster_chain(disk, &info, first_cluster);
    }
    fclose(f);
    free(cluster_buf);
    free(path_copy);
    return err;
}

ErrorCode fat32_delete_file(Disk *disk, uint64_t start_lba, const char *path) {
    // Разбираем путь на родительский каталог и имя файла
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

    // Получаем информацию о FAT32-разделе
    Fat32PartInfo info;
    ErrorCode err = fat32_get_part_info(disk, start_lba, &info);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    // Находим кластер родительского каталога
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
    if (!parent_buf) {
        free(path_copy);
        return ERR_GENERIC;
    }

    err = disk_read(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(parent_buf);
        free(path_copy);
        return err;
    }

    // Генерируем короткое имя для поиска (временное решение)
    uint8_t short_name[11];
    lfn_generate_short_name(file_name, short_name);

    // Ищем запись с таким коротким именем
    FAT32_DirEntry *parent_entries = (FAT32_DirEntry*)parent_buf;
    size_t max_entries = cluster_bytes / sizeof(FAT32_DirEntry);
    int found_index = -1;
    uint32_t first_cluster = 0;

    for (size_t i = 0; i < max_entries; i++) {
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

    // 1. Удаляем связанные LFN-записи (перед пометкой основной)
    size_t entry_offset = found_index * sizeof(FAT32_DirEntry);
    err = lfn_remove_entries(disk, &info, parent_cluster, entry_offset);
    if (err != ERR_OK) {
        // Не критично, можно продолжить, но сообщим
        printf("Warning: failed to remove LFN entries (code %d).\n", err);
    }

    // 2. Помечаем основную запись как удалённую
    parent_entries[found_index].name[0] = 0xE5;

    // 3. Записываем обновлённый каталог на диск
    err = disk_write(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    free(parent_buf);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    // 4. Освобождаем кластеры файла
    if (first_cluster != 0) {
        err = fat32_free_cluster_chain(disk, &info, first_cluster);
        if (err != ERR_OK) {
            free(path_copy);
            return err;
        }
    }

    free(path_copy);
    printf("File '%s' deleted successfully.\n", path);
    return ERR_OK;
}

ErrorCode fat32_create_dir(Disk *disk, uint64_t start_lba, const char *path) {
    // Разбираем путь на родительский каталог и имя нового каталога
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
    const char *new_name = last_slash + 1;

    if (strlen(new_name) == 0) {
        free(path_copy);
        printf("Error: empty directory name.\n");
        return ERR_INVALID_VALUE;
    }

    // Получаем информацию о FAT32-разделе
    Fat32PartInfo info;
    ErrorCode err = fat32_get_part_info(disk, start_lba, &info);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    // Находим кластер родительского каталога
    uint32_t parent_cluster;
    err = fat32_find_dir(disk, &info, parent_path, &parent_cluster);
    if (err != ERR_OK) {
        printf("Error: parent directory '%s' not found.\n", parent_path);
        free(path_copy);
        return err;
    }

    // Проверяем, существует ли уже запись с таким именем (по короткому имени)
    uint64_t parent_lba = info.first_data_lba + (parent_cluster - 2) * info.sectors_per_cluster;
    size_t cluster_bytes = info.sectors_per_cluster * info.bytes_per_sector;
    uint8_t *parent_buf = (uint8_t*)malloc(cluster_bytes);
    if (!parent_buf) {
        free(path_copy);
        return ERR_GENERIC;
    }

    err = disk_read(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(parent_buf);
        free(path_copy);
        return err;
    }

    // Генерируем короткое имя для проверки существования
    uint8_t short_name[11];
    lfn_generate_short_name(new_name, short_name);

    FAT32_DirEntry *parent_entries = (FAT32_DirEntry*)parent_buf;
    int found = 0;
    size_t max_entries = cluster_bytes / sizeof(FAT32_DirEntry);
    for (size_t i = 0; i < max_entries; i++) {
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
        printf("Error: '%s' already exists.\n", new_name);
        free(path_copy);
        return ERR_INVALID_VALUE;
    }

    // Выделяем новый кластер для каталога
    uint32_t new_cluster = fat32_alloc_cluster(disk, &info);
    if (new_cluster == 0) {
        free(path_copy);
        printf("Error: no free clusters available.\n");
        return ERR_INVALID_VALUE;
    }

    // Инициализируем новый кластер каталога (записи "." и "..")
    err = fat32_init_dir_cluster(disk, &info, new_cluster, parent_cluster);
    if (err != ERR_OK) {
        fat32_free_cluster_chain(disk, &info, new_cluster);
        free(path_copy);
        return err;
    }

    // Добавляем запись в родительский каталог (с поддержкой длинных имён)
    err = fat32_add_dir_entry(disk, &info, parent_cluster, new_name,
                              new_cluster, 0, FAT32_ATTR_DIRECTORY);
    if (err != ERR_OK) {
        fat32_free_cluster_chain(disk, &info, new_cluster);
        free(path_copy);
        return err;
    }

    free(path_copy);
    printf("Directory '%s' created successfully.\n", path);
    return ERR_OK;
}

ErrorCode fat32_remove_dir(Disk *disk, uint64_t start_lba, const char *path) {
    // Разбираем путь на родительский каталог и имя удаляемого каталога
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

    // Получаем информацию о FAT32-разделе
    Fat32PartInfo info;
    ErrorCode err = fat32_get_part_info(disk, start_lba, &info);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    // Находим кластер родительского каталога
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
    if (!parent_buf) {
        free(path_copy);
        return ERR_GENERIC;
    }

    err = disk_read(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(parent_buf);
        free(path_copy);
        return err;
    }

    // Генерируем короткое имя для поиска (временное решение)
    uint8_t short_name[11];
    lfn_generate_short_name(dir_name, short_name);

    // Ищем запись с таким коротким именем
    FAT32_DirEntry *parent_entries = (FAT32_DirEntry*)parent_buf;
    size_t max_entries = cluster_bytes / sizeof(FAT32_DirEntry);
    int found_index = -1;
    uint32_t dir_cluster = 0;

    for (size_t i = 0; i < max_entries; i++) {
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

    // 1. Удаляем связанные LFN-записи (перед пометкой основной)
    size_t entry_offset = found_index * sizeof(FAT32_DirEntry);
    err = lfn_remove_entries(disk, &info, parent_cluster, entry_offset);
    if (err != ERR_OK) {
        // Не критично, можно продолжить, но сообщим
        printf("Warning: failed to remove LFN entries (code %d).\n", err);
    }

    // 2. Помечаем основную запись как удалённую
    parent_entries[found_index].name[0] = 0xE5;

    // 3. Записываем обновлённый родительский каталог
    err = disk_write(disk, parent_buf, cluster_bytes, parent_lba * SECTOR_SIZE);
    free(parent_buf);
    if (err != ERR_OK) {
        free(path_copy);
        return err;
    }

    // 4. Освобождаем кластеры удаляемого каталога
    err = fat32_free_cluster_chain(disk, &info, dir_cluster);
    free(path_copy);
    if (err != ERR_OK) return err;

    printf("Directory '%s' removed successfully.\n", path);
    return ERR_OK;
}