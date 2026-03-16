#include "help.h"
#include <stdio.h>

void print_short_help(void) {
    printf("%s version %s\n", PROGRAM_NAME, PROGRAM_VERSION);
    printf("Use '%s --help' for detailed help.\n", "diskman");
}

void print_version(void) {
    printf("%s version %s\n", PROGRAM_NAME, PROGRAM_VERSION);
}

 void print_general_help(void) {
    printf("Disk Manager - Utility for disk and partition management\n");
    printf("\nDisk operations:\n");
    printf("  --disk-create -file=<path> -table=mbr|gpt -size=<size>\n");
    printf("  --disk-info -file=<path>\n");
    printf("  --disk-read -file=<path> -offset=<bytes> -count=<bytes>\n");
	printf("  --disk-read-s -file=<path> -offset=<sectors> -count=<sectors>\n");
    printf("  --mbr-write -file=<path> -src=<file>   Write boot code to MBR (first 446 bytes)\n");
    printf("  --bpb-write -file=<path> -part=<num> -src=<file>   Write boot code to partition's BPB (offset 90).\n");
    printf("\nPartition operations:\n");
    printf("  --part-create -file=<path> -part=<num> -size=<size> -type=<type>\n");
    printf("  --part-delete -file=<path> -part=<num>\n");
    printf("  --part-set-type -file=<path> -part=<num> -type=<type>\n");
    printf("  --part-set-active -file=<path> -part=<num>\n");
    printf("  --part-set-inactive -file=<path> -part=<num>\n");
    printf("\nFormat operations:\n");
    printf("  --format -file=<path> -part=<num> -fs=fat32\n");
    printf("\nFilesystem operations (direct):\n");
    printf("  --fs-ls -file=<path> -part=<num> [-path=<dir>]\n");
    printf("  --fs-copy -file=<path> -part=<num> -src=<host-file> -dest=<fs-path>\n");
    printf("  --fs-mkdir -file=<path> -part=<num> -path=<dir>\n");
    printf("  --fs-rm -file=<path> -part=<num> -path=<file>\n");
    printf("  --fs-rmdir -file=<path> -part=<num> -path=<dir>\n");
    printf("\nReserve operations (filesystem registry):\n");
    printf("  --fs-reserve-init -file=<path> -part=<num>\n");
    printf("  --fs-reserve-ls -file=<path> -part=<num>\n");
    printf("  --fs-reserve-add -file=<path> -part=<num> -path=<file-path>\n");
    printf("  --fs-reserve-rm -file=<path> -part=<num> -name=<entry-name>\n");
    printf("  --fs-reserve-clear -file=<path> -part=<num>\n");
    printf("  --fs-reserve-dump -file=<path> -part=<num>\n");
    printf("  --fs-reserve-info -file=<path> -part=<num>\n");
    printf("\nInteractive shell:\n");
    printf("  --shell -file=<path> -part=<num>\n");
    printf("\nGlobal options:\n");
    printf("  --help, -h                 Show this help\n");
    printf("  --version, -v              Show version\n");
}