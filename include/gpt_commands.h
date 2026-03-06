#pragma once

#include "disk.h"
#include "mbr_commands.h"
#include "error_code.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

// Размеры
#define GPT_HEADER_SIZE 92
#define GPT_PARTITION_ENTRY_SIZE 128
#define GPT_SIGNATURE "EFI PART"
#define GPT_REVISION 0x00010000  // 1.0

// Заголовок GPT
#pragma pack(push, 1)
typedef struct {
    char     signature[8];             // "EFI PART"
    uint32_t revision;                 // 0x00010000
    uint32_t header_size;              // 92
    uint32_t header_crc32;             // контрольная сумма
    uint32_t reserved;                 // 0
    uint64_t current_lba;              // LBA этого заголовка (1)
    uint64_t backup_lba;               // LBA резервной копии (последний сектор)
    uint64_t first_usable_lba;         // первый доступный для разделов LBA (обычно 34)
    uint64_t last_usable_lba;          // последний доступный LBA
    uint8_t  disk_guid[16];            // GUID диска
    uint64_t partition_entry_lba;      // LBA начала таблицы разделов (обычно 2)
    uint32_t num_partition_entries;    // количество записей (обычно 128)
    uint32_t partition_entry_size;     // размер записи (128)
    uint32_t partitions_crc32;         // CRC32 таблицы разделов
    uint8_t  padding[420];             // остаток до 512 байт (512 - 92 = 420)
} GPTHeader;
#pragma pack(pop)

// Запись раздела GPT
#pragma pack(push, 1)
typedef struct {
    uint8_t  type_guid[16];             // GUID типа раздела
    uint8_t  partition_guid[16];        // уникальный GUID раздела
    uint64_t first_lba;                 // первый LBA
    uint64_t last_lba;                  // последний LBA
    uint64_t attributes;                // атрибуты
    uint16_t partition_name[36];        // имя (UTF-16LE), 36 символов
} GPTPartitionEntry;
#pragma pack(pop)

// Типы разделов (GUID)
#define GPT_TYPE_UNUSED                {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
#define GPT_TYPE_EFI_SYSTEM            {0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B}
#define GPT_TYPE_LINUX_FILESYSTEM      {0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4}
#define GPT_TYPE_LINUX_SWAP            {0x6D,0xFD,0x57,0x06,0xAB,0xA4,0xC4,0x43,0x84,0xE5,0x09,0x33,0xC8,0x4B,0x4F,0x4F}
#define GPT_TYPE_LINUX_LVM             {0x79,0xD3,0xD6,0xE6,0x07,0xF5,0xC2,0x44,0xA2,0x3C,0x23,0x8F,0x2A,0x3D,0xF9,0x28}
#define GPT_TYPE_WINDOWS_BASIC_DATA    {0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7}

/**
 * Создаёт GPT (GUID Partition Table) на открытом диске.
 * Записывает защитный MBR в первый сектор, заголовок GPT во второй сектор,
 * пустую таблицу разделов (обычно в секторах 2–33) и их резервные копии
 * в конце диска. Диск должен быть открыт для записи и иметь достаточный размер
 * (минимум 34 сектора).
 *
 * @param disk Указатель на открытый диск (структура Disk).
 * @return Код ошибки:
 *         - ERR_OK – GPT успешно создана;
 *         - ERR_DISK_OPEN – диск не открыт или передан нулевой указатель;
 *         - ERR_DISK_CREATE – диск слишком мал для GPT (меньше 34 секторов);
 *         - ERR_GENERIC – ошибка выделения памяти;
 *         - ERR_DISK_WRITE – ошибка записи одного из компонентов GPT.
 */
ErrorCode gpt_create(Disk *disk);

// int gpt_creat_partition_table( ... );
// int gpt_delete_partition_table( ... );

// Работа с GUID
void guid_to_string(const uint8_t *guid, char *str);
int guid_from_string(const char *str, uint8_t *guid);
/**
 * Выводит информацию о GPT-таблице разделов на открытом диске.
 * Читает заголовок GPT из первого сектора (LBA 1) и отображает:
 * - ревизию,
 * - GUID диска,
 * - первый и последний доступные LBA,
 * - количество записей о разделах.
 * (В будущем может выводить и сами разделы.)
 *
 * @param disk Указатель на открытый диск (должен быть корректен и содержать GPT).
 */
void gpt_print_info(Disk *disk);