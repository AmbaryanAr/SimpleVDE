#include "fat32.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Чтение сектора FAT
static ErrorCode read_fat_sector(Disk *disk, uint64_t fat_lba, uint32_t sector_num, uint8_t *buffer) {
    return disk_read(disk, buffer, SECTOR_SIZE, (fat_lba + sector_num) * SECTOR_SIZE);
}

static ErrorCode write_fat_sector(Disk *disk, uint64_t fat_lba, uint32_t sector_num, const uint8_t *buffer) {
    return disk_write(disk, buffer, SECTOR_SIZE, (fat_lba + sector_num) * SECTOR_SIZE);
}

static ErrorCode get_fat_entry(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t *value) {
    if (cluster < 2 || cluster > info->total_clusters + 1)
        return ERR_INVALID_ARGUMENT;
    uint64_t fat_offset = cluster * 4;
    uint32_t fat_sector = (uint32_t)(fat_offset / info->bytes_per_sector);
    uint32_t sector_offset = fat_offset % info->bytes_per_sector;
    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = read_fat_sector(disk, info->fat1_lba, fat_sector, sector);
    if (err != ERR_OK) return err;
    *value = *(uint32_t*)(sector + sector_offset) & 0x0FFFFFFF;
    return ERR_OK;
}

static ErrorCode set_fat_entry(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t value) {
    if (cluster < 2 || cluster > info->total_clusters + 1)
        return ERR_INVALID_ARGUMENT;
    uint64_t fat_offset = cluster * 4;
    uint32_t fat_sector = (uint32_t)(fat_offset / info->bytes_per_sector);
    uint32_t sector_offset = fat_offset % info->bytes_per_sector;
    uint8_t sector[SECTOR_SIZE];

    for (uint8_t i = 0; i < info->num_fats; i++) {
        uint64_t fat_lba = info->fat1_lba + i * info->fat_size_sectors;
        ErrorCode err = read_fat_sector(disk, fat_lba, fat_sector, sector);
        if (err != ERR_OK) return err;
        uint32_t *entry = (uint32_t*)(sector + sector_offset);
        *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);
        err = write_fat_sector(disk, fat_lba, fat_sector, sector);
        if (err != ERR_OK) return err;
    }
    return ERR_OK;
}

ErrorCode fat32_get_info(Disk *disk, uint64_t start_lba, Fat32Info *info) {
    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    Fat32BPB *bpb = (Fat32BPB*)sector;
    // Проверка сигнатуры загрузочного сектора
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return ERR_INVALID_SIGNATURE;

    // Проверка признаков FAT32
    if (bpb->bytes_per_sector != SECTOR_SIZE ||
        bpb->sectors_per_cluster == 0 ||
        (bpb->sectors_per_cluster & (bpb->sectors_per_cluster - 1)) != 0 ||
        bpb->reserved_sectors == 0 ||
        bpb->num_fats == 0 ||
        bpb->fat_size_32 == 0 ||
        bpb->root_entries != 0)
        return ERR_INVALID_SIGNATURE;

    uint32_t total_sec = bpb->total_sectors_32;
    if (total_sec == 0) // для FAT32 должно быть >0
        return ERR_INVALID_SIGNATURE;

    info->bytes_per_sector = bpb->bytes_per_sector;
    info->sectors_per_cluster = bpb->sectors_per_cluster;
    info->reserved_sectors = bpb->reserved_sectors;
    info->num_fats = bpb->num_fats;
    info->fat_size_sectors = bpb->fat_size_32;
    info->root_cluster = bpb->root_cluster;

    info->fat1_lba = start_lba + info->reserved_sectors;
    info->fat2_lba = info->fat1_lba + info->fat_size_sectors;
    info->first_data_lba = info->fat2_lba + (info->num_fats - 1) * info->fat_size_sectors;

    uint64_t data_sectors = total_sec - info->reserved_sectors - info->num_fats * info->fat_size_sectors;
    info->total_clusters = (uint32_t)(data_sectors / info->sectors_per_cluster);
    return ERR_OK;
}

uint32_t fat32_alloc_cluster(Disk *disk, const Fat32Info *info) {
    for (uint32_t c = 2; c <= info->total_clusters + 1; c++) {
        uint32_t val;
        if (get_fat_entry(disk, info, c, &val) != ERR_OK)
            return 0;  // ошибка чтения FAT
        if (val == 0) { // свободен
            if (set_fat_entry(disk, info, c, 0x0FFFFFFF) != ERR_OK)
                return 0;
            return c;
        }
    }
    return 0; // нет свободных
}

ErrorCode fat32_free_cluster_chain(Disk *disk, const Fat32Info *info, uint32_t first_cluster) {
    uint32_t current = first_cluster;
    while (current >= 2 && current <= info->total_clusters + 1) {
        uint32_t next;
        ErrorCode err = get_fat_entry(disk, info, current, &next);
        if (err != ERR_OK) return err;
        err = set_fat_entry(disk, info, current, 0);
        if (err != ERR_OK) return err;
        if (next >= 0x0FFFFFF8) break; // конец цепочки
        current = next;
    }
    return ERR_OK;
}

ErrorCode fat32_read_cluster(Disk *disk, const Fat32Info *info, uint32_t cluster, uint8_t *buffer) {
    if (cluster < 2 || cluster > info->total_clusters + 1) return ERR_INVALID_ARGUMENT;
    uint64_t lba = info->first_data_lba + (uint64_t)(cluster - 2) * info->sectors_per_cluster;
    size_t bytes = info->sectors_per_cluster * info->bytes_per_sector;
    return disk_read(disk, buffer, bytes, lba * SECTOR_SIZE);
}

ErrorCode fat32_write_cluster(Disk *disk, const Fat32Info *info, uint32_t cluster, const uint8_t *buffer) {
    if (cluster < 2 || cluster > info->total_clusters + 1) return ERR_INVALID_ARGUMENT;
    uint64_t lba = info->first_data_lba + (uint64_t)(cluster - 2) * info->sectors_per_cluster;
    size_t bytes = info->sectors_per_cluster * info->bytes_per_sector;
    return disk_write(disk, buffer, bytes, lba * SECTOR_SIZE);
}

ErrorCode fat32_get_next_cluster(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t *next) {
    if (cluster < 2) return ERR_INVALID_ARGUMENT;
    return get_fat_entry(disk, info, cluster, next);
}

ErrorCode fat32_set_fat_entry(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t value) {
    return set_fat_entry(disk, info, cluster, value);
}