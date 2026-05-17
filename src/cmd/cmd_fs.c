#include "cmd_fs.h"
#include "output.h"
#include "cmd_common.h"
#include "fat32_util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    uint32_t cluster;
    char *name;
    int is_dir;
    uint32_t size;
} child_entry_t;

// Рекурсивно выводит дерево каталога, начиная с cluster, с отступами prefix.
// is_last указывает, является ли текущий элемент последним на своём уровне.
static void print_tree(Disk *disk, const Fat32Info *info, uint32_t cluster,
                       const char *name, const char *prefix, int is_last) {
    svde_out("%s%s%s\n", prefix, (is_last ? "'-- " : "|-- "), name);

    uint8_t *buffer = NULL;
    uint32_t entries = 0;
    if (fat32_read_dir(disk, info, cluster, &buffer, &entries) != ERR_OK) {
        return;
    }

    child_entry_t *children = NULL;
    uint32_t child_count = 0;

    for (uint32_t i = 0; i < entries; i++) {
        const uint8_t *entry = buffer + i * 32;
        if (entry[0] == 0x00) {
            break;
        }
        if (entry[0] == 0xE5) {
            continue;
        }
        if (entry[11] == FAT32_ATTR_LFN) {
            continue;
        }

        Fat32ShortEntry *se = (Fat32ShortEntry*)entry;
        char display_name[256];

        char *long_name = extract_lfn_name(buffer, i);
        if (long_name) {
            strcpy(display_name, long_name);
            free(long_name);
        } else {
            sfn_to_display_name(se->name, display_name, sizeof(display_name));
        }

        if (strcmp(display_name, ".") == 0 || strcmp(display_name, "..") == 0) {
            continue;
        }

        children = realloc(children, (child_count + 1) * sizeof(child_entry_t));
        children[child_count].name = my_strdup(display_name);
        children[child_count].cluster = ((uint32_t)se->first_cluster_hi << 16) | se->first_cluster_lo;
        children[child_count].is_dir = (se->attr & FAT32_ATTR_DIRECTORY) != 0;
        children[child_count].size = se->file_size;
        child_count++;
    }

    free(buffer);

    char new_prefix[512];
    snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix, (is_last ? "    " : "|   "));

    for (uint32_t i = 0; i < child_count; i++) {
        child_entry_t *child = &children[i];
        int last = (i == child_count - 1);

        if (child->is_dir) {
            print_tree(disk, info, child->cluster, child->name, new_prefix, last);
        } else {
            char file_info[256];
            snprintf(file_info, sizeof(file_info), "%s (%u bytes)", child->name, child->size);
            printf("%s%s%s\n", new_prefix, (last ? "'-- " : "|-- "), file_info);
        }
        free(child->name);
    }
    free(children);
}

ErrorCode cmd_fs_ls(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    const char *path = args->path ? args->path : "/";
    err = fat32_list_dir(&disk, start_lba, path);
    if (err != ERR_OK) {
        print_error(err, "cannot list directory");
    }

    disk_close(&disk);
    return err;
}

