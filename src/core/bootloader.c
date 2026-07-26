#define _GNU_SOURCE
#include "bootloader.h"
#include "bootdata.h"
#include "../util/util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#define GRUB_CFG \
    "set timeout=5\n" \
    "set default=0\n\n" \
    "menuentry \"Install Windows\" {\n" \
    "    insmod part_msdos\n" \
    "    insmod fat\n" \
    "    search --no-floppy --set=root --file /bootmgr\n" \
    "    ntldr /bootmgr\n" \
    "}\n"

static int write_boot_data(const char *device_path, Error *err) {
    int fd = open(device_path, O_WRONLY | O_SYNC);
    if (fd < 0) {
        ERR_RETURN(err, ERR_BOOTLOADER_FAILED,
                  "Cannot open %s: %s", device_path, strerror(errno));
    }

    unsigned char sector[512];
    memset(sector, 0, 512);

    if (pread(fd, sector, 512, 0) < 0) {
        close(fd);
        ERR_RETURN(err, ERR_BOOTLOADER_FAILED,
                  "Cannot read MBR from %s: %s", device_path, strerror(errno));
    }

    unsigned char saved_pt[66];
    memcpy(saved_pt, sector + 446, 66);

    if (grub_boot_img_size >= 446) {
        memcpy(sector, grub_boot_img, 446);
    } else {
        memcpy(sector, grub_boot_img, grub_boot_img_size);
    }

    memcpy(sector + 446, saved_pt, 66);

    if (pwrite(fd, sector, 512, 0) < 0) {
        close(fd);
        ERR_RETURN(err, ERR_BOOTLOADER_FAILED,
                  "Cannot write MBR to %s: %s", device_path, strerror(errno));
    }

    if (grub_core_img_size > 1) {
        unsigned int sector_size = 512;
        if (pwrite(fd, grub_core_img, grub_core_img_size, sector_size) < 0) {
            close(fd);
            ERR_RETURN(err, ERR_BOOTLOADER_FAILED,
                      "Cannot write core.img to %s: %s", device_path, strerror(errno));
        }
    }

    fsync(fd);
    close(fd);
    if (err) err->code = ERR_SUCCESS;
    return 0;
}

static int install_grub_bios(const char *device_path, const char *mount_point, Error *err) {
    if (write_boot_data(device_path, err) < 0) return -1;

    char cfg_dir[1024];
    snprintf(cfg_dir, sizeof(cfg_dir), "%s/boot/grub", mount_point);
    mkdir_p(cfg_dir, 0755);

    char cfg_path[1024];
    snprintf(cfg_path, sizeof(cfg_path), "%s/boot/grub/grub.cfg", mount_point);

    FILE *f = fopen(cfg_path, "w");
    if (!f) {
        ERR_RETURN(err, ERR_BOOTLOADER_FAILED,
                  "Cannot write GRUB config: %s", strerror(errno));
    }
    fprintf(f, "%s", GRUB_CFG);
    fclose(f);

    if (err) err->code = ERR_SUCCESS;
    return 0;
}

static int handle_efi_bootloader(const char *iso_mount, const char *target_mount, Error *err) {
    char efi_src[1024];
    snprintf(efi_src, sizeof(efi_src), "%s/efi", iso_mount);
    char efi_dst[1024];
    snprintf(efi_dst, sizeof(efi_dst), "%s/efi", target_mount);

    if (path_exists(efi_src)) {
        if (!path_exists(efi_dst)) mkdir_p(efi_dst, 0755);
        ExecResult res;
        int ret = exec_cmd(&res, err, "cp", "-r", efi_src, target_mount, NULL);
        exec_result_free(&res);
        if (ret < 0) return -1;
    }

    char bootx64_path[1024];
    snprintf(bootx64_path, sizeof(bootx64_path), "%s/EFI/Boot/bootx64.efi", target_mount);
    if (!path_exists(bootx64_path)) {
        char alt_path[1024];
        snprintf(alt_path, sizeof(alt_path), "%s/efi/boot/bootx64.efi", target_mount);
        if (path_exists(alt_path)) {
            mkdir_p(target_mount, 0755);
            ExecResult res;
            exec_cmd(&res, NULL, "cp", alt_path, bootx64_path, NULL);
            exec_result_free(&res);
        }
    }

    if (err) err->code = ERR_SUCCESS;
    return 0;
}

static int setup_uefi_ntfs_esp(const char *esp_mount, Error *err) {
    char efi_dir[1024];
    snprintf(efi_dir, sizeof(efi_dir), "%s/EFI/BOOT", esp_mount);
    mkdir_p(efi_dir, 0755);

    char efi_path[1024];
    snprintf(efi_path, sizeof(efi_path), "%s/EFI/BOOT/BOOTX64.EFI", esp_mount);

    FILE *f = fopen(efi_path, "wb");
    if (!f) {
        ERR_RETURN(err, ERR_BOOTLOADER_FAILED,
                  "Cannot write UEFI:NTFS: %s", strerror(errno));
    }
    size_t written = fwrite(uefi_ntfs_efi, 1, uefi_ntfs_efi_size, f);
    fclose(f);

    if (written != uefi_ntfs_efi_size) {
        ERR_RETURN(err, ERR_BOOTLOADER_FAILED,
                  "Wrote %zu/%u bytes of UEFI:NTFS", written, uefi_ntfs_efi_size);
    }

    if (err) err->code = ERR_SUCCESS;
    return 0;
}

static int fix_zero_bootx64(const char *mount_point, Error *err) {
    char bootx64_path[1024];
    snprintf(bootx64_path, sizeof(bootx64_path), "%s/EFI/Boot/bootx64.efi", mount_point);

    struct stat st;
    if (stat(bootx64_path, &st) == 0 && st.st_size == 0) {
        ExecResult res;
        memset(&res, 0, sizeof(res));
        int ret = exec_cmd(&res, err, "wimlib-imagex", "extract",
                          mount_point, "/sources/install.wim", "1",
                          "/Windows/Boot/EFI/bootmgfw.efi",
                          "--dest-dir=/tmp/ignis-efi-fix", NULL);
        if (ret == 0) {
            exec_cmd(NULL, NULL, "cp", "/tmp/ignis-efi-fix/bootmgfw.efi", bootx64_path, NULL);
        }
        exec_result_free(&res);
    }

    if (err) err->code = ERR_SUCCESS;
    return 0;
}

int bootloader_install(const char *device_path, const char *mount_point, FlashMode mode, Error *err) {
    switch (mode) {
        case FLASH_MODE_MBR_FAT32:
            if (install_grub_bios(device_path, mount_point, err) < 0) {
                if (err && err->code != ERR_SUCCESS) return -1;
            }
            break;
        default:
            break;
    }

    if (err) err->code = ERR_SUCCESS;
    return 0;
}

int bootloader_setup_efi(const char *iso_mount, const char *target_mount, Error *err) {
    return handle_efi_bootloader(iso_mount, target_mount, err);
}

int bootloader_setup_uefi_ntfs(const char *esp_mount, Error *err) {
    return setup_uefi_ntfs_esp(esp_mount, err);
}

int bootloader_fix_zero_efi(const char *target_mount, Error *err) {
    return fix_zero_bootx64(target_mount, err);
}

int bootloader_write_grub_mbr(const char *device_path, Error *err) {
    return write_boot_data(device_path, err);
}
