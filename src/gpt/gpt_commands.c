#include "gpt_commands.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// *** Вспомогательная функция ***
/**
 * Генерирует случайный GUID (UUID версии 4) для использования в GPT.
 * 
 * GUID (Globally Unique Identifier) — 16-байтовый идентификатор,
 * который в GPT используется для идентификации диска и разделов.
 * 
 * Данная реализация является упрощённой:
 * - использует генератор псевдослучайных чисел rand(),
 * - инициализируется текущим временем (time(NULL)),
 * - не является криптографически безопасной,
 * - устанавливает биты версии (4) и варианта (RFC 4122) в соответствии со спецификацией.
 *
 * @param guid Указатель на массив из 16 байт, куда будет записан GUID.
 */
static void generate_guid(uint8_t *guid) {
    srand(time(NULL));
    for (int i = 0; i < 16; i++) {
        guid[i] = rand() & 0xFF;
    }
    // Установить версию 4 (случайный GUID)
    guid[6] = (guid[6] & 0x0F) | 0x40;
    guid[8] = (guid[8] & 0x3F) | 0x80;
}

/**
 * Вычисляет контрольную сумму CRC-32 для блока данных.
 * Реализация использует стандартный полином 0xEDB88320 (CRC-32).
 * Применяется для расчёта CRC32 заголовка GPT и таблицы разделов.
 *
 * @param data Указатель на начало данных.
 * @param len  Длина данных в байтах.
 * @return     Контрольная сумма CRC-32.
 */
