#ifndef WINFLASH_BOOTLOADER_H
#define WINFLASH_BOOTLOADER_H

#include "../util/error.h"
#include "partition.h"

int bootloader_install(const char *device_path, const char *mount_point, FlashMode mode, Error *err);
int bootloader_setup_efi(const char *iso_mount, const char *target_mount, Error *err);
int bootloader_setup_uefi_ntfs(const char *esp_mount, Error *err);
int bootloader_fix_zero_efi(const char *target_mount, Error *err);
int bootloader_write_grub_mbr(const char *device_path, Error *err);

#endif
