#ifndef MBR_H
#define MBR_H

#include <stdint.h>
#include <stdbool.h>

#include "disk.h"
#include "error_codes.h"

#define MBR_PARTITION_TABLE_OFFSET 446   // смещение таблицы разделов в MBR-секторе
#define MBR_PARTITION_ENTRY_SIZE 16      // размер одной записи раздела
#define MBR_SIGNATURE_OFFSET 510         // смещение сигнатуры 0xAA55
#define MBR_SIGNATURE 0xAA55             // корректная сигнатура MBR

#pragma pack(push, 1)
// Запись раздела в таблице MBR (16 байт)
typedef struct {
    uint8_t boot_flag;       // 0x00 — неактивен, 0x80 — активен (загрузочный)
    uint8_t start_head;      // начальная головка (CHS)
    uint8_t start_sector;    // начальный сектор (CHS)
    uint8_t start_cylinder;  // начальный цилиндр (CHS)
    uint8_t partition_type;  // код типа раздела (0x0B — FAT32, 0x83 — Linux, и т.д.)
    uint8_t end_head;        // конечная головка (CHS)
    uint8_t end_sector;      // конечный сектор (CHS)
    uint8_t end_cylinder;    // конечный цилиндр (CHS)
    uint32_t lba_start;      // начальный LBA-адрес раздела
    uint32_t sector_count;   // количество секторов в разделе
} MbrPartitionEntry;

// Полный MBR-сектор (512 байт)
typedef struct {
    uint8_t bootstrap[MBR_PARTITION_TABLE_OFFSET];  // загрузочный код (446 байт)
    MbrPartitionEntry partitions[4];                 // 4 первичных раздела
    uint16_t signature;                              // сигнатура 0xAA55
} MbrSector;
#pragma pack(pop)

// Инициализирует пустую MBR с загрузчиком-заглушкой и корректной сигнатурой
ErrorCode mbr_init(Disk *disk);

// Создаёт раздел с указанным индексом (0–3), размером в секторах и кодом типа
ErrorCode mbr_create_partition(Disk *disk, int index, uint64_t size_sectors, uint8_t type);

// Удаляет раздел по индексу (заполняет запись нулями)
ErrorCode mbr_delete_partition(Disk *disk, int index);

// Устанавливает флаг активности для раздела, сбрасывая флаг у остальных
ErrorCode mbr_set_active(Disk *disk, int index);

// Снимает флаг активности с раздела
ErrorCode mbr_set_inactive(Disk *disk, int index);

// Изменяет код типа раздела
ErrorCode mbr_set_partition_type(Disk *disk, int index, uint8_t type);

// Возвращает начальный LBA и размер раздела в секторах
ErrorCode mbr_get_partition_info(Disk *disk, int index, uint64_t *start_lba, uint64_t *size_sectors);

// Выводит информацию о MBR-разделах через svde_out
void mbr_print_info(Disk *disk);

// Преобразует имя типа (например, "fat32", "linux") в байт типа MBR.
// Возвращает false, если имя не распознано.
bool mbr_type_from_name(const char *name, uint8_t *type);

#endif