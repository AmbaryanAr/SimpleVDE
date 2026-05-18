#include "output.h"
#include "cmd_part.h"
#include "partition.h"
#include "cmd_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ErrorCode cmd_part_create(CMDArgs *args) {
    Disk disk;
    ErrorCode err = open_disk_and_check_table(args, &disk);
    if (err != ERR_OK) {
        return err;
    }

    int part_index = parse_part_index(args->part);

    if (part_index == PART_INDEX_RAW) {
        disk_close(&disk);
        svde_err("Error: 'raw' is not valid for partition operations. Use MBR or GPT disk.\n");
        return ERR_INVALID_ARGUMENT;
    }

    if (part_index < 0) {
        disk_close(&disk);
        svde_err( "Invalid partition index: %s\n", args->part);
        return ERR_INVALID_ARGUMENT;
    }

    uint64_t size_bytes = 0;
    if (args->size) {
        if (!parse_size(args->size, &size_bytes)) {
            svde_err( "Invalid size: %s\n", args->size);
            disk_close(&disk);
            return ERR_INVALID_ARGUMENT;
        }
    }

    const char *type_str = args->type ? args->type : "linux";
    err = partition_create(&disk, part_index, size_bytes, type_str);
    disk_close(&disk);
    if (err != ERR_OK) {
        svde_err( "Failed to create partition (error %d).\n", err);
        return err;
    }
    svde_out("Partition %d created successfully.\n", part_index + 1);
    return ERR_OK;
}

ErrorCode cmd_part_delete(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba, size_sectors;
    ErrorCode err = open_disk_and_get_partition_info(args, &disk, &start_lba, &size_sectors);
    if (err != ERR_OK) {
        return err;
    }

    int part_index = parse_part_index(args->part);
    err = partition_delete(&disk, part_index);
    disk_close(&disk);
    if (err != ERR_OK) {
        svde_err( "Failed to delete partition (error %d).\n", err);
        return err;
    }
    svde_out("Partition %d deleted successfully.\n", part_index + 1);
    return ERR_OK;
}

ErrorCode cmd_part_set_type(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba, size_sectors;
    ErrorCode err = open_disk_and_get_partition_info(args, &disk, &start_lba, &size_sectors);
    if (err != ERR_OK) {
        return err;
    }

    int part_index = parse_part_index(args->part);
    err = partition_set_type(&disk, part_index, args->type);
    disk_close(&disk);
    if (err != ERR_OK) {
        svde_err( "Failed to set partition type (error %d).\n", err);
        return err;
    }
    svde_out("Partition %d type set to %s.\n", part_index + 1, args->type);
    return ERR_OK;
}

ErrorCode cmd_part_set_active(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba, size_sectors;
    ErrorCode err = open_disk_and_get_partition_info(args, &disk, &start_lba, &size_sectors);
    if (err != ERR_OK) {
        return err;
    }

    int part_index = parse_part_index(args->part);
    err = partition_set_active(&disk, part_index, true);
    disk_close(&disk);
    if (err != ERR_OK) {
        svde_err( "Failed to set partition active (error %d).\n", err);
        return err;
    }
    svde_out("Partition %d set active.\n", part_index + 1);
    return ERR_OK;
}

ErrorCode cmd_part_set_inactive(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba, size_sectors;
    ErrorCode err = open_disk_and_get_partition_info(args, &disk, &start_lba, &size_sectors);
    if (err != ERR_OK) {
        return err;
    }

    int part_index = parse_part_index(args->part);
    err = partition_set_active(&disk, part_index, false);
    disk_close(&disk);
    if (err != ERR_OK) {
        svde_err( "Failed to set partition inactive (error %d).\n", err);
        return err;
    }
    svde_out("Partition %d set inactive.\n", part_index + 1);
    return ERR_OK;
}