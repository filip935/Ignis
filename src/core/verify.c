#define _GNU_SOURCE
#include "verify.h"
#include "../util/util.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

static int count_entries(const char *dir_path, int *files, int *dirs) {
    DIR *dir = opendir(dir_path);
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        if (entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) {
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
                (*dirs)++;
                count_entries(path, files, dirs);
            } else if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
                (*files)++;
            }
        } else if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
            (*files)++;
        }
    }
    closedir(dir);
    return 0;
}

int verify_flash(const char *mount_point, VerifyResult *result, Error *err) {
    memset(result, 0, sizeof(*result));

    struct stat st;

    char bootmgr_path[1024];
    snprintf(bootmgr_path, sizeof(bootmgr_path), "%s/bootmgr", mount_point);
    result->bootmgr_exists = (stat(bootmgr_path, &st) == 0);
    if (result->bootmgr_exists) result->bootmgr_size = (int)st.st_size;

    char bootmgr_efi_path[1024];
    snprintf(bootmgr_efi_path, sizeof(bootmgr_efi_path), "%s/bootmgr.efi", mount_point);
    if (stat(bootmgr_efi_path, &st) == 0 && st.st_size > 0) {
        result->bootmgr_exists = 1;
        if (result->bootmgr_size == 0) result->bootmgr_size = (int)st.st_size;
    }

    char bootx64_path[1024];
    snprintf(bootx64_path, sizeof(bootx64_path), "%s/EFI/Boot/bootx64.efi", mount_point);
    result->bootx64_exists = (stat(bootx64_path, &st) == 0 && st.st_size > 0);
    if (result->bootx64_exists) result->bootx64_size = (int)st.st_size;

    if (!result->bootx64_exists) {
        snprintf(bootx64_path, sizeof(bootx64_path), "%s/efi/boot/bootx64.efi", mount_point);
        result->bootx64_exists = (stat(bootx64_path, &st) == 0 && st.st_size > 0);
        if (result->bootx64_exists) result->bootx64_size = (int)st.st_size;
    }

    char sources_path[1024];
    snprintf(sources_path, sizeof(sources_path), "%s/sources", mount_point);
    result->sources_dir_exists = (stat(sources_path, &st) == 0 && S_ISDIR(st.st_mode));

    if (result->sources_dir_exists) {
        char wim_path[1024];
        snprintf(wim_path, sizeof(wim_path), "%s/sources/install.wim", mount_point);
        result->install_wim_exists = (stat(wim_path, &st) == 0);

        if (!result->install_wim_exists) {
            snprintf(wim_path, sizeof(wim_path), "%s/sources/install.esd", mount_point);
            result->install_esd_exists = (stat(wim_path, &st) == 0);
        }

        for (int i = 1; i <= 9; i++) {
            snprintf(wim_path, sizeof(wim_path), "%s/sources/install.swm", mount_point);
            if (stat(wim_path, &st) == 0) {
                result->install_wim_exists = 1;
            }
        }
    }

    char efi_path[1024];
    snprintf(efi_path, sizeof(efi_path), "%s/efi", mount_point);
    result->efi_dir_exists = (stat(efi_path, &st) == 0 && S_ISDIR(st.st_mode));

    snprintf(efi_path, sizeof(efi_path), "%s/EFI", mount_point);
    if (stat(efi_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        result->efi_dir_exists = 1;
    }

    count_entries(mount_point, &result->total_files, &result->total_dirs);

    result->all_files_copied = (result->bootmgr_exists && result->sources_dir_exists &&
                                (result->install_wim_exists || result->install_esd_exists) && result->total_files > 10);

    if (err) err->code = ERR_SUCCESS;
    return 0;
}
