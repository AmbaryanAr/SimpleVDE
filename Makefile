# Makefile для SimpleVDE (MinGW64)
# Использование: mingw32-make [all|clean|debug]

SHELL = cmd
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude -g -D_POSIX_C_SOURCE=200809L
LDFLAGS =
BUILDDIR = build
TARGET = $(BUILDDIR)/simplevde
SRCDIR = src
OBJDIR = obj
INCDIR = include

# Явно перечисляем все исходные файлы
SOURCES = src/main.c \
		src/main_commands.c \
		src/disk/disk.c \
		src/disk/disk_commands.c \
		src/mbr/mbr_commands.c \
		src/gpt/gpt_commands.c \
		src/fs/fat32/fat32_commands.c \
		src/help/help.c

# Преобразование в объектные файлы
OBJECTS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SOURCES))

# Список поддиректорий для создания в obj/
OBJ_SUBDIRS = $(sort $(dir $(OBJECTS)))

.PHONY: all clean debug

all: $(TARGET)

# Создание поддиректорий
$(OBJ_SUBDIRS):
	if not exist "$@" mkdir "$@"

# Создание папки build
$(BUILDDIR):
	if not exist "$@" mkdir "$@"

# Компиляция .c в .o
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJ_SUBDIRS)
	$(CC) $(CFLAGS) -c $< -o $@

# Линковка
$(TARGET): $(OBJECTS) | $(BUILDDIR)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	if exist $(OBJDIR) rmdir /s /q $(OBJDIR)
	if exist $(BUILDDIR) rmdir /s /q $(BUILDDIR)

debug:
	@echo SOURCES: $(SOURCES)
	@echo OBJECTS: $(OBJECTS)
	@echo OBJ_SUBDIRS: $(OBJ_SUBDIRS)