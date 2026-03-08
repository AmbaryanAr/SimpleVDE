#pragma once

#include "disk.h"
#include "error_code.h"
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>

#define MBR_SIZE 512
#define PARTITION_ENTRY_SIZE 16
#define PARTITION_TABLE_OFFSET 446
#define PARTITION_BOOTABLE     0x80
#define PARTITION_NON_BOOTABLE 0x00
#define MBR_SIGNATURE 0xAA55

#pragma pack(push, 1)
typedef struct {
    uint8_t  boot_flag;
    uint8_t  start_head;
    uint8_t  start_sector;
    uint8_t  start_cylinder;
    uint8_t  partition_type;
    uint8_t  end_head;
    uint8_t  end_sector;
    uint8_t  end_cylinder;
    uint32_t lba_start;
    uint32_t sector_count;
} PartitionEntry;

typedef struct {
    uint8_t bootstrap[PARTITION_TABLE_OFFSET];
    PartitionEntry partitions[4];
    uint16_t signature;
} MBR;
#pragma pack(pop)

// Параметры для создания раздела
typedef struct {
    char disk_path[MAX_PATH];      // Путь к файлу диска
    int index;                     // Номер раздела (1-4)
    uint32_t size_sectors;         // Размер в секторах
    uint8_t type;                  // Тип раздела (по умолчанию 0x83 - Linux)
    bool type_specified;           // Был ли указан тип явно
} CreatePartitionParams;

/**
 * Преобразует имя файловой системы (например, "linux", "fat32") в hex-код MBR.
 * @param name Имя файловой системы (регистр не учитывается).
 * @return Код типа (0x00–0xFF) или 0xFF, если имя не найдено.
 */
uint8_t mbr_type_from_name(const char *name);

/**
 * Создаёт MBR (Master Boot Record) на открытом диске.
 * Записывает в первый сектор стандартную загрузочную запись и пустую таблицу разделов.
 * Диск должен быть открыт для записи.
 *
 * @param disk Указатель на открытый диск (структура Disk).
 * @return Код ошибки:
 *         - ERR_OK – MBR успешно создана;
 *         - ERR_DISK_OPEN – диск не открыт или передан нулевой указатель;
 *         - ERR_DISK_WRITE – ошибка записи в первый сектор.
 */
ErrorCode mbr_create(Disk *disk);

// ErrorCode cmd_mbr_create_partition( ... );
// ErrorCode cmd_mbr_delete_partition( ... );
// ErrorCode cmd_mbr_set_partition_type( ... );
// ErrorCode cmd_mbr_set_active_partition( ... );

/**
 * Выводит информацию о MBR-таблице разделов на открытом диске.
 * Читает MBR из первого сектора и отображает:
 * - количество первичных разделов,
 * - для каждого непустого раздела: тип, статус (активный/неактивный),
 *   начальный LBA и размер в секторах.
 *
 * @param disk Указатель на открытый диск (должен быть корректен и содержать MBR).
 */
void mbr_print_info(Disk *disk);

/**
 * Удаляет запись о разделе из MBR (Master Boot Record) на открытом диске.
 *
 * Функция читает первый сектор диска (MBR), зануляет запись раздела
 * с указанным индексом и записывает обновлённый сектор обратно на диск.
 * Остальные разделы и загрузочный код не изменяются.
 *
 * @param disk  Указатель на открытый диск (структура Disk).
 * @param index Индекс удаляемого раздела (0–3, где 0 соответствует первому разделу).
 * @return Код ошибки:
 *         - ERR_OK – раздел успешно удалён;
 *         - ERR_DISK_OPEN – диск не открыт или передан нулевой указатель;
 *         - ERR_INVALID_VALUE – индекс выходит за пределы 0..3;
 *         - ERR_DISK_READ – ошибка чтения MBR-сектора;
 *         - ERR_DISK_WRITE – ошибка записи обновлённого MBR-сектора.
 */
ErrorCode mbr_delete_partition(Disk *disk, int index);

