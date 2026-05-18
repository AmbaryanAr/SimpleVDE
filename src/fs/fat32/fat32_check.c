#include "fat32.h"
#include "output.h"
#include "fat32_check.h"
#include <string.h>
#include <stdlib.h>

// Проверяет сигнатуру BPB (0x55AA) и корректность основных полей
static ErrorCode check_bpb(Disk *disk, uint64_t start_lba, Fat32Info *info) {
    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) {
        svde_out("  [FAIL] Cannot read boot sector\n");
        return ERR_DISK_READ;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        svde_out("  [FAIL] Invalid boot signature (expected 0x55AA)\n");
        return ERR_INVALID_SIGNATURE;
    }
    svde_out("  [OK] Boot signature 0x55AA\n");

    Fat32BPB *bpb = (Fat32BPB*)sector;

    if (bpb->bytes_per_sector != SECTOR_SIZE) {
        svde_out("  [FAIL] Invalid bytes per sector: %u (expected 512)\n", bpb->bytes_per_sector);
        return ERR_FAT32_BAD_BPB;
    }
    svde_out("  [OK] Bytes per sector: 512\n");

    if (bpb->sectors_per_cluster == 0 || (bpb->sectors_per_cluster & (bpb->sectors_per_cluster - 1)) != 0) {
        svde_out("  [FAIL] Invalid sectors per cluster: %u (must be power of 2)\n", bpb->sectors_per_cluster);
        return ERR_FAT32_BAD_BPB;
    }
    svde_out("  [OK] Sectors per cluster: %u\n", bpb->sectors_per_cluster);

    if (bpb->reserved_sectors == 0) {
        svde_out("  [FAIL] Reserved sectors is zero\n");
        return ERR_FAT32_BAD_BPB;
    }
    svde_out("  [OK] Reserved sectors: %u\n", bpb->reserved_sectors);

    if (bpb->num_fats == 0) {
        svde_out("  [FAIL] Number of FATs is zero\n");
        return ERR_FAT32_BAD_BPB;
    }
    svde_out("  [OK] Number of FATs: %u\n", bpb->num_fats);

    if (bpb->fat_size_32 == 0) {
        svde_out("  [FAIL] FAT size is zero\n");
        return ERR_FAT32_BAD_BPB;
    }
    svde_out("  [OK] FAT size: %u sectors\n", bpb->fat_size_32);

    if (bpb->root_entries != 0) {
        svde_out("  [FAIL] Root entries must be 0 for FAT32 (got %u)\n", bpb->root_entries);
        return ERR_FAT32_BAD_BPB;
    }

    // Заполняем info для дальнейших проверок
    err = fat32_get_info(disk, start_lba, info);
    if (err != ERR_OK) {
        svde_out("  [FAIL] Cannot parse filesystem info\n");
        return err;
    }

    return ERR_OK;
}

// Проверяет сигнатуры FSInfo
static ErrorCode check_fsinfo(Disk *disk, const Fat32Info *info) {
    uint8_t sector[SECTOR_SIZE];
    uint64_t fsinfo_lba = info->fat1_lba - info->reserved_sectors + 1;
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, fsinfo_lba * SECTOR_SIZE);
    if (err != ERR_OK) {
        svde_out("  [WARN] Cannot read FSInfo sector\n");
        return ERR_OK; // не критично
    }

    uint32_t lead_sig = *(uint32_t*)(sector + 0);
    uint32_t struct_sig = *(uint32_t*)(sector + 484);
    uint32_t trail_sig = *(uint32_t*)(sector + 508);

    int errors = 0;
    if (lead_sig != 0x41615252) {
        svde_out("  [FAIL] FSInfo lead signature invalid (0x%08X)\n", lead_sig);
        errors++;
    }
    if (struct_sig != 0x61417272) {
        svde_out("  [FAIL] FSInfo struct signature invalid (0x%08X)\n", struct_sig);
        errors++;
    }
    if (trail_sig != 0xAA550000) {
        svde_out("  [FAIL] FSInfo trail signature invalid (0x%08X)\n", trail_sig);
        errors++;
    }

    if (errors == 0) {
        svde_out("  [OK] FSInfo signatures\n");
    }
    return ERR_OK;
}

