#include "disk.h"
#include "fat32.h"
#include "error_codes.h"
#include <string.h>
#include <stdlib.h>

ErrorCode fat32_format(Disk *disk, uint64_t start_lba, uint64_t total_sectors,
                       uint8_t drive_number, uint32_t volume_id, const char *volume_label) {
    // FAT32 использует 32-битные поля для числа секторов
    if (total_sectors > UINT32_MAX) {
        return ERR_INVALID_ARGUMENT;
    }
    uint32_t total_sec = (uint32_t)total_sectors;

    // Минимальный размер: нужны зарезервированные секторы (32) и хотя бы один кластер для корня
    if (total_sec <= 32) {
        return ERR_INVALID_ARGUMENT;
    }

    // Определяем sectors_per_cluster (степень двойки) в зависимости от размера раздела
    uint8_t spc;
    if (total_sec <= 512 * 1024)          // до 256 МБ (512 * 1024 секторов * 512 байт = 256 МБ)
        spc = 1;
    else if (total_sec <= 1024 * 1024)    // до 512 МБ
        spc = 2;
    else if (total_sec <= 2048 * 1024)    // до 1 ГБ
        spc = 4;
    else if (total_sec <= 4096 * 1024)    // до 2 ГБ
        spc = 8;
    else if (total_sec <= 8192 * 1024)    // до 4 ГБ
        spc = 16;
    else                                  // больше 4 ГБ
        spc = 32;

    uint32_t data_sectors = total_sec - 32;               // секторы данных (после резервных и FAT)
    uint32_t clusters = data_sectors / spc;
    if (clusters < 1) {
        return ERR_INVALID_ARGUMENT; // нет места даже для корневого каталога
    }

    // Размер одной FAT в секторах (каждая запись 4 байта)
    uint32_t fat_sectors = ((clusters + 2) * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;

    // Заполняем BPB
    Fat32BPB bpb;
    memset(&bpb, 0, sizeof(bpb));
    bpb.jump_boot[0] = 0xEB;
    bpb.jump_boot[1] = 0x58;
    bpb.jump_boot[2] = 0x90;
    memcpy(bpb.oem_name, "MSWIN4.1", 8);
    bpb.bytes_per_sector = SECTOR_SIZE;
    bpb.sectors_per_cluster = spc;
    bpb.reserved_sectors = 32;
    bpb.num_fats = 2;
    bpb.root_entries = 0;
    bpb.total_sectors_16 = 0;
    bpb.media_descriptor = 0xF8;
    bpb.fat_size_16 = 0;
    bpb.sectors_per_track = 63;
    bpb.num_heads = 255;
    bpb.hidden_sectors = (uint32_t)start_lba;      // LBA начала раздела
    bpb.total_sectors_32 = total_sec;
    bpb.fat_size_32 = fat_sectors;
    bpb.ext_flags = 0;
    bpb.fs_version = 0;
    bpb.root_cluster = 2;
    bpb.fs_info_sector = 1;
    bpb.backup_boot_sector = 6;
    bpb.drive_number = drive_number;
    bpb.reserved1 = 0;
    bpb.boot_signature = 0x29;
    bpb.volume_id = volume_id ? volume_id : (uint32_t)rand();

    // Метка тома: ровно 11 символов, дополнено пробелами
    if (volume_label) {
        strncpy(bpb.volume_label, volume_label, 11);
        for (int i = strlen(volume_label); i < 11; i++) {
            bpb.volume_label[i] = ' ';
        }
    } else {
        memcpy(bpb.volume_label, "NO NAME    ", 11);
    }
    memcpy(bpb.fs_type, "FAT32   ", 8);

    // Подготовка загрузочного сектора (512 байт)
    uint8_t boot_sector[SECTOR_SIZE];
    memset(boot_sector, 0, SECTOR_SIZE);
    memcpy(boot_sector, &bpb, sizeof(bpb));
    boot_sector[510] = 0x55;
    boot_sector[511] = 0xAA;

    // Запись основного и резервного загрузочных секторов
    ErrorCode err = disk_write(disk, boot_sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;
    err = disk_write(disk, boot_sector, SECTOR_SIZE, (start_lba + 6) * SECTOR_SIZE); // backup
    if (err != ERR_OK) return err;

    // Сектор FSInfo (основной и резервный)
    uint8_t fsinfo[SECTOR_SIZE];
    memset(fsinfo, 0, SECTOR_SIZE);
    
    *(uint32_t*)(fsinfo + FAT32_FSINFO_LEAD_OFFSET) = FAT32_FSINFO_LEAD_SIG;
    *(uint32_t*)(fsinfo + FAT32_FSINFO_STRUCT_OFFSET) = FAT32_FSINFO_STRUCT_SIG;
    *(uint32_t*)(fsinfo + FAT32_FSINFO_FREE_OFFSET) = 0xFFFFFFFF;
    *(uint32_t*)(fsinfo + FAT32_FSINFO_NEXT_FREE_OFFSET) = 0xFFFFFFFF;
    *(uint32_t*)(fsinfo + FAT32_FSINFO_TRAIL_OFFSET) = FAT32_FSINFO_TRAIL_SIG;

    err = disk_write(disk, fsinfo, SECTOR_SIZE, (start_lba + 1) * SECTOR_SIZE);
    if (err != ERR_OK) return err;
    err = disk_write(disk, fsinfo, SECTOR_SIZE, (start_lba + 7) * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Формирование FAT
    uint32_t fat_size_bytes = fat_sectors * SECTOR_SIZE;
    uint8_t *fat_buffer = (uint8_t*)malloc(fat_size_bytes);
    if (!fat_buffer) return ERR_OUT_OF_MEMORY;
    memset(fat_buffer, 0, fat_size_bytes);
    uint32_t *fat = (uint32_t*)fat_buffer;
    fat[0] = 0x0FFFFFF8;      // медиа-дескриптор
    fat[1] = 0x0FFFFFFF;      // зарезервирован
    fat[2] = 0x0FFFFFFF;      // корневой каталог (конец цепочки)

    // Запись первой FAT
    err = disk_write(disk, fat_buffer, fat_size_bytes, (start_lba + 32) * SECTOR_SIZE);
    if (err != ERR_OK) { free(fat_buffer); return err; }
    // Запись второй FAT
    err = disk_write(disk, fat_buffer, fat_size_bytes, (start_lba + 32 + fat_sectors) * SECTOR_SIZE);
    free(fat_buffer);
    if (err != ERR_OK) return err;

    // Создание корневого каталога в кластере 2 (область данных)
    uint32_t cluster_size = spc * SECTOR_SIZE;
    uint8_t *root_buf = (uint8_t*)calloc(1, cluster_size);
    if (!root_buf) return ERR_OUT_OF_MEMORY;
    uint64_t root_lba = start_lba + 32 + 2 * fat_sectors; // первый сектор данных
    err = disk_write(disk, root_buf, cluster_size, root_lba * SECTOR_SIZE);
    free(root_buf);
    return err; // ERR_OK при успехе
}