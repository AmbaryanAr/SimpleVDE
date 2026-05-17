#include "fat32.h"
#include "utils.h"
#include "output.h"
#include "cmd_disk.h"
#include "partition.h"
#include "cmd_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// Читает count_bytes байт с диска от offset_bytes и выводит hex-дамп с ASCII-колонкой
static ErrorCode disk_read_and_dump(Disk *disk, uint64_t offset_bytes, uint64_t count_bytes) {
    if (offset_bytes + count_bytes > disk->size) {
        svde_err( "Read range exceeds disk size.\n");
        return ERR_DISK_READ;
    }

    uint8_t *buffer = (uint8_t*)malloc(count_bytes);
    if (!buffer) {
        return ERR_OUT_OF_MEMORY;
    }

    ErrorCode err = disk_read(disk, buffer, count_bytes, offset_bytes);
    if (err != ERR_OK) {
        svde_err( "Failed to read from disk.\n");
        free(buffer);
        return err;
    }

    svde_out("Read %" PRIu64 " bytes from offset %" PRIu64 ":\n", count_bytes, offset_bytes);
    for (uint64_t i = 0; i < count_bytes; i += 16) {
        printf("%016" PRIx64 ": ", offset_bytes + i);
        uint64_t line_len = count_bytes - i;
        if (line_len > 16) {
            line_len = 16;
        }

        for (uint64_t j = 0; j < line_len; j++) {
            printf("%02x ", buffer[i + j]);
        }
        if (line_len < 16) {
            for (uint64_t j = 0; j < 16 - line_len; j++) {
                printf("   ");
            }
        }
        printf(" ");
        for (uint64_t j = 0; j < line_len; j++) {
            uint8_t c = buffer[i + j];
            if (c >= 32 && c <= 126) {
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        printf("\n");
    }

    free(buffer);
    return ERR_OK;
}

ErrorCode cmd_disk_create(CMDArgs *args) {
    uint64_t size_bytes;
    if (!parse_size(args->size, &size_bytes)) {
        svde_err( "Error: invalid size format: %s\n", args->size);
        return ERR_INVALID_ARGUMENT;
    }

    Disk disk;
    ErrorCode err = disk_create(args->file, size_bytes, &disk);
    if (err != ERR_OK) {
        svde_err( "Failed to create disk image: %s\n", args->file);
        return err;
    }

    if (args->table) {
        PartitionTableType table_type;
        if (strcmp(args->table, "mbr") == 0) {
            table_type = PT_MBR;
        } else if (strcmp(args->table, "gpt") == 0) {
            table_type = PT_GPT;
        } else {
            svde_err( "Error: unknown partition table type: %s\n", args->table);
            disk_close(&disk);
            return ERR_INVALID_ARGUMENT;
        }
        err = partition_create_table(&disk, table_type);
        if (err != ERR_OK) {
            svde_err( "Failed to create partition table: %s\n", args->table);
            disk_close(&disk);
            return err;
        }
    }

    disk_close(&disk);
    svde_out("Disk image '%s' created successfully (%" PRIu64 " bytes).\n", args->file, size_bytes);
    return ERR_OK;
}

ErrorCode cmd_disk_info(CMDArgs *args) {
    Disk disk;
    ErrorCode err = disk_open(args->file, &disk);
    if (err != ERR_OK) {
        svde_err( "Failed to open disk image: %s\n", args->file);
        return err;
    }

    svde_out("Disk: %s\n", disk.path);
    svde_out("Size: %" PRIu64 " bytes (%.2f MB)\n", disk.size, disk.size / (1024.0 * 1024.0));

    PartitionTableType table_type;
    err = partition_detect_type(&disk, &table_type);
    if (err == ERR_OK) {
        const char *type_str = "Unknown";
        if (table_type == PT_MBR) {
            type_str = "MBR";
        } else if (table_type == PT_GPT) {
            type_str = "GPT";
        }
        svde_out("Partition table: %s\n", type_str);
        if (table_type != PT_UNKNOWN) {
            partition_print_info(&disk);
        } else {
            svde_out("No valid partition table found.\n");
        }
    } else {
        svde_out("Failed to detect partition table (error %d).\n", err);
    }

    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_disk_read(CMDArgs *args) {
    Disk disk;
    ErrorCode err = disk_open(args->file, &disk);
    if (err != ERR_OK) {
        svde_err( "Failed to open disk image: %s\n", args->file);
        return err;
    }

    uint64_t offset, count;
    if (!parse_size(args->offset, &offset)) {
        svde_err( "Invalid offset: %s\n", args->offset);
        disk_close(&disk);
        return ERR_INVALID_ARGUMENT;
    }
    if (!parse_size(args->count, &count)) {
        svde_err( "Invalid count: %s\n", args->count);
        disk_close(&disk);
        return ERR_INVALID_ARGUMENT;
    }

    err = disk_read_and_dump(&disk, offset, count);
    disk_close(&disk);
    return err;
}

ErrorCode cmd_disk_read_s(CMDArgs *args) {
    Disk disk;
    ErrorCode err = disk_open(args->file, &disk);
    if (err != ERR_OK) {
        svde_err( "Failed to open disk image: %s\n", args->file);
        return err;
    }

    uint64_t offset_sectors, count_sectors;
    if (!parse_integer(args->offset, &offset_sectors)) {
        svde_err( "Invalid offset (sectors): %s\n", args->offset);
        disk_close(&disk);
        return ERR_INVALID_ARGUMENT;
    }
    if (!parse_integer(args->count, &count_sectors)) {
        svde_err( "Invalid count (sectors): %s\n", args->count);
        disk_close(&disk);
        return ERR_INVALID_ARGUMENT;
    }

    uint64_t offset_bytes = offset_sectors * SECTOR_SIZE;
    uint64_t count_bytes = count_sectors * SECTOR_SIZE;

    err = disk_read_and_dump(&disk, offset_bytes, count_bytes);
    disk_close(&disk);
    return err;
}

ErrorCode cmd_mbr_write(CMDArgs *args) {
    uint8_t *code_buf = NULL;
    size_t code_size = 0;
    ErrorCode err = read_whole_file(args->src, &code_buf, &code_size);
    if (err != ERR_OK) {
        svde_err( "Error: cannot read source file '%s'.\n", args->src);
        return err;
    }

    if (code_size == 0) {
        free(code_buf);
        svde_err( "Error: source file is empty.\n");
        return ERR_INVALID_ARGUMENT;
    }

    Disk disk;
    err = disk_open(args->file, &disk);
    if (err != ERR_OK) {
        free(code_buf);
        svde_err( "Failed to open disk image: %s\n", args->file);
        return err;
    }

    uint8_t mbr[512];
    err = disk_read(&disk, mbr, 512, 0);
    if (err != ERR_OK) {
        free(code_buf);
        disk_close(&disk);
        svde_err( "Failed to read MBR.\n");
        return err;
    }

    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        svde_err( "Warning: MBR signature not found, but will write anyway.\n");
    }

    size_t write_size = (code_size < 446) ? code_size : 446;
    if (code_size > 446) {
        svde_err( "Warning: source file is larger than 446 bytes. Only first 446 bytes will be written.\n");
    }

    memcpy(mbr, code_buf, write_size);

    err = disk_write(&disk, mbr, 512, 0);
    if (err != ERR_OK) {
        free(code_buf);
        disk_close(&disk);
        svde_err( "Failed to write MBR.\n");
        return err;
    }

    svde_out("Successfully wrote %zu bytes to MBR (first 446 bytes of sector 0).\n", write_size);
    free(code_buf);
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_bpb_write(CMDArgs *args) {
    uint8_t *code_buf = NULL;
    size_t code_size = 0;
    ErrorCode err = read_whole_file(args->src, &code_buf, &code_size);
    if (err != ERR_OK) {
        svde_err( "Error: cannot read source file '%s'.\n", args->src);
        return err;
    }

    if (code_size == 0) {
        free(code_buf);
        svde_err( "Error: source file is empty.\n");
        return ERR_INVALID_ARGUMENT;
    }

    Disk disk;
    uint64_t start_lba, size_sectors;
    err = open_disk_and_get_partition_info(args, &disk, &start_lba, &size_sectors);
    if (err != ERR_OK) {
        free(code_buf);
        return err;
    }

    Fat32Info info;
    err = fat32_get_info(&disk, start_lba, &info);
    if (err != ERR_OK) {
        disk_close(&disk);
        free(code_buf);
        svde_err( "Partition %s is not a valid FAT32 filesystem.\n", args->part);
        return ERR_INVALID_SIGNATURE;
    }

    uint8_t sector[512];
    err = disk_read(&disk, sector, 512, start_lba * 512);
    if (err != ERR_OK) {
        disk_close(&disk);
        free(code_buf);
        svde_err( "Failed to read BPB sector at LBA %" PRIu64 ".\n", start_lba);
        return err;
    }

    const uint32_t code_offset = sizeof(Fat32BPB);
    const uint32_t max_code_size = 512 - code_offset - 2;

    if (code_size > max_code_size) {
        svde_err( "Warning: source file is larger than available space (%u bytes). Only first %u bytes will be written.\n", max_code_size, max_code_size);
        code_size = max_code_size;
    }

    memcpy(sector + code_offset, code_buf, code_size);

    err = disk_write(&disk, sector, 512, start_lba * 512);
    if (err != ERR_OK) {
        disk_close(&disk);
        free(code_buf);
        svde_err( "Failed to write BPB sector at LBA %" PRIu64 ".\n", start_lba);
        return err;
    }

    err = disk_write(&disk, sector, 512, (start_lba + 6) * 512);
    if (err != ERR_OK) {
        svde_err( "Warning: failed to write backup BPB sector.\n");
    }

    svde_out("Successfully wrote %zu bytes of boot code to partition %s BPB (LBA %" PRIu64 ", offset %u).\n",
           code_size, args->part, start_lba, code_offset);

    free(code_buf);
    disk_close(&disk);
    return ERR_OK;
}