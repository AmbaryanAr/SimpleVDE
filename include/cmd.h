#ifndef CMD_H
#define CMD_H

/**
 * @brief Структура для хранения распарсенных аргументов командной строки.
 */
typedef struct {
    const char *category;   /**< категория: "--disk", "--part", "--format", "--fs", "--shell" */
    const char *command;    /**< подкоманда: "create", "info", "read", "delete", "set-type",
                                  "set-active", "set-inactive", "ls", "copy", "mkdir", "rm", "rmdir" */
    const char *name;       /**< --name= */
    const char *file;       /**< --file= */
    const char *part;       /**< --part= */
    const char *size;       /**< --size= */
    const char *type;       /**< --type= (тип раздела или файловой системы) */
    const char *table;      /**< --table= (mbr/gpt) */
    const char *offset;     /**< --offset= */
    const char *count;      /**< --count= */
    const char *src;        /**< --src= */
    const char *dest;       /**< --dest= */
    const char *path;       /**< --path= */
    const char *fs;         /**< --fs= (для команды --format) */
} CMDArgs;

#endif