/**
 * Устанавливает активный флаг (boot_flag) для указанного раздела MBR.
 * Активным может быть только один раздел, поэтому у всех остальных разделов
 * флаг сбрасывается (устанавливается 0x00).
 *
 * @param disk  Указатель на открытый диск.
 * @param index Индекс раздела (0–3).
 * @return Код ошибки:
 *         - ERR_OK – успешно;
 *         - ERR_DISK_OPEN – диск не открыт;
 * - ERR_INVALID_VALUE – индекс вне диапазона;
 *         - ERR_DISK_READ – ошибка чтения MBR;
 *         - ERR_DISK_WRITE – ошибка записи MBR.
 */
ErrorCode mbr_set_active(Disk *disk, int index);

/**
 * Устанавливает тип (идентификатор) для указанного раздела MBR.
 *
 * Функция читает первый сектор диска (MBR), изменяет байт типа раздела
 * (смещение +4 в записи) для заданного индекса и записывает обновлённый
 * сектор обратно на диск. Остальные данные MBR не изменяются.
 *
 * @param disk  Указатель на открытый диск (структура Disk).
 * @param index Индекс раздела (0–3, где 0 соответствует первому разделу).
 * @param type  Новый тип раздела (например, 0x83 для Linux, 0x07 для NTFS).
 * @return Код ошибки:
 *         - ERR_OK – тип раздела успешно изменён;
 *         - ERR_DISK_OPEN – диск не открыт или передан нулевой указатель;
 *         - ERR_INVALID_VALUE – индекс выходит за пределы 0..3;
 *         - ERR_DISK_READ – ошибка чтения MBR-сектора;
 *         - ERR_DISK_WRITE – ошибка записи обновлённого MBR-сектора.
 */
ErrorCode mbr_set_partition_type(Disk *disk, int index, uint8_t type);

/**
 * Получает информацию о MBR-разделе по индексу.
 *
 * @param disk         Открытый диск.
 * @param index        Индекс раздела (0-3).
 * @param start_lba    Указатель для записи начального LBA (абсолютный на диске).
 * @param size_sectors Указатель для записи размера в секторах.
 * @return Код ошибки: ERR_OK – успешно, ERR_INVALID_VALUE – раздел не существует или неверный индекс,
 *         ERR_DISK_READ – ошибка чтения MBR.
 */
ErrorCode mbr_get_partition_info(Disk *disk, int index, uint64_t *start_lba, uint64_t *size_sectors);

/**
 * Записывает загрузочный код в область bootstrap MBR (первые 446 байт).
 * Существующая таблица разделов и сигнатура не изменяются.
 *
 * @param disk      Указатель на открытый диск.
 * @param code      Указатель на буфер с кодом.
 * @param code_size Количество байт для записи (не более 446).
 * @return Код ошибки:
 *         - ERR_OK – код успешно записан;
 *         - ERR_DISK_OPEN – диск не открыт;
 *         - ERR_INVALID_VALUE – code_size > 446;
 *         - ERR_DISK_READ – ошибка чтения MBR;
 *         - ERR_DISK_WRITE – ошибка записи MBR.
 */
ErrorCode mbr_write_code(Disk *disk, const uint8_t *code, size_t code_size);

/**
 * Создаёт новый раздел в MBR (Master Boot Record) на открытом диске.
 *
 * @param disk         Указатель на открытый диск.
 * @param index        Индекс нового раздела (0–3).
 * @param size_sectors Размер раздела в секторах. Если 0, занять всё свободное место до конца диска.
 * @param type         Тип раздела (например, 0x83 для Linux).
 * @return Код ошибки:
 *         - ERR_OK – раздел успешно создан;
 *         - ERR_DISK_OPEN – диск не открыт;
 *         - ERR_INVALID_VALUE – неверный индекс, запись занята, недостаточно места или другой недопустимый параметр;
 *         - ERR_DISK_READ – ошибка чтения MBR;
 *         - ERR_DISK_WRITE – ошибка записи MBR.
 */
ErrorCode mbr_create_partition(Disk *disk, int index, uint32_t size_sectors, uint8_t type);