#include "cmd.h"
#include "help.h"
#include "utils.h"
#include "shell.h"
#include "cmd_fs.h"
#include "output.h"
#include "cmd_disk.h"
#include "cmd_part.h"
#include "cmd_format.h"
#include "error_codes.h"

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define CHECK_ARG(field, name) \
    do { \
        if (!(field)) { \
            svde_err( "Error: missing -%s= parameter.\n", name); \
            return 1; \
        } \
    } while (0)

static void parse_arguments(int argc, char *argv[], CMDArgs *args) {
    srand((unsigned)time(NULL));
    memset(args, 0, sizeof(CMDArgs));

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];

        if (arg[0] != '-') {
            if (args->category && args->command == NULL) {
                args->command = arg;
            } else {
                svde_err( "Warning: unexpected argument '%s' ignored.\n", arg);
            }
            continue;
        }

        char *eq = strchr(arg, '=');
        if (eq) {
            *eq = '\0';
            char *key = arg;
            while (*key == '-') key++;
            char *value = eq + 1;

            if (strcmp(key, "file") == 0) {
                args->file = value;
            } else if (strcmp(key, "name") == 0) {
                args->name = value;
            } else if (strcmp(key, "part") == 0) {
                args->part = value;
            } else if (strcmp(key, "size") == 0) {
                args->size = value;
            } else if (strcmp(key, "type") == 0) {
                args->type = value;
            } else if (strcmp(key, "table") == 0) {
                args->table = value;
            } else if (strcmp(key, "offset") == 0) {
                args->offset = value;
            } else if (strcmp(key, "count") == 0) {
                args->count = value;
            } else if (strcmp(key, "src") == 0) {
                args->src = value;
            } else if (strcmp(key, "dest") == 0) {
                args->dest = value;
            } else if (strcmp(key, "path") == 0) {
                args->path = value;
            } else if (strcmp(key, "fs") == 0) {
                args->fs = value;
            } else if (strcmp(key, "level") == 0) {
                args->level = value;
            } else {
                svde_err( "Warning: unknown parameter -%s ignored.\n", key);
            }

            *eq = '=';
        } else {
            if (arg[0] != '-' || arg[1] != '-') {
                svde_err( "Warning: unexpected argument '%s' ignored.\n", arg);
                continue;
            }

            char *dash = strchr(arg + 2, '-');
            if (dash) {
                *dash = '\0';
                args->category = arg;
                args->command = dash + 1;
            } else {
                args->category = arg;
                args->command = NULL;
            }
        }
    }

    if (!args->category) {
        svde_err( "Error: no command specified.\n");
        svde_err( "Try '%s --help' for more information.\n", argv[0]);
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_short_help();
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_global_help();
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        print_version();
        return 0;
    }

    CMDArgs args;
    parse_arguments(argc, argv, &args);
    ErrorCode err = ERR_UNKNOWN;

    const char *cat = args.category + 2;

    if (args.command == NULL) {
        if (strcmp(cat, "format") != 0 && strcmp(cat, "shell") != 0) {
            print_category_help(cat);
            return 0;
        }
    }

    if (args.command && (strcmp(args.command, "?") == 0 || strcmp(args.command, "help") == 0)) {
        print_category_help(cat);
        return 0;
    }

    if (strcmp(cat, "disk") == 0) {
        if (strcmp(args.command, "create") == 0) {
            CHECK_ARG(args.file, "file");
            // CHECK_ARG(args.table, "table");
            CHECK_ARG(args.size, "size");
            err = cmd_disk_create(&args);
        } else if (strcmp(args.command, "info") == 0) {
            CHECK_ARG(args.file, "file");
            err = cmd_disk_info(&args);
        } else if (strcmp(args.command, "read") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.offset, "offset");
            CHECK_ARG(args.count, "count");
            err = cmd_disk_read(&args);
        } else if (strcmp(args.command, "read-s") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.offset, "offset");
            CHECK_ARG(args.count, "count");
            err = cmd_disk_read_s(&args);
        } else {
            svde_err( "Error: unknown disk command '%s'.\n", args.command);
            return 1;
        }
    } else if (strcmp(cat, "part") == 0) {
        if (strcmp(args.command, "create") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_part_create(&args);
        } else if (strcmp(args.command, "delete") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_part_delete(&args);
        } else if (strcmp(args.command, "set-type") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            CHECK_ARG(args.type, "type");
            err = cmd_part_set_type(&args);
        } else if (strcmp(args.command, "set-active") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_part_set_active(&args);
        } else if (strcmp(args.command, "set-inactive") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_part_set_inactive(&args);
        } else {
            svde_err( "Error: unknown partition command '%s'.\n", args.command);
            return 1;
        }
    } else if (strcmp(cat, "format") == 0) {
        CHECK_ARG(args.file, "file");
        CHECK_ARG(args.part, "part");
        CHECK_ARG(args.fs, "fs");
        err = cmd_format(&args);
    } else if (strcmp(cat, "fs") == 0) {
        if (strcmp(args.command, "ls") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_ls(&args);
        } else if (strcmp(args.command, "copy") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            CHECK_ARG(args.src, "src");
            CHECK_ARG(args.dest, "dest");
            err = cmd_fs_copy(&args);
        } else if (strcmp(args.command, "mkdir") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            CHECK_ARG(args.path, "path");
            err = cmd_fs_mkdir(&args);
        } else if (strcmp(args.command, "rm") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            CHECK_ARG(args.path, "path");
            err = cmd_fs_rm(&args);
        } else if (strcmp(args.command, "rmdir") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            CHECK_ARG(args.path, "path");
            err = cmd_fs_rmdir(&args);
        } else if (strcmp(args.command, "tree") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_tree(&args);
        } else if (strcmp(args.command, "check") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_check(&args);
        } else if (strcmp(args.command, "label") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_label(&args);
        } else if (strcmp(args.command, "reserve-init") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_reserve_init(&args);
        } else if (strcmp(args.command, "reserve-ls") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_reserve_ls(&args);
        } else if (strcmp(args.command, "reserve-add") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            CHECK_ARG(args.path, "path");
            err = cmd_fs_reserve_add(&args);
        } else if (strcmp(args.command, "reserve-rm") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            CHECK_ARG(args.name, "name");
            err = cmd_fs_reserve_rm(&args);
        } else if (strcmp(args.command, "reserve-clear") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_reserve_clear(&args);
        } else if (strcmp(args.command, "reserve-dump") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_reserve_dump(&args);
        } else if (strcmp(args.command, "reserve-info") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_reserve_info(&args);
		} else if (strcmp(args.command, "reserve-boot-set") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            CHECK_ARG(args.name, "name");
            err = cmd_fs_reserve_boot_set(&args);
        } else if (strcmp(args.command, "reserve-boot-show") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_reserve_boot_show(&args);
        } else if (strcmp(args.command, "reserve-boot-clear") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            err = cmd_fs_reserve_boot_clear(&args);
        } else {
            svde_err( "Error: unknown fs command '%s'.\n", args.command);
            return 1;
        }
    } else if (strcmp(cat, "shell") == 0) {
        CHECK_ARG(args.file, "file");
        CHECK_ARG(args.part, "part");
        err = cmd_shell(&args);
        return 1;
    } else if (strcmp(cat, "mbr") == 0) {
        if (strcmp(args.command, "write") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.src, "src");
            err = cmd_mbr_write(&args);
        } else {
            svde_err( "Error: unknown mbr command '%s'.\n", args.command);
            return 1;
        }
    } else if (strcmp(cat, "bpb") == 0) {
        if (strcmp(args.command, "write") == 0) {
            CHECK_ARG(args.file, "file");
            CHECK_ARG(args.part, "part");
            CHECK_ARG(args.src, "src");
            err = cmd_bpb_write(&args);
        } else {
            svde_err( "Error: unknown bpb command '%s'.\n", args.command);
            return 1;
        }
    } else {
        svde_err( "Error: unknown command category '%s'.\n", args.category);
        svde_err( "Try '%s --help' for more information.\n", argv[0]);
        return 1;
    }

    if (err != ERR_OK) {
        svde_err( "Command failed with error code: %d\n", err);
        return 1;
    }
    return 0;
}