static uint32_t crc32(const void *data, size_t len) {
    const uint8_t *buf = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

static int gpt_initialize(MBR *mbr, GPTHeader *header, uint64_t disk_sectors, uint32_t num_partitions) {
    if (!mbr || !header || disk_sectors < 34) return -1;

    // Обнуляем весь MBR
    memset(mbr, 0, sizeof(MBR));

    // Заполняем защитную запись (первая)
    mbr->partitions[0].boot_flag = 0;
    mbr->partitions[0].start_head = 0;
    mbr->partitions[0].start_sector = 1;
    mbr->partitions[0].start_cylinder = 0;
    mbr->partitions[0].partition_type = 0xEE;  // GPT protective
    mbr->partitions[0].end_head = 0xFF;
    mbr->partitions[0].end_sector = 0xFF;
    mbr->partitions[0].end_cylinder = 0xFF;
    mbr->partitions[0].lba_start = 1;
    mbr->partitions[0].sector_count = (disk_sectors - 1 > UINT32_MAX) ? UINT32_MAX : (uint32_t)(disk_sectors - 1);

    // Остальные записи уже нулевые (memset)
    mbr->signature = MBR_SIGNATURE;  // 0xAA55

    // Заполнение заголовка GPT (без изменений)
    memset(header, 0, sizeof(GPTHeader));
    memcpy(header->signature, GPT_SIGNATURE, 8);
    header->revision = GPT_REVISION;
    header->header_size = 92;
    header->reserved = 0;
    header->current_lba = 1;
    header->backup_lba = disk_sectors - 1;
    header->first_usable_lba = 34;
    header->last_usable_lba = disk_sectors - 34;
    generate_guid(header->disk_guid);
    header->partition_entry_lba = 2;
    header->num_partition_entries = num_partitions;
    header->partition_entry_size = 128;
    header->partitions_crc32 = 0;
    // CRC заголовка будет вычислен позже

    return 0;
}
// ***

ErrorCode gpt_create(Disk *disk) {
    if (!disk || !disk->is_open)
        return ERR_DISK_OPEN;

    uint64_t disk_sectors = disk->size / SECTOR_SIZE;
    if (disk_sectors < 34) {
        return ERR_DISK_CREATE;  // диск слишком мал для GPT
    }

    MBR mbr;
    GPTHeader header;
    if (gpt_initialize(&mbr, &header, disk_sectors, 128) != 0)
        return ERR_DISK_CREATE;

    // Выделяем память под таблицу разделов (128 записей)
    uint32_t num_entries = header.num_partition_entries;
    size_t table_size = num_entries * sizeof(GPTPartitionEntry);
    GPTPartitionEntry *partitions = (GPTPartitionEntry*)calloc(1, table_size);
    if (!partitions) {
        return ERR_GENERIC;
    }

    // Вычисляем CRC32 таблицы
    header.partitions_crc32 = crc32(partitions, table_size);

    // Вычисляем CRC32 заголовка (с обнулённым полем header_crc32)
    header.header_crc32 = 0;
    header.header_crc32 = crc32(&header, GPT_HEADER_SIZE);

    // Запись защитного MBR (сектор 0)
    ErrorCode err = disk_write(disk, &mbr, sizeof(MBR), 0);
    if (err != ERR_OK) {
        free(partitions);
        return err;
    }

    // Запись заголовка GPT (сектор 1)
    err = disk_write(disk, &header, sizeof(GPTHeader), 1 * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(partitions);
        return err;
    }

    // Запись таблицы разделов (начиная с LBA 2)
    uint64_t table_lba = header.partition_entry_lba;
    uint32_t sectors_per_table = (uint32_t)((table_size + SECTOR_SIZE - 1) / SECTOR_SIZE);
    for (uint32_t i = 0; i < sectors_per_table; i++) {
        uint64_t offset = (table_lba + i) * SECTOR_SIZE;
        uint8_t *ptr = (uint8_t*)partitions + i * SECTOR_SIZE;
        uint32_t chunk = (i == sectors_per_table - 1) ? (uint32_t)(table_size % SECTOR_SIZE) : SECTOR_SIZE;
        if (chunk == 0) chunk = SECTOR_SIZE;  // если ровно по секторам
        err = disk_write(disk, ptr, chunk, offset);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    // Резервная копия таблицы разделов (перед последним сектором)
    uint64_t backup_table_lba = header.backup_lba - sectors_per_table;
    for (uint32_t i = 0; i < sectors_per_table; i++) {
        uint64_t offset = (backup_table_lba + i) * SECTOR_SIZE;
        uint8_t *ptr = (uint8_t*)partitions + i * SECTOR_SIZE;
        uint32_t chunk = (i == sectors_per_table - 1) ? (uint32_t)(table_size % SECTOR_SIZE) : SECTOR_SIZE;
        if (chunk == 0) chunk = SECTOR_SIZE;
        err = disk_write(disk, ptr, chunk, offset);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    // Резервная копия заголовка GPT (последний сектор)
    err = disk_write(disk, &header, sizeof(GPTHeader), header.backup_lba * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(partitions);
        return err;
    }

    free(partitions);
    return ERR_OK;
}

// Преобразование GUID в строку формата XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
void gpt_guid_to_string(const uint8_t *guid, char *str) {
    sprintf(str, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            guid[3], guid[2], guid[1], guid[0],
            guid[5], guid[4],
            guid[7], guid[6],
            guid[8], guid[9],
            guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
}

// Преобразование строки GUID в 16 байт (little-endian)
int gpt_guid_from_string(const char *str, uint8_t *guid) {
    const char *p = str;
    for (int byte = 0; byte < 16; byte++) {
        char hex[3] = {0};
        while (*p == '-') p++;
        if (!isxdigit(p[0]) || !isxdigit(p[1])) return -1;
        hex[0] = *p++;
        hex[1] = *p++;
        guid[byte] = (uint8_t)strtoul(hex, NULL, 16);
    }
    return 0;
}

void gpt_print_info(Disk *disk) {
    uint8_t sector[SECTOR_SIZE];
    // Читаем заголовок GPT (сектор 1)
    if (disk_read(disk, sector, SECTOR_SIZE, 1 * SECTOR_SIZE) != ERR_OK) {
        printf(" - Error: cannot read GPT header.\n");
        return;
    }
    GPTHeader *header = (GPTHeader*)sector;

    // Проверка сигнатуры ещё раз
    if (memcmp(header->signature, GPT_SIGNATURE, 8) != 0) {
        printf(" - Error: invalid GPT signature.\n");
        return;
    }

    // Вывод основной информации
    printf(" - Revision: %u.%u\n", header->revision >> 16, header->revision & 0xFFFF);
    printf(" - Disk GUID: ");
    char guid_str[37];
    gpt_guid_to_string(header->disk_guid, guid_str);
    printf("%s\n", guid_str);
    printf(" - First usable LBA: %" PRIu64 "\n", header->first_usable_lba);
    printf(" - Last usable LBA: %" PRIu64 "\n", header->last_usable_lba);
    printf(" - Number of partition entries: %u\n", header->num_partition_entries);
}

// TODO: реализовать удаление раздела GPT, пока что заглушка
ErrorCode gpt_delete_partition(Disk *disk, int index) {
    (void)disk;
    (void)index;
	printf("\n>>> STUB: gpt_delete_partition\n");
    return ERR_OK;
}