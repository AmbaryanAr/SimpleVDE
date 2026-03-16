#ifndef GPT_H
#define GPT_H

#include <stdint.h>
#include <stdbool.h>
#include "disk.h"
#include "error_codes.h"

#define GPT_SIGNATURE "EFI PART"
#define GPT_REVISION 0x00010000
#define GPT_HEADER_SIZE 92
#define GPT_PARTITION_ENTRY_SIZE 128
#define GPT_MIN_SIZE 34 // секторов

#pragma pack(push, 1)
/**
 * @brief Структура заголовка GPT (основной или резервный).
 */
typedef struct {
    char     signature[8];          /**< Сигнатура "EFI PART" */
    uint32_t revision;               /**< Версия (1.0 = 0x00010000) */
    uint32_t header_size;            /**< Размер заголовка в байтах (обычно 92) */
    uint32_t header_crc32;           /**< CRC32 всего заголовка (поле header_crc32 обнулено при вычислении) */
    uint32_t reserved;               /**< Зарезервировано, должно быть 0 */
    uint64_t current_lba;            /**< LBA текущего заголовка (для основного 1, для резервного – последний LBA) */
    uint64_t backup_lba;             /**< LBA резервного заголовка */
    uint64_t first_usable_lba;       /**< Первый LBA, доступный для разделов */
    uint64_t last_usable_lba;        /**< Последний LBA, доступный для разделов */
    uint8_t  disk_guid[16];          /**< GUID диска */
    uint64_t partition_entry_lba;    /**< LBA начала таблицы разделов */
    uint32_t num_partition_entries;  /**< Количество записей в таблице разделов (обычно 128) */
    uint32_t partition_entry_size;   /**< Размер одной записи в байтах (обычно 128) */
    uint32_t partitions_crc32;       /**< CRC32 всей таблицы разделов */
    uint8_t  padding[420];           /**< Выравнивание до 512 байт (заполнено нулями) */
} GptHeader;

/**
 * @brief Структура записи раздела GPT.
 */
typedef struct {
    uint8_t  type_guid[16];          /**< GUID типа раздела (например, для FAT32: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7) */
    uint8_t  partition_guid[16];     /**< Уникальный GUID данного раздела */
    uint64_t first_lba;              /**< Первый LBA раздела (включительно) */
    uint64_t last_lba;               /**< Последний LBA раздела (включительно) */
    uint64_t attributes;             /**< Атрибуты раздела (битовые флаги) */
    uint16_t partition_name[36];     /**< Имя раздела в UTF-16LE (максимум 36 символов) */
} GptPartitionEntry;
#pragma pack(pop)

/**
 * @brief Инициализирует GPT на диске (с защитным MBR).
 * @param disk Открытый диск.
 * @return ErrorCode.
 */
ErrorCode gpt_init(Disk *disk);

/**
 * @brief Создаёт новый раздел в GPT.
 * @param disk Открытый диск.
 * @param index Индекс раздела (0 .. num_partition_entries-1).
 * @param size_sectors Размер в секторах (0 = использовать всё свободное место).
 * @param type_guid GUID типа раздела (16 байт).
 * @return ErrorCode.
 */
ErrorCode gpt_create_partition(Disk *disk, int index, uint64_t size_sectors, const uint8_t *type_guid);

/**
 * @brief Удаляет раздел по индексу.
 * @param disk Открытый диск.
 * @param index Индекс раздела.
 * @return ErrorCode.
 */
ErrorCode gpt_delete_partition(Disk *disk, int index);

/**
 * @brief Изменяет GUID типа раздела.
 * @param disk Открытый диск.
 * @param index Индекс раздела.
 * @param type_guid Новый GUID типа.
 * @return ErrorCode.
 */
ErrorCode gpt_set_partition_type(Disk *disk, int index, const uint8_t *type_guid);

/**
 * @brief Получает информацию о разделе.
 * @param disk Открытый диск.
 * @param index Индекс раздела.
 * @param start_lba Указатель для записи начального LBA.
 * @param size_sectors Указатель для записи размера в секторах.
 * @return ErrorCode (ERR_NOT_FOUND, если раздел не существует).
 */
ErrorCode gpt_get_partition_info(Disk *disk, int index, uint64_t *start_lba, uint64_t *size_sectors);

/**
 * @brief Выводит информацию о GPT на экран.
 * @param disk Открытый диск.
 */
void gpt_print_info(Disk *disk);

/**
 * @brief Преобразует имя типа (например, "linux") в GUID.
 * @param name Имя типа.
 * @param guid Буфер для записи GUID (16 байт).
 * @return true, если имя найдено, иначе false.
 */
bool gpt_type_from_name(const char *name, uint8_t *guid);

/**
 * @brief Преобразует GUID в строку формата XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX.
 * @param guid Входной GUID (16 байт, little-endian).
 * @param str Буфер для строки (минимум 37 байт).
 */
void gpt_guid_to_string(const uint8_t *guid, char *str);

/**
 * @brief Преобразует строку GUID в 16-байтовый массив (little-endian).
 * @param str Входная строка.
 * @param guid Буфер для GUID.
 * @return 0 при успехе, -1 при ошибке.
 */
int gpt_guid_from_string(const char *str, uint8_t *guid);

#endif // GPT_H