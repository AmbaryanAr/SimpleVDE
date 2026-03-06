#include "disk_commands.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>

#ifdef _WIN32
    #include <io.h>
    #define access _access
#else
    #include <unistd.h>
#endif

// *** Вспомогательная функция ***
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

// Определение типа таблицы на диске
static PartitionTableType detect_partition_table(Disk *disk) {
    unsigned char sector[MBR_SIZE];
    ErrorCode err = disk_read(disk, sector, MBR_SIZE, 0);
    if (err != ERR_OK) {
        return PT_UNKNOWN;
    }

    // Проверка сигнатуры MBR
    if (sector[510] == 0x55 && sector[511] == 0xAA) {
        // Ищем защитный раздел типа 0xEE (признак GPT)
        int is_protective = 0;
        for (int i = 0; i < 4; i++) {
            if (sector[PARTITION_TABLE_OFFSET + i * PARTITION_ENTRY_SIZE + 4] == 0xEE) {
                is_protective = 1;
                break;
            }
        }
        if (is_protective) {
            // Читаем второй сектор (LBA1) и проверяем сигнатуру GPT
            unsigned char gpt_sig[8];
            if (disk_read(disk, gpt_sig, 8, MBR_SIZE) == ERR_OK &&
                memcmp(gpt_sig, "EFI PART", 8) == 0) {
                return PT_GPT;
            }
        }
        return PT_MBR;
    }

    return PT_UNKNOWN;
}
// ***

ErrorCode cmd_disk_open_and_detect(const char *path, Disk *disk, PartitionTableType *type) {
    if (!path || !disk || !type)
        return ERR_NULL_POINTER;

    ErrorCode err = disk_open(path, disk);
    if (err != ERR_OK)
        return err;

    *type = detect_partition_table(disk);
    return ERR_OK;
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

	ErrorCode err;
    if (strcmp(type_upper, "MBR") == 0) {
		err = mbr_create(&disk);
		if (err != ERR_OK) {
			disk_close(&disk);
			return err;
		}
		printf(" - MBR initialized.\n");
	} else if (strcmp(type_upper, "GPT") == 0) {
		err = gpt_create(&disk);
		if (err != ERR_OK) {
			disk_close(&disk);
			return err;
		}
		printf(" - GPT initialized.\n");
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
    printf(" - Size: %" PRIu64 " bytes (%" PRIu64 " MB, %" PRIu64 " GB)\n", disk.size, size_mb, size_gb);

    PartitionTableType table_type = detect_partition_table(&disk);
    if (table_type == PT_GPT) {
        printf(" - Partition table: GPT\n");
        gpt_print_info(&disk);
    } else if (table_type == PT_MBR) {
        printf(" - Partition table: MBR\n");
        mbr_print_info(&disk);
    } else {
        printf(" - No valid MBR/GPT signature found (disk may be raw)\n");
    }

    disk_close(&disk);
    printf("Disk closed. Operation completed.\n\n");
    return ERR_OK;
}

// Чтение секторов и вывод hex-дампа
ErrorCode cmd_disk_read_sector(const char *path, uint64_t offset_sectors, uint64_t size_sectors) {
    printf("The process of read sector a virtual disk:\n");

    // Проверка переполнения при умножении
    if (offset_sectors > UINT64_MAX / SECTOR_SIZE) {
        printf(" - ERROR: Offset too large (overflow)\n");
        return ERR_INVALID_VALUE;
    }
    if (size_sectors > UINT64_MAX / SECTOR_SIZE) {
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
        printf(" - ERROR: Offset %" PRIu64 " sectors (byte %" PRIu64 ") exceeds disk size (%" PRIu64 " bytes)\n",
               offset_sectors, start_byte, disk.size);
        disk_close(&disk);
        return ERR_INVALID_VALUE;
    }

    uint64_t max_bytes = disk.size - start_byte;
    uint64_t max_sectors = max_bytes / SECTOR_SIZE;
    if (size_sectors > max_sectors) {
        printf(" - Warning: Requested %" PRIu64 " sectors, but only %" PRIu64 " sectors available. Reading %" PRIu64 " sectors.\n",
               size_sectors, max_sectors, max_sectors);
        size_sectors = max_sectors;
    }

    if (size_sectors == 0) {
        printf(" - No sectors to read at this offset.\n");
        disk_close(&disk);
        return ERR_OK;
    }

	uint64_t bytes_to_read_64 = size_sectors * SECTOR_SIZE;
	if (bytes_to_read_64 > UINT32_MAX) {
		printf(" - ERROR: Requested read size too large (exceeds 4 GB).\n");
		disk_close(&disk);
		return ERR_INVALID_VALUE;
	}
	uint32_t bytes_to_read = (uint32_t)bytes_to_read_64;

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

    for (uint64_t i = 0; i < bytes_to_read; i += HEX_DUMP_WIDTH) {
        printf("0x%08" PRIx64 "  ", start_byte + i);
        for (int j = 0; j < HEX_DUMP_WIDTH; j++) {
            if (i + j < bytes_to_read)
                printf("%02X ", buffer[i + j]);
            else
                printf("   ");
        }
        printf(" ");
        for (int j = 0; j < HEX_DUMP_WIDTH; j++) {
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