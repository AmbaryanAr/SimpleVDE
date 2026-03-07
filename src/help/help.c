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
    printf("  --partition -disk=DISK_PATH -index=IDX -op=create -size=SIZE [-fs=FSTYPE] Create partition\n");
    printf("                     FSTYPE can be a hex code (e.g., 0x83) or a name (linux, swap, efi, ...).\n");
    printf("                     Default: linux (0x83 for MBR, Linux filesystem GUID for GPT).\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=delete                         Delete partition\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=active|inactive                Set partition active/bootable (MBR only)\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=set_type -fs=FSTYPE            Change partition type\n");
    printf("                     FSTYPE: hex code or name (linux, swap, efi, ...).\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=format -fs=FSTYPE              Format partition (stub)\n");
    printf("  --partition -disk=DISK_PATH -op=write_mbr -file=FILE                      Write MBR code (first 446 bytes)\n");
    printf("  --partition -disk=DISK_PATH -index=IDX -op=write_bpb -file=FILE           Write BPB code (stub)\n");
    printf("\nFile system operations:\n");
    printf("  --copy -src=HOST_FILE -disk=DISK_PATH -index=IDX -path=FS_PATH            Copy file from OS to disk (stub)\n");
    printf("  --ls -disk=DISK_PATH -index=IDX -path=FS_PATH                             List files/directories (stub)\n");
    printf("  --mkdir -disk=DISK_PATH -index=IDX -path=FS_PATH                          Create directory (stub)\n");
    printf("  --rmdir -disk=DISK_PATH -index=IDX -path=FS_PATH                          Remove directory (stub)\n");
    printf("  --rm -disk=DISK_PATH -index=IDX -path=FS_PATH                             Remove file (stub)\n");
    printf("\nSpecial file map operations:\n");
    printf("  --map_file -disk=DISK_PATH -index=IDX -op=list                            List map file entries (stub)\n");
    printf("  --map_file -disk=DISK_PATH -index=IDX -op=delete -name=ENTRY_NAME         Delete map entry (stub)\n");
    printf("  --map_file -disk=DISK_PATH -index=IDX -op=copy -src=HOST_FILE -path=FS_PATH Copy file with map entry (stub)\n");
    printf("\nGlobal options:\n");
    printf("  --help, -h                                                                 Show this help\n");
    printf("  --version, -v                                                              Show version\n");
}

void print_version(void) {
    printf("%s version %s\n", PROGRAM_NAME, PROGRAM_VERSION);
}