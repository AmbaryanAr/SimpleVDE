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
typedef struct {
    char     signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t partition_entry_size;
    uint32_t partitions_crc32;
    uint8_t  padding[420];
} GptHeader;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  partition_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t partition_name[36];
} GptPartitionEntry;
#pragma pack(pop)

/** Инициализирует GPT на диске (с защитным MBR). */
ErrorCode gpt_init(Disk *disk);

/** Создаёт новый раздел в GPT. */
ErrorCode gpt_create_partition(Disk *disk, int index, uint64_t size_sectors, const uint8_t *type_guid);

/** Удаляет раздел по индексу. */
ErrorCode gpt_delete_partition(Disk *disk, int index);

/** Изменяет GUID типа раздела. */
ErrorCode gpt_set_partition_type(Disk *disk, int index, const uint8_t *type_guid);

/** Получает информацию о разделе. */
ErrorCode gpt_get_partition_info(Disk *disk, int index, uint64_t *start_lba, uint64_t *size_sectors);

/** Выводит информацию о GPT на экран. */
void gpt_print_info(Disk *disk);

/** Преобразует имя типа в GUID. */
bool gpt_type_from_name(const char *name, uint8_t *guid);

/** Преобразует GUID в строку. */
void gpt_guid_to_string(const uint8_t *guid, char *str);

/** Преобразует строку GUID в массив. */
int gpt_guid_from_string(const char *str, uint8_t *guid);

#endif