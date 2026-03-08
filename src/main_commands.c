#include "main_commands.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <inttypes.h>
// *** Вспомогательная функция для вывода аргументов ***
static void print_args_summary(CommandArgs *args) {
    printf("Arguments received:\n");
    printf("  Command category: %s\n", args->command ? args->command : "NULL");
    
    if (args->has_disk_path) printf("  disk_path: %s\n", args->disk_path);
    if (args->has_part_index) printf("  part_index: %s\n", args->part_index_raw);
    if (args->has_size) printf("  size: %s\n", args->size_raw);
    if (args->has_type) printf("  type: %s\n", args->type_raw);
    if (args->has_path) printf("  path (disk): %s\n", args->path);
    if (args->has_path_raw) printf("  path (fs): %s\n", args->path_raw);
    if (args->has_fs_type) printf("  fs_type: %s\n", args->fs_type);
    if (args->has_name) printf("  name: %s\n", args->name_raw);
    if (args->has_file) printf("  file: %s\n", args->file_raw);
    if (args->has_src) printf("  src: %s\n", args->src_raw);
    if (args->has_op) printf("  op: %s\n", args->op_raw);
    if (args->has_offset) printf("  offset: %s\n", args->offset_raw);
}

// Парсинг размера с поддержкой суффиксов K, M, G (по умолчанию M)
// Возвращает true и записывает результат в *size_mb, иначе false
static bool parse_number(const char *size_str, uint64_t *size_mb) {
    if (!size_str || !*size_str) return false;

    char *endptr;
    uint64_t value = strtoull(size_str, &endptr, 10);
    if (endptr == size_str) return false; // нет цифр
    if (value <= 0) return false;

    // Пропускаем пробелы после числа (хотя их быть не должно)
    while (*endptr == ' ') endptr++;

    char suffix = *endptr;
    if (suffix == '\0') {
        // нет суффикса — считаем мегабайтами
        *size_mb = value;
        return true;
    }

    // Приводим к верхнему регистру для простоты
    if (suffix >= 'a' && suffix <= 'z') suffix -= 32;

	switch (suffix) {
		case 'K':
			*size_mb = value / 1024;
			if (value % 1024 != 0) 
				return false;
			break;
		case 'M':
			*size_mb = value;
			break;
		case 'G':
			*size_mb = value * 1024;
			break;
		default:
			return false;
	}

    // Проверяем, что после суффикса ничего нет
    endptr++;
    if (*endptr != '\0') return false;

    return true;
}

// Парсинг целого числа с поддержкой десятичной и шестнадцатеричной записи (0x...)
static bool parse_integer(const char *str, uint64_t *out) {
    if (!str || !*str) return false;
    char *endptr;
    errno = 0;
    int64_t val = strtoll(str, &endptr, 0); // автоопределение основания
    if (endptr == str || *endptr != '\0' || errno == ERANGE) return false;
    if (val < 0) return false; // отрицательные значения не допускаются
    *out = val;
    return true;
}
// ***

