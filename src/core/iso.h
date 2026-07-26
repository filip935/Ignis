#ifndef IGNIS_ISO_H
#define IGNIS_ISO_H

#include "../util/error.h"
#include <stdint.h>

typedef struct {
    char iso_path[1024];
    int valid;
    char windows_version[64];
    int has_install_wim;
    int has_install_esd;
    uint64_t install_wim_size;
    int needs_wim_split;
    int bootx64_efi_exists;
    int bootx64_efi_zero;
} IsoInfo;

int iso_parse(const char *path, IsoInfo *info, Error *err);
int iso_mount(const char *path, const char *mount_point, Error *err);
int iso_unmount(const char *mount_point, Error *err);
int iso_copy_files(const char *src_mount, const char *dst_mount, Error *err);

#endif
