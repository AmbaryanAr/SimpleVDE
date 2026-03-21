#include "help.h"
#include "version.h"
#include <stdio.h>
#include <string.h>

void print_short_help(void) {
    printf("%s version %s\n", PROGRAM_NAME, PROGRAM_VERSION);
    printf("Use '%s --help' for detailed help.\n", "diskman");
}

void print_version(void) {
    printf("%s version %s\n", PROGRAM_NAME, PROGRAM_VERSION);
    printf("Author: %s\n", PROGRAM_AUTHOR);
}

void print_global_help(void) {
    printf("%s version %s - Utility for disk and partition management\n\n", PROGRAM_NAME, PROGRAM_VERSION);

    printf("Global options:\n");
    printf("  --help, -h                 Show this help\n");
    printf("  --version, -v              Show version\n");
    printf("  --<category> help          Show help for a specific category\n\n");

    printf("Categories:\n");
    printf("  disk      - Disk operations (create, info, read, read-s, mbr-write, bpb-write)\n");
    printf("  part      - Partition operations (create, delete, set-type, set-active, set-inactive)\n");
    printf("  format    - Format a partition (only fat32)\n");
    printf("  fs        - Filesystem operations (ls, copy, mkdir, rm, rmdir, tree, reserve-*)\n");
    printf("  shell     - Interactive shell\n");
    printf("  mbr       - Write boot code to MBR\n");
    printf("  bpb       - Write boot code to BPB of a FAT32 partition\n\n");

    printf("For detailed help on a category, use: --<category> help\n");
    printf("Example: --disk help\n");
}

void print_category_help(const char *category) {
    if (strcmp(category, "disk") == 0) {
        printf("Disk operations:\n");
        printf("  --disk-create -file=<path> -table=mbr|gpt -size=<size>   Create a new disk image\n");
        printf("  --disk-info -file=<path>                                 Show disk information\n");
        printf("  --disk-read -file=<path> -offset=<bytes> -count=<bytes>  Read bytes and hexdump\n");
        printf("  --disk-read-s -file=<path> -offset=<sectors> -count=<sectors>  Read sectors\n");
        printf("  --mbr-write -file=<path> -src=<file>                     Write boot code to MBR (first 446 bytes)\n");
        printf("  --bpb-write -file=<path> -part=<num> -src=<file>         Write boot code to partition's BPB (offset 90)\n");
        printf("\nNote: <size> supports K/M/G suffixes (e.g., 64M).\n");
    } else if (strcmp(category, "part") == 0) {
        printf("Partition operations:\n");
        printf("  --part-create -file=<path> -part=<num> -size=<size> -type=<type>   Create a partition\n");
        printf("  --part-delete -file=<path> -part=<num>                              Delete a partition\n");
        printf("  --part-set-type -file=<path> -part=<num> -type=<type>               Change partition type\n");
        printf("  --part-set-active -file=<path> -part=<num>                          Set active flag (MBR only)\n");
        printf("  --part-set-inactive -file=<path> -part=<num>                        Unset active flag\n");
        printf("\n<type> can be a name (e.g., linux, fat32) or hex code for MBR, or GUID for GPT.\n");
    } else if (strcmp(category, "format") == 0) {
        printf("Format operations:\n");
        printf("  --format -file=<path> -part=<num> -fs=fat32   Format a partition as FAT32\n");
    } else if (strcmp(category, "fs") == 0) {
        printf("Filesystem operations:\n");
        printf("  --fs-ls -file=<path> -part=<num> [-path=<dir>]                List directory contents\n");
        printf("  --fs-copy -file=<path> -part=<num> -src=<host-file> -dest=<fs-path>  Copy file from host to image\n");
        printf("  --fs-mkdir -file=<path> -part=<num> -path=<dir>               Create directory\n");
        printf("  --fs-rm -file=<path> -part=<num> -path=<file>                 Delete file\n");
        printf("  --fs-rmdir -file=<path> -part=<num> -path=<dir>               Remove empty directory\n");
        printf("  --fs-tree -file=<path> -part=<num> [-path=<dir>]              Display directory tree\n");
        printf("\nReserve operations (filesystem registry):\n");
        printf("  --fs-reserve-init -file=<path> -part=<num>                    Initialize reserve cluster\n");
        printf("  --fs-reserve-ls -file=<path> -part=<num>                      List reserve entries\n");
        printf("  --fs-reserve-add -file=<path> -part=<num> -path=<file-path>   Add file entry to reserve\n");
        printf("  --fs-reserve-rm -file=<path> -part=<num> -name=<entry-name>   Remove entry by name\n");
        printf("  --fs-reserve-clear -file=<path> -part=<num>                   Clear all entries\n");
        printf("  --fs-reserve-dump -file=<path> -part=<num>                    Hex dump of reserve cluster\n");
        printf("  --fs-reserve-boot-set -file=<path> -part=<num> -name=<entry-name>\n");
        printf("  --fs-reserve-boot-show -file=<path> -part=<num>\n");
        printf("  --fs-reserve-boot-clear -file=<path> -part=<num>\n");
        printf("  --fs-reserve-info -file=<path> -part=<num>                    Show reserve info\n");
    } else if (strcmp(category, "shell") == 0) {
        printf("Interactive shell:\n");
        printf("  --shell -file=<path> -part=<num>   Start interactive FAT32 shell\n");
        printf("  Inside shell, type 'help' for available commands.\n");
    } else if (strcmp(category, "mbr") == 0) {
        printf("MBR boot code writing:\n");
        printf("  --mbr-write -file=<path> -src=<file>   Write boot code to MBR (first 446 bytes)\n");
    } else if (strcmp(category, "bpb") == 0) {
        printf("BPB boot code writing:\n");
        printf("  --bpb-write -file=<path> -part=<num> -src=<file>   Write boot code to partition's BPB (offset 90)\n");
    } else {
        printf("Unknown category '%s'. Use --help for list of categories.\n", category);
    }
}