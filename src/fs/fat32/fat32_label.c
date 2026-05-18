#include "fat32_label.h"
#include "fat32.h"
#include <string.h>

ErrorCode fat32_get_label(Disk *disk, uint64_t start_lba, char *out, size_t out_size) {
    if (!disk || !out || out_size < 12) return ERR_INVALID_ARGUMENT;

    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    Fat32BPB *bpb = (Fat32BPB*)sector;
    memcpy(out, bpb->volume_label, 11);
    out[11] = '\0';

    // Убираем trailing-пробелы
    for (int i = 10; i >= 0 && out[i] == ' '; i--) {
        out[i] = '\0';
    }

    return ERR_OK;
}

ErrorCode fat32_set_label(Disk *disk, uint64_t start_lba, const char *label) {
    if (!disk || !label) return ERR_INVALID_ARGUMENT;

    uint8_t sector[SECTOR_SIZE];
    ErrorCode err = disk_read(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    Fat32BPB *bpb = (Fat32BPB*)sector;
    memset(bpb->volume_label, ' ', 11);
    size_t len = strlen(label);
    if (len > 11) len = 11;
    memcpy(bpb->volume_label, label, len);

    // Основной BPB
    err = disk_write(disk, sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Резервный BPB (сектор 6)
    err = disk_write(disk, sector, SECTOR_SIZE, (start_lba + 6) * SECTOR_SIZE);
    return err;
}