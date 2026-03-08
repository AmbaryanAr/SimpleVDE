#include "fat32_commands.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <stdlib.h>

// Структура BPB для FAT32
#pragma pack(push, 1)
typedef struct {
    uint8_t  jump_boot[3];          // 0xEB 0x?? 0x90
    char     oem_name[8];            // "MSWIN4.1" или что-то подобное
    uint16_t bytes_per_sector;       // обычно 512
    uint8_t  sectors_per_cluster;    // должно быть степенью двойки (1,2,4,8,16,32,64,128)
    uint16_t reserved_sectors;       // обычно 32 для FAT32
    uint8_t  num_fats;               // обычно 2
    uint16_t root_entries;           // для FAT32 всегда 0
    uint16_t total_sectors_16;       // для FAT32 всегда 0
    uint8_t  media_descriptor;       // 0xF8 для жёсткого диска
    uint16_t fat_size_16;            // для FAT32 всегда 0
    uint16_t sectors_per_track;      // обычно 63
    uint16_t num_heads;              // обычно 255
    uint32_t hidden_sectors;         // количество секторов до начала раздела (start_lba)
    uint32_t total_sectors_32;       // общее количество секторов в разделе
    uint32_t fat_size_32;            // количество секторов на одну FAT
    uint16_t ext_flags;              // 0
    uint16_t fs_version;             // 0
    uint32_t root_cluster;           // первый кластер корневого каталога (обычно 2)
    uint16_t fs_info;                 // сектор FSInfo (обычно 1)
    uint16_t backup_boot_sector;     // сектор резервной копии загрузочного сектора (обычно 6)
    uint8_t  reserved[12];           // зарезервировано
    uint8_t  drive_number;           // 0x00 или 0x80
    uint8_t  reserved1;              // 0
    uint8_t  boot_signature;         // 0x29
    uint32_t volume_id;              // серийный номер тома
    char     volume_label[11];       // "NO NAME    "
    char     fs_type[8];             // "FAT32   "
} BPB_FAT32;

typedef struct {
    uint32_t lead_signature;              // 0x41615252
    uint8_t  reserved1[480];
    uint32_t struct_signature;             // 0x61417272
    uint32_t free_count;                    // 0xFFFFFFFF
    uint32_t next_free;                      // 0xFFFFFFFF
    uint8_t  reserved2[12];
    uint32_t trail_signature;                // 0xAA550000
} FSInfo;
#pragma pack(pop)

// *** Вспомогательные функции ***

// Функция для вычисления размера FAT
static uint32_t fat32_calc_fat_sectors(uint32_t total_sectors, uint32_t sectors_per_cluster) {
    uint32_t data_sectors = total_sectors - 32; // резервные секторы (32)
    uint32_t clusters = data_sectors / sectors_per_cluster;
    // Каждый кластер занимает 4 байта в FAT
    uint32_t fat_bytes = clusters * 4;
    uint32_t fat_sectors = (fat_bytes + 511) / 512;
    // Округлим вверх, чтобы хватило
    return fat_sectors;
}
// ***

