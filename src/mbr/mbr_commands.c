#include "mbr_commands.h"
#include <stdio.h>

// *** Инициализация MBR сектор ***
static void cmd_mbr_initialize(MBR *mbr) {
	memset(mbr, 0, sizeof(MBR));
	mbr->signature = MBR_SIGNATURE;
	uint8_t boot_code[] = {
		0x33, 0xC0, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0x8E, 0xD8, 0x8E, 0xC0,
		0xBE, 0x74, 0x7C, 0xB4, 0x0E, 0xAC, 0x3C, 0x00, 0x74, 0x04, 0xCD,
		0x10, 0xEB, 0xF7, 0xEB, 0xFE,
		'N','o',' ','b','o','o','t','a','b','l','e',' ','d','i','s','k','!',
		0x0D, 0x0A, 0
	};
	size_t boot_size = sizeof(boot_code) < PARTITION_TABLE_OFFSET ? sizeof(boot_code) : PARTITION_TABLE_OFFSET;
	memcpy(mbr->bootstrap, boot_code, boot_size);
}

// Таблица соответствия типов
// static const struct {
// 	uint8_t type;
// 	const char *name;
// } type_names[] = {
// 	{0x00, "Empty"},
// 	{0x01, "FAT12"},
// 	{0x04, "FAT16 (<32M)"},
// 	{0x05, "Extended"},
// 	{0x06, "FAT16B"},
// 	{0x07, "NTFS/HPFS"},
// 	{0x0B, "FAT32"},
// 	{0x0C, "FAT32 LBA"},
// 	{0x0E, "FAT16 LBA"},
// 	{0x0F, "Extended LBA"},
// 	{0x82, "Linux Swap"},
// 	{0x83, "Linux"},
// 	{0x8E, "Linux LVM"},
// 	{0xEE, "GPT Protective"},
// 	{0xEF, "EFI System"},
// 	{0, NULL}
// };
// ***

ErrorCode mbr_create(Disk *disk) {
    if (!disk || !disk->is_open)
        return ERR_DISK_OPEN;

    MBR mbr;
    cmd_mbr_initialize(&mbr);

    ErrorCode err = disk_write(disk, &mbr, sizeof(MBR), 0);
    if (err != ERR_OK)
        return err;

    return ERR_OK;
}

// Создание раздела (выбор свободного места)
ErrorCode mbr_create_partition(Disk *disk, int index, uint32_t size_sectors, uint8_t type) {
    if (!disk || !disk->is_open) return ERR_DISK_OPEN;
    if (index < 0 || index >= 4) return ERR_INVALID_VALUE;

    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, 0);
    if (err != ERR_OK) return err;

    // Проверяем, свободна ли целевая запись
    if (sector[PARTITION_TABLE_OFFSET + index * PARTITION_ENTRY_SIZE + 4] != 0) {
        printf(" - ERROR: partition entry %d is already in use.\n", index + 1);
        return ERR_INVALID_VALUE;
    }

    uint64_t total_sectors = disk->size / SECTOR_SIZE;

    // Определяем следующий доступный LBA (конец последнего существующего раздела)
    uint64_t next_lba = 2048; // стандартное смещение для первого раздела
    for (int i = 0; i < 4; i++) {
        uint8_t part_type = sector[PARTITION_TABLE_OFFSET + i * PARTITION_ENTRY_SIZE + 4];
        if (part_type != 0) {
            uint32_t start = *(uint32_t*)(sector + PARTITION_TABLE_OFFSET + i * PARTITION_ENTRY_SIZE + 8);
            uint32_t size = *(uint32_t*)(sector + PARTITION_TABLE_OFFSET + i * PARTITION_ENTRY_SIZE + 12);
            uint64_t end = (uint64_t)start + size;
            if (end > next_lba) next_lba = end;
        }
    }

    // Если размер не указан, занимаем всё оставшееся место
    uint64_t new_size = size_sectors;
    if (new_size == 0) {
        new_size = total_sectors - next_lba;
        if (new_size > UINT32_MAX) new_size = UINT32_MAX; // MBR ограничение 32 бита
    }

    // Проверки границ
    if (next_lba + new_size > total_sectors) {
        printf("ERROR: not enough free space.\n");
        return ERR_INVALID_VALUE;
    }
    if (new_size > UINT32_MAX) {
        printf("ERROR: partition size too large for MBR.\n");
        return ERR_INVALID_VALUE;
    }
    if (next_lba > UINT32_MAX) {
        printf("ERROR: start LBA too large for MBR.\n");
        return ERR_INVALID_VALUE;
    }

    // Заполняем запись раздела
    uint8_t *entry = sector + PARTITION_TABLE_OFFSET + index * PARTITION_ENTRY_SIZE;
    entry[0] = 0;                          // boot flag (неактивен)
    // CHS-адреса: для простоты используем "LBA-only" значения
    entry[1] = 0;                          // head start
    entry[2] = 0;                          // sector/cylinder
    entry[3] = 0;                          // cylinder
    entry[4] = type;                       // тип
    entry[5] = 0xFE;                       // head end (максимум)
    entry[6] = 0xFF;                       // sector/cylinder end
    entry[7] = 0xFF;                       // cylinder end
    *(uint32_t*)(entry + 8) = (uint32_t)next_lba;   // LBA start
    *(uint32_t*)(entry + 12) = (uint32_t)new_size;  // размер в секторах

    return disk_write(disk, sector, SECTOR_SIZE, 0);
}

