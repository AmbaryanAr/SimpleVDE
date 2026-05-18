#ifndef FAT32_H
#define FAT32_H

#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#include "disk.h"
#include "utils.h"
#include "error_codes.h"

// ==================== Атрибуты записей каталога ====================
#define FAT32_ATTR_READ_ONLY    0x01  // файл только для чтения
#define FAT32_ATTR_HIDDEN       0x02  // скрытый файл
#define FAT32_ATTR_SYSTEM       0x04  // системный файл
#define FAT32_ATTR_VOLUME_ID    0x08  // метка тома
#define FAT32_ATTR_DIRECTORY    0x10  // каталог
#define FAT32_ATTR_ARCHIVE      0x20  // архивный (требует резервного копирования)
#define FAT32_ATTR_LFN          0x0F  // маска LFN-записи (read-only | hidden | system | volume)

// ==================== Константы для FSInfo-сектора ====================
#define FAT32_FSINFO_LEAD_SIG         0x41615252  // сигнатура начала FSInfo
#define FAT32_FSINFO_STRUCT_SIG       0x61417272  // сигнатура структуры FSInfo
#define FAT32_FSINFO_TRAIL_SIG        0xAA550000  // сигнатура конца FSInfo

#define FAT32_FSINFO_LEAD_OFFSET      0    // смещение lead-сигнатуры
#define FAT32_FSINFO_STRUCT_OFFSET    484  // смещение struct-сигнатуры
#define FAT32_FSINFO_FREE_OFFSET      488  // смещение счётчика свободных кластеров
#define FAT32_FSINFO_NEXT_FREE_OFFSET 492  // смещение подсказки для поиска свободного
#define FAT32_FSINFO_TRAIL_OFFSET     508  // смещение trail-сигнатуры

// ==================== Специальные значения кластеров в FAT ====================
#define FAT32_CLUSTER_FREE       0x00000000  // кластер свободен
#define FAT32_CLUSTER_RESERVED   0x00000001  // зарезервированный кластер
#define FAT32_CLUSTER_BAD        0x0FFFFFF7  // сбойный кластер
#define FAT32_CLUSTER_LAST_MIN   0x0FFFFFF8  // минимальное значение конца цепочки
#define FAT32_CLUSTER_LAST_MAX   0x0FFFFFFF  // максимальное значение конца цепочки
#define FAT32_CLUSTER_EOC        0x0FFFFFF8  // конец цепочки кластеров (End Of Chain)

// ==================== Специальные байты в записях каталога ====================
#define FAT32_DIRENT_FREE        0x00  // запись свободна (конец каталога)
#define FAT32_DIRENT_DELETED     0xE5  // запись удалена
#define FAT32_DIRENT_LFN_LAST    0x40  // флаг последней LFN-записи в наборе

// ==================== Размеры и ограничения ====================
#define FAT32_DIRENT_SIZE        32    // размер одной записи каталога в байтах
#define FAT32_SFN_LENGTH         11    // длина короткого имени (8+3 без точки)
#define FAT32_MAX_LFN_ENTRIES    20    // максимальное количество LFN-записей

// ==================== Маски для проверки типа записи ====================
#define FAT32_ATTR_FILE_MASK     (FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | FAT32_ATTR_SYSTEM | FAT32_ATTR_ARCHIVE)
#define FAT32_ATTR_DIR_MASK      (FAT32_ATTR_DIRECTORY)

#pragma pack(push, 1)