ErrorCode fat32_format(Disk *disk, uint64_t start_lba, uint64_t total_sectors, uint8_t drive_number) {
    if (total_sectors > UINT32_MAX) {
        printf("Error: FAT32 partition too large (max 2TB)\n");
        return ERR_INVALID_VALUE;
    }
    uint32_t total_sec = (uint32_t)total_sectors;

    // Выбираем размер кластера в зависимости от размера раздела
    uint8_t sectors_per_cluster;
    if (total_sec <= 512 * 1024) // 256 МБ
        sectors_per_cluster = 1;
    else if (total_sec <= 1024 * 1024) // 512 МБ
        sectors_per_cluster = 2;
    else if (total_sec <= 2048 * 1024) // 1 ГБ
        sectors_per_cluster = 4;
    else if (total_sec <= 4096 * 1024) // 2 ГБ
        sectors_per_cluster = 8;
    else if (total_sec <= 8192 * 1024) // 4 ГБ
        sectors_per_cluster = 16;
    else
        sectors_per_cluster = 32; // до 2 ТБ

    uint32_t fat_sectors = fat32_calc_fat_sectors(total_sec, sectors_per_cluster);

    // Заполняем BPB
    BPB_FAT32 bpb;
    memset(&bpb, 0, sizeof(bpb));
    bpb.jump_boot[0] = 0xEB;
    bpb.jump_boot[1] = 0x58;
    bpb.jump_boot[2] = 0x90;
    memcpy(bpb.oem_name, "MSWIN4.1", 8);
    bpb.bytes_per_sector = 512;
    bpb.sectors_per_cluster = sectors_per_cluster;
    bpb.reserved_sectors = 32;
    bpb.num_fats = 2;
    bpb.root_entries = 0;
    bpb.total_sectors_16 = 0;
    bpb.media_descriptor = 0xF8;
    bpb.fat_size_16 = 0;
    bpb.sectors_per_track = 63;
    bpb.num_heads = 255;
    bpb.hidden_sectors = (uint32_t)start_lba;
    bpb.total_sectors_32 = total_sec;
    bpb.fat_size_32 = fat_sectors;
    bpb.ext_flags = 0;
    bpb.fs_version = 0;
    bpb.root_cluster = 2;
    bpb.fs_info = 1;
    bpb.backup_boot_sector = 6;
    bpb.drive_number = drive_number;
    bpb.boot_signature = 0x29;
    // Генерируем случайный volume_id (можно использовать rand)
    srand(time(NULL));
    bpb.volume_id = (uint32_t)rand();
    memcpy(bpb.volume_label, "NO NAME    ", 11);
    memcpy(bpb.fs_type, "FAT32   ", 8);

	// Создаём буфер для целого сектора
	uint8_t boot_sector[SECTOR_SIZE];
	memset(boot_sector, 0, SECTOR_SIZE);
	memcpy(boot_sector, &bpb, sizeof(bpb));
	boot_sector[510] = 0x55;
	boot_sector[511] = 0xAA;

    // Запись BPB в первый сектор раздела
    ErrorCode err = disk_write(disk, boot_sector, SECTOR_SIZE, start_lba * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Запись резервной копии BPB в сектор 6 (backup boot sector)
    err = disk_write(disk, boot_sector, SECTOR_SIZE, (start_lba + 6) * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Заполнение FSInfo
    FSInfo fsi;
    memset(&fsi, 0, sizeof(fsi));
    fsi.lead_signature = 0x41615252;
    fsi.struct_signature = 0x61417272;
    fsi.free_count = 0xFFFFFFFF; // неизвестно
    fsi.next_free = 0xFFFFFFFF;
    fsi.trail_signature = 0xAA550000;

    // Запись FSInfo в сектор 1
    err = disk_write(disk, &fsi, sizeof(fsi), (start_lba + 1) * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Резервная копия FSInfo в сектор 7
    err = disk_write(disk, &fsi, sizeof(fsi), (start_lba + 7) * SECTOR_SIZE);
    if (err != ERR_OK) return err;

    // Инициализация FAT (таблицы размещения файлов)
    uint32_t fat_size_bytes = fat_sectors * SECTOR_SIZE;
    uint8_t *fat_buffer = (uint8_t*)malloc(fat_size_bytes);
    if (!fat_buffer) return ERR_GENERIC;
    memset(fat_buffer, 0, fat_size_bytes);

    // Первые два кластера зарезервированы
    // Кластер 0: обычно 0x0FFFFFF8 (конец цепочки для зарезервированных)
    // Кластер 1: 0x0FFFFFFF (конец цепочки)
    // Кластер 2: 0x0FFFFFFF (корневой каталог)
    uint32_t *fat = (uint32_t*)fat_buffer;
    fat[0] = 0x0FFFFFF8; // зарезервированный кластер
    fat[1] = 0x0FFFFFFF; // конец цепочки для корневого каталога? Обычно в FAT32 первый кластер корня = 2, и в FAT[2] ставится 0x0FFFFFFF. Но по спецификации FAT[0] и FAT[1] зарезервированы.
    // Установим кластер 2 как конец цепочки
    fat[2] = 0x0FFFFFFF;

    // Запись основной FAT (начинается с сектора 32)
    err = disk_write(disk, fat_buffer, fat_size_bytes, (start_lba + 32) * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(fat_buffer);
        return err;
    }

    // Запись второй FAT (сразу за первой)
    err = disk_write(disk, fat_buffer, fat_size_bytes, (start_lba + 32 + fat_sectors) * SECTOR_SIZE);
    if (err != ERR_OK) {
        free(fat_buffer);
        return err;
    }
    free(fat_buffer);

    // Инициализация корневого каталога (кластер 2) – заполняем нулями
    uint32_t root_sectors = sectors_per_cluster; // размер одного кластера
    uint8_t *root_buffer = (uint8_t*)calloc(1, root_sectors * SECTOR_SIZE);
    if (!root_buffer) return ERR_GENERIC;
    uint64_t root_lba = start_lba + 32 + 2 * fat_sectors; // после двух FAT
    err = disk_write(disk, root_buffer, root_sectors * SECTOR_SIZE, root_lba * SECTOR_SIZE);
    free(root_buffer);
    if (err != ERR_OK) return err;

    // Всё готово
    return ERR_OK;
}