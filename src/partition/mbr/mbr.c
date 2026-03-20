#include "mbr.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static ErrorCode mbr_read(Disk *disk, MbrSector *mbr) {
    return disk_read(disk, mbr, sizeof(MbrSector), 0);
}

static ErrorCode mbr_write(Disk *disk, const MbrSector *mbr) {
    return disk_write(disk, mbr, sizeof(MbrSector), 0);
}

static bool is_partition_empty(const MbrPartitionEntry *entry) {
    return entry->partition_type == 0;
}

static uint64_t find_next_free_lba(const MbrPartitionEntry *parts) {
    uint64_t next = 2048;
    for (int i = 0; i < 4; i++) {
        if (!is_partition_empty(&parts[i])) {
            uint64_t end = (uint64_t)parts[i].lba_start + parts[i].sector_count;
            if (end > next) {
                next = end;
            }
        }
    }
    return next;
}

ErrorCode mbr_init(Disk *disk) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }

    MbrSector mbr;
    memset(&mbr, 0, sizeof(mbr));

    const uint8_t boot_code[] = {
        0x33, 0xC0, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0x8E, 0xD8, 0x8E, 0xC0,
        0xBE, 0x74, 0x7C, 0xB4, 0x0E, 0xAC, 0x3C, 0x00, 0x74, 0x04, 0xCD,
        0x10, 0xEB, 0xF7, 0xEB, 0xFE,
        'N','o',' ','b','o','o','t','a','b','l','e',' ','d','i','s','k','!',
        0x0D, 0x0A, 0
    };
    size_t boot_size = sizeof(boot_code);
    if (boot_size > MBR_PARTITION_TABLE_OFFSET) {
        boot_size = MBR_PARTITION_TABLE_OFFSET;
    }
    memcpy(mbr.bootstrap, boot_code, boot_size);

    mbr.signature = MBR_SIGNATURE;
    return mbr_write(disk, &mbr);
}

ErrorCode mbr_create_partition(Disk *disk, int index, uint64_t size_sectors, uint8_t type) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }
    if (index < 0 || index >= 4) {
        return ERR_INVALID_ARGUMENT;
    }
    if (size_sectors == 0) {
        size_sectors = UINT64_MAX;
    }

    MbrSector mbr;
    ErrorCode err = mbr_read(disk, &mbr);
    if (err != ERR_OK) {
        return err;
    }

    if (!is_partition_empty(&mbr.partitions[index])) {
        return ERR_ALREADY_EXISTS;
    }

    uint64_t disk_sectors = disk->size / SECTOR_SIZE;
    uint64_t next_lba = find_next_free_lba(mbr.partitions);

    if (size_sectors == UINT64_MAX) {
        size_sectors = disk_sectors - next_lba;
    }

    if (size_sectors > UINT32_MAX) {
        size_sectors = UINT32_MAX;
    }

    if (next_lba + size_sectors > disk_sectors) {
        return ERR_NO_FREE_SPACE;
    }

    MbrPartitionEntry *entry = &mbr.partitions[index];
    memset(entry, 0, sizeof(*entry));
    entry->boot_flag = 0;
    entry->start_head = 0;
    entry->start_sector = 1;
    entry->start_cylinder = 0;
    entry->partition_type = type;
    entry->end_head = 0xFE;
    entry->end_sector = 0xFF;
    entry->end_cylinder = 0xFF;
    entry->lba_start = (uint32_t)next_lba;
    entry->sector_count = (uint32_t)size_sectors;

    return mbr_write(disk, &mbr);
}

ErrorCode mbr_delete_partition(Disk *disk, int index) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }
    if (index < 0 || index >= 4) {
        return ERR_INVALID_ARGUMENT;
    }

    MbrSector mbr;
    ErrorCode err = mbr_read(disk, &mbr);
    if (err != ERR_OK) {
        return err;
    }

    if (is_partition_empty(&mbr.partitions[index])) {
        return ERR_NOT_FOUND;
    }

    memset(&mbr.partitions[index], 0, sizeof(MbrPartitionEntry));
    return mbr_write(disk, &mbr);
}

