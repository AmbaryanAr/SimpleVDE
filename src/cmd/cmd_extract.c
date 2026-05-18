#include "utils.h"
#include "output.h"
#include "cmd_common.h"
#include "cmd_extract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// Посекторно копирует диапазон диска в файл
static ErrorCode copy_disk_range(Disk *src_disk, uint64_t start_lba,
                                  uint64_t size_sectors, const char *dest_path) {
    // Проверяем, не существует ли уже файл
    FILE *check = fopen(dest_path, "rb");
    if (check) {
        fclose(check);
        svde_err("Error: output file already exists: %s\n", dest_path);
        return ERR_ALREADY_EXISTS;
    }

    FILE *out = fopen(dest_path, "wb");
    if (!out) {
        svde_err("Cannot create output file: %s\n", dest_path);
        return ERR_DISK_CREATE;
    }

    uint64_t bytes_total = size_sectors * SECTOR_SIZE;
    if (bytes_total == 0) {
        fclose(out);
        svde_out("Partition is empty, nothing to copy.\n");
        return ERR_OK;
    }

    uint64_t bytes_copied = 0;
    size_t buf_size = 1024 * 1024;
    uint8_t *buffer = (uint8_t*)malloc(buf_size);
    if (!buffer) {
        fclose(out);
        return ERR_OUT_OF_MEMORY;
    }

    svde_out("Copying %" PRIu64 " bytes (%" PRIu64 " sectors)...\n",
             bytes_total, size_sectors);

    int last_percent = -1;
    while (bytes_copied < bytes_total) {
        size_t chunk = (bytes_total - bytes_copied < buf_size)
                       ? (size_t)(bytes_total - bytes_copied)
                       : buf_size;

        uint64_t offset = (start_lba * SECTOR_SIZE) + bytes_copied;
        ErrorCode err = disk_read(src_disk, buffer, chunk, offset);
        if (err != ERR_OK) {
            free(buffer);
            fclose(out);
            return err;
        }

        if (fwrite(buffer, 1, chunk, out) != chunk) {
            free(buffer);
            fclose(out);
            return ERR_DISK_WRITE;
        }

        bytes_copied += chunk;
        int percent = (int)(bytes_copied / (bytes_total / 100));
        if (percent != last_percent) {
            svde_progress(percent);
            last_percent = percent;
        }
    }

    free(buffer);
    fclose(out);
    svde_out("Done. Copied %" PRIu64 " bytes to %s\n", bytes_copied, dest_path);
    return ERR_OK;
}

ErrorCode cmd_extract_copy(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba, size_sectors;

    ErrorCode err = open_disk_and_get_partition_info(args, &disk, &start_lba, &size_sectors);
    if (err != ERR_OK) return err;

    int part_index = parse_part_index(args->part);
    if (part_index == PART_INDEX_RAW) {
        svde_err("Error: 'raw' is not valid for extract. Use MBR or GPT disk.\n");
        disk_close(&disk);
        return ERR_INVALID_ARGUMENT;
    }

    svde_out("Extracting partition %d to '%s'...\n", part_index + 1, args->output);

    err = copy_disk_range(&disk, start_lba, size_sectors, args->output);
    disk_close(&disk);
    return err;
}