#include "help.h"
#include "version.h"
#include <stdio.h>

void print_short_help(void) {
    printf("%s\n", PROGRAM_NAME);
    printf("Use '%s --help' for detailed help.\n", PROGRAM_EXECUTABLE_NAME);
}

void print_help(void) {
    printf("%s - Utility for disk and partition management\n", PROGRAM_SHORT_NAME);
    printf("\nDisk operations:\n");
    printf("  --disk -op=create -path=DISK_PATH -size=SIZE -type=MBR|GPT                Create disk image\n");
    printf("  --disk -op=info -path=DISK_PATH                                           Show disk information\n");
    printf("  --disk -op=read -path=DISK_PATH -offset=OFFSET -size=SIZE                 Read disk sector\n");
    printf("\nPartition operations:\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=create -size=SIZE -type=MBR|GPT Create partition\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=delete                         Delete partition\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=active|inactive                Set partition active/bootable\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=set_type -type=FAT32           Change partition type\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=format -type=FAT32             Format partition\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=write_mbr -file=FILE           Write MBR code\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=write_bpb -file=FILE           Write BPB code\n");
    printf("\nFile system operations:\n");
    printf("  --copy -src=HOST_FILE -disk=DISK_PATH -index=IDX -path=FS_PATH            Copy file from OS to disk\n");
    printf("  --ls -disk=DISK_PATH -index=IDX -path=FS_PATH                             List files/directories\n");
    printf("  --mkdir -disk=DISK_PATH -index=IDX -path=FS_PATH                          Create directory\n");
    printf("  --rmdir -disk=DISK_PATH -index=IDX -path=FS_PATH                          Remove directory\n");
    printf("  --rm -disk=DISK_PATH -index=IDX -path=FS_PATH                             Remove file\n");
    printf("\nSpecial file map operations:\n");
    printf("  --map_file -disk=DISK_PATH -index=IDX -op=list                            List map file entries\n");
    printf("  --map_file -disk=DISK_PATH -index=IDX -op=delete -name=ENTRY_NAME         Delete map entry\n");
    printf("  --map_file -disk=DISK_PATH -index=IDX -op=copy -src=HOST_FILE -path=FS_PATH Copy file with map entry\n");
    printf("\nGlobal options:\n");
    printf("  --help, -h                                                                 Show this help\n");
    printf("  --version, -v                                                              Show version\n");
}

void print_version(void) {
    printf("%s version %s\n", PROGRAM_NAME, PROGRAM_VERSION);
}