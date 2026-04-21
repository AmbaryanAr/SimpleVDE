#include "output.h"
#include <stdarg.h>

void svde_out(const char *fmt, ...) {
    va_list args;
    printf("svde: > ");
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void svde_err(const char *fmt, ...) {
    va_list args;
    fprintf(stderr, "svde: > ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}