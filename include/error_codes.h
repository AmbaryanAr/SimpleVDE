#ifndef ERROR_CODES_H
#define ERROR_CODES_H

// Коды ошибок, возвращаемые всеми функциями утилиты
typedef enum {
    ERR_OK = 0,               // Успешное завершение
    ERR_MISSING_ARGUMENT,     // Отсутствует обязательный аргумент
    ERR_INVALID_ARGUMENT,     // Некорректное значение аргумента
    ERR_OUT_OF_MEMORY,        // Не удалось выделить память
    ERR_DISK_OPEN,            // Не удалось открыть файл диска
    ERR_DISK_CREATE,          // Ошибка создания файла диска
    ERR_DISK_READ,            // Ошибка чтения с диска
    ERR_DISK_WRITE,           // Ошибка записи на диск
    ERR_DISK_SEEK,            // Ошибка позиционирования в файле
    ERR_DISK_CLOSE,           // Ошибка закрытия файла
    ERR_NOT_FOUND,            // Запрошенный объект не найден
    ERR_ALREADY_EXISTS,       // Объект уже существует
    ERR_NOT_SUPPORTED,        // Операция не поддерживается
    ERR_INVALID_SIGNATURE,    // Неверная сигнатура (MBR/GPT/FAT)
    ERR_NO_FREE_SPACE,        // Нет свободного места
    ERR_INTERNAL,             // Внутренняя ошибка
    ERR_NOT_IMPLEMENTED,      // Функция не реализована
    ERR_DIR_NOT_EMPTY,        // Каталог не пуст
    ERR_UNKNOWN,              // Неизвестная ошибка
    // Коды ошибок FAT32
    ERR_FAT32_BAD_BPB,             // Повреждённый BPB-сектор
    ERR_FAT32_FSINFO_CORRUPT,      // Повреждённый FSInfo-сектор
    ERR_FAT32_VOLUME_NOT_MOUNTED,  // Том не смонтирован
    ERR_FAT32_NO_FREE_CLUSTER,     // Нет свободных кластеров
    ERR_FAT32_BAD_CLUSTER,         // Обнаружен сбойный кластер
    ERR_FAT32_FAT_CORRUPT,         // Повреждена FAT-таблица
    ERR_FAT32_DIR_NO_FREE_ENTRY,   // Нет свободной записи в каталоге
    ERR_FAT32_DIR_IS_NOT_DIRECTORY,// Запись не является каталогом
    ERR_FAT32_NAME_TOO_LONG,       // Имя файла слишком длинное
    ERR_FAT32_NAME_INVALID,        // Недопустимые символы в имени
    ERR_FAT32_UTF16_CONVERSION,    // Ошибка преобразования UTF-16
    ERR_FAT32_LFN_CHECKSUM,        // Несовпадение контрольной суммы LFN
    ERR_FAT32_TOO_MANY_LFN_ENTRIES,// Слишком много LFN-записей
    ERR_FAT32_SFN_SUFFIX_OVERFLOW, // Переполнение суффикса SFN (~1..~99)
    ERR_RESERVE_NOT_INIT,          // Резервный кластер не инициализирован
} ErrorCode;

// Возвращает строковое описание кода ошибки
const char* error_code_to_string(ErrorCode code);

#endif