// BPB (BIOS Parameter Block) — загрузочный сектор FAT32
typedef struct {
    uint8_t  jump_boot[3];         // код перехода на загрузчик (EB 58 90)
    char     oem_name[8];          // имя OEM (например, "MSWIN4.1")
    uint16_t bytes_per_sector;     // байт на сектор (обычно 512)
    uint8_t  sectors_per_cluster;  // секторов на кластер (степень двойки)
    uint16_t reserved_sectors;     // количество зарезервированных секторов
    uint8_t  num_fats;             // количество FAT-таблиц (обычно 2)
    uint16_t root_entries;         // записей в корневом каталоге (0 для FAT32)
    uint16_t total_sectors_16;     // общее количество секторов (0 для FAT32)
    uint8_t  media_descriptor;     // тип носителя (0xF8 — жёсткий диск)
    uint16_t fat_size_16;          // размер FAT в секторах (0 для FAT32)
    uint16_t sectors_per_track;    // секторов на дорожку
    uint16_t num_heads;            // количество головок
    uint32_t hidden_sectors;       // скрытых секторов перед разделом
    uint32_t total_sectors_32;     // общее количество секторов (для FAT32)
    uint32_t fat_size_32;          // размер одной FAT в секторах
    uint16_t ext_flags;            // флаги (зеркалирование FAT)
    uint16_t fs_version;           // версия ФС (0)
    uint32_t root_cluster;         // номер первого кластера корневого каталога
    uint16_t fs_info_sector;       // номер сектора FSInfo (обычно 1)
    uint16_t backup_boot_sector;   // номер резервного загрузочного сектора (обычно 6)
    uint8_t  reserved[12];         // зарезервировано
    uint8_t  drive_number;         // номер диска (0x80 — жёсткий)
    uint8_t  reserved1;            // зарезервировано
    uint8_t  boot_signature;       // сигнатура расширенного BPB (0x29)
    uint32_t volume_id;            // серийный номер тома
    char     volume_label[11];     // метка тома (11 символов, без '\0')
    char     fs_type[8];           // строка типа ФС ("FAT32   ")
} Fat32BPB;

// Короткая запись каталога (SFN, 32 байта)
typedef struct {
    uint8_t  name[11];             // имя в формате 8.3 (без точки, заполнено пробелами)
    uint8_t  attr;                 // атрибуты (комбинация FAT32_ATTR_*)
    uint8_t  nt_reserved;          // зарезервировано для NT
    uint8_t  create_time_tenth;    // десятые доли секунды времени создания
    uint16_t create_time;          // время создания (часы/минуты/секунды)
    uint16_t create_date;          // дата создания
    uint16_t last_access_date;     // дата последнего доступа
    uint16_t first_cluster_hi;     // старшие 16 бит первого кластера
    uint16_t write_time;           // время последней записи
    uint16_t write_date;           // дата последней записи
    uint16_t first_cluster_lo;     // младшие 16 бит первого кластера
    uint32_t file_size;            // размер файла в байтах
} Fat32ShortEntry;

// Длинная запись каталога (LFN, 32 байта)
typedef struct {
    uint8_t  order;                // номер записи (1..20), последняя имеет бит 0x40
    uint16_t name1[5];             // символы 1–5 в UTF-16LE
    uint8_t  attr;                 // атрибут (всегда FAT32_ATTR_LFN)
    uint8_t  type;                 // тип (0)
    uint8_t  checksum;             // контрольная сумма короткого имени
    uint16_t name2[6];             // символы 6–11 в UTF-16LE
    uint16_t first_cluster;        // всегда 0
    uint16_t name3[2];             // символы 12–13 в UTF-16LE
} Fat32LongEntry;

#pragma pack(pop)

// Рассчитанные параметры смонтированного тома FAT32
typedef struct {
    uint64_t first_data_lba;       // LBA начала области данных
    uint64_t fat1_lba;             // LBA первой FAT
    uint64_t fat2_lba;             // LBA второй FAT
    uint32_t bytes_per_sector;     // байт на сектор
    uint8_t  sectors_per_cluster;  // секторов на кластер
    uint32_t reserved_sectors;     // количество зарезервированных секторов
    uint8_t  num_fats;             // количество FAT-таблиц
    uint32_t fat_size_sectors;     // размер одной FAT в секторах
    uint32_t root_cluster;         // номер кластера корневого каталога
    uint32_t total_clusters;       // общее количество кластеров данных
    uint32_t free_clusters;        // количество свободных кластеров (из FSInfo)
    uint32_t next_free_cluster;    // подсказка для поиска свободного (из FSInfo)
} Fat32Info;

