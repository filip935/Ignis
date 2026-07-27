#define _GNU_SOURCE
#include "flash.h"
#include "filesystem.h"
#include "bootloader.h"
#include "wim.h"
#include "../util/util.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static const char *stage_name(FlashStage stage) {
    switch (stage) {
        case STAGE_NONE: return "Starting";
        case STAGE_UNMOUNT: return "Unmounting existing partitions";
        case STAGE_WIPE: return "Wiping device";
        case STAGE_PARTITION: return "Creating partitions";
        case STAGE_FORMAT: return "Formatting filesystems";
        case STAGE_MOUNT_TARGET: return "Mounting target";
        case STAGE_MOUNT_ISO: return "Mounting ISO";
        case STAGE_COPY_FILES: return "Copying files";
        case STAGE_SPLIT_WIM: return "Splitting WIM files";
        case STAGE_FIX_BOOTLOADER: return "Fixing bootloader";
        case STAGE_INSTALL_BOOTLOADER: return "Installing bootloader";
        case STAGE_UNMOUNT_FINAL: return "Unmounting";
        case STAGE_VERIFY: return "Verifying";
        case STAGE_DONE: return "Done";
        case STAGE_FAILED: return "Failed";
        default: return "Unknown";
    }
}

static int report_progress(FlashConfig *cfg, FlashStage stage, int pct, const char *msg) {
    if (cfg->cancel_requested && *cfg->cancel_requested) return -1;
    if (cfg->progress_cb) {
        return cfg->progress_cb(stage, pct, msg ? msg : stage_name(stage), cfg->progress_user_data);
    }
    return 0;
}

static int get_partition_path(const char *device_path, int part_num, char *out, size_t out_size) {
    const char *p = strrchr(device_path, '/');
    if (!p) return -1;
    p++;

    if (strncmp(p, "nvme", 4) == 0 || strncmp(p, "mmcblk", 6) == 0)
        snprintf(out, out_size, "%sp%d", device_path, part_num);
    else
        snprintf(out, out_size, "%s%d", device_path, part_num);
    return 0;
}

typedef struct {
    FlashConfig *cfg;
    FlashStage stage;
    int pct_base;
    int pct_range;
} CopyProgress;

static void copy_progress_cb(int pct, void *user) {
    CopyProgress *cp = user;
    int overall = cp->pct_base + (pct * cp->pct_range / 100);
    report_progress(cp->cfg, cp->stage, overall, "Copying files...");
}

static int cleanup(FlashConfig *cfg, const char *iso_mnt, const char *target_mnt) {
    (void)cfg;
    if (target_mnt && path_exists(target_mnt)) {
        filesystem_unmount(target_mnt, NULL);
    }
    if (iso_mnt && path_exists(iso_mnt)) {
        iso_unmount(iso_mnt, NULL);
    }
    if (target_mnt) rmdir(target_mnt);
    if (iso_mnt) rmdir(iso_mnt);
    return 0;
}

