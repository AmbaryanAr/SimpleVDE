#include "help.h"
#include "output.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

void print_short_help(void) {
    svde_out("%s version %s\n", PROGRAM_NAME, PROGRAM_VERSION);
    svde_out("Use '%s --help' for detailed help.\n", "diskman");
}

void print_version(void) {
    svde_out("%s version %s\n", PROGRAM_NAME, PROGRAM_VERSION);
    svde_out("Author: %s\n", PROGRAM_AUTHOR);
}

void print_global_help(void) {
    svde_out("%s version %s - Utility for disk and partition management\n\n", PROGRAM_NAME, PROGRAM_VERSION);

    svde_out("Global options:\n");
    svde_out("  --help, -h                 Show this help\n");
    svde_out("  --version, -v              Show version\n");
    svde_out("  --<category> help          Show help for a specific category\n\n");

    svde_out("Categories:\n");
    svde_out("  disk      - Disk operations (create, info, read, read-s, mbr-write, bpb-write)\n");
    svde_out("  part      - Partition operations (create, delete, set-type, set-active, set-inactive)\n");
    svde_out("  format    - Format a partition (only fat32)\n");
    svde_out("  fs        - Filesystem operations (ls, copy, mkdir, rm, rmdir, tree, reserve-*)\n");
    svde_out("  shell     - Interactive shell\n");
    svde_out("  mbr       - Write boot code to MBR\n");
    svde_out("  bpb       - Write boot code to BPB of a FAT32 partition\n\n");

    svde_out("For detailed help on a category, use: --<category> help\n");
    svde_out("Example: --disk help\n");
}

void print_category_help(const char *category) {
    if (strcmp(category, "disk") == 0) {
        svde_out("Disk operations:\n");
        svde_out("  --disk-create -file=<path> -table=mbr|gpt -size=<size>   Create a new disk image\n");
        svde_out("  --disk-info -file=<path>                                 Show disk information\n");
        svde_out("  --disk-read -file=<path> -offset=<bytes> -count=<bytes>  Read bytes and hexdump\n");
        svde_out("  --disk-read-s -file=<path> -offset=<sectors> -count=<sectors>  Read sectors\n");
        svde_out("  --mbr-write -file=<path> -src=<file>                     Write boot code to MBR (first 446 bytes)\n");
        svde_out("  --bpb-write -file=<path> -part=<num> -src=<file>         Write boot code to partition's BPB (offset 90)\n");
        svde_out("\nNote: <size> supports K/M/G suffixes (e.g., 64M).\n");
    } else if (strcmp(category, "part") == 0) {
        svde_out("Partition operations:\n");
        svde_out("  --part-create -file=<path> -part=<num> -size=<size> [-type=<type>]  Create a partition (default type: linux)\n");
        svde_out("  --part-delete -file=<path> -part=<num>                              Delete a partition\n");
        svde_out("  --part-set-type -file=<path> -part=<num> -type=<type>               Change partition type\n");
        svde_out("  --part-set-active -file=<path> -part=<num>                          Set active flag (MBR only)\n");
        svde_out("  --part-set-inactive -file=<path> -part=<num>                        Unset active flag\n");
        svde_out("\n<type> can be a name (e.g., linux, fat32) or hex code for MBR, or GUID for GPT.\n");
    } else if (strcmp(category, "format") == 0) {
        svde_out("Format operations:\n");
        svde_out("  --format -file=<path> -part=<num> -fs=fat32   Format a partition as FAT32\n");
    } else if (strcmp(category, "fs") == 0) {
        svde_out("Filesystem operations:\n");
        svde_out("  --fs-ls -file=<path> -part=<num> [-path=<dir>]                List directory contents\n");
        svde_out("  --fs-copy -file=<path> -part=<num> -src=<host-file> -dest=<fs-path>  Copy file from host to image\n");
        svde_out("  --fs-mkdir -file=<path> -part=<num> -path=<dir>               Create directory\n");
        svde_out("  --fs-rm -file=<path> -part=<num> -path=<file>                 Delete file\n");
        svde_out("  --fs-rmdir -file=<path> -part=<num> -path=<dir>               Remove empty directory\n");
        svde_out("  --fs-tree -file=<path> -part=<num> [-path=<dir>]              Display directory tree\n");
        svde_out("  --fs-check -file=<path> -part=<num|raw> [-level=quick|full]   Check filesystem integrity\n");
        svde_out("  --fs-label -file=<path> -part=<num|raw> [-name=<label>]       Get/set volume label\n");
        svde_out("  --fs-info -file=<path> -part=<num|raw>                        Show filesystem information\n");
        svde_out("\nReserve operations (filesystem registry):\n");
        svde_out("  --fs-reserve-init -file=<path> -part=<num>                    Initialize reserve cluster\n");
        svde_out("  --fs-reserve-ls -file=<path> -part=<num>                      List reserve entries\n");
        svde_out("  --fs-reserve-add -file=<path> -part=<num> -path=<file-path>   Add file entry to reserve\n");
        svde_out("  --fs-reserve-rm -file=<path> -part=<num> -name=<entry-name>   Remove entry by name\n");
        svde_out("  --fs-reserve-clear -file=<path> -part=<num>                   Clear all entries\n");
        svde_out("  --fs-reserve-dump -file=<path> -part=<num>                    Hex dump of reserve cluster\n");
        svde_out("  --fs-reserve-boot-set -file=<path> -part=<num> -name=<entry-name>\n");
        svde_out("  --fs-reserve-boot-show -file=<path> -part=<num>\n");
        svde_out("  --fs-reserve-boot-clear -file=<path> -part=<num>\n");
        svde_out("  --fs-reserve-info -file=<path> -part=<num>                    Show reserve info\n");
    } else if (strcmp(category, "shell") == 0) {
        svde_out("Interactive shell:\n");
        svde_out("  --shell -file=<path> -part=<num>   Start interactive FAT32 shell\n");
        svde_out("  Inside shell, type 'help' for available commands.\n");
    } else if (strcmp(category, "mbr") == 0) {
        svde_out("MBR boot code writing:\n");
        svde_out("  --mbr-write -file=<path> -src=<file>   Write boot code to MBR (first 446 bytes)\n");
    } else if (strcmp(category, "bpb") == 0) {
        svde_out("BPB boot code writing:\n");
        svde_out("  --bpb-write -file=<path> -part=<num> -src=<file>   Write boot code to partition's BPB (offset 90)\n");
    } else {
        svde_out("Unknown category '%s'. Use --help for list of categories.\n", category);
    }
}