// Представление одного кластера данных каталога в памяти
typedef struct {
    uint32_t    cluster_num;       // номер кластера
    uint32_t    offset_bytes;      // смещение в байтах от начала каталога
    uint8_t*    data;              // данные кластера
} Fat32DirCluster;

// Представление открытого каталога в памяти (цепочка кластеров)
typedef struct {
    Fat32Info*         volume;     // указатель на параметры тома
    Fat32DirCluster*   clusters;   // массив кластеров каталога
    uint32_t           cluster_count; // количество кластеров в цепочке
} Fat32Directory;

// Подготовленная информация о файле/каталоге для записи в каталог
typedef struct {
    const char*      name;          // исходное имя (UTF-8, только ASCII)
    uint8_t          sfn[FAT32_SFN_LENGTH];   // сгенерированное короткое имя
    uint16_t*        utf16_name;    // имя в UTF-16LE
    size_t           utf16_len;     // длина имени в символах UTF-16
    Fat32LongEntry*  lfn_entries;   // массив LFN-записей
    uint8_t          lfn_count;     // количество LFN-записей
    uint8_t          total_entries; // всего записей в каталоге (lfn_count + 1 SFN)
    uint8_t          attr;          // атрибуты файла/каталога
    uint16_t         crt_time;      // время создания
    uint16_t         crt_date;      // дата создания
    uint16_t         wrt_time;      // время последней записи
    uint16_t         wrt_date;      // дата последней записи
    uint16_t         lst_acc_date;  // дата последнего доступа
    uint8_t          crt_time_tenth;// десятые доли секунды времени создания
    uint32_t         first_cluster; // номер первого кластера данных
    uint32_t         file_size;     // размер файла в байтах
} fat32_entry_info_t;

// ==================== Основные функции тома ====================

// Форматирует раздел как FAT32 с заданными параметрами
ErrorCode fat32_format(Disk *disk, uint64_t start_lba, uint64_t total_sectors,
                       uint8_t drive_number, uint32_t volume_id, const char *volume_label);

// Считывает BPB и заполняет структуру Fat32Info (параметры тома)
ErrorCode fat32_get_info(Disk *disk, uint64_t start_lba, Fat32Info *info);

// Находит и выделяет один свободный кластер, помечая его как конец цепочки
uint32_t fat32_alloc_cluster(Disk *disk, const Fat32Info *info);

// Освобождает цепочку кластеров, начиная с first_cluster
ErrorCode fat32_free_cluster_chain(Disk *disk, const Fat32Info *info, uint32_t first_cluster);

// Читает содержимое одного кластера в буфер (буфер должен быть размера кластера)
ErrorCode fat32_read_cluster(Disk *disk, const Fat32Info *info, uint32_t cluster, uint8_t *buffer);

// Записывает данные в один кластер
ErrorCode fat32_write_cluster(Disk *disk, const Fat32Info *info, uint32_t cluster, const uint8_t *buffer);

// Возвращает следующий кластер в цепочке для заданного кластера
ErrorCode fat32_get_next_cluster(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t *next);

// Устанавливает значение записи FAT для кластера (во всех копиях FAT)
ErrorCode fat32_set_fat_entry(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t value);

// Обновляет счётчики свободных кластеров и подсказку next_free в FSInfo
ErrorCode fat32_update_fsinfo(Disk *disk, const Fat32Info *info);

// ==================== Функции для работы с каталогами ====================

// Открывает каталог: читает всю цепочку кластеров в память
ErrorCode fat32_dir_open(Disk *disk, const Fat32Info *info, uint32_t start_cluster, Fat32Directory *dir);

// Освобождает память, занятую открытым каталогом
void fat32_dir_close(Fat32Directory *dir);

// Ищет needed свободных записей подряд в открытом каталоге.
// Возвращает индексы кластера и смещения первой записи.
bool fat32_find_free_entries(const Fat32Directory *dir, uint8_t needed,
                             uint32_t *out_cluster_idx, uint32_t *out_entry_off);

// Добавляет новый (пустой) кластер в конец цепочки каталога
ErrorCode fat32_append_cluster_to_dir(Disk *disk, const Fat32Info *info, Fat32Directory *dir);

