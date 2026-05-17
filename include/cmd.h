#ifndef CMD_H
#define CMD_H

// Структура для хранения распарсенных аргументов командной строки.
// Заполняется в parse_arguments() на основе ключей вида --category-command -key=value.
typedef struct {
    const char *category;   // категория: "--disk", "--part", "--format", "--fs", "--shell"
    const char *command;    // подкоманда: "create", "info", "read", "delete", "set-type",
                            //  "set-active", "set-inactive", "ls", "copy", "mkdir", "rm", "rmdir"
    const char *name;       // значение -name=
    const char *file;       // значение -file= (путь к образу диска)
    const char *part;       // значение -part= (номер раздела)
    const char *size;       // значение -size= (размер с суффиксом K/M/G)
    const char *type;       // значение -type= (тип раздела или файловой системы)
    const char *table;      // значение -table= (mbr или gpt)
    const char *offset;     // значение -offset= (смещение в байтах или секторах)
    const char *count;      // значение -count= (количество байт или секторов)
    const char *src;        // значение -src= (путь к исходному файлу)
    const char *dest;       // значение -dest= (путь назначения)
    const char *path;       // значение -path= (путь внутри ФС)
    const char *fs;         // значение -fs= (тип файловой системы)
} CMDArgs;

#endif