#include "fat32.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

// ==================== Вспомогательные функции ====================

// Вычисление контрольной суммы SFN (11 байт) – алгоритм из спецификации FAT
static uint8_t calc_checksum(const uint8_t *sfn) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + sfn[i];
    }
    return sum;
}

// Проверка, что строка состоит только из печатных ASCII символов (0x20-0x7E)
static bool is_all_ascii(const char *s) {
    while (*s) {
        unsigned char c = *s++;
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// Преобразование ASCII-строки в UTF-16LE (простое расширение)
static size_t ascii_to_utf16le(const char *src, uint16_t *dst, size_t dst_capacity) {
    size_t i = 0;
    while (src[i] && i < dst_capacity) {
        dst[i] = (uint16_t)(unsigned char)src[i];
        i++;
    }
    return i;
}

// Разделение имени на основу и расширение (по последней точке)
static void split_name(const char *name, char *base, size_t base_size, char *ext, size_t ext_size) {
    const char *dot = strrchr(name, '.');
    if (dot) {
        size_t base_len = dot - name;
        if (base_len >= base_size) base_len = base_size - 1;
        strncpy(base, name, base_len);
        base[base_len] = '\0';
        size_t ext_len = strlen(dot + 1);
        if (ext_len >= ext_size) ext_len = ext_size - 1;
        strncpy(ext, dot + 1, ext_len);
        ext[ext_len] = '\0';
    } else {
        strncpy(base, name, base_size - 1);
        base[base_size - 1] = '\0';
        ext[0] = '\0';
    }
}

// Формирование базового SFN (без суффикса) из основы и расширения.
static void make_basic_sfn_from_parts(const char *base, const char *ext, uint8_t sfn[11]) {
    memset(sfn, ' ', 11);
    // Основная часть (до 5 символов)
    size_t base_len = strlen(base);
    for (size_t i = 0; i < base_len && i < 5; i++) {
        char c = toupper((unsigned char)base[i]);
        sfn[i] = (uint8_t)c;
    }
    // Суффикс (временный)
    sfn[5] = '~';
    sfn[6] = '1';
    sfn[7] = ' ';
    // Расширение (до 3 символов)
    size_t ext_len = strlen(ext);
    for (size_t i = 0; i < ext_len && i < 3; i++) {
        char c = toupper((unsigned char)ext[i]);
        sfn[8 + i] = (uint8_t)c;
    }
}

// Поиск максимального суффикса для заданной основы (первые 5 байт) в буфере каталога.
static int find_next_suffix(const uint8_t *dir_buffer, uint32_t entry_count, const uint8_t *base_sfn) {
    int max_suffix = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        const uint8_t *entry = dir_buffer + i * 32;
        if (entry[0] == 0x00 || entry[0] == 0xE5) continue;
        if (entry[11] == FAT32_ATTR_LFN) continue; // атрибут по смещению 11

        if (memcmp(entry, base_sfn, 5) != 0) continue;
        if (entry[5] != '~') continue;

        int suffix = 0;
        if (isdigit(entry[6])) {
            if (isdigit(entry[7])) {
                suffix = (entry[6] - '0') * 10 + (entry[7] - '0');
            } else if (entry[7] == ' ') {
                suffix = entry[6] - '0';
            }
        }
        if (suffix > max_suffix) max_suffix = suffix;
    }
    return max_suffix + 1;
}

// Генерация уникального SFN (всегда с суффиксом)
static ErrorCode make_unique_sfn(const uint8_t *dir_buffer, uint32_t entry_count,
                                      const char *base, const char *ext,
                                      uint8_t sfn[11]) {
    uint8_t temp_sfn[11];
    make_basic_sfn_from_parts(base, ext, temp_sfn);
    int suffix = find_next_suffix(dir_buffer, entry_count, temp_sfn);
    if (suffix > 99) {
        return ERR_FAT32_SFN_SUFFIX_OVERFLOW;
    }

    memcpy(sfn, temp_sfn, 5);
    sfn[5] = '~';
    if (suffix < 10) {
        sfn[6] = '0' + (uint8_t)suffix;
        sfn[7] = ' ';
    } else {
        sfn[6] = '0' + (uint8_t)(suffix / 10);
        sfn[7] = '0' + (uint8_t)(suffix % 10);
    }
    memcpy(sfn + 8, temp_sfn + 8, 3);
    return ERR_OK;
}

// Создание LFN-записей для заданного UTF-16LE имени и контрольной суммы
static ErrorCode create_lfn_entries(const uint16_t *utf16_name, size_t len,
                                         uint8_t checksum,
                                         Fat32LongEntry **entries_out, uint8_t *count_out) {
    if (len > 255) return ERR_FAT32_NAME_TOO_LONG;

    uint8_t num_entries = (uint8_t)((len + 12) / 13);
    if (num_entries > FAT32_MAX_LFN_ENTRIES) return ERR_FAT32_TOO_MANY_LFN_ENTRIES;

    Fat32LongEntry *entries = (Fat32LongEntry*)calloc(num_entries, sizeof(Fat32LongEntry));
    if (!entries) return ERR_OUT_OF_MEMORY;

    for (uint8_t i = 0; i < num_entries; i++) {
        Fat32LongEntry *lfn = &entries[i];
        // Номер записи: i+1. Для последней (i == num_entries-1) устанавливаем бит 0x40.
        lfn->order = (i == num_entries - 1) ? (i + 1) | 0x40 : (i + 1);
        lfn->attr = FAT32_ATTR_LFN;
        lfn->type = 0;
        lfn->checksum = checksum;
        lfn->first_cluster = 0;

        size_t start = i * 13;
        for (int j = 0; j < 5; j++) {
            lfn->name1[j] = (start + j < len) ? utf16_name[start + j] : 0xFFFF;
        }
        for (int j = 0; j < 6; j++) {
            lfn->name2[j] = (start + 5 + j < len) ? utf16_name[start + 5 + j] : 0xFFFF;
        }
        for (int j = 0; j < 2; j++) {
            lfn->name3[j] = (start + 11 + j < len) ? utf16_name[start + 11 + j] : 0xFFFF;
        }
    }

    *entries_out = entries;
    *count_out = num_entries;
    return ERR_OK;
}

// ==================== Преобразование времени ====================

static uint16_t fat_time_from_tm(const struct tm *tm) {
    uint16_t time = 0;
    time |= (tm->tm_hour << 11) & 0xF800;   // часы 5 бит
    time |= (tm->tm_min << 5) & 0x07E0;     // минуты 6 бит
    time |= (tm->tm_sec / 2) & 0x001F;      // секунды/2 5 бит
    return time;
}

static uint16_t fat_date_from_tm(const struct tm *tm) {
    uint16_t date = 0;
    uint16_t year = (uint16_t)(tm->tm_year + 1900 - 1980);
    date |= (year << 9) & 0xFE00;
    date |= ((tm->tm_mon + 1) << 5) & 0x01E0;
    date |= tm->tm_mday & 0x001F;
    return date;
}

// ==================== Экспортируемые функции ====================

ErrorCode fat32_prepare_entry(const char *utf8_name, uint8_t attr,
                                   const uint8_t *dir_buffer, uint32_t entry_count,
                                   fat32_entry_info_t *info) {
    if (!utf8_name || !info || !dir_buffer) return ERR_INVALID_ARGUMENT;
    if (!is_all_ascii(utf8_name)) return ERR_FAT32_NAME_INVALID;

    memset(info, 0, sizeof(fat32_entry_info_t));

    size_t utf8_len = strlen(utf8_name);
    uint16_t *utf16 = (uint16_t*)malloc((utf8_len + 1) * sizeof(uint16_t));
    if (!utf16) return ERR_OUT_OF_MEMORY;

    size_t utf16_len = ascii_to_utf16le(utf8_name, utf16, utf8_len + 1);
    if (utf16_len == 0) {
        free(utf16);
        return ERR_FAT32_UTF16_CONVERSION;
    }

    info->utf16_name = utf16;
    info->utf16_len = utf16_len;
    info->name = utf8_name;
    info->attr = attr;

    char base[256];
    char ext[4];
    split_name(utf8_name, base, sizeof(base), ext, sizeof(ext));

    ErrorCode err = make_unique_sfn(dir_buffer, entry_count, base, ext, info->sfn);
    if (err != ERR_OK) {
        free(utf16);
        info->utf16_name = NULL;
        return err;
    }

    uint8_t checksum = calc_checksum(info->sfn);
    err = create_lfn_entries(utf16, utf16_len, checksum,
                             &info->lfn_entries, &info->lfn_count);
    if (err != ERR_OK) {
        free(utf16);
        info->utf16_name = NULL;
        return err;
    }

    info->total_entries = info->lfn_count + 1;
    return ERR_OK;
}

void fat32_free_entry_info(fat32_entry_info_t *info) {
    if (info) {
        free(info->utf16_name);
        free(info->lfn_entries);
        memset(info, 0, sizeof(fat32_entry_info_t));
    }
}

void fat32_set_current_time(fat32_entry_info_t *info) {
    if (!info) return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) {
        info->crt_time = 0;
        info->crt_date = 0;
        info->wrt_time = 0;
        info->wrt_date = 0;
        info->lst_acc_date = 0;
        info->crt_time_tenth = 0;
        return;
    }
    info->crt_time = fat_time_from_tm(tm);
    info->crt_date = fat_date_from_tm(tm);
    info->wrt_time = info->crt_time;
    info->wrt_date = info->crt_date;
    info->lst_acc_date = info->crt_date;
    info->crt_time_tenth = 0;
}