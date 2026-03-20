#ifndef ERROR_CODES_H
#define ERROR_CODES_H

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
    ERR_UNKNOWN               // Неизвестная ошибка
} ErrorCode;

/** Возвращает строковое описание кода ошибки. */
const char* error_code_to_string(ErrorCode code);

#endif