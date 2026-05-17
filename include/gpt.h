#ifndef GPT_H
#define GPT_H

#include <stdint.h>
#include <stdbool.h>

#include "disk.h"
#include "error_codes.h"

#define GPT_SIGNATURE "EFI PART"          // сигнатура GPT-заголовка
#define GPT_REVISION 0x00010000           // ревизия GPT (1.0)
#define GPT_HEADER_SIZE 92                // размер заголовка в байтах
#define GPT_PARTITION_ENTRY_SIZE 128      // размер одной записи раздела
#define GPT_MIN_SIZE 34                   // минимальный размер диска в секторах

#pragma pack(push, 1)
// GPT-заголовок (LBA 1)
typedef struct {
    char     signature[8];          // "EFI PART"
    uint32_t revision;              // версия спецификации
    uint32_t header_size;           // размер заголовка (92 байта)
    uint32_t header_crc32;          // CRC32 заголовка (с обнулённым полем crc32)
    uint32_t reserved;              // зарезервировано
    uint64_t current_lba;           // LBA текущего заголовка
    uint64_t backup_lba;            // LBA резервного заголовка
    uint64_t first_usable_lba;      // первый LBA, доступный для разделов
    uint64_t last_usable_lba;       // последний LBA, доступный для разделов
    uint8_t  disk_guid[16];         // уникальный идентификатор диска
    uint64_t partition_entry_lba;   // LBA начала таблицы разделов
    uint32_t num_partition_entries; // количество записей в таблице
    uint32_t partition_entry_size;  // размер одной записи (обычно 128)
    uint32_t partitions_crc32;      // CRC32 таблицы разделов
    uint8_t  padding[420];          // заполнение до размера сектора
} GptHeader;

// Запись раздела в таблице GPT (128 байт)
typedef struct {
    uint8_t  type_guid[16];         // GUID типа раздела
    uint8_t  partition_guid[16];    // уникальный GUID раздела
    uint64_t first_lba;             // начальный LBA
    uint64_t last_lba;              // конечный LBA (включительно)
    uint64_t attributes;            // атрибуты раздела
    uint16_t partition_name[36];    // имя раздела в UTF-16LE (36 символов)
} GptPartitionEntry;
#pragma pack(pop)

// Инициализирует GPT на диске: защитный MBR, заголовок, пустую таблицу разделов
ErrorCode gpt_init(Disk *disk);

// Создаёт раздел с указанным индексом, размером в секторах и GUID типа
ErrorCode gpt_create_partition(Disk *disk, int index, uint64_t size_sectors, const uint8_t *type_guid);

// Удаляет раздел по индексу (заполняет запись нулями)
ErrorCode gpt_delete_partition(Disk *disk, int index);

// Изменяет GUID типа существующего раздела
ErrorCode gpt_set_partition_type(Disk *disk, int index, const uint8_t *type_guid);

// Возвращает начальный LBA и размер раздела в секторах
ErrorCode gpt_get_partition_info(Disk *disk, int index, uint64_t *start_lba, uint64_t *size_sectors);

// Выводит информацию о GPT-разделах через svde_out
void gpt_print_info(Disk *disk);

// Преобразует имя типа (например, "linux", "efi") в GUID типа раздела.
// Возвращает false, если имя не распознано.
bool gpt_type_from_name(const char *name, uint8_t *guid);

// Преобразует 16-байтовый GUID в строку формата "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
void gpt_guid_to_string(const uint8_t *guid, char *str);

// Преобразует строку GUID в 16-байтовый массив. Возвращает 0 при успехе, -1 при ошибке.
int gpt_guid_from_string(const char *str, uint8_t *guid);

#endif