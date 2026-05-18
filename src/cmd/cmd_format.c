#include "utils.h"
#include "output.h"
#include "partition.h"
#include "cmd_format.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

ErrorCode cmd_format(CMDArgs *args) {
    if (strcmp(args->fs, "fat32") != 0) {
        svde_err( "Error: unsupported filesystem type '%s'. Only 'fat32' is supported.\n", args->fs);
        return ERR_NOT_SUPPORTED;
    }

    Disk disk;
    ErrorCode err = disk_open(args->file, &disk);
    if (err != ERR_OK) {
        svde_err( "Failed to open disk image: %s\n", args->file);
        return err;
    }

    int part_index = parse_part_index(args->part);
    if (part_index == PART_INDEX_INVALID) {
        svde_err( "Invalid partition number: %s\n", args->part);
        disk_close(&disk);
        return ERR_INVALID_ARGUMENT;
    }

    uint64_t start_lba, size_sectors;

    if (part_index == PART_INDEX_RAW) {
        // Raw-образ: форматируем весь диск как один раздел
        start_lba = 0;
        size_sectors = disk.size / SECTOR_SIZE;
    } else {
        PartitionTableType table_type;
        err = partition_detect_type(&disk, &table_type);
        if (err != ERR_OK || table_type == PT_UNKNOWN) {
            svde_err( "No valid partition table found on disk.\n");
            disk_close(&disk);
            return err != ERR_OK ? err : ERR_INVALID_SIGNATURE;
        }

        err = partition_get_info(&disk, part_index, &start_lba, &size_sectors);
        if (err != ERR_OK) {
            if (err == ERR_NOT_FOUND) {
                svde_err( "Partition %d does not exist.\n", part_index + 1);
            } else {
                svde_err( "Failed to get partition info (error %d).\n", err);
            }
            disk_close(&disk);
            return err;
        }
    }

    err = fat32_format(&disk, start_lba, size_sectors, 0x80, 0, NULL);
    if (err != ERR_OK) {
        svde_err( "Failed to format as FAT32 (error %d).\n", err);
        disk_close(&disk);
        return err;
    }

    // Обновляем тип раздела, только если есть таблица разделов
    if (part_index != PART_INDEX_RAW) {
        err = partition_set_type(&disk, part_index, "fat32");
        if (err != ERR_OK) {
            svde_err( "Warning: partition formatted but failed to update partition type (error %d).\n", err);
        } else {
            svde_out("Partition type set to FAT32.\n");
        }
    }

    disk_close(&disk);
    svde_out("Successfully formatted as FAT32.\n");
    return ERR_OK;
}