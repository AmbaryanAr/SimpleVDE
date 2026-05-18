#include "fat32.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static ErrorCode get_fat_entry(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t *value) {
    (void)disk;
    if (cluster < 2 || cluster > info->total_clusters + 1)
        return ERR_INVALID_ARGUMENT;
    if (!info->fat_cache)
        return ERR_FAT32_FAT_CORRUPT;
    *value = info->fat_cache[cluster] & 0x0FFFFFFF;
    return ERR_OK;
}

static ErrorCode set_fat_entry(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t value) {
    if (cluster < 2 || cluster > info->total_clusters + 1)
        return ERR_INVALID_ARGUMENT;
    if (!info->fat_cache)
        return ERR_FAT32_FAT_CORRUPT;

    // Обновляем кэш
    info->fat_cache[cluster] = (info->fat_cache[cluster] & 0xF0000000) | (value & 0x0FFFFFFF);

    // Пишем во все копии FAT на диск
    uint64_t fat_offset = cluster * 4;
    uint32_t fat_sector = (uint32_t)(fat_offset / info->bytes_per_sector);
    uint32_t sector_offset = fat_offset % info->bytes_per_sector;
    uint8_t sector[SECTOR_SIZE];

    for (uint8_t i = 0; i < info->num_fats; i++) {
        uint64_t fat_lba = info->fat1_lba + i * info->fat_size_sectors;
        ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, fat_lba * SECTOR_SIZE + fat_sector * SECTOR_SIZE);
        if (err != ERR_OK) return err;
        *(uint32_t*)(sector + sector_offset) = info->fat_cache[cluster];
        err = disk_write(disk, sector, SECTOR_SIZE, fat_lba * SECTOR_SIZE + fat_sector * SECTOR_SIZE);
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

    // Читаем FSInfo
    uint8_t fsinfo[SECTOR_SIZE];
    uint64_t fsinfo_lba = start_lba + bpb->fs_info_sector;
    if (disk_read(disk, fsinfo, SECTOR_SIZE, fsinfo_lba * SECTOR_SIZE) == ERR_OK) {
        info->free_clusters = *(uint32_t*)(fsinfo + FAT32_FSINFO_FREE_OFFSET);
        info->next_free_cluster = *(uint32_t*)(fsinfo + FAT32_FSINFO_NEXT_FREE_OFFSET);
    } else {
        info->free_clusters = 0xFFFFFFFF;
        info->next_free_cluster = 0xFFFFFFFF;
    }

    // Загружаем FAT в кэш
    info->fat_cache = (uint32_t*)malloc(info->fat_size_sectors * info->bytes_per_sector);
    if (info->fat_cache) {
        err = disk_read(disk, info->fat_cache,
                        info->fat_size_sectors * info->bytes_per_sector,
                        info->fat1_lba * SECTOR_SIZE);
        if (err != ERR_OK) {
            free(info->fat_cache);
            info->fat_cache = NULL;
            return err;
        }
    }

    return ERR_OK;
}

uint32_t fat32_alloc_cluster(Disk *disk, const Fat32Info *info) {
    if (!info->fat_cache) return 0;
    uint32_t start = info->next_free_cluster;
    if (start < 2 || start > info->total_clusters + 1)
        start = 2;

    // Ищем от start до конца
    for (uint32_t c = start; c <= info->total_clusters + 1; c++) {
        if ((info->fat_cache[c] & 0x0FFFFFFF) == 0) {
            set_fat_entry(disk, info, c, 0x0FFFFFFF);
            Fat32Info *mutable_info = (Fat32Info*)info;
            mutable_info->free_clusters--;
            mutable_info->next_free_cluster = (c + 1 <= info->total_clusters + 1) ? c + 1 : 2;
            fat32_update_fsinfo(disk, info);
            return c;
        }
    }
    // Ищем от 2 до start
    for (uint32_t c = 2; c < start; c++) {
        if ((info->fat_cache[c] & 0x0FFFFFFF) == 0) {
            set_fat_entry(disk, info, c, 0x0FFFFFFF);
            Fat32Info *mutable_info = (Fat32Info*)info;
            mutable_info->free_clusters--;
            mutable_info->next_free_cluster = (c + 1 <= info->total_clusters + 1) ? c + 1 : 2;
            fat32_update_fsinfo(disk, info);
            return c;
        }
    }
    return 0;
}

ErrorCode fat32_free_cluster_chain(Disk *disk, const Fat32Info *info, uint32_t first_cluster) {
    uint32_t current = first_cluster;
    uint32_t freed = 0;
    while (current >= 2 && current <= info->total_clusters + 1) {
        uint32_t next;
        ErrorCode err = get_fat_entry(disk, info, current, &next);
        if (err != ERR_OK) return err;
        err = set_fat_entry(disk, info, current, 0);
        if (err != ERR_OK) return err;
        freed++;
        if (next >= 0x0FFFFFF8) break;
        current = next;
    }
    // Обновляем счётчики
    Fat32Info *mutable_info = (Fat32Info*)info;
    mutable_info->free_clusters += freed;
    fat32_update_fsinfo(disk, info);
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

ErrorCode fat32_update_fsinfo(Disk *disk, const Fat32Info *info) {
    uint8_t sector[SECTOR_SIZE];
    uint64_t fsinfo_lba = info->fat1_lba - info->reserved_sectors + 1;

    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, fsinfo_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Обновляем счётчик свободных кластеров
    *(uint32_t*)(sector + FAT32_FSINFO_FREE_OFFSET) = info->free_clusters;

    // Обновляем подсказку для следующего свободного кластера
    *(uint32_t*)(sector + FAT32_FSINFO_NEXT_FREE_OFFSET) = info->next_free_cluster;

    // Основной FSInfo
    err = disk_write(disk, sector, SECTOR_SIZE, fsinfo_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Резервный FSInfo (сектор 7)
    err = disk_write(disk, sector, SECTOR_SIZE, (fsinfo_lba + 6) * SECTOR_SIZE);
    return err;
}

void fat32_free_cache(Fat32Info *info) {
    if (info && info->fat_cache) {
        free(info->fat_cache);
        info->fat_cache = NULL;
    }
}