ErrorCode mbr_delete_partition(Disk *disk, int index) {
    if (!disk || !disk->is_open) return ERR_DISK_OPEN;
    if (index < 0 || index >= 4) return ERR_INVALID_VALUE;

    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, 0);
    if (err != ERR_OK) return err;

    // Зануляем запись раздела
    memset(sector + PARTITION_TABLE_OFFSET + index * PARTITION_ENTRY_SIZE,
           0, PARTITION_ENTRY_SIZE);

    return disk_write(disk, sector, SECTOR_SIZE, 0);
}

ErrorCode mbr_set_active(Disk *disk, int index) {
    if (!disk || !disk->is_open)
        return ERR_DISK_OPEN;
    if (index < 0 || index >= 4)
        return ERR_INVALID_VALUE;

    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, 0);
    if (err != ERR_OK)
        return err;

    for (int i = 0; i < 4; i++) {
        sector[PARTITION_TABLE_OFFSET + i * PARTITION_ENTRY_SIZE] = 0x00;
    }
    sector[PARTITION_TABLE_OFFSET + index * PARTITION_ENTRY_SIZE] = 0x80;

    return disk_write(disk, sector, SECTOR_SIZE, 0);
}

ErrorCode mbr_set_partition_type(Disk *disk, int index, uint8_t type) {
    if (!disk || !disk->is_open) return ERR_DISK_OPEN;
    if (index < 0 || index >= 4) return ERR_INVALID_VALUE;

    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, 0);
    if (err != ERR_OK) return err;

    sector[PARTITION_TABLE_OFFSET + index * PARTITION_ENTRY_SIZE + 4] = type;

    return disk_write(disk, sector, SECTOR_SIZE, 0);
}

ErrorCode mbr_write_code(Disk *disk, const uint8_t *code, size_t code_size) {
    if (!disk || !disk->is_open)
        return ERR_DISK_OPEN;
    if (code_size > PARTITION_TABLE_OFFSET)
        return ERR_INVALID_VALUE;

    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, 0);
    if (err != ERR_OK)
        return err;

    memcpy(sector, code, code_size);

    return disk_write(disk, sector, SECTOR_SIZE, 0);
}

void mbr_print_info(Disk *disk) {
    uint8_t sector[SECTOR_SIZE];
    if (disk_read(disk, sector, SECTOR_SIZE, 0) != ERR_OK) {
        printf(" -  Error: cannot read MBR sector.\n");
        return;
    }

    int part_count = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t type = sector[PARTITION_TABLE_OFFSET + i*PARTITION_ENTRY_SIZE + 4];
        if (type != 0x00) part_count++;
    }
    printf(" -  Number of primary partitions: %d\n", part_count);

    for (int i = 0; i < 4; i++) {
        uint8_t type = sector[PARTITION_TABLE_OFFSET + i*PARTITION_ENTRY_SIZE + 4];
        if (type == 0x00) continue;
        uint8_t status = sector[PARTITION_TABLE_OFFSET + i*PARTITION_ENTRY_SIZE]; // 0x80 = active
        uint32_t lba = *(uint32_t*)(sector + PARTITION_TABLE_OFFSET + i*PARTITION_ENTRY_SIZE + 8);
        uint32_t size = *(uint32_t*)(sector + PARTITION_TABLE_OFFSET + i*PARTITION_ENTRY_SIZE + 12);
        printf(" -  Partition %d: type=0x%02X, %s, start LBA=%u, size=%u sectors\n",
               i+1, type, (status == 0x80) ? "active" : "inactive", lba, size);
    }
}
