CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Iinclude
TARGET = simplevde.exe
SRCDIR = src
OBJDIR = obj

# Все исходные файлы
SOURCES = $(SRCDIR)/main.c \
          $(SRCDIR)/utils.c \
          $(SRCDIR)/error_codes.c \
          $(SRCDIR)/partition/partition.c \
          $(wildcard $(SRCDIR)/cmd/*.c) \
          $(wildcard $(SRCDIR)/disk/*.c) \
          $(wildcard $(SRCDIR)/fs/fat32/*.c) \
          $(wildcard $(SRCDIR)/help/*.c) \
          $(wildcard $(SRCDIR)/partition/gpt/*.c) \
          $(wildcard $(SRCDIR)/partition/mbr/*.c) \
          $(wildcard $(SRCDIR)/shell/*.c)

# Преобразование в объектные файлы
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $^ -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET)
	rm -rf $(OBJDIR)

.PHONY: all clean