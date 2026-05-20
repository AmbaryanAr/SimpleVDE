CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Iinclude
LDFLAGS =

# Автоопределение расширения исполняемого файла
ifeq ($(OS),Windows_NT)
    TARGET = simplevde.exe
else
    TARGET = simplevde
    CFLAGS += -D_FILE_OFFSET_BITS=64 -D_DEFAULT_SOURCE
endif

SRCDIR = src
OBJDIR = obj

SOURCES = $(SRCDIR)/main.c \
          $(SRCDIR)/utils.c \
          $(SRCDIR)/output.c \
          $(SRCDIR)/error_codes.c \
          $(SRCDIR)/partition/partition.c \
          $(wildcard $(SRCDIR)/cmd/*.c) \
          $(wildcard $(SRCDIR)/disk/*.c) \
          $(wildcard $(SRCDIR)/fs/fat32/*.c) \
          $(wildcard $(SRCDIR)/help/*.c) \
          $(wildcard $(SRCDIR)/partition/gpt/*.c) \
          $(wildcard $(SRCDIR)/partition/mbr/*.c) \
          $(wildcard $(SRCDIR)/shell/*.c)

OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET)
	rm -rf $(OBJDIR)

.PHONY: all clean