#include "disk.h"
#include "utils.h"
#include "cmd_disk.h"
#include "partition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static ErrorCode disk_read_and_dump(Disk *disk, uint64_t offset_bytes, uint64_t count_bytes) {
    if (offset_bytes + count_bytes > disk->size) {
        fprintf(stderr, "Read range exceeds disk size.\n");
        return ERR_DISK_READ;
    }

    uint8_t *buffer = (uint8_t*)malloc(count_bytes);
    if (!buffer) return ERR_OUT_OF_MEMORY;

    ErrorCode err = disk_read(disk, buffer, count_bytes, offset_bytes);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to read from disk.\n");
        free(buffer);
        return err;
    }

    printf("Read %" PRIu64 " bytes from offset %" PRIu64 ":\n", count_bytes, offset_bytes);
    for (uint64_t i = 0; i < count_bytes; i += 16) {
        printf("%016" PRIx64 ": ", offset_bytes + i);
        uint64_t line_len = count_bytes - i;
        if (line_len > 16) line_len = 16;
        
        // Hex
        for (uint64_t j = 0; j < line_len; j++) {
            printf("%02x ", buffer[i + j]);
        }
        // Если строка неполная, дополняем пробелами
        if (line_len < 16) {
            for (uint64_t j = 0; j < 16 - line_len; j++) {
                printf("   ");
            }
        }
        printf(" ");
        // ASCII
        for (uint64_t j = 0; j < line_len; j++) {
            uint8_t c = buffer[i + j];
            if (c >= 32 && c <= 126) printf("%c", c);
            else printf(".");
        }
        printf("\n");
    }

    free(buffer);
    return ERR_OK;
}

ErrorCode cmd_disk_create(CMDArgs *args) {
    uint64_t size_bytes;
    if (!parse_size(args->size, &size_bytes)) {
        fprintf(stderr, "Error: invalid size format: %s\n", args->size);
        return ERR_INVALID_ARGUMENT;
    }

    Disk disk;
    ErrorCode err = disk_create(args->file, size_bytes, &disk);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to create disk image: %s\n", args->file);
        return err;
    }

    if (args->table) {
        PartitionTableType table_type;
        if (strcmp(args->table, "mbr") == 0)
            table_type = PT_MBR;
        else if (strcmp(args->table, "gpt") == 0)
            table_type = PT_GPT;
        else {
            fprintf(stderr, "Error: unknown partition table type: %s\n", args->table);
            disk_close(&disk);
            return ERR_INVALID_ARGUMENT;
        }
        err = partition_create_table(&disk, table_type);
        if (err != ERR_OK) {
            fprintf(stderr, "Failed to create partition table: %s\n", args->table);
            disk_close(&disk);
            return err;
        }
    }

    disk_close(&disk);
    printf("Disk image '%s' created successfully (%" PRIu64 " bytes).\n", args->file, size_bytes);
    return ERR_OK;
}