// ---------- Функции для дисковых операций ----------
ErrorCode process_create_disk(CommandArgs *args) {
	if (!args->has_path) {
		printf("Error: missing -path= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}
	if (!args->has_size) {
		printf("Error: missing -size= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}
	if (!args->has_type) {
		printf("Error: missing -type= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}

    CreateDiskParams params;

    // Путь
    strncpy(params.path, args->path, sizeof(params.path) - 1);
    params.path[sizeof(params.path) - 1] = '\0';

    // Размер
    uint64_t size_mb = 0;
    if (!parse_number(args->size_raw, &size_mb) || size_mb <= 0)
        return ERR_INVALID_VALUE;
    params.size_mb = size_mb;

    // Тип таблицы разделов
    // args->type_raw гарантированно есть (has_type)
    strncpy(params.partition_type, args->type_raw, sizeof(params.partition_type) - 1);
    params.partition_type[sizeof(params.partition_type) - 1] = '\0';

    // Вызов команды создания диска
   return cmd_create_disk(&params);
}

ErrorCode process_disk_info(CommandArgs *args) {
    if (!args->has_path){
		printf("Error: missing -path= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}

    return cmd_disk_info(args->path);
}

ErrorCode process_disk_read(CommandArgs *args) {
	if (!args->has_path) {
		printf("Error: missing -path= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}
	if (!args->has_offset) {
		printf("Error: missing -offset= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}
	if (!args->has_size) {
		printf("Error: missing -size= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}

    // Парсинг offset и size (в секторах)
    uint64_t offset_sectors, size_sectors;
    if (!parse_integer(args->offset_raw, &offset_sectors)) {
        printf("ERROR: Invalid offset value '%s' (must be non‑negative integer, decimal or hex)\n", args->offset_raw);
        return ERR_INVALID_VALUE;
    }
    if (!parse_integer(args->size_raw, &size_sectors)) {
        printf("ERROR: Invalid size value '%s' (must be non‑negative integer, decimal or hex)\n", args->size_raw);
        return ERR_INVALID_VALUE;
    }
    if (size_sectors == 0) {
        printf("Warning: size is zero, nothing to read.\n");
        return ERR_OK;
    }
	
    return cmd_disk_read_sector(args->path, offset_sectors, size_sectors);
}

// ---------- Функции для операций с разделами ----------
ErrorCode process_create_partition(CommandArgs *args) {
	if (!args->has_disk_path) {
		printf("Error: missing -disk= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}
	if (!args->has_part_index) {
		printf("Error: missing -index= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}

    // Парсим индекс (пользовательский счёт с 1)
    int index = atoi(args->part_index_raw) - 1;
    if (index < 0) {
        printf("ERROR: invalid partition index.\n");
        return ERR_INVALID_VALUE;
    }

    // Открываем диск и определяем тип таблицы
    Disk disk;
    PartitionTableType table_type;
    ErrorCode err = cmd_disk_open_and_detect(args->disk_path, &disk, &table_type);
	if (err != ERR_OK) {
		if (err == ERR_DISK_OPEN)
			printf("Error: cannot open disk file '%s'\n", args->disk_path);
		else
			printf("Error: failed to open disk (code %d)\n", err);
		return err;
	}

    // Парсим размер (если не указан, будет 0 – занять всё свободное место)
	uint64_t size_sectors = 0;
	if (args->has_size) {
		uint64_t size_mb;
		if (!parse_number(args->size_raw, &size_mb)) {
			printf("ERROR: invalid size format.\n");
			disk_close(&disk);
			return ERR_INVALID_VALUE;
		}
		uint64_t size_bytes = size_mb * 1024 * 1024;
		if (size_bytes % SECTOR_SIZE != 0) {
			printf("Warning: size not multiple of sector size (%d), rounding down.\n", SECTOR_SIZE);
		}
		size_sectors = size_bytes / SECTOR_SIZE;
		if (size_sectors == 0) {
			printf("ERROR: size too small (must be at least one sector).\n");
			disk_close(&disk);
			return ERR_INVALID_VALUE;
		}
	} else {
		printf("No size specified, partition will occupy all free space.\n");
	}

    if (table_type == PT_MBR) {
        if (index >= 4) {
            printf("ERROR: MBR supports only 4 partitions (index 1-4).\n");
            disk_close(&disk);
            return ERR_INVALID_VALUE;
        }

        // Определяем тип файловой системы
        uint8_t fs_type = 0x83; // умолчание для MBR (Linux)
        if (args->has_type) {
			// Проверяем, похоже ли на hex (0x... или просто hex-цифры)
			if (strncmp(args->type_raw, "0x", 2) == 0 || isxdigit(args->type_raw[0])) {
				char *endptr;
				unsigned long val = strtoul(args->type_raw, &endptr, 16);
				if (*endptr != '\0' || val > 0xFF) {
					printf("ERROR: invalid filesystem type '%s'.\n", args->type_raw);
					disk_close(&disk);
					return ERR_INVALID_VALUE;
				}
				fs_type = (uint8_t)val;
			} else {
				fs_type = mbr_type_from_name(args->type_raw);
				if (fs_type == 0xFF) {
					printf("ERROR: unknown filesystem name '%s'.\n", args->type_raw);
					disk_close(&disk);
					return ERR_INVALID_VALUE;
				}
			}
		}

		if (size_sectors > UINT32_MAX) {
			printf("ERROR: size too large for MBR partition (max 2 TiB).\n");
			disk_close(&disk);
			return ERR_INVALID_VALUE;
		}
		uint32_t mbr_size = (uint32_t)size_sectors;
		err = mbr_create_partition(&disk, index, mbr_size, fs_type);

        if (err == ERR_OK) {
            printf("Partition %d created successfully.\n", index + 1);
        } else {
            printf("ERROR: failed to create partition (code %d).\n", err);
        }
    } else if (table_type == PT_GPT) {
		// Для GPT индекс может быть до 128, проверять не здесь (функция сама проверит)
		uint8_t fs_guid[16];
		if (args->has_type) {
			// Проверяем, похоже ли на hex (GUID в виде строки)
			if (strncmp(args->type_raw, "0x", 2) == 0 || isxdigit(args->type_raw[0])) {
				if (gpt_guid_from_string(args->type_raw, fs_guid) != 0) {
					printf("ERROR: invalid filesystem GUID '%s'.\n", args->type_raw);
					disk_close(&disk);
					return ERR_INVALID_VALUE;
				}
			} else {
				if (gpt_type_from_name(args->type_raw, fs_guid) != 0) {
					printf("ERROR: unknown filesystem name '%s'.\n", args->type_raw);
					disk_close(&disk);
					return ERR_INVALID_VALUE;
				}
			}
		} else {
			// GUID по умолчанию (Linux filesystem)
			uint8_t default_guid[16] = GPT_TYPE_LINUX_FILESYSTEM;
			memcpy(fs_guid, default_guid, 16);
		}
		// Размер в секторах (уже посчитан ранее, в size_sectors)
		err = gpt_create_partition(&disk, index, size_sectors, fs_guid);
		if (err == ERR_OK) {
			printf("Partition %d created successfully.\n", index + 1);
		} else {
			printf("ERROR: failed to create GPT partition (code %d).\n", err);
		}
	} else {
        printf("ERROR: unknown partition table type.\n");
        err = ERR_INVALID_VALUE;
    }
	
    disk_close(&disk);
    return err;
}

ErrorCode process_delete_partition(CommandArgs *args) {
    if (!args->has_disk_path) {
        printf("Error: missing -disk= parameter.\n");
        return ERR_MISSING_ARGUMENT;
    }
    if (!args->has_part_index) {
        printf("Error: missing -index= parameter.\n");
        return ERR_MISSING_ARGUMENT;
    }

    Disk disk;
    PartitionTableType table_type;
    ErrorCode err = cmd_disk_open_and_detect(args->disk_path, &disk, &table_type);
    if (err != ERR_OK) {
        if (err == ERR_DISK_OPEN)
            printf("Error: cannot open disk file '%s'\n", args->disk_path);
        else
            printf("Error: failed to open disk (code %d)\n", err);
        return err;
    }

    int index = atoi(args->part_index_raw) - 1;
    if (index < 0) {
        printf("Error: invalid partition index.\n");
        disk_close(&disk);
        return ERR_INVALID_VALUE;
    }

    if (table_type == PT_MBR) {
        if (index >= 4) {
            printf("Error: MBR supports only 4 partitions (index 1-4).\n");
            disk_close(&disk);
            return ERR_INVALID_VALUE;
        }
        err = mbr_delete_partition(&disk, index);
        if (err == ERR_OK) {
            printf("Partition %d deleted successfully.\n", index + 1);
        } else if (err == ERR_INVALID_VALUE) {
            printf("Error: partition %d does not exist.\n", index + 1);
        } else {
            printf("Error: failed to delete partition (code %d).\n", err);
        }
    } else if (table_type == PT_GPT) {
        err = gpt_delete_partition(&disk, index);
        if (err == ERR_OK) {
            printf("Partition %d deleted successfully.\n", index + 1);
        } else if (err == ERR_INVALID_VALUE) {
            printf("Error: partition %d does not exist or invalid index.\n", index + 1);
        } else {
            printf("Error: failed to delete partition (code %d).\n", err);
        }
    } else {
        printf("Error: unknown partition table type.\n");
        err = ERR_INVALID_VALUE;
    }

    disk_close(&disk);
    return err;
}

ErrorCode process_set_active(CommandArgs *args) {
	if (!args->has_disk_path) {
		printf("Error: missing -disk= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}
	if (!args->has_part_index) {
		printf("Error: missing -index= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}
	if (!args->has_op) {
		printf("Error: missing -op= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}

    // Определяем, активный или неактивный режим
    bool set_active;
    if (strcmp(args->op_raw, "active") == 0) {
        set_active = true;
    } else if (strcmp(args->op_raw, "inactive") == 0) {
        set_active = false;
    } else {
        printf("ERROR: -op= must be 'active' or 'inactive' for set active\n");
        return ERR_INVALID_VALUE;
    }

    // Открываем диск и определяем тип таблицы разделов
    Disk disk;
    PartitionTableType table_type;
    ErrorCode err = cmd_disk_open_and_detect(args->disk_path, &disk, &table_type);
	if (err != ERR_OK) {
		if (err == ERR_DISK_OPEN)
			printf("Error: cannot open disk file '%s'\n", args->disk_path);
		else
			printf("Error: failed to open disk (code %d)\n", err);
		return err;
	}

    // Парсим индекс раздела (пользователь вводит 1-4)
    int index = atoi(args->part_index_raw) - 1;
    if (index < 0 || index >= 4) {
        printf("ERROR: partition index must be between 1 and 4\n");
        disk_close(&disk);
        return ERR_INVALID_VALUE;
    }

    // Действия в зависимости от типа таблицы
    if (table_type == PT_MBR) {
        if (set_active) {
			err = mbr_set_active(&disk, index);
			if (err == ERR_OK) {
				printf("Partition %d activated successfully.\n", index + 1);
			} else if (err == ERR_INVALID_VALUE) {
				printf("Error: partition %d does not exist.\n", index + 1);
			} else {
				printf("Error: failed to activate partition (code %d).\n", err);
			}
		} else {
            // Для неактивного просто сбрасываем флаг у указанного раздела
            uint8_t sector[SECTOR_SIZE];
            err = disk_read(&disk, sector, SECTOR_SIZE, 0);
            if (err != ERR_OK) {
                printf("Error: cannot read MBR.\n");
                disk_close(&disk);
                return err;
            }
            // Проверим, существует ли раздел (тип не 0)
            uint8_t part_type = sector[PARTITION_TABLE_OFFSET + index * PARTITION_ENTRY_SIZE + 4];
            if (part_type == 0) {
                printf("Warning: partition %d does not exist (type=0). Nothing to deactivate.\n", index + 1);
                disk_close(&disk);
                return ERR_OK;
            }
            sector[PARTITION_TABLE_OFFSET + index * PARTITION_ENTRY_SIZE] = 0x00;
            err = disk_write(&disk, sector, SECTOR_SIZE, 0);
            if (err == ERR_OK) {
                printf("Partition %d deactivated successfully.\n", index + 1);
            } else {
                printf("Error: failed to deactivate partition (code %d).\n", err);
            }
        }
    } else if (table_type == PT_GPT) {
        printf("Error: Active flag is only supported for MBR partitions.\n");
        err = ERR_INVALID_VALUE;
    } else {
        printf("Error: Unknown partition table type.\n");
        err = ERR_INVALID_VALUE;
    }

    disk_close(&disk);
    return err;
}

ErrorCode process_set_type(CommandArgs *args) {
	if (!args->has_disk_path) {
		printf("Error: missing -disk= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}
	if (!args->has_part_index) {
		printf("Error: missing -index= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}
	if (!args->has_type) {
		printf("Error: missing -fs= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}

    Disk disk;
    PartitionTableType table_type;
    ErrorCode err = cmd_disk_open_and_detect(args->disk_path, &disk, &table_type);
	if (err != ERR_OK) {
		if (err == ERR_DISK_OPEN)
			printf("Error: cannot open disk file '%s'\n", args->disk_path);
		else
			printf("Error: failed to open disk (code %d)\n", err);
		return err;
	}

	int index = atoi(args->part_index_raw) - 1;
	if (index < 0) {
		printf("Error: invalid partition index.\n");
		disk_close(&disk);
		return ERR_INVALID_VALUE;
	}
	if (table_type == PT_MBR && index >= 4) {
		printf("Error: MBR supports only 4 partitions (index 1-4).\n");
		disk_close(&disk);
		return ERR_INVALID_VALUE;
	}

    if (table_type == PT_MBR) {
		uint8_t fs_type;
		if (strncmp(args->type_raw, "0x", 2) == 0 || isxdigit(args->type_raw[0])) {
			char *endptr;
			unsigned long val = strtoul(args->type_raw, &endptr, 16);
			if (*endptr != '\0' || val > 0xFF) {
				printf("ERROR: invalid filesystem type '%s'.\n", args->type_raw);
				disk_close(&disk);
				return ERR_INVALID_VALUE;
			}
			fs_type = (uint8_t)val;
		} else {
			fs_type = mbr_type_from_name(args->type_raw);
			if (fs_type == 0xFF) {
				printf("ERROR: unknown filesystem name '%s'.\n", args->type_raw);
				disk_close(&disk);
				return ERR_INVALID_VALUE;
			}
		}

        err = mbr_set_partition_type(&disk, index, fs_type);
		if (err == ERR_OK) {
			printf("Partition %d deleted successfully.\n", index + 1);
		} else if (err == ERR_INVALID_VALUE) {
			printf("Error: partition %d does not exist.\n", index + 1);
		} else {
			printf("Error: failed to delete partition (code %d).\n", err);
		}
    } else if (table_type == PT_GPT) {
		uint8_t fs_guid[16];
		if (strncmp(args->type_raw, "0x", 2) == 0 || isxdigit(args->type_raw[0])) {
			if (gpt_guid_from_string(args->type_raw, fs_guid) != 0) {
				printf("ERROR: invalid filesystem GUID '%s'.\n", args->type_raw);
				disk_close(&disk);
				return ERR_INVALID_VALUE;
			}
		} else {
			if (gpt_type_from_name(args->type_raw, fs_guid) != 0) {
				printf("ERROR: unknown filesystem name '%s'.\n", args->type_raw);
				disk_close(&disk);
				return ERR_INVALID_VALUE;
			}
		}

		err = gpt_set_partition_type(&disk, index, fs_guid);
		if (err == ERR_OK) {
			printf("Partition %d type changed successfully.\n", index + 1);
		} else if (err == ERR_INVALID_VALUE) {
			printf("Error: partition %d does not exist or invalid GUID.\n", index + 1);
		} else {
			printf("Error: failed to change partition type (code %d).\n", err);
		}
	} else {
        printf("Error: Unknown partition table type.\n");
        err = ERR_INVALID_VALUE;
    }

    disk_close(&disk);
    return err;
}

ErrorCode process_format(CommandArgs *args) {
    if (!args->has_disk_path || !args->has_part_index || !args->has_type)
        return ERR_MISSING_ARGUMENT;

    // Пока поддерживаем только FAT32
    if (strcasecmp(args->type_raw, "fat32") != 0) {
        printf("Error: Unsupported filesystem type '%s'. Currently only FAT32 is supported.\n", args->type_raw);
        return ERR_INVALID_VALUE;
    }

    Disk disk;
    PartitionTableType table_type;
    ErrorCode err = cmd_disk_open_and_detect(args->disk_path, &disk, &table_type);
    if (err != ERR_OK) {
        if (err == ERR_DISK_OPEN)
            printf("Error: cannot open disk file '%s'\n", args->disk_path);
        else
            printf("Error: failed to open disk (code %d)\n", err);
        return err;
    }

    int index = atoi(args->part_index_raw) - 1;
    if (index < 0) {
        printf("Error: invalid partition index.\n");
        disk_close(&disk);
        return ERR_INVALID_VALUE;
    }

    uint64_t start_lba, size_sectors;

    if (table_type == PT_MBR) {
        if (index >= 4) {
            printf("Error: MBR supports only 4 partitions (index 1-4).\n");
            disk_close(&disk);
            return ERR_INVALID_VALUE;
        }
        err = mbr_get_partition_info(&disk, index, &start_lba, &size_sectors);
    } else if (table_type == PT_GPT) {
        err = gpt_get_partition_info(&disk, index, &start_lba, &size_sectors);
    } else {
        printf("Error: unknown partition table type.\n");
        disk_close(&disk);
        return ERR_INVALID_VALUE;
    }

    if (err != ERR_OK) {
        if (err == ERR_INVALID_VALUE)
            printf("Error: partition %d does not exist.\n", index + 1);
        else
            printf("Error: failed to read partition info (code %d).\n", err);
        disk_close(&disk);
        return err;
    }

    // Определяем номер диска (для FAT32 BPB). Пока всегда 0x80 (жёсткий диск)
    uint8_t drive_number = 0x80;

    err = fat32_format(&disk, start_lba, size_sectors, drive_number);
    if (err == ERR_OK) {
        printf("Partition %d formatted as FAT32 successfully.\n", index + 1);
    } else {
        printf("Error: failed to format partition (code %d).\n", err);
    }

    disk_close(&disk);
    return err;
}

ErrorCode process_write_mbr_loader(CommandArgs *args) {
	if (!args->has_disk_path) {
		printf("Error: missing -disk= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}
	if (!args->has_file) {
		printf("Error: missing -file= parameter.\n");
		return ERR_MISSING_ARGUMENT;
	}

    // Открываем файл с кодом
    FILE *f = fopen(args->file_raw, "rb");
    if (!f) {
        printf("Error: cannot open file '%s'\n", args->file_raw);
        return ERR_GENERIC;
    }

    // Определяем размер файла
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size > PARTITION_TABLE_OFFSET) {
        printf("Warning: file size (%ld) exceeds MBR bootstrap area (446 bytes). Truncating to 446 bytes.\n", file_size);
        file_size = PARTITION_TABLE_OFFSET;
    }

    uint8_t buffer[PARTITION_TABLE_OFFSET];
    size_t read_bytes = fread(buffer, 1, file_size, f);
    fclose(f);

    if (read_bytes != (size_t)file_size) {
        printf("Error: failed to read file (expected %ld bytes, got %zu)\n", file_size, read_bytes);
        return ERR_GENERIC;
    }

    // Открываем диск и определяем тип таблицы
    Disk disk;
    PartitionTableType table_type;
    ErrorCode err = cmd_disk_open_and_detect(args->disk_path, &disk, &table_type);
	if (err != ERR_OK) {
		if (err == ERR_DISK_OPEN)
			printf("Error: cannot open disk file '%s'\n", args->disk_path);
		else
			printf("Error: failed to open disk (code %d)\n", err);
		return err;
	}

    if (table_type != PT_MBR) {
        printf("Error: Writing MBR code is only supported for MBR-partitioned disks.\n");
        disk_close(&disk);
        return ERR_INVALID_VALUE;
    }

    err = mbr_write_code(&disk, buffer, read_bytes);
    disk_close(&disk);

    if (err == ERR_OK) {
        printf("MBR code successfully written (%zu bytes).\n", read_bytes);
    } else {
        printf("Error: failed to write MBR code (code %d).\n", err);
    }

    return err;
}

ErrorCode process_write_bpb_loader(CommandArgs *args) {
    printf("\n>>> STUB: process_write_bpb_loader\n");
    print_args_summary(args);

	if (!args->has_disk_path || !args->has_part_index || !args->has_file)
		return ERR_MISSING_ARGUMENT;

    printf("ACTION: Would write BPB loader from file '%s' to partition at index %s on disk '%s'\n", 
           args->file_raw, args->part_index_raw, args->disk_path);
    return ERR_OK;
}

// ---------- Заглушки для файловых операций ----------
ErrorCode process_ls(CommandArgs *args) {
    printf("\n>>> STUB: process_ls\n");
    print_args_summary(args);

	if (!args->has_disk_path || !args->has_part_index)
		return ERR_MISSING_ARGUMENT;

    const char *path = args->has_path_raw ? args->path_raw : "/";
    printf("ACTION: Would list contents of '%s' on partition %s of disk '%s'\n", 
           path, args->part_index_raw, args->disk_path);
    return ERR_OK;
}

ErrorCode process_copy(CommandArgs *args) {
    printf("\n>>> STUB: process_copy\n");
    print_args_summary(args);

	if (!args->has_src || !args->has_disk_path || !args->has_part_index || !args->has_path_raw)
		return ERR_MISSING_ARGUMENT;

    printf("ACTION: Would copy host file '%s' to '%s' on partition %s of disk '%s'\n", 
           args->src_raw, args->path_raw, args->part_index_raw, args->disk_path);
    return ERR_OK;
}

ErrorCode process_rm(CommandArgs *args) {
    printf("\n>>> STUB: process_rm\n");
    print_args_summary(args);

	if (!args->has_disk_path || !args->has_part_index || !args->has_path_raw)
		return ERR_MISSING_ARGUMENT;

    printf("ACTION: Would remove file '%s' on partition %s of disk '%s'\n", 
           args->path_raw, args->part_index_raw, args->disk_path);
    return ERR_OK;
}

ErrorCode process_mkdir(CommandArgs *args) {
    printf("\n>>> STUB: process_mkdir\n");
    print_args_summary(args);

	if (!args->has_disk_path || !args->has_part_index || !args->has_path_raw)
        return ERR_MISSING_ARGUMENT;

    printf("ACTION: Would create directory '%s' on partition %s of disk '%s'\n", 
           args->path_raw, args->part_index_raw, args->disk_path);
    return ERR_OK;
}

ErrorCode process_rmdir(CommandArgs *args) {
    printf("\n>>> STUB: process_rmdir\n");
    print_args_summary(args);

	if (!args->has_disk_path || !args->has_part_index || !args->has_path_raw)
        return ERR_MISSING_ARGUMENT;

    printf("ACTION: Would remove directory '%s' on partition %s of disk '%s'\n", 
           args->path_raw, args->part_index_raw, args->disk_path);
    return ERR_OK;
}

// ---------- Заглушки для карты специальных файлов ----------
ErrorCode process_map_file(CommandArgs *args) {
    printf("\n>>> STUB: process_map_file\n");
    print_args_summary(args);

	if (!args->has_disk_path || !args->has_part_index || !args->has_op)
        return ERR_MISSING_ARGUMENT;

    if (strcmp(args->op_raw, "delete") == 0 && !args->has_name) {
        printf("ERROR: -name= is required for delete operation\n");
        return ERR_INVALID_VALUE;
    }

    if (strcmp(args->op_raw, "list") == 0) {
        printf("ACTION: Would list map file entries on partition %s of disk '%s'\n", 
               args->part_index_raw, args->disk_path);
    } else if (strcmp(args->op_raw, "delete") == 0) {
        printf("ACTION: Would delete map file entry '%s' on partition %s of disk '%s'\n", 
               args->name_raw, args->part_index_raw, args->disk_path);
    } else {
        printf("ERROR: Unsupported operation '%s' for map_file\n", args->op_raw);
        return ERR_INVALID_VALUE;
    }

    return ERR_OK;
}

ErrorCode process_copy_special(CommandArgs *args) {
    printf("\n>>> STUB: process_copy_special\n");
    print_args_summary(args);

	if (!args->has_disk_path || !args->has_part_index || !args->has_src || !args->has_path_raw)
        return ERR_MISSING_ARGUMENT;

    printf("ACTION: Would copy host file '%s' to '%s' on partition %s of disk '%s' with map file entry\n", 
           args->src_raw, args->path_raw, args->part_index_raw, args->disk_path);
    return ERR_OK;
}