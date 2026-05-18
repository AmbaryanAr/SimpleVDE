#include "mbr.h"
#include "gpt.h"
#include "utils.h"
#include "output.h"
#include "partition.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

ErrorCode partition_detect_type(Disk *disk, PartitionTableType *type) {
    if (!disk || !type) {
        return ERR_INVALID_ARGUMENT;
    }

    uint8_t sector[512];
    ErrorCode err = disk_read(disk, sector, 512, 0);
    if (err != ERR_OK) {
        return err;
    }

    // Проверяем, не является ли это FAT32 boot-сектором (сигнатура EB xx 90)
    // Такой сектор имеет 55 AA, но не является MBR
    if (sector[0] == 0xEB && sector[2] == 0x90) {
        *type = PT_UNKNOWN;
        return ERR_OK;
    }

    if (sector[510] == 0x55 && sector[511] == 0xAA) {
        // Проверяем защитный MBR для GPT
        int protective = 0;
        for (int i = 0; i < 4; i++) {
            uint8_t part_type = sector[MBR_PARTITION_TABLE_OFFSET + i * MBR_PARTITION_ENTRY_SIZE + 4];
            if (part_type == 0xEE) {
                protective = 1;
                break;
            }
        }
        if (protective) {
            uint8_t gpt_sig[8];
            err = disk_read(disk, gpt_sig, 8, 512);
            if (err == ERR_OK && memcmp(gpt_sig, "EFI PART", 8) == 0) {
                *type = PT_GPT;
                return ERR_OK;
            }
        }
        *type = PT_MBR;
        return ERR_OK;
    }
    *type = PT_UNKNOWN;
    return ERR_OK;
}

ErrorCode partition_create_table(Disk *disk, PartitionTableType type) {
    if (!disk) {
        return ERR_INVALID_ARGUMENT;
    }
    if (type == PT_MBR) {
        return mbr_init(disk);
    } else if (type == PT_GPT) {
        return gpt_init(disk);
    } else {
        return ERR_NOT_SUPPORTED;
    }
}

ErrorCode partition_create(Disk *disk, int part_index, uint64_t size_bytes, const char *type_str) {
    if (!disk || !type_str) {
        return ERR_INVALID_ARGUMENT;
    }

    PartitionTableType table_type;
    ErrorCode err = partition_detect_type(disk, &table_type);
    if (err != ERR_OK) {
        return err;
    }

    uint64_t size_sectors = size_bytes / SECTOR_SIZE;
    if (size_bytes % SECTOR_SIZE != 0) {
        size_sectors++;
    }

    if (table_type == PT_MBR) {
        uint8_t mbr_type;
        if (!mbr_type_from_name(type_str, &mbr_type)) {
            uint64_t val;
            if (parse_integer(type_str, &val) && val <= 0xFF) {
                mbr_type = (uint8_t)val;
            } else {
                return ERR_INVALID_ARGUMENT;
            }
        }
        return mbr_create_partition(disk, part_index, size_sectors, mbr_type);
    } else if (table_type == PT_GPT) {
        uint8_t guid[16];
        if (!gpt_type_from_name(type_str, guid)) {
            if (gpt_guid_from_string(type_str, guid) != 0) {
                return ERR_INVALID_ARGUMENT;
            }
        }
        return gpt_create_partition(disk, part_index, size_sectors, guid);
    } else {
        return ERR_NOT_SUPPORTED;
    }
}

ErrorCode partition_delete(Disk *disk, int part_index) {
    PartitionTableType table_type;
    ErrorCode err = partition_detect_type(disk, &table_type);
    if (err != ERR_OK) {
        return err;
    }

    if (table_type == PT_MBR) {
        return mbr_delete_partition(disk, part_index);
    } else if (table_type == PT_GPT) {
        return gpt_delete_partition(disk, part_index);
    } else {
        return ERR_NOT_SUPPORTED;
    }
}

ErrorCode partition_set_type(Disk *disk, int part_index, const char *type_str) {
    PartitionTableType table_type;
    ErrorCode err = partition_detect_type(disk, &table_type);
    if (err != ERR_OK) {
        return err;
    }

    if (table_type == PT_MBR) {
        uint8_t mbr_type;
        if (!mbr_type_from_name(type_str, &mbr_type)) {
            uint64_t val;
            if (parse_integer(type_str, &val) && val <= 0xFF) {
                mbr_type = (uint8_t)val;
            } else {
                return ERR_INVALID_ARGUMENT;
            }
        }
        return mbr_set_partition_type(disk, part_index, mbr_type);
    } else if (table_type == PT_GPT) {
        uint8_t guid[16];
        if (!gpt_type_from_name(type_str, guid)) {
            if (gpt_guid_from_string(type_str, guid) != 0) {
                return ERR_INVALID_ARGUMENT;
            }
        }
        return gpt_set_partition_type(disk, part_index, guid);
    } else {
        return ERR_NOT_SUPPORTED;
    }
}

ErrorCode partition_set_active(Disk *disk, int part_index, bool active) {
    PartitionTableType table_type;
    ErrorCode err = partition_detect_type(disk, &table_type);
    if (err != ERR_OK) {
        return err;
    }

    if (table_type == PT_MBR) {
        if (active) {
            return mbr_set_active(disk, part_index);
        } else {
            return mbr_set_inactive(disk, part_index);
        }
    } else {
        return ERR_NOT_SUPPORTED;
    }
}

ErrorCode partition_get_info(Disk *disk, int part_index, uint64_t *start_lba, uint64_t *size_sectors) {
    if (!disk || !start_lba || !size_sectors) {
        return ERR_INVALID_ARGUMENT;
    }

    // Raw-образ: весь диск как один раздел
    if (part_index == PART_INDEX_RAW) {
        *start_lba = 0;
        *size_sectors = disk->size / SECTOR_SIZE;
        return ERR_OK;
    }

    PartitionTableType table_type;
    ErrorCode err = partition_detect_type(disk, &table_type);
    if (err != ERR_OK) {
        return err;
    }

    if (table_type == PT_MBR) {
        return mbr_get_partition_info(disk, part_index, start_lba, size_sectors);
    } else if (table_type == PT_GPT) {
        return gpt_get_partition_info(disk, part_index, start_lba, size_sectors);
    } else {
        return ERR_NOT_SUPPORTED;
    }
}

void partition_print_info(Disk *disk) {
    PartitionTableType table_type;
    if (partition_detect_type(disk, &table_type) != ERR_OK) {
        svde_out("Failed to detect partition table.\n");
        return;
    }
    if (table_type == PT_MBR) {
        mbr_print_info(disk);
    } else if (table_type == PT_GPT) {
        gpt_print_info(disk);
    } else {
        // PT_UNKNOWN — возможно, raw-образ
        svde_out("No valid partition table found (raw image?).\n");
    }
}