ErrorCode cmd_disk_info(CMDArgs *args) {
    Disk disk;
    ErrorCode err = disk_open(args->file, &disk);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to open disk image: %s\n", args->file);
        return err;
    }

    printf("Disk: %s\n", disk.path);
    printf("Size: %" PRIu64 " bytes (%.2f MB)\n", disk.size, disk.size / (1024.0 * 1024.0));

    PartitionTableType table_type;
    err = partition_detect_type(&disk, &table_type);
    if (err == ERR_OK) {
        const char *type_str = "Unknown";
        if (table_type == PT_MBR) type_str = "MBR";
        else if (table_type == PT_GPT) type_str = "GPT";
        printf("Partition table: %s\n", type_str);
        if (table_type != PT_UNKNOWN)
            partition_print_info(&disk);
        else
            printf("No valid partition table found.\n");
    } else {
        printf("Failed to detect partition table (error %d).\n", err);
    }

    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_disk_read(CMDArgs *args) {
    Disk disk;
    ErrorCode err = disk_open(args->file, &disk);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to open disk image: %s\n", args->file);
        return err;
    }

    uint64_t offset, count;
    if (!parse_size(args->offset, &offset)) {
        fprintf(stderr, "Invalid offset: %s\n", args->offset);
        disk_close(&disk);
        return ERR_INVALID_ARGUMENT;
    }
    if (!parse_size(args->count, &count)) {
        fprintf(stderr, "Invalid count: %s\n", args->count);
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
        fprintf(stderr, "Failed to open disk image: %s\n", args->file);
        return err;
    }

    uint64_t offset_sectors, count_sectors;
    if (!parse_integer(args->offset, &offset_sectors)) {
        fprintf(stderr, "Invalid offset (sectors): %s\n", args->offset);
        disk_close(&disk);
        return ERR_INVALID_ARGUMENT;
    }
    if (!parse_integer(args->count, &count_sectors)) {
        fprintf(stderr, "Invalid count (sectors): %s\n", args->count);
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
    if (!args->file) {
        fprintf(stderr, "Error: missing -file= parameter.\n");
        return ERR_MISSING_ARGUMENT;
    }
    if (!args->src) {
        fprintf(stderr, "Error: missing -src= parameter.\n");
        return ERR_MISSING_ARGUMENT;
    }

    // Читаем файл с кодом
    uint8_t *code_buf = NULL;
    size_t code_size = 0;
    ErrorCode err = read_whole_file(args->src, &code_buf, &code_size);
    if (err != ERR_OK) {
        fprintf(stderr, "Error: cannot read source file '%s'.\n", args->src);
        return err;
    }

    if (code_size == 0) {
        free(code_buf);
        fprintf(stderr, "Error: source file is empty.\n");
        return ERR_INVALID_ARGUMENT;
    }

    // Открываем диск
    Disk disk;
    err = disk_open(args->file, &disk);
    if (err != ERR_OK) {
        free(code_buf);
        fprintf(stderr, "Failed to open disk image: %s\n", args->file);
        return err;
    }

    // Читаем текущий MBR (512 байт)
    uint8_t mbr[512];
    err = disk_read(&disk, mbr, 512, 0);
    if (err != ERR_OK) {
        free(code_buf);
        disk_close(&disk);
        fprintf(stderr, "Failed to read MBR.\n");
        return err;
    }

    // Проверяем сигнатуру в конце (не обязательно, но полезно)
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        fprintf(stderr, "Warning: MBR signature not found, but will write anyway.\n");
    }

    // Определяем, сколько байт записать (не более 446)
    size_t write_size = (code_size < 446) ? code_size : 446;
    if (code_size > 446) {
        fprintf(stderr, "Warning: source file is larger than 446 bytes. Only first 446 bytes will be written.\n");
    }

    // Копируем код в начало MBR
    memcpy(mbr, code_buf, write_size);

    // Записываем обновлённый MBR обратно
    err = disk_write(&disk, mbr, 512, 0);
    if (err != ERR_OK) {
        free(code_buf);
        disk_close(&disk);
        fprintf(stderr, "Failed to write MBR.\n");
        return err;
    }

    printf("Successfully wrote %zu bytes to MBR (first 446 bytes of sector 0).\n", write_size);
    free(code_buf);
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_bpb_write(CMDArgs *args) {
    if (!args->file) {
        fprintf(stderr, "Error: missing -file= parameter.\n");
        return ERR_MISSING_ARGUMENT;
    }
    if (!args->part) {
        fprintf(stderr, "Error: missing -part= parameter.\n");
        return ERR_MISSING_ARGUMENT;
    }
    if (!args->src) {
        fprintf(stderr, "Error: missing -src= parameter.\n");
        return ERR_MISSING_ARGUMENT;
    }

    // Читаем файл с кодом
    uint8_t *code_buf = NULL;
    size_t code_size = 0;
    ErrorCode err = read_whole_file(args->src, &code_buf, &code_size);
    if (err != ERR_OK) {
        fprintf(stderr, "Error: cannot read source file '%s'.\n", args->src);
        return err;
    }

    if (code_size == 0) {
        free(code_buf);
        fprintf(stderr, "Error: source file is empty.\n");
        return ERR_INVALID_ARGUMENT;
    }

    // Открываем диск
    Disk disk;
    err = disk_open(args->file, &disk);
    if (err != ERR_OK) {
        free(code_buf);
        fprintf(stderr, "Failed to open disk image: %s\n", args->file);
        return err;
    }

    // Проверяем таблицу разделов
    PartitionTableType table_type;
    err = partition_detect_type(&disk, &table_type);
    if (err != ERR_OK || table_type == PT_UNKNOWN) {
        disk_close(&disk);
        free(code_buf);
        fprintf(stderr, "No valid partition table found.\n");
        return err != ERR_OK ? err : ERR_INVALID_SIGNATURE;
    }

    int part_index = parse_part_index(args->part);
    if (part_index < 0) {
        disk_close(&disk);
        free(code_buf);
        fprintf(stderr, "Invalid partition number: %s\n", args->part);
        return ERR_INVALID_ARGUMENT;
    }

    uint64_t start_lba, size_sectors;
    err = partition_get_info(&disk, part_index, &start_lba, &size_sectors);
    if (err != ERR_OK) {
        disk_close(&disk);
        free(code_buf);
        if (err == ERR_NOT_FOUND)
            fprintf(stderr, "Partition %d does not exist.\n", part_index + 1);
        else
            fprintf(stderr, "Failed to get partition info.\n");
        return err;
    }

    // Проверяем, что раздел содержит FAT32
    Fat32Info info;
    err = fat32_get_info(&disk, start_lba, &info);
    if (err != ERR_OK) {
        disk_close(&disk);
        free(code_buf);
        fprintf(stderr, "Partition %d is not a valid FAT32 filesystem.\n", part_index + 1);
        return ERR_INVALID_SIGNATURE;
    }

    // Читаем BPB-сектор раздела
    uint8_t sector[512];
    err = disk_read(&disk, sector, 512, start_lba * 512);
    if (err != ERR_OK) {
        free(code_buf);
        disk_close(&disk);
        fprintf(stderr, "Failed to read BPB sector at LBA %" PRIu64 ".\n", start_lba);
        return err;
    }

    const uint32_t code_offset = sizeof(Fat32BPB); // 90 байт
    const uint32_t max_code_size = 512 - code_offset - 2; // 420 байт (оставляем сигнатуру)

    if (code_size > max_code_size) {
        fprintf(stderr, "Warning: source file is larger than available space (%u bytes). "
                        "Only first %u bytes will be written.\n", max_code_size, max_code_size);
        code_size = max_code_size;
    }

    // Копируем код в буфер сектора
    memcpy(sector + code_offset, code_buf, code_size);

    // Записываем обновлённый сектор
    err = disk_write(&disk, sector, 512, start_lba * 512);
    if (err != ERR_OK) {
        free(code_buf);
        disk_close(&disk);
        fprintf(stderr, "Failed to write BPB sector at LBA %" PRIu64 ".\n", start_lba);
        return err;
    }

    printf("Successfully wrote %zu bytes of boot code to partition %d BPB (LBA %" PRIu64 ", offset %u).\n",
           code_size, part_index + 1, start_lba, code_offset);

    free(code_buf);
    disk_close(&disk);
    return ERR_OK;
}