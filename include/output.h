#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdio.h>

// Выводит форматированное сообщение в stdout с префиксом "svde: > "
void svde_out(const char *fmt, ...);

// Выводит форматированное сообщение об ошибке в stderr с префиксом "svde: > "
void svde_err(const char *fmt, ...);

// Выводит/обновляет строку прогресса в консоли (без префикса, с \r).
// percent: 0-100. При 100 завершает строку переводом строки.
void svde_progress(int percent);

#endif