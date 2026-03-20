#include "utils.h"
#include "partition.h"
#include "cmd_format.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

ErrorCode cmd_format(CMDArgs *args) {
    if (strcmp(args->fs, "fat32") != 0) {
        fprintf(stderr, "Error: unsupported filesystem type '%s'. Only 'fat32' is supported.\n", args->fs);
        return ERR_NOT_SUPPORTED;
    }

    Disk disk;
    ErrorCode err = disk_open(args->file, &disk);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to open disk image: %s\n", args->file);
        return err;
    }

    PartitionTableType table_type;
    err = partition_detect_type(&disk, &table_type);
    if (err != ERR_OK || table_type == PT_UNKNOWN) {
        fprintf(stderr, "No valid partition table found on disk.\n");
        disk_close(&disk);
        return err != ERR_OK ? err : ERR_INVALID_SIGNATURE;
    }

    int part_index = parse_part_index(args->part);
    if (part_index < 0) {
        fprintf(stderr, "Invalid partition number: %s\n", args->part);
        disk_close(&disk);
        return ERR_INVALID_ARGUMENT;
    }

    uint64_t start_lba, size_sectors;
    err = partition_get_info(&disk, part_index, &start_lba, &size_sectors);
    if (err != ERR_OK) {
        if (err == ERR_NOT_FOUND) {
            fprintf(stderr, "Partition %d does not exist.\n", part_index + 1);
        } else {
            fprintf(stderr, "Failed to get partition info (error %d).\n", err);
        }
        disk_close(&disk);
        return err;
    }

    err = fat32_format(&disk, start_lba, size_sectors, 0x80, 0, NULL);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to format partition %d as FAT32 (error %d).\n", part_index + 1, err);
        disk_close(&disk);
        return err;
    }

    err = partition_set_type(&disk, part_index, "fat32");
    if (err != ERR_OK) {
        fprintf(stderr, "Warning: partition formatted but failed to update partition type (error %d).\n", err);
    } else {
        printf("Partition type set to FAT32.\n");
    }

    disk_close(&disk);
    printf("Partition %d successfully formatted as FAT32.\n", part_index + 1);
    return ERR_OK;
}