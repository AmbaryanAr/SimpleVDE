#ifndef HELP_H
#define HELP_H

#include "version.h"

// Выводит полную справку по всем категориям команд
void print_global_help(void);

// Выводит справку по конкретной категории (disk, part, format, fs, shell, mbr, bpb)
void print_category_help(const char *category);

// Выводит краткую справку (название, версия, приглашение к --help)
void print_short_help(void);

// Выводит версию программы и автора
void print_version(void);

#endif