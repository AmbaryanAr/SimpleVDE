#include "disk_commands.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#ifdef _WIN32
    #include <io.h>
    #define access _access
#else
    #include <unistd.h>
#endif

// Проверка существования файла
static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

// Преобразование строки в верхний регистр
static void str_toupper(char *str) {
    for (; *str; ++str) *str = toupper((unsigned char)*str);
}

// Создание виртуального диска
ErrorCode cmd_create_disk(CreateDiskParams *params) {
    printf("The process of creating a virtual disk:\n");
    if (file_exists(params->path)) {
        printf(" - Error: File '%s' already exists.\n", params->path);
        return ERR_DISK_CREATE;
    }

    Disk disk;
    ErrorCode res = disk_create(params->path, params->size_mb, &disk);
    if (res != ERR_OK) {
        printf(" - Error: Failed to create disk (code %d).\n", res);
        return ERR_DISK_CREATE;
    }
    printf(" - The '%s' file has been created.\n", params->path);

    char type_upper[32];
    strncpy(type_upper, params->partition_type, sizeof(type_upper)-1);
    type_upper[sizeof(type_upper)-1] = '\0';
    str_toupper(type_upper);

    if (strcmp(type_upper, "MBR") == 0) {
        printf(">>> STUB: MBR initialized.\n");
    } else if (strcmp(type_upper, "GPT") == 0) {
        printf(">>> STUB: GPT initialized.\n");
    } else {
        printf(" - Warning: Unknown partition type '%s'.\n", params->partition_type);
    }

    disk_close(&disk);
    printf("Disk closed. Operation completed.\n\n");
    return ERR_OK;
}

// Информация о диске
ErrorCode cmd_disk_info(const char *path) {
    printf("Disk information for: %s\n", path);

    Disk disk;
    ErrorCode err = disk_open(path, &disk);
    if (err != ERR_OK) {
        printf(" - Error: cannot open disk (code %d)\n", err);
        return err;
    }

    uint64_t size_mb = disk.size / (1024 * 1024);
    uint64_t size_gb = size_mb / 1024;
    printf(" - Size: %llu bytes (%llu MB, %llu GB)\n",
           disk.size, size_mb, size_gb);

    unsigned char sector[512];
    err = disk_read(&disk, sector, 512, 0);
    if (err != ERR_OK) {
        printf(" - Error: cannot read first sector (code %d)\n", err);
        disk_close(&disk);
        return err;
    }

    int is_gpt = 0;
    if (sector[510] == 0x55 && sector[511] == 0xAA) {
        int is_protective = 0;
        for (int i = 0; i < 4; i++) {
            if (sector[446 + i*16 + 4] == 0xEE) {
                is_protective = 1;
                break;
            }
        }
        if (is_protective) {
            unsigned char gpt_sig[8];
            if (disk_read(&disk, gpt_sig, 8, 512) == ERR_OK &&
                memcmp(gpt_sig, "EFI PART", 8) == 0) {
                is_gpt = 1;
            }
        }
    }

    if (is_gpt) {
        printf(" - Partition table: GPT\n");
    } else {
        if (sector[510] == 0x55 && sector[511] == 0xAA) {
            printf(" - Partition table: MBR\n");
            int part_count = 0;
            for (int i = 0; i < 4; i++) {
                if (sector[446 + i*16 + 4] != 0x00) part_count++;
            }
            printf(" - Number of primary partitions: %d\n", part_count);
            for (int i = 0; i < 4; i++) {
                unsigned char type = sector[446 + i*16 + 4];
                if (type == 0x00) continue;
                unsigned int lba = *(unsigned int*)(sector + 446 + i*16 + 8);
                unsigned int size = *(unsigned int*)(sector + 446 + i*16 + 12);
                printf("    Partition %d: type=0x%02X, start LBA=%u, size=%u sectors\n",
                       i+1, type, lba, size);
            }
        } else {
            printf(" - No valid MBR/GPT signature found (disk may be raw)\n");
        }
    }

    disk_close(&disk);
    printf("Disk closed. Operation completed.\n\n");
    return ERR_OK;
}

// Чтение секторов и вывод hex-дампа
ErrorCode cmd_disk_read_sector(const char *path, uint64_t offset_sectors, uint64_t size_sectors) {
    printf("The process of read sector a virtual disk:\n");
    const uint64_t SECTOR_SIZE = 512;

    // Проверка переполнения при умножении
    if (offset_sectors > LLONG_MAX / SECTOR_SIZE) {
        printf(" - ERROR: Offset too large (overflow)\n");
        return ERR_INVALID_VALUE;
    }
    if (size_sectors > LLONG_MAX / SECTOR_SIZE) {
        printf(" - ERROR: Size too large (overflow)\n");
        return ERR_INVALID_VALUE;
    }

    Disk disk;
    ErrorCode derr = disk_open(path, &disk);
    if (derr != ERR_OK) {
        printf(" - ERROR: Cannot open disk '%s' (code %d)\n", path, derr);
        return ERR_GENERIC;
    }

    uint64_t start_byte = offset_sectors * SECTOR_SIZE;
    if (start_byte >= disk.size) {
        printf(" - ERROR: Offset %lld sectors (byte %llu) exceeds disk size (%llu bytes)\n",
               offset_sectors, start_byte, disk.size);
        disk_close(&disk);
        return ERR_INVALID_VALUE;
    }

    uint64_t max_bytes = disk.size - start_byte;
    uint64_t max_sectors = max_bytes / SECTOR_SIZE;
    if (size_sectors > max_sectors) {
        printf(" - Warning: Requested %lld sectors, but only %llu sectors available. Reading %llu sectors.\n",
               size_sectors, max_sectors, max_sectors);
        size_sectors = max_sectors;
    }

    if (size_sectors == 0) {
        printf(" - No sectors to read at this offset.\n");
        disk_close(&disk);
        return ERR_OK;
    }

    uint32_t bytes_to_read = size_sectors * SECTOR_SIZE;
    // Ограничение на 4 ГБ за раз (из-за uint32_t в disk_read)
    if (bytes_to_read > UINT32_MAX) {
        printf(" - ERROR: Requested read size too large (exceeds 4 GB).\n");
        disk_close(&disk);
        return ERR_INVALID_VALUE;
    }

    unsigned char *buffer = (unsigned char*)malloc(bytes_to_read);
    if (!buffer) {
        printf(" - ERROR: Out of memory\n");
        disk_close(&disk);
        return ERR_GENERIC;
    }

    derr = disk_read(&disk, buffer, bytes_to_read, start_byte);
    if (derr != ERR_OK) {
        printf(" - ERROR: Failed to read disk (code %d)\n", derr);
        free(buffer);
        disk_close(&disk);
        return ERR_GENERIC;
    }

    printf("\nOffset (hex)   Hex data (first %u bytes)                               Ascii\n", bytes_to_read);
    printf("-------------- ------------------------------------------------------------ ----------------\n");

    for (uint64_t i = 0; i < bytes_to_read; i += 16) {
        printf("0x%08llx  ", (start_byte + i));
        for (int j = 0; j < 16; j++) {
            if (i + j < bytes_to_read)
                printf("%02X ", buffer[i + j]);
            else
                printf("   ");
        }
        printf(" ");
        for (int j = 0; j < 16; j++) {
            if (i + j < bytes_to_read) {
                unsigned char c = buffer[i + j];
                printf("%c", (c >= 32 && c <= 126) ? c : '.');
            } else {
                printf(" ");
            }
        }
        printf("\n");
    }

    free(buffer);
    disk_close(&disk);
    printf("Disk closed. Operation completed.\n\n");
    return ERR_OK;
}