FlashResult flash_run(FlashConfig *cfg) {
    FlashResult result;
    memset(&result, 0, sizeof(result));

    char iso_mnt[128] = "";
    char target_mnt[128] = "";
    char part1_path[256] = "";
    char part2_path[256] = "";

    if (geteuid() != 0) {
        result.success = 0;
        result.error_code = ERR_PERMISSION_DENIED;
        snprintf(result.error_message, sizeof(result.error_message),
                 "NihilFlash must be run as root. Use sudo.");
        result.failed_stage = STAGE_NONE;
        return result;
    }

    report_progress(cfg, STAGE_NONE, 0, "Starting NihilFlash");

    report_progress(cfg, STAGE_UNMOUNT, 2, "Unmounting existing partitions");
    if (device_unmount_all(cfg->device_path, NULL) < 0) {
        report_progress(cfg, STAGE_UNMOUNT, 2, "Warning: some partitions could not be unmounted");
    }
    usleep(500000);

    if (cfg->cancel_requested && *cfg->cancel_requested) {
        result.error_code = ERR_CANCELLED;
        snprintf(result.error_message, sizeof(result.error_message), "Cancelled at unmount stage");
        result.failed_stage = STAGE_UNMOUNT;
        return result;
    }

    if (cfg->mode == FLASH_MODE_RAW_DD) {
        report_progress(cfg, STAGE_WIPE, 10, "Writing ISO to device with dd...");
        char dd_cmd[4096];
        snprintf(dd_cmd, sizeof(dd_cmd),
                 "dd if='%s' of='%s' bs=4M status=progress conv=fsync 2>&1",
                 cfg->iso_path, cfg->device_path);
        int dd_ret = exec_cmd(NULL, NULL, "sh", "-c", dd_cmd, NULL);
        sync();
        if (dd_ret < 0) {
            result.success = 0;
            result.error_code = ERR_COPY_FAILED;
            snprintf(result.error_message, sizeof(result.error_message),
                     "dd write failed for %s", cfg->iso_path);
            result.failed_stage = STAGE_COPY_FILES;
            return result;
        }
        report_progress(cfg, STAGE_DONE, 100, "Complete!");
        result.success = 1;
        result.verify.total_files = 1;
        return result;
    }

    report_progress(cfg, STAGE_WIPE, 5, "Wiping partition table");
    if (partition_wipe(cfg->device_path, NULL) < 0) {
        result.success = 0;
        result.error_code = ERR_PARTITION_FAILED;
        snprintf(result.error_message, sizeof(result.error_message), "Failed to wipe device %s", cfg->device_path);
        result.failed_stage = STAGE_WIPE;
        return result;
    }

    if (cfg->cancel_requested && *cfg->cancel_requested) {
        result.error_code = ERR_CANCELLED;
        snprintf(result.error_message, sizeof(result.error_message), "Cancelled after wipe");
        result.failed_stage = STAGE_WIPE;
        return result;
    }

    report_progress(cfg, STAGE_PARTITION, 10, "Creating partition table");
    if (partition_create(cfg->device_path, cfg->mode, NULL) < 0) {
        result.success = 0;
        result.error_code = ERR_PARTITION_FAILED;
        snprintf(result.error_message, sizeof(result.error_message), "Failed to create partitions on %s", cfg->device_path);
        result.failed_stage = STAGE_PARTITION;
        return result;
    }

    if (cfg->cancel_requested && *cfg->cancel_requested) {
        result.error_code = ERR_CANCELLED; return result;
    }

    report_progress(cfg, STAGE_FORMAT, 20, "Formatting filesystems");
    if (get_partition_path(cfg->device_path, 1, part1_path, sizeof(part1_path)) < 0) {
        result.success = 0;
        result.error_code = ERR_UNKNOWN;
        snprintf(result.error_message, sizeof(result.error_message), "Cannot determine partition path");
        result.failed_stage = STAGE_FORMAT;
        return result;
    }

    Error fmt_err;
    memset(&fmt_err, 0, sizeof(fmt_err));

    if (cfg->mode == FLASH_MODE_GPT_DUAL || cfg->mode == FLASH_MODE_GPT_NTFS) {
        if (get_partition_path(cfg->device_path, 2, part2_path, sizeof(part2_path)) < 0 ||
            filesystem_format(part1_path, FLASH_MODE_GPT_FAT32, &fmt_err) < 0 ||
            filesystem_format_ntfs(part2_path, &fmt_err) < 0) {
            result.success = 0;
            result.error_code = ERR_FORMAT_FAILED;
            snprintf(result.error_message, sizeof(result.error_message), "Format failed: %s", fmt_err.message);
            result.failed_stage = STAGE_FORMAT;
            return result;
        }
    } else {
        if (filesystem_format(part1_path, cfg->mode, &fmt_err) < 0) {
            result.success = 0;
            result.error_code = ERR_FORMAT_FAILED;
            snprintf(result.error_message, sizeof(result.error_message), "Format failed: %s", fmt_err.message);
            result.failed_stage = STAGE_FORMAT;
            return result;
        }
    }

    if (cfg->cancel_requested && *cfg->cancel_requested) {
        result.error_code = ERR_CANCELLED; return result;
    }

    report_progress(cfg, STAGE_MOUNT_TARGET, 30, "Mounting target filesystem");

    char *tmp_target = temp_dir_template("target");
    if (!tmp_target || !mkdtemp(tmp_target)) {
        free(tmp_target);
        result.success = 0;
        result.error_code = ERR_MOUNT_FAILED;
        snprintf(result.error_message, sizeof(result.error_message), "Cannot create temp mount point");
        result.failed_stage = STAGE_MOUNT_TARGET;
        return result;
    }
    snprintf(target_mnt, sizeof(target_mnt), "%s", tmp_target);
    free(tmp_target);

    char ntfs_mnt[128] = "";

    if (cfg->mode == FLASH_MODE_GPT_DUAL || cfg->mode == FLASH_MODE_GPT_NTFS) {
        char *tmp_esp = temp_dir_template("esp");
        if (!tmp_esp || !mkdtemp(tmp_esp)) {
            free(tmp_esp);
            cleanup(cfg, NULL, target_mnt);
            result.success = 0;
            result.error_code = ERR_MOUNT_FAILED;
            snprintf(result.error_message, sizeof(result.error_message), "Cannot create ESP mount point");
            result.failed_stage = STAGE_MOUNT_TARGET;
            return result;
        }
        snprintf(target_mnt, sizeof(target_mnt), "%s", tmp_esp);
        free(tmp_esp);

        char *tmp_ntfs = temp_dir_template("ntfs");
        if (!tmp_ntfs || !mkdtemp(tmp_ntfs)) {
            free(tmp_ntfs);
            cleanup(cfg, NULL, target_mnt);
            result.success = 0;
            result.error_code = ERR_MOUNT_FAILED;
            snprintf(result.error_message, sizeof(result.error_message), "Cannot create NTFS mount point");
            result.failed_stage = STAGE_MOUNT_TARGET;
            return result;
        }
        snprintf(ntfs_mnt, sizeof(ntfs_mnt), "%s", tmp_ntfs);
        free(tmp_ntfs);

        if (filesystem_mount(part2_path, ntfs_mnt, NULL) < 0) {
            cleanup(cfg, NULL, target_mnt);
            rmdir(ntfs_mnt);
            result.success = 0;
            result.error_code = ERR_MOUNT_FAILED;
            snprintf(result.error_message, sizeof(result.error_message), "Cannot mount NTFS partition");
            result.failed_stage = STAGE_MOUNT_TARGET;
            return result;
        }
    }

    if (filesystem_mount(part1_path, target_mnt, NULL) < 0) {
        if (ntfs_mnt[0]) filesystem_unmount(ntfs_mnt, NULL);
        cleanup(cfg, NULL, target_mnt);
        if (ntfs_mnt[0]) rmdir(ntfs_mnt);
        result.success = 0;
        result.error_code = ERR_MOUNT_FAILED;
        snprintf(result.error_message, sizeof(result.error_message), "Cannot mount partition %s", part1_path);
        result.failed_stage = STAGE_MOUNT_TARGET;
        return result;
    }

    if (cfg->cancel_requested && *cfg->cancel_requested) {
        result.error_code = ERR_CANCELLED; goto cleanup_all;
    }

    report_progress(cfg, STAGE_MOUNT_ISO, 35, "Mounting ISO");

    char *tmp_iso = temp_dir_template("iso");
    if (!tmp_iso || !mkdtemp(tmp_iso)) {
        free(tmp_iso);
        result.success = 0;
        result.error_code = ERR_ISO_MOUNT_FAILED;
        snprintf(result.error_message, sizeof(result.error_message), "Cannot create ISO mount point");
        result.failed_stage = STAGE_MOUNT_ISO;
        goto cleanup_all;
    }
    snprintf(iso_mnt, sizeof(iso_mnt), "%s", tmp_iso);
    free(tmp_iso);

    if (iso_mount(cfg->iso_path, iso_mnt, NULL) < 0) {
        result.success = 0;
        result.error_code = ERR_ISO_MOUNT_FAILED;
        snprintf(result.error_message, sizeof(result.error_message), "Cannot mount ISO");
        result.failed_stage = STAGE_MOUNT_ISO;
        goto cleanup_all;
    }

    if (cfg->cancel_requested && *cfg->cancel_requested) {
        result.error_code = ERR_CANCELLED; goto cleanup_all;
    }

    char iso_src[256];
    snprintf(iso_src, sizeof(iso_src), "%s/.", iso_mnt);

    char *copy_target;
    if (cfg->mode == FLASH_MODE_GPT_DUAL || cfg->mode == FLASH_MODE_GPT_NTFS) {
        copy_target = ntfs_mnt;
    } else {
        copy_target = target_mnt;
    }

    report_progress(cfg, STAGE_COPY_FILES, 40, "Copying files to USB...");
    CopyProgress cp = { cfg, STAGE_COPY_FILES, 40, 15 };
    if (cp_progress(iso_src, copy_target,
                    cfg->cancel_requested, copy_progress_cb, &cp) < 0) {
        if (cfg->cancel_requested && *cfg->cancel_requested) {
            result.error_code = ERR_CANCELLED; goto cleanup_all;
        }
        result.success = 0;
        result.error_code = ERR_COPY_FAILED;
        snprintf(result.error_message, sizeof(result.error_message), "Failed to copy files to USB");
        result.failed_stage = STAGE_COPY_FILES;
        goto cleanup_all;
    }

    sync();

    if (cfg->cancel_requested && *cfg->cancel_requested) {
        result.error_code = ERR_CANCELLED; goto cleanup_all;
    }

    if (cfg->mode == FLASH_MODE_GPT_DUAL || cfg->mode == FLASH_MODE_GPT_NTFS) {
        report_progress(cfg, STAGE_COPY_FILES, 55, "Setting up ESP...");

        if (cfg->mode == FLASH_MODE_GPT_DUAL) {
            char esp_sources[1024];
            snprintf(esp_sources, sizeof(esp_sources), "%s/sources", target_mnt);
            mkdir_p(esp_sources, 0755);

            char esp_boot_wim[1024];
            snprintf(esp_boot_wim, sizeof(esp_boot_wim), "%s/boot.wim", esp_sources);

            char src_boot_wim[1024];
            if (path_exists(ntfs_mnt)) {
                snprintf(src_boot_wim, sizeof(src_boot_wim), "%s/sources/boot.wim", ntfs_mnt);
            } else {
                snprintf(src_boot_wim, sizeof(src_boot_wim), "%s/sources/boot.wim", iso_mnt);
            }

            if (path_exists(src_boot_wim)) {
                copy_file(src_boot_wim, esp_boot_wim);
            }
        }
    }

    if (cfg->cancel_requested && *cfg->cancel_requested) {
        result.error_code = ERR_CANCELLED; goto cleanup_all;
    }

    if (cfg->iso.needs_wim_split && cfg->mode != FLASH_MODE_GPT_DUAL && cfg->mode != FLASH_MODE_GPT_NTFS) {
        report_progress(cfg, STAGE_SPLIT_WIM, 60, "Splitting large install.wim (this may take a while)");
        if (wim_split_if_needed(iso_mnt, copy_target, cfg->iso.install_wim_size, NULL) < 0) {
            result.success = 0;
            result.error_code = ERR_WIM_SPLIT_FAILED;
            snprintf(result.error_message, sizeof(result.error_message), "Failed to split WIM file");
            result.failed_stage = STAGE_SPLIT_WIM;
            goto cleanup_all;
        }
    }

    if (cfg->cancel_requested && *cfg->cancel_requested) {
        result.error_code = ERR_CANCELLED; goto cleanup_all;
    }

    report_progress(cfg, STAGE_INSTALL_BOOTLOADER, 80, "Installing bootloader");

    if (cfg->mode == FLASH_MODE_GPT_NTFS) {
        bootloader_setup_uefi_ntfs(target_mnt, NULL);
        wim_fix_boot_files(ntfs_mnt, iso_mnt, NULL);
    } else if (cfg->mode == FLASH_MODE_GPT_DUAL) {
        bootloader_setup_efi(iso_mnt, target_mnt, NULL);
        wim_fix_boot_files(target_mnt, iso_mnt, NULL);
    } else if (cfg->mode == FLASH_MODE_MBR_FAT32) {
        bootloader_install(cfg->device_path, copy_target, cfg->mode, NULL);
        bootloader_setup_efi(iso_mnt, copy_target, NULL);
        wim_fix_boot_files(copy_target, iso_mnt, NULL);
    } else {
        bootloader_setup_efi(iso_mnt, copy_target, NULL);
        wim_fix_boot_files(copy_target, iso_mnt, NULL);
    }

    sync();

    report_progress(cfg, STAGE_UNMOUNT_FINAL, 90, "Unmounting filesystems");
    if (ntfs_mnt[0]) filesystem_unmount(ntfs_mnt, NULL);
    if (target_mnt[0]) filesystem_unmount(target_mnt, NULL);
    if (iso_mnt[0]) iso_unmount(iso_mnt, NULL);
    if (ntfs_mnt[0]) rmdir(ntfs_mnt);
    if (target_mnt[0]) rmdir(target_mnt);
    if (iso_mnt[0]) { rmdir(iso_mnt); }
    target_mnt[0] = '\0';
    iso_mnt[0] = '\0';

    report_progress(cfg, STAGE_VERIFY, 95, "Verifying flash");

    char verify_mnt[128] = "";
    char *tmp_verify = temp_dir_template("verify");
    if (tmp_verify && mkdtemp(tmp_verify)) {
        snprintf(verify_mnt, sizeof(verify_mnt), "%s", tmp_verify);
        free(tmp_verify);

        const char *verify_dev = part1_path;
        if (cfg->mode == FLASH_MODE_GPT_DUAL || cfg->mode == FLASH_MODE_GPT_NTFS) {
            verify_dev = part2_path;
        }
        filesystem_mount(verify_dev, verify_mnt, NULL);
    }

    if (verify_mnt[0]) {
        verify_flash(verify_mnt, &result.verify, NULL);
        filesystem_unmount(verify_mnt, NULL);
        rmdir(verify_mnt);
    }

    report_progress(cfg, STAGE_DONE, 100, "Complete!");
    result.success = 1;
    return result;

cleanup_all:
    if (ntfs_mnt[0]) filesystem_unmount(ntfs_mnt, NULL);
    if (target_mnt[0]) filesystem_unmount(target_mnt, NULL);
    if (iso_mnt[0]) iso_unmount(iso_mnt, NULL);
    rmdir(target_mnt);
    rmdir(iso_mnt);
    return result;
}
