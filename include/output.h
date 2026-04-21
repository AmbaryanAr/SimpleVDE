#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdio.h>

// Обычный вывод (stdout)
void svde_out(const char *fmt, ...);

// Вывод ошибок (stderr)
void svde_err(const char *fmt, ...);

#endif