#include "disk.h"
#include "utils.h"
#include "cmd_part.h"
#include "partition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static ErrorCode open_disk_and_parse_part(CMDArgs *args, Disk *disk, int *part_index) {
    ErrorCode err = disk_open(args->file, disk);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to open disk image: %s\n", args->file);
        return err;
    }

    *part_index = parse_part_index(args->part);
    if (*part_index < 0) {
        fprintf(stderr, "Invalid partition index: %s\n", args->part);
        disk_close(disk);
        return ERR_INVALID_ARGUMENT;
    }

    return ERR_OK;
}

ErrorCode cmd_part_create(CMDArgs *args) {
    Disk disk;
    int part_index;
    ErrorCode err = open_disk_and_parse_part(args, &disk, &part_index);
    if (err != ERR_OK)
        return err;

    uint64_t size_bytes = 0; // по умолчанию 0 = всё свободное место
    if (args->size) {
        if (!parse_size(args->size, &size_bytes)) {
            fprintf(stderr, "Invalid size: %s\n", args->size);
            disk_close(&disk);
            return ERR_INVALID_ARGUMENT;
        }
    }

    const char *type_str = args->type ? args->type : "linux"; // тип по умолчанию
    err = partition_create(&disk, part_index, size_bytes, type_str);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to create partition (error %d).\n", err);
        disk_close(&disk);
        return err;
    }

    disk_close(&disk);
    printf("Partition %d created successfully.\n", part_index + 1);
    return ERR_OK;
}

ErrorCode cmd_part_delete(CMDArgs *args) {
    Disk disk;
    int part_index;
    ErrorCode err = open_disk_and_parse_part(args, &disk, &part_index);
    if (err != ERR_OK)
        return err;

    err = partition_delete(&disk, part_index);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to delete partition (error %d).\n", err);
        disk_close(&disk);
        return err;
    }

    disk_close(&disk);
    printf("Partition %d deleted successfully.\n", part_index + 1);
    return ERR_OK;
}

ErrorCode cmd_part_set_type(CMDArgs *args) {
    Disk disk;
    int part_index;
    ErrorCode err = open_disk_and_parse_part(args, &disk, &part_index);
    if (err != ERR_OK)
        return err;

    err = partition_set_type(&disk, part_index, args->type);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to set partition type (error %d).\n", err);
        disk_close(&disk);
        return err;
    }

    disk_close(&disk);
    printf("Partition %d type set to %s.\n", part_index + 1, args->type);
    return ERR_OK;
}

ErrorCode cmd_part_set_active(CMDArgs *args) {
    Disk disk;
    int part_index;
    ErrorCode err = open_disk_and_parse_part(args, &disk, &part_index);
    if (err != ERR_OK)
        return err;

    err = partition_set_active(&disk, part_index, true);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to set partition active (error %d).\n", err);
        disk_close(&disk);
        return err;
    }

    disk_close(&disk);
    printf("Partition %d set active.\n", part_index + 1);
    return ERR_OK;
}

ErrorCode cmd_part_set_inactive(CMDArgs *args) {
    Disk disk;
    int part_index;
    ErrorCode err = open_disk_and_parse_part(args, &disk, &part_index);
    if (err != ERR_OK)
        return err;

    err = partition_set_active(&disk, part_index, false);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to set partition inactive (error %d).\n", err);
        disk_close(&disk);
        return err;
    }

    disk_close(&disk);
    printf("Partition %d set inactive.\n", part_index + 1);
    return ERR_OK;
}