ErrorCode cmd_fs_mkdir(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_create_dir(&disk, start_lba, args->path);
    if (err != ERR_OK) {
        print_error(err, "cannot create directory");
        disk_close(&disk);
        return err;
    }

    svde_out("Directory '%s' created successfully.\n", args->path);
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_copy(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_copy_file(&disk, start_lba, args->src, args->dest);
    if (err != ERR_OK) {
        print_error(err, "cannot copy file");
        if (err == ERR_INVALID_ARGUMENT) {
            svde_err( "Note: Destination path must be absolute, e.g., /dir/file.txt\n");
        } else if (err == ERR_NOT_FOUND) {
            svde_err( "Note: Check that source file exists and is readable.\n");
        } else if (err == ERR_ALREADY_EXISTS) {
            svde_err( "Note: A file with the same name already exists.\n");
        }
        disk_close(&disk);
        return err;
    }

    svde_out("File '%s' copied to '%s' successfully.\n", args->src, args->dest);
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_rm(CMDArgs *args) {
    if (strcmp(args->path, "/") == 0) {
        svde_err( "Error: cannot delete root directory.\n");
        return ERR_INVALID_ARGUMENT;
    }

    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_delete_file(&disk, start_lba, args->path);
    if (err != ERR_OK) {
        print_error(err, "cannot delete file");
        if (err == ERR_INVALID_ARGUMENT) {
            svde_err( "Note: Path must be absolute and point to a file, not a directory.\n");
        }
        disk_close(&disk);
        return err;
    }

    svde_out("File '%s' deleted successfully.\n", args->path);
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_rmdir(CMDArgs *args) {
    if (strcmp(args->path, "/") == 0) {
        svde_err( "Error: cannot remove root directory.\n");
        return ERR_INVALID_ARGUMENT;
    }

    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_remove_dir(&disk, start_lba, args->path);
    if (err != ERR_OK) {
        print_error(err, "cannot remove directory");
        if (err == ERR_INVALID_ARGUMENT) {
            svde_err( "Note: Path must be absolute and point to a directory.\n");
        } else if (err == ERR_DIR_NOT_EMPTY) {
            svde_err( "Note: Directory is not empty.\n");
        }
        disk_close(&disk);
        return err;
    }

    svde_out("Directory '%s' removed successfully.\n", args->path);
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_tree(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    uint32_t start_cluster = info.root_cluster;
    const char *start_name = "/";
    if (args->path) {
        err = fat32_find_dir(&disk, &info, args->path, &start_cluster);
        if (err != ERR_OK) {
            disk_close(&disk);
            print_error(err, "cannot find path");
            return err;
        }
        start_name = args->path;
    }

    svde_out("Directory tree of %s:\n", start_name);
    print_tree(&disk, &info, start_cluster, start_name, "", 1);

    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_reserve_init(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_reserve_init(&disk, start_lba);
    if (err != ERR_OK) {
        print_error(err, "cannot initialize reserve cluster");
        disk_close(&disk);
        return err;
    }

    svde_out("Reserve cluster initialized successfully.\n");
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_reserve_ls(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_reserve_list(&disk, start_lba);
    disk_close(&disk);
    return err;
}

ErrorCode cmd_fs_reserve_add(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_reserve_add(&disk, start_lba, args->path);
    if (err != ERR_OK) {
        print_error(err, "cannot add entry to reserve");
        disk_close(&disk);
        return err;
    }

    svde_out("Entry added to reserve successfully.\n");
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_reserve_rm(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_reserve_remove(&disk, start_lba, args->name);
    if (err != ERR_OK) {
        print_error(err, "cannot remove entry from reserve");
        disk_close(&disk);
        return err;
    }

    svde_out("Entry removed from reserve successfully.\n");
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_reserve_clear(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_reserve_clear(&disk, start_lba);
    if (err != ERR_OK) {
        print_error(err, "cannot clear reserve");
        disk_close(&disk);
        return err;
    }

    svde_out("Reserve cleared successfully.\n");
    disk_close(&disk);
    return ERR_OK;
}

ErrorCode cmd_fs_reserve_dump(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_reserve_dump(&disk, start_lba);
    disk_close(&disk);
    return err;
}

ErrorCode cmd_fs_reserve_info(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) {
        return err;
    }

    err = fat32_reserve_info(&disk, start_lba);
    disk_close(&disk);
    return err;
}

ErrorCode cmd_fs_reserve_boot_set(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_reserve_boot_set(&disk, start_lba, args->name);
    disk_close(&disk);
    return err;
}

ErrorCode cmd_fs_reserve_boot_show(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_reserve_boot_show(&disk, start_lba);
    disk_close(&disk);
    return err;
}

ErrorCode cmd_fs_reserve_boot_clear(CMDArgs *args) {
    Disk disk;
    uint64_t start_lba;
    Fat32Info info;
    ErrorCode err = open_disk_and_prepare_fs(args, &disk, &start_lba, &info);
    if (err != ERR_OK) return err;

    err = fat32_reserve_boot_clear(&disk, start_lba);
    disk_close(&disk);
    return err;
}