// Записывает подготовленную запись (LFN + SFN) в открытый каталог
ErrorCode fat32_write_entries_to_dir(Disk *disk, const Fat32Info *info,
                                     Fat32Directory *dir, uint32_t cluster_idx,
                                     uint32_t entry_off, const fat32_entry_info_t *entry);

// Инициализирует новый каталог: создаёт записи "." и ".."
ErrorCode fat32_init_new_dir(Disk *disk, const Fat32Info *info, uint32_t cluster, uint32_t parent_cluster);

// Создаёт каталог по указанному абсолютному пути
ErrorCode fat32_create_dir(Disk *disk, uint64_t start_lba, const char *path);

// ==================== Функции чтения/поиска каталогов ====================

// Читает содержимое каталога в линейный буфер (вызывающий должен освободить *buffer)
ErrorCode fat32_read_dir(Disk *disk, const Fat32Info *info, uint32_t start_cluster,
                         uint8_t **buffer, uint32_t *entries_count);

// Ищет каталог по абсолютному пути, возвращает номер его первого кластера
ErrorCode fat32_find_dir(Disk *disk, const Fat32Info *info, const char *path, uint32_t *dir_cluster);

// Выводит содержимое каталога через svde_out
ErrorCode fat32_list_dir(Disk *disk, uint64_t start_lba, const char *path);

// ==================== Функции подготовки записей ====================

// Подготавливает запись для нового файла/каталога: генерирует SFN, LFN, заполняет info
ErrorCode fat32_prepare_entry(const char *utf8_name, uint8_t attr,
                                   const uint8_t *dir_buffer, uint32_t entry_count,
                                   fat32_entry_info_t *info);

// Освобождает память, выделенную в fat32_entry_info_t
void fat32_free_entry_info(fat32_entry_info_t *info);

// Заполняет поля времени в fat32_entry_info_t текущим временем
void fat32_set_current_time(fat32_entry_info_t *info);

// ==================== Функции для работы с файлами ====================

// Удаляет файл по абсолютному пути (освобождает кластеры, помечает записи как удалённые)
ErrorCode fat32_delete_file(Disk *disk, uint64_t start_lba, const char *path);

// Копирует файл с хоста в образ по указанному пути назначения
ErrorCode fat32_copy_file(Disk *disk, uint64_t start_lba, const char *host_path, const char *dest_path);

// Удаляет пустой каталог по абсолютному пути
ErrorCode fat32_remove_dir(Disk *disk, uint64_t start_lba, const char *path);

// ==================== Функции для работы с реестром ====================

// Инициализирует резервный кластер в BPB (выделяет кластер, записывает номер в BPB)
ErrorCode fat32_reserve_init(Disk *disk, uint64_t start_lba);

// Выводит список записей реестра через svde_out
ErrorCode fat32_reserve_list(Disk *disk, uint64_t start_lba);

// Добавляет запись о файле в реестр
ErrorCode fat32_reserve_add(Disk *disk, uint64_t start_lba, const char *path);

// Удаляет запись из реестра по имени
ErrorCode fat32_reserve_remove(Disk *disk, uint64_t start_lba, const char *name);

// Очищает все записи реестра
ErrorCode fat32_reserve_clear(Disk *disk, uint64_t start_lba);

// Выводит шестнадцатеричный дамп резервного кластера
ErrorCode fat32_reserve_dump(Disk *disk, uint64_t start_lba);

// Выводит информацию о резервном кластере (размер, занято/свободно)
ErrorCode fat32_reserve_info(Disk *disk, uint64_t start_lba);

// Устанавливает файл из реестра как загрузочный (сохраняет кластер в BPB)
ErrorCode fat32_reserve_boot_set(Disk *disk, uint64_t start_lba, const char *name);

// Показывает текущий загрузочный файл из реестра
ErrorCode fat32_reserve_boot_show(Disk *disk, uint64_t start_lba);

// Сбрасывает указатель загрузочного файла в BPB
ErrorCode fat32_reserve_boot_clear(Disk *disk, uint64_t start_lba);

#endif