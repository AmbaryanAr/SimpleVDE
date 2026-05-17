#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdio.h>

// Выводит форматированное сообщение в stdout с префиксом "svde: > "
void svde_out(const char *fmt, ...);

// Выводит форматированное сообщение об ошибке в stderr с префиксом "svde: > "
void svde_err(const char *fmt, ...);

#endif