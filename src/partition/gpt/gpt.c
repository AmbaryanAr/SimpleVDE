#include "gpt.h"
#include "mbr.h"
#include "utils.h"

#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static void generate_guid(uint8_t *guid) {
    for (int i = 0; i < 16; i++) {
        guid[i] = rand() & 0xFF;
    }
    guid[6] = (guid[6] & 0x0F) | 0x40;
    guid[8] = (guid[8] & 0x3F) | 0x80;
}

static uint32_t crc32(const void *data, size_t len) {
    const uint8_t *buf = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

static bool is_partition_empty(const GptPartitionEntry *entry) {
    const uint8_t *p = (const uint8_t*)entry;
    for (size_t i = 0; i < sizeof(GptPartitionEntry); i++) {
        if (p[i] != 0) {
            return false;
        }
    }
    return true;
}

static ErrorCode write_protective_mbr(Disk *disk, uint64_t disk_sectors) {
    MbrSector mbr;
    memset(&mbr, 0, sizeof(mbr));

    MbrPartitionEntry *p = &mbr.partitions[0];
    p->boot_flag = 0;
    p->start_head = 0;
    p->start_sector = 1;
    p->start_cylinder = 0;
    p->partition_type = 0xEE;
    p->end_head = 0xFE;
    p->end_sector = 0xFF;
    p->end_cylinder = 0xFF;
    p->lba_start = 1;
    uint64_t size = disk_sectors - 1;
    if (size > UINT32_MAX) {
        size = UINT32_MAX;
    }
    p->sector_count = (uint32_t)size;
    mbr.signature = MBR_SIGNATURE;
    return disk_write(disk, &mbr, sizeof(mbr), 0);
}

ErrorCode gpt_init(Disk *disk) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }
    uint64_t disk_sectors = disk->size / SECTOR_SIZE;
    if (disk_sectors < GPT_MIN_SIZE) {
        return ERR_DISK_CREATE;
    }

    ErrorCode err = write_protective_mbr(disk, disk_sectors);
    if (err != ERR_OK) {
        return err;
    }

    GptHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.signature, GPT_SIGNATURE, 8);
    header.revision = GPT_REVISION;
    header.header_size = GPT_HEADER_SIZE;
    header.current_lba = 1;
    header.backup_lba = disk_sectors - 1;
    header.first_usable_lba = 34;
    header.last_usable_lba = disk_sectors - 34;
    generate_guid(header.disk_guid);
    header.partition_entry_lba = 2;
    header.num_partition_entries = 128;
    header.partition_entry_size = GPT_PARTITION_ENTRY_SIZE;

    size_t table_size = header.num_partition_entries * header.partition_entry_size;
    GptPartitionEntry *partitions = (GptPartitionEntry*)calloc(1, table_size);
    if (!partitions) {
        return ERR_OUT_OF_MEMORY;
    }

    header.partitions_crc32 = crc32(partitions, table_size);
    header.header_crc32 = 0;
    header.header_crc32 = crc32(&header, GPT_HEADER_SIZE);

    err = disk_write(disk, &header, sizeof(header), 1 * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(partitions);
        return err;
    }

    uint32_t sectors_per_table = (uint32_t)((table_size + SECTOR_SIZE - 1) / SECTOR_SIZE);
    for (uint32_t i = 0; i < sectors_per_table; i++) {
        uint64_t offset = (header.partition_entry_lba + i) * SECTOR_SIZE;
        uint8_t *ptr = (uint8_t*)partitions + i * SECTOR_SIZE;
        uint32_t chunk = (i == sectors_per_table - 1) ? (uint32_t)(table_size % SECTOR_SIZE) : SECTOR_SIZE;
        if (chunk == 0) {
            chunk = SECTOR_SIZE;
        }
        err = disk_write(disk, ptr, chunk, offset);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    uint64_t backup_table_lba = header.backup_lba - sectors_per_table;
    for (uint32_t i = 0; i < sectors_per_table; i++) {
        uint64_t offset = (backup_table_lba + i) * SECTOR_SIZE;
        uint8_t *ptr = (uint8_t*)partitions + i * SECTOR_SIZE;
        uint32_t chunk = (i == sectors_per_table - 1) ? (uint32_t)(table_size % SECTOR_SIZE) : SECTOR_SIZE;
        if (chunk == 0) {
            chunk = SECTOR_SIZE;
        }
        err = disk_write(disk, ptr, chunk, offset);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    err = disk_write(disk, &header, sizeof(header), header.backup_lba * SECTOR_SIZE);
    free(partitions);
    return err;
}

static uint64_t find_next_free_lba(const GptPartitionEntry *parts, uint32_t num_entries,
                                   uint64_t first_usable, uint64_t last_usable) {
    uint64_t next = first_usable;
    for (uint32_t i = 0; i < num_entries; i++) {
        if (!is_partition_empty(&parts[i])) {
            uint64_t end = parts[i].last_lba + 1;
            if (end > next) {
                next = end;
            }
        }
    }
    if (next > last_usable) {
        next = last_usable + 1;
    }
    return next;
}

ErrorCode gpt_create_partition(Disk *disk, int index, uint64_t size_sectors, const uint8_t *type_guid) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }
    if (!type_guid) {
        return ERR_INVALID_ARGUMENT;
    }

    GptHeader header;
    ErrorCode err = disk_read(disk, &header, sizeof(header), 1 * SECTOR_SIZE);
    if (err != ERR_OK) {
        return err;
    }
    if (memcmp(header.signature, GPT_SIGNATURE, 8) != 0) {
        return ERR_INVALID_SIGNATURE;
    }

    if (index < 0 || (uint32_t)index >= header.num_partition_entries) {
        return ERR_INVALID_ARGUMENT;
    }

    size_t table_size = header.num_partition_entries * header.partition_entry_size;
    uint32_t sectors_per_table = (uint32_t)((table_size + SECTOR_SIZE - 1) / SECTOR_SIZE);

    GptPartitionEntry *partitions = (GptPartitionEntry*)malloc(table_size);
    if (!partitions) {
        return ERR_OUT_OF_MEMORY;
    }

    uint64_t table_lba = header.partition_entry_lba;
    for (uint32_t i = 0; i < sectors_per_table; i++) {
        err = disk_read(disk, (uint8_t*)partitions + i * SECTOR_SIZE, SECTOR_SIZE,
                        (table_lba + i) * SECTOR_SIZE);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    if (!is_partition_empty(&partitions[index])) {
        free(partitions);
        return ERR_ALREADY_EXISTS;
    }

    uint64_t next_lba = find_next_free_lba(partitions, header.num_partition_entries,
                                           header.first_usable_lba, header.last_usable_lba);
    if (next_lba > header.last_usable_lba) {
        free(partitions);
        return ERR_NO_FREE_SPACE;
    }

    uint64_t free_space = header.last_usable_lba - next_lba + 1;
    uint64_t size = size_sectors;
    if (size == 0) {
        size = free_space;
    }
    if (size > free_space || size == 0) {
        free(partitions);
        return ERR_NO_FREE_SPACE;
    }

    uint64_t end_lba = next_lba + size - 1;
    if (end_lba > header.last_usable_lba) {
        free(partitions);
        return ERR_NO_FREE_SPACE;
    }

    GptPartitionEntry *entry = &partitions[index];
    memcpy(entry->type_guid, type_guid, 16);
    generate_guid(entry->partition_guid);
    entry->first_lba = next_lba;
    entry->last_lba = end_lba;
    entry->attributes = 0;
    memset(entry->partition_name, 0, sizeof(entry->partition_name));

    uint32_t new_table_crc = crc32(partitions, table_size);
    header.partitions_crc32 = new_table_crc;
    header.header_crc32 = 0;
    header.header_crc32 = crc32(&header, GPT_HEADER_SIZE);

    for (uint32_t i = 0; i < sectors_per_table; i++) {
        uint64_t offset = (table_lba + i) * SECTOR_SIZE;
        uint8_t *ptr = (uint8_t*)partitions + i * SECTOR_SIZE;
        uint32_t chunk = (i == sectors_per_table - 1) ? (uint32_t)(table_size % SECTOR_SIZE) : SECTOR_SIZE;
        if (chunk == 0) {
            chunk = SECTOR_SIZE;
        }
        err = disk_write(disk, ptr, chunk, offset);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    uint64_t backup_table_lba = header.backup_lba - sectors_per_table;
    for (uint32_t i = 0; i < sectors_per_table; i++) {
        uint64_t offset = (backup_table_lba + i) * SECTOR_SIZE;
        uint8_t *ptr = (uint8_t*)partitions + i * SECTOR_SIZE;
        uint32_t chunk = (i == sectors_per_table - 1) ? (uint32_t)(table_size % SECTOR_SIZE) : SECTOR_SIZE;
        if (chunk == 0) {
            chunk = SECTOR_SIZE;
        }
        err = disk_write(disk, ptr, chunk, offset);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    err = disk_write(disk, &header, sizeof(header), 1 * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(partitions);
        return err;
    }
    err = disk_write(disk, &header, sizeof(header), header.backup_lba * SECTOR_SIZE);
    free(partitions);
    return err;
}

ErrorCode gpt_delete_partition(Disk *disk, int index) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }

    GptHeader header;
    ErrorCode err = disk_read(disk, &header, sizeof(header), 1 * SECTOR_SIZE);
    if (err != ERR_OK) {
        return err;
    }
    if (memcmp(header.signature, GPT_SIGNATURE, 8) != 0) {
        return ERR_INVALID_SIGNATURE;
    }

    if (index < 0 || (uint32_t)index >= header.num_partition_entries) {
        return ERR_INVALID_ARGUMENT;
    }

    size_t table_size = header.num_partition_entries * header.partition_entry_size;
    uint32_t sectors_per_table = (uint32_t)((table_size + SECTOR_SIZE - 1) / SECTOR_SIZE);
    uint64_t table_lba = header.partition_entry_lba;

    GptPartitionEntry *partitions = (GptPartitionEntry*)malloc(table_size);
    if (!partitions) {
        return ERR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < sectors_per_table; i++) {
        err = disk_read(disk, (uint8_t*)partitions + i * SECTOR_SIZE, SECTOR_SIZE,
                        (table_lba + i) * SECTOR_SIZE);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    if (is_partition_empty(&partitions[index])) {
        free(partitions);
        return ERR_NOT_FOUND;
    }

    memset(&partitions[index], 0, sizeof(GptPartitionEntry));

    uint32_t new_table_crc = crc32(partitions, table_size);
    header.partitions_crc32 = new_table_crc;
    header.header_crc32 = 0;
    header.header_crc32 = crc32(&header, GPT_HEADER_SIZE);

    for (uint32_t i = 0; i < sectors_per_table; i++) {
        uint64_t offset = (table_lba + i) * SECTOR_SIZE;
        uint8_t *ptr = (uint8_t*)partitions + i * SECTOR_SIZE;
        uint32_t chunk = (i == sectors_per_table - 1) ? (uint32_t)(table_size % SECTOR_SIZE) : SECTOR_SIZE;
        if (chunk == 0) {
            chunk = SECTOR_SIZE;
        }
        err = disk_write(disk, ptr, chunk, offset);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    uint64_t backup_table_lba = header.backup_lba - sectors_per_table;
    for (uint32_t i = 0; i < sectors_per_table; i++) {
        uint64_t offset = (backup_table_lba + i) * SECTOR_SIZE;
        uint8_t *ptr = (uint8_t*)partitions + i * SECTOR_SIZE;
        uint32_t chunk = (i == sectors_per_table - 1) ? (uint32_t)(table_size % SECTOR_SIZE) : SECTOR_SIZE;
        if (chunk == 0) {
            chunk = SECTOR_SIZE;
        }
        err = disk_write(disk, ptr, chunk, offset);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    err = disk_write(disk, &header, sizeof(header), 1 * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(partitions);
        return err;
    }
    err = disk_write(disk, &header, sizeof(header), header.backup_lba * SECTOR_SIZE);
    free(partitions);
    return err;
}

ErrorCode gpt_set_partition_type(Disk *disk, int index, const uint8_t *type_guid) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }
    if (!type_guid) {
        return ERR_INVALID_ARGUMENT;
    }

    GptHeader header;
    ErrorCode err = disk_read(disk, &header, sizeof(header), 1 * SECTOR_SIZE);
    if (err != ERR_OK) {
        return err;
    }
    if (memcmp(header.signature, GPT_SIGNATURE, 8) != 0) {
        return ERR_INVALID_SIGNATURE;
    }

    if (index < 0 || (uint32_t)index >= header.num_partition_entries) {
        return ERR_INVALID_ARGUMENT;
    }

    size_t table_size = header.num_partition_entries * header.partition_entry_size;
    uint32_t sectors_per_table = (uint32_t)((table_size + SECTOR_SIZE - 1) / SECTOR_SIZE);
    uint64_t table_lba = header.partition_entry_lba;

    GptPartitionEntry *partitions = (GptPartitionEntry*)malloc(table_size);
    if (!partitions) {
        return ERR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < sectors_per_table; i++) {
        err = disk_read(disk, (uint8_t*)partitions + i * SECTOR_SIZE, SECTOR_SIZE,
                        (table_lba + i) * SECTOR_SIZE);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    if (is_partition_empty(&partitions[index])) {
        free(partitions);
        return ERR_NOT_FOUND;
    }

    memcpy(partitions[index].type_guid, type_guid, 16);

    uint32_t new_table_crc = crc32(partitions, table_size);
    header.partitions_crc32 = new_table_crc;
    header.header_crc32 = 0;
    header.header_crc32 = crc32(&header, GPT_HEADER_SIZE);

    for (uint32_t i = 0; i < sectors_per_table; i++) {
        uint64_t offset = (table_lba + i) * SECTOR_SIZE;
        uint8_t *ptr = (uint8_t*)partitions + i * SECTOR_SIZE;
        uint32_t chunk = (i == sectors_per_table - 1) ? (uint32_t)(table_size % SECTOR_SIZE) : SECTOR_SIZE;
        if (chunk == 0) {
            chunk = SECTOR_SIZE;
        }
        err = disk_write(disk, ptr, chunk, offset);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    uint64_t backup_table_lba = header.backup_lba - sectors_per_table;
    for (uint32_t i = 0; i < sectors_per_table; i++) {
        uint64_t offset = (backup_table_lba + i) * SECTOR_SIZE;
        uint8_t *ptr = (uint8_t*)partitions + i * SECTOR_SIZE;
        uint32_t chunk = (i == sectors_per_table - 1) ? (uint32_t)(table_size % SECTOR_SIZE) : SECTOR_SIZE;
        if (chunk == 0) {
            chunk = SECTOR_SIZE;
        }
        err = disk_write(disk, ptr, chunk, offset);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    err = disk_write(disk, &header, sizeof(header), 1 * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(partitions);
        return err;
    }
    err = disk_write(disk, &header, sizeof(header), header.backup_lba * SECTOR_SIZE);
    free(partitions);
    return err;
}

ErrorCode gpt_get_partition_info(Disk *disk, int index, uint64_t *start_lba, uint64_t *size_sectors) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }
    if (!start_lba || !size_sectors) {
        return ERR_INVALID_ARGUMENT;
    }

    GptHeader header;
    ErrorCode err = disk_read(disk, &header, sizeof(header), 1 * SECTOR_SIZE);
    if (err != ERR_OK) {
        return err;
    }
    if (memcmp(header.signature, GPT_SIGNATURE, 8) != 0) {
        return ERR_INVALID_SIGNATURE;
    }

    if (index < 0 || (uint32_t)index >= header.num_partition_entries) {
        return ERR_INVALID_ARGUMENT;
    }

    size_t table_size = header.num_partition_entries * header.partition_entry_size;
    uint32_t sectors_per_table = (uint32_t)((table_size + SECTOR_SIZE - 1) / SECTOR_SIZE);
    uint64_t table_lba = header.partition_entry_lba;

    GptPartitionEntry *partitions = (GptPartitionEntry*)malloc(table_size);
    if (!partitions) {
        return ERR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < sectors_per_table; i++) {
        err = disk_read(disk, (uint8_t*)partitions + i * SECTOR_SIZE, SECTOR_SIZE,
                        (table_lba + i) * SECTOR_SIZE);
        if (err != ERR_OK) {
            free(partitions);
            return err;
        }
    }

    if (is_partition_empty(&partitions[index])) {
        free(partitions);
        return ERR_NOT_FOUND;
    }

    *start_lba = partitions[index].first_lba;
    *size_sectors = partitions[index].last_lba - partitions[index].first_lba + 1;
    free(partitions);
    return ERR_OK;
}

void gpt_print_info(Disk *disk) {
    if (!disk || !disk->is_open) {
        printf("Disk not open.\n");
        return;
    }

    GptHeader header;
    if (disk_read(disk, &header, sizeof(header), 1 * SECTOR_SIZE) != ERR_OK) {
        printf("Failed to read GPT header.\n");
        return;
    }
    if (memcmp(header.signature, GPT_SIGNATURE, 8) != 0) {
        printf("Invalid GPT signature.\n");
        return;
    }

    char disk_guid_str[37];
    gpt_guid_to_string(header.disk_guid, disk_guid_str);
    printf("GPT: revision %u.%u, disk GUID %s\n",
           header.revision >> 16, header.revision & 0xFFFF, disk_guid_str);
    printf("First usable LBA: %" PRIu64 ", last usable LBA: %" PRIu64 "\n",
           header.first_usable_lba, header.last_usable_lba);
    printf("Number of partition entries: %u\n", header.num_partition_entries);

    size_t table_size = header.num_partition_entries * header.partition_entry_size;
    uint32_t sectors_per_table = (uint32_t)((table_size + SECTOR_SIZE - 1) / SECTOR_SIZE);
    uint64_t table_lba = header.partition_entry_lba;

    GptPartitionEntry *partitions = (GptPartitionEntry*)malloc(table_size);
    if (!partitions) {
        printf("Out of memory.\n");
        return;
    }

    for (uint32_t i = 0; i < sectors_per_table; i++) {
        if (disk_read(disk, (uint8_t*)partitions + i * SECTOR_SIZE, SECTOR_SIZE,
                      (table_lba + i) * SECTOR_SIZE) != ERR_OK) {
            printf("Failed to read partition table.\n");
            free(partitions);
            return;
        }
    }

    printf("\nPartitions:\n");
    printf("--------------------------------------------------------\n");
    int count = 0;
    for (uint32_t i = 0; i < header.num_partition_entries; i++) {
        if (!is_partition_empty(&partitions[i])) {
            count++;
            char type_guid_str[37], part_guid_str[37];
            gpt_guid_to_string(partitions[i].type_guid, type_guid_str);
            gpt_guid_to_string(partitions[i].partition_guid, part_guid_str);
            uint64_t sectors = partitions[i].last_lba - partitions[i].first_lba + 1;
            uint64_t size_mb = (sectors * SECTOR_SIZE) / (1024 * 1024);
            char name_ascii[37] = {0};
            for (int j = 0; j < 36 && partitions[i].partition_name[j] != 0; j++) {
                uint16_t wc = partitions[i].partition_name[j];
                name_ascii[j] = (wc < 0x80) ? (char)wc : '?';
            }
            printf("Part %u: %s\n", i + 1, name_ascii);
            printf("  Type GUID: %s\n", type_guid_str);
            printf("  Part GUID: %s\n", part_guid_str);
            printf("  LBA: %" PRIu64 " - %" PRIu64 " (%" PRIu64 " MB)\n",
                   partitions[i].first_lba, partitions[i].last_lba, size_mb);
        }
    }
    if (count == 0) {
        printf("(no partitions)\n");
    }
    free(partitions);
}

static const struct {
    const char *name;
    uint8_t guid[16];
} gpt_type_table[] = {
    {"linux",    {0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4}},
    {"efi",      {0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B}},
    {"swap",     {0x6D,0xFD,0x57,0x06,0xAB,0xA4,0xC4,0x43,0x84,0xE5,0x09,0x33,0xC8,0x4B,0x4F,0x4F}},
    {"lvm",      {0x79,0xD3,0xD6,0xE6,0x07,0xF5,0xC2,0x44,0xA2,0x3C,0x23,0x8F,0x2A,0x3D,0xF9,0x28}},
    {"windows",  {0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7}},
    {"fat32",    {0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7}},
    {NULL, {0}}
};

bool gpt_type_from_name(const char *name, uint8_t *guid) {
    if (!name || !guid) {
        return false;
    }
    char name_upper[32];
    if (!strlcpy_safe(name_upper, sizeof(name_upper), name)) {
        return false;
    }
    str_toupper(name_upper);
    for (int i = 0; gpt_type_table[i].name != NULL; i++) {
        char table_upper[32];
        strlcpy_safe(table_upper, sizeof(table_upper), gpt_type_table[i].name);
        str_toupper(table_upper);
        if (strcmp(name_upper, table_upper) == 0) {
            memcpy(guid, gpt_type_table[i].guid, 16);
            return true;
        }
    }
    return false;
}

void gpt_guid_to_string(const uint8_t *guid, char *str) {
    sprintf(str, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            guid[3], guid[2], guid[1], guid[0],
            guid[5], guid[4],
            guid[7], guid[6],
            guid[8], guid[9],
            guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
}

int gpt_guid_from_string(const char *str, uint8_t *guid) {
    const char *p = str;
    for (int byte = 0; byte < 16; byte++) {
        char hex[3] = {0};
        while (*p == '-') {
            p++;
        }
        if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) {
            return -1;
        }
        hex[0] = *p++;
        hex[1] = *p++;
        guid[byte] = (uint8_t)strtoul(hex, NULL, 16);
    }
    while (*p == '-') {
        p++;
    }
    if (*p != '\0') {
        return -1;
    }
    return 0;
}