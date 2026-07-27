#ifndef NIHILFLASH_VERIFY_H
#define NIHILFLASH_VERIFY_H

#include "../util/error.h"

typedef struct {
    int bootmgr_exists;
    int bootmgr_size;
    int bootx64_exists;
    int bootx64_size;
    int install_wim_exists;
    int install_esd_exists;
    int sources_dir_exists;
    int efi_dir_exists;
    int all_files_copied;
    int total_files;
    int total_dirs;
} VerifyResult;

int verify_flash(const char *mount_point, VerifyResult *result, Error *err);

#endif