ErrorCode mbr_set_active(Disk *disk, int index) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }
    if (index < 0 || index >= 4) {
        return ERR_INVALID_ARGUMENT;
    }

    MbrSector mbr;
    ErrorCode err = mbr_read(disk, &mbr);
    if (err != ERR_OK) {
        return err;
    }

    if (is_partition_empty(&mbr.partitions[index])) {
        return ERR_NOT_FOUND;
    }

    for (int i = 0; i < 4; i++) {
        mbr.partitions[i].boot_flag = 0;
    }
    mbr.partitions[index].boot_flag = 0x80;
    return mbr_write(disk, &mbr);
}

ErrorCode mbr_set_inactive(Disk *disk, int index) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }
    if (index < 0 || index >= 4) {
        return ERR_INVALID_ARGUMENT;
    }

    MbrSector mbr;
    ErrorCode err = mbr_read(disk, &mbr);
    if (err != ERR_OK) {
        return err;
    }

    if (is_partition_empty(&mbr.partitions[index])) {
        return ERR_NOT_FOUND;
    }

    mbr.partitions[index].boot_flag = 0;
    return mbr_write(disk, &mbr);
}

ErrorCode mbr_set_partition_type(Disk *disk, int index, uint8_t type) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }
    if (index < 0 || index >= 4) {
        return ERR_INVALID_ARGUMENT;
    }

    MbrSector mbr;
    ErrorCode err = mbr_read(disk, &mbr);
    if (err != ERR_OK) {
        return err;
    }

    if (is_partition_empty(&mbr.partitions[index])) {
        return ERR_NOT_FOUND;
    }

    mbr.partitions[index].partition_type = type;
    return mbr_write(disk, &mbr);
}

ErrorCode mbr_get_partition_info(Disk *disk, int index, uint64_t *start_lba, uint64_t *size_sectors) {
    if (!disk || !disk->is_open) {
        return ERR_DISK_OPEN;
    }
    if (index < 0 || index >= 4) {
        return ERR_INVALID_ARGUMENT;
    }
    if (!start_lba || !size_sectors) {
        return ERR_INVALID_ARGUMENT;
    }

    MbrSector mbr;
    ErrorCode err = mbr_read(disk, &mbr);
    if (err != ERR_OK) {
        return err;
    }

    if (is_partition_empty(&mbr.partitions[index])) {
        return ERR_NOT_FOUND;
    }

    *start_lba = mbr.partitions[index].lba_start;
    *size_sectors = mbr.partitions[index].sector_count;
    return ERR_OK;
}

void mbr_print_info(Disk *disk) {
    if (!disk || !disk->is_open) {
        printf("Disk not open.\n");
        return;
    }

    MbrSector mbr;
    if (mbr_read(disk, &mbr) != ERR_OK) {
        printf("Failed to read MBR.\n");
        return;
    }

    int count = 0;
    for (int i = 0; i < 4; i++) {
        if (!is_partition_empty(&mbr.partitions[i])) {
            count++;
        }
    }
    printf("MBR: %d primary partition(s)\n", count);
    for (int i = 0; i < 4; i++) {
        if (!is_partition_empty(&mbr.partitions[i])) {
            MbrPartitionEntry *p = &mbr.partitions[i];
            printf("  Part %d: type=0x%02X, %s, LBA start=%u, size=%u sectors\n",
                   i + 1, p->partition_type,
                   (p->boot_flag == 0x80) ? "active" : "inactive",
                   p->lba_start, p->sector_count);
        }
    }
}

static const struct {
    const char *name;
    uint8_t type;
} mbr_type_table[] = {
    {"empty",    0x00},
    {"fat12",    0x01},
    {"fat16",    0x06},
    {"ntfs",     0x07},
    {"fat32",    0x0B},
    {"fat32lba", 0x0C},
    {"fat16lba", 0x0E},
    {"extended", 0x05},
    {"linux",    0x83},
    {"swap",     0x82},
    {"lvm",      0x8E},
    {"efi",      0xEF},
    {"gpt",      0xEE},
    {NULL, 0}
};

bool mbr_type_from_name(const char *name, uint8_t *type) {
    if (!name || !type) {
        return false;
    }

    char name_upper[32];
    if (!strlcpy_safe(name_upper, sizeof(name_upper), name)) {
        return false;
    }
    str_toupper(name_upper);

    for (int i = 0; mbr_type_table[i].name != NULL; i++) {
        char table_upper[32];
        strlcpy_safe(table_upper, sizeof(table_upper), mbr_type_table[i].name);
        str_toupper(table_upper);
        if (strcmp(name_upper, table_upper) == 0) {
            *type = mbr_type_table[i].type;
            return true;
        }
    }
    return false;
}