// Полная проверка FAT и цепочек кластеров
static ErrorCode check_fat_and_clusters(Disk *disk, const Fat32Info *info) {
    uint32_t total_clusters = info->total_clusters;
    uint8_t *cluster_map = (uint8_t*)calloc(total_clusters + 2, 1);
    if (!cluster_map) {
        svde_out("  [FAIL] Out of memory\n");
        return ERR_OUT_OF_MEMORY;
    }

    int errors = 0;
    int free_count = 0;
    int bad_count = 0;

    svde_out("  Scanning FAT (%u clusters)...\n", total_clusters);

    for (uint32_t c = 2; c <= total_clusters + 1; c++) {
        uint32_t val;
        if (fat32_get_next_cluster(disk, info, c, &val) != ERR_OK) {
            svde_out("  [FAIL] Cannot read FAT entry for cluster %u\n", c);
            errors++;
            continue;
        }

        if (val == FAT32_CLUSTER_FREE) {
            free_count++;
        } else if (val == FAT32_CLUSTER_BAD) {
            bad_count++;
        } else if (val >= FAT32_CLUSTER_LAST_MIN) {
            // Конец цепочки — корректно
        } else if (val >= 2 && val <= total_clusters + 1) {
            // Ссылка на другой кластер — проверим, что нет дублей
            cluster_map[val]++;
            if (cluster_map[val] > 1) {
                svde_out("  [FAIL] Cluster %u referenced more than once\n", val);
                errors++;
            }
        } else {
            svde_out("  [FAIL] Cluster %u has invalid FAT value 0x%08X\n", c, val);
            errors++;
        }
    }

    // Проверка кластеров, которые не принадлежат ни одной цепочке (потерянные)
    // int orphaned = 0;
    // for (uint32_t c = 2; c <= total_clusters + 1; c++) {
    //     uint32_t val;
    //     if (fat32_get_next_cluster(disk, info, c, &val) == ERR_OK) {
    //         if (val != FAT32_CLUSTER_FREE && cluster_map[c] == 0) {
    //             // Занятый кластер, на который никто не ссылается
    //             // Это может быть начало цепочки (файл/каталог) — не ошибка.
    //             // Потерянные — те, на которые нет ссылок и которые не являются началом.
    //             // Пропускаем для простоты.
    //         }
    //     }
    // }

    svde_out("  Free clusters: %d\n", free_count);
    svde_out("  Bad clusters: %d\n", bad_count);

    if (errors > 0) {
        svde_out("  [FAIL] %d error(s) in FAT\n", errors);
    } else {
        svde_out("  [OK] FAT table\n");
    }

    free(cluster_map);
    return errors > 0 ? ERR_FAT32_FAT_CORRUPT : ERR_OK;
}

ErrorCode fat32_check(Disk *disk, uint64_t start_lba, int level) {
    svde_out("FAT32 filesystem check (level: %s)\n", level == FSCHECK_LEVEL_FULL ? "full" : "quick");
    svde_out("========================================\n");

    Fat32Info info;
    ErrorCode err = check_bpb(disk, start_lba, &info);
    if (err != ERR_OK) {
        svde_out("\nCheck aborted: BPB is corrupt.\n");
        return err;
    }

    err = check_fsinfo(disk, &info);
    if (err != ERR_OK && level == FSCHECK_LEVEL_QUICK) {
        // FSInfo ошибки не прерывают проверку
    }

    if (level == FSCHECK_LEVEL_FULL) {
        ErrorCode fat_err = check_fat_and_clusters(disk, &info);
        if (fat_err != ERR_OK) {
            svde_out("\nCheck finished with errors.\n");
            return fat_err;
        }
    }

    svde_out("\nCheck finished: no errors.\n");
    return ERR_OK;
}