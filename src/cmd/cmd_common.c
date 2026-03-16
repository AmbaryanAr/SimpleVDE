#include "cmd_common.h"
#include "utils.h"
#include <stdio.h>

void print_error(ErrorCode err, const char *context) {
    fprintf(stderr, "Error: %s - %s\n", context, error_code_to_string(err));
}

ErrorCode open_disk_and_check_table(CMDArgs *args, Disk *disk) {
    ErrorCode err = disk_open(args->file, disk);
    if (err != ERR_OK) {
        print_error(err, "cannot open disk image");
        return err;
    }

    PartitionTableType table_type;
    err = partition_detect_type(disk, &table_type);
    if (err != ERR_OK || table_type == PT_UNKNOWN) {
        disk_close(disk);
        print_error(err != ERR_OK ? err : ERR_INVALID_SIGNATURE, "invalid partition table");
        return err != ERR_OK ? err : ERR_INVALID_SIGNATURE;
    }
    return ERR_OK;
}

ErrorCode open_disk_and_get_partition_info(CMDArgs *args, Disk *disk, uint64_t *start_lba, uint64_t *size_sectors) {
    ErrorCode err = disk_open(args->file, disk);
    if (err != ERR_OK) {
        print_error(err, "cannot open disk image");
        return err;
    }

    PartitionTableType table_type;
    err = partition_detect_type(disk, &table_type);
    if (err != ERR_OK || table_type == PT_UNKNOWN) {
        disk_close(disk);
        print_error(err != ERR_OK ? err : ERR_INVALID_SIGNATURE, "invalid partition table");
        return err != ERR_OK ? err : ERR_INVALID_SIGNATURE;
    }

    int part_index = parse_part_index(args->part);
    if (part_index < 0) {
        disk_close(disk);
        fprintf(stderr, "Error: invalid partition number '%s'.\n", args->part);
        return ERR_INVALID_ARGUMENT;
    }

    err = partition_get_info(disk, part_index, start_lba, size_sectors);
    if (err != ERR_OK) {
        disk_close(disk);
        if (err == ERR_NOT_FOUND) {
            fprintf(stderr, "Error: partition %d does not exist.\n", part_index + 1);
        } else {
            print_error(err, "cannot get partition info");
        }
        return err;
    }
    return ERR_OK;
}

ErrorCode open_disk_and_get_partition(CMDArgs *args, Disk *disk, uint64_t *start_lba) {
    ErrorCode err = disk_open(args->file, disk);
    if (err != ERR_OK) {
        print_error(err, "cannot open disk image");
        return err;
    }

    PartitionTableType table_type;
    err = partition_detect_type(disk, &table_type);
    if (err != ERR_OK || table_type == PT_UNKNOWN) {
        disk_close(disk);
        print_error(err != ERR_OK ? err : ERR_INVALID_SIGNATURE, "invalid partition table");
        return err != ERR_OK ? err : ERR_INVALID_SIGNATURE;
    }

    int part_index = parse_part_index(args->part);
    if (part_index < 0) {
        disk_close(disk);
        fprintf(stderr, "Error: invalid partition number '%s'.\n", args->part);
        return ERR_INVALID_ARGUMENT;
    }

    uint64_t size_sectors;
    err = partition_get_info(disk, part_index, start_lba, &size_sectors);
    if (err != ERR_OK) {
        disk_close(disk);
        if (err == ERR_NOT_FOUND) {
            fprintf(stderr, "Error: partition %d does not exist.\n", part_index + 1);
        } else {
            print_error(err, "cannot get partition info");
        }
        return err;
    }
    return ERR_OK;
}

ErrorCode open_disk_and_prepare_fs(CMDArgs *args, Disk *disk, uint64_t *start_lba, Fat32Info *info) {
    ErrorCode err = open_disk_and_get_partition(args, disk, start_lba);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_get_info(disk, *start_lba, info);
    if (err != ERR_OK) {
        disk_close(disk);
        fprintf(stderr, "Error: partition %d is not a valid FAT32 filesystem.\n", parse_part_index(args->part) + 1);
        return ERR_INVALID_SIGNATURE;
    }
    return ERR_OK;
}