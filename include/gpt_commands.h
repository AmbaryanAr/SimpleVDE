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
 * Преобразует имя файловой системы (например, "linux", "efi") в GUID.
 * @param name Имя файловой системы (регистр не учитывается).
 * @param guid  Указатель на массив из 16 байт для записи GUID.
 * @return 0 при успехе, -1 если имя не найдено.
 */
int gpt_type_from_name(const char *name, uint8_t *guid);

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

/**
 * Создаёт новый раздел в таблице GPT на открытом диске.
 *
 * Функция читает заголовок GPT и таблицу разделов, находит свободную запись
 * с заданным индексом (если индекс уже занят, возвращается ошибка),
 * определяет стартовый LBA (вслед за последним существующим разделом),
 * выделяет указанное количество секторов (если size_sectors == 0, занимает
 * всё доступное место до конца области разделов), заполняет запись раздела
 * (тип, GUID раздела, начальный и конечный LBA, атрибуты), пересчитывает
 * CRC32 таблицы и заголовка, и записывает обновлённые структуры как в
 * основное, так и в резервное расположение.
 *
 * @param disk         Указатель на открытый диск.
 * @param index        Индекс нового раздела (от 0 до header.num_partition_entries-1).
 * @param size_sectors Размер раздела в секторах. Если 0, занимает всё оставшееся свободное место.
 * @param type_guid    Указатель на 16-байтовый GUID типа раздела (например, Linux filesystem).
 * @return Код ошибки:
 *         - ERR_OK – раздел успешно создан;
 *         - ERR_DISK_OPEN – диск не открыт;
 *         - ERR_INVALID_VALUE – неверный индекс, запись занята, недостаточно места,
 *           или диск не содержит корректной GPT;
 *         - ERR_DISK_READ – ошибка чтения заголовка или таблицы разделов;
 *         - ERR_DISK_WRITE – ошибка записи обновлённых структур;
 *         - ERR_GENERIC – ошибка выделения памяти.
 */
ErrorCode gpt_create_partition(Disk *disk, int index, uint64_t size_sectors, const uint8_t *type_guid);

/**
 * Удаляет запись о разделе из таблицы GPT (GUID Partition Table) на открытом диске.
 *
 * Функция выполняет следующие действия:
 * - читает заголовок GPT из второго сектора (LBA 1);
 * - читает таблицу разделов, расположенную по адресу partition_entry_lba;
 * - зануляет запись с указанным индексом;
 * - пересчитывает CRC32 таблицы разделов и CRC32 заголовка GPT;
 * - записывает обновлённую таблицу разделов в основное и резервное расположения;
 * - записывает обновлённый заголовок GPT в основной (LBA 1) и резервный (последний сектор) экземпляры.
 *
 * @param disk  Указатель на открытый диск (структура Disk).
 * @param index Индекс удаляемого раздела (от 0 до header.num_partition_entries - 1).
 * @return Код ошибки:
 *         - ERR_OK – раздел успешно удалён;
 *         - ERR_DISK_OPEN – диск не открыт или передан нулевой указатель;
 *         - ERR_INVALID_VALUE – индекс выходит за допустимые пределы;
 *         - ERR_DISK_READ – ошибка чтения заголовка GPT или таблицы разделов;
 *         - ERR_DISK_WRITE – ошибка записи обновлённых данных;
 *         - ERR_GENERIC – ошибка выделения памяти или несоответствие сигнатуры.
 */
ErrorCode gpt_delete_partition(Disk *disk, int index);

/**
 * Изменяет GUID типа существующего раздела в таблице GPT.
 *
 * @param disk      Указатель на открытый диск.
 * @param index     Индекс раздела (0..header.num_partition_entries-1).
 * @param type_guid Новый GUID типа (16 байт).
 * @return Код ошибки:
 *         - ERR_OK – тип успешно изменён;
 *         - ERR_DISK_OPEN – диск не открыт;
 *         - ERR_INVALID_VALUE – неверный индекс, раздел не существует,
 *           диск не содержит корректной GPT;
 *         - ERR_DISK_READ – ошибка чтения;
 *         - ERR_DISK_WRITE – ошибка записи;
 *         - ERR_GENERIC – ошибка выделения памяти.
 */
ErrorCode gpt_set_partition_type(Disk *disk, int index, const uint8_t *type_guid);

/**
 * Получает информацию о GPT-разделе по индексу.
 *
 * @param disk         Открытый диск.
 * @param index        Индекс раздела (0..header.num_partition_entries-1).
 * @param start_lba    Указатель для записи начального LBA (абсолютный на диске).
 * @param size_sectors Указатель для записи размера в секторах.
 * @return Код ошибки: ERR_OK – успешно, ERR_INVALID_VALUE – раздел не существует или неверный индекс,
 *         ERR_DISK_READ – ошибка чтения GPT-структур, ERR_GENERIC – ошибка выделения памяти.
 */
ErrorCode gpt_get_partition_info(Disk *disk, int index, uint64_t *start_lba, uint64_t *size_sectors);

// Работа с GUID
void gpt_guid_to_string(const uint8_t *guid, char *str);
int gpt_guid_from_string(const char *str, uint8_t *guid);
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