#include "disk.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h> 

// *** Вспомогательные функции ***
// Смещение указателя 
static bool _seek(FILE *file, uint64_t offset) {
    return fseek64(file, offset, SEEK_SET) == 0;
}

static size_t _read(FILE *file, void *value, uint32_t size_value, uint64_t offset) {
    if (!_seek(file, offset)) return 0;
    return fread(value, 1, size_value, file);
}

static size_t _write(FILE *file, const void *value, uint32_t size_value, uint64_t offset) {
    if (!_seek(file, offset)) return 0;
    return fwrite(value, 1, size_value, file);
}
// ***

// Открыть диск
ErrorCode disk_open(const char *path, Disk *disk) {
    if (!path || !disk)
		return ERR_DISK_OPEN;
    FILE *f = fopen(path, "rb+");
    if (!f)
		return ERR_DISK_OPEN;

    // Определяем размер файла
    if (fseek64(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ERR_DISK_SEEK;
    }
    int64_t sz = ftell64(f);
    if (sz == -1) {
        fclose(f);
        return ERR_DISK_SEEK;
    }
    rewind(f); // или fseek64(f, 0, SEEK_SET)

    strncpy(disk->path, path, MAX_PATH - 1);
    disk->path[MAX_PATH - 1] = '\0';
    disk->size = (uint64_t)sz;
    disk->file = f;
    disk->is_open = 1;
    return ERR_OK;
}

// Закрыть диск
void disk_close(Disk *disk) {
    if (disk && disk->is_open && disk->file) {
        fclose(disk->file);
        disk->file = NULL;
        disk->is_open = 0;
    }
}

// Создать новый диск
ErrorCode disk_create(const char *path, uint64_t size_mb, Disk *disk) {
    if (!path || !disk)
        return ERR_DISK_CREATE;

    // Проверка переполнения при size_mb * 1024 * 1024
    if (size_mb > UINT64_MAX / (1024ULL * 1024ULL))
        return ERR_INVALID_VALUE;
    uint64_t size_bytes = size_mb * 1024ULL * 1024ULL;

    // Открываем файл в режиме чтения/записи, создавая новый
    FILE *f = fopen(path, "wb+");
    if (!f)
        return ERR_DISK_OPEN;

    char *buffer = (char*)malloc(1024 * 1024); // 1 MB
    if (!buffer) {
        fclose(f);
        return ERR_DISK_CREATE;
    }
    memset(buffer, 0, 1024 * 1024);

    for (uint64_t i = 0; i < size_bytes; i += 1024 * 1024) {
        size_t to_write = (size_bytes - i) < (1024 * 1024) ? (size_t)(size_bytes - i) : 1024 * 1024;
        size_t written = fwrite(buffer, 1, to_write, f);
        if (written != to_write) {
            free(buffer);
            fclose(f);
            return ERR_DISK_WRITE;
        }
        if (i % (10 * 1024 * 1024) == 0) {
            printf("Progress: %" PRIu64 " / %" PRIu64 " MB\r", i / (1024 * 1024), size_mb);
            fflush(stdout);
        }
    }
    free(buffer);

    // Перематываем на начало для последующего чтения/записи
    rewind(f);

    // Заполняем структуру диска
    strncpy(disk->path, path, MAX_PATH - 1);
    disk->path[MAX_PATH - 1] = '\0';
    disk->size = size_bytes;
    disk->file = f;
    disk->is_open = 1;

    return ERR_OK;
}

// Прочитать с диска
ErrorCode disk_read(Disk *disk, void *value, uint32_t size_value, uint64_t offset) {
    if (!disk || !disk->is_open || !disk->file)
		return ERR_DISK_OPEN;
	else
    if(_read(disk->file, value, size_value, offset) != size_value)
		return ERR_DISK_READ;
	else
		return ERR_OK;
}

// Записать на диск
ErrorCode disk_write(Disk *disk, const void *value, uint32_t size_value, uint64_t offset) {
    if (!disk || !disk->is_open || !disk->file)
		return ERR_DISK_OPEN;
	else
    if(_write(disk->file, value, size_value, offset) != size_value)
		return ERR_DISK_WRITE;
	else
		return ERR_OK;
}