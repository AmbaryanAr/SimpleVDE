#include "fat32_info.h"
#include "fat32.h"
#include "fat32_label.h"
#include "output.h"
#include <string.h>
#include <inttypes.h>

ErrorCode fat32_print_info(Disk *disk, uint64_t start_lba) {
    Fat32Info info;
    ErrorCode err = fat32_get_info(disk, start_lba, &info);
    if (err != ERR_OK) return err;

    char label[12];
    fat32_get_label(disk, start_lba, label, sizeof(label));

    uint32_t cluster_size = info.sectors_per_cluster * info.bytes_per_sector;

    svde_out("Filesystem: FAT32\n");
    svde_out("Volume label: %s\n", label[0] ? label : "(empty)");

    // Читаем серийный номер из BPB
    uint8_t sector[SECTOR_SIZE];
    disk_read(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    Fat32BPB *bpb = (Fat32BPB*)sector;
    uint32_t serial = bpb->volume_id;
    svde_out("Serial number: %08X\n", serial);

    svde_out("\nGeometry:\n");
    svde_out("  Bytes per sector:     %u\n", info.bytes_per_sector);
    svde_out("  Sectors per cluster:  %u\n", info.sectors_per_cluster);
    svde_out("  Cluster size:         %u bytes (%.1f KB)\n", cluster_size, cluster_size / 1024.0);
    svde_out("  Reserved sectors:     %u\n", info.reserved_sectors);
    svde_out("  Number of FATs:       %u\n", info.num_fats);
    svde_out("  FAT size:             %u sectors (%u KB)\n",
           info.fat_size_sectors, (info.fat_size_sectors * info.bytes_per_sector) / 1024);

    svde_out("\nLayout:\n");
    svde_out("  Boot sector LBA:      %" PRIu64 "\n", start_lba);
    svde_out("  FAT1 LBA:             %" PRIu64 "\n", info.fat1_lba);
    svde_out("  FAT2 LBA:             %" PRIu64 "\n", info.fat2_lba);
    svde_out("  Data area LBA:        %" PRIu64 "\n", info.first_data_lba);

    // Подсчитываем занятые кластеры (быстрый проход по FAT)
    uint32_t free_count = 0;
    uint32_t bad_count = 0;
    uint32_t used_count = 0;

    for (uint32_t c = 2; c <= info.total_clusters + 1; c++) {
        uint32_t val;
        if (fat32_get_next_cluster(disk, &info, c, &val) == ERR_OK) {
            if (val == FAT32_CLUSTER_FREE) {
                free_count++;
            } else if (val == FAT32_CLUSTER_BAD) {
                bad_count++;
            } else {
                used_count++;
            }
        }
    }

    uint64_t total_bytes = (uint64_t)info.total_clusters * cluster_size;
    uint64_t free_bytes = (uint64_t)free_count * cluster_size;
    uint64_t used_bytes = (uint64_t)used_count * cluster_size;

    svde_out("\nUsage:\n");
    svde_out("  Total clusters:       %u\n", info.total_clusters);
    svde_out("  Used clusters:        %u\n", used_count);
    svde_out("  Free clusters:        %u\n", free_count);
    svde_out("  Bad clusters:         %u\n", bad_count);
    svde_out("  Total space:          %" PRIu64 " bytes (%.2f MB)\n",
           total_bytes, total_bytes / (1024.0 * 1024.0));
    svde_out("  Used space:           %" PRIu64 " bytes (%.2f MB)\n",
           used_bytes, used_bytes / (1024.0 * 1024.0));
    svde_out("  Free space:           %" PRIu64 " bytes (%.2f MB)\n",
           free_bytes, free_bytes / (1024.0 * 1024.0));

    return ERR_OK;
}