#define _GNU_SOURCE
#include "partition.h"
#include "../util/util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

const char *flash_mode_name(FlashMode mode) {
    switch (mode) {
        case FLASH_MODE_MBR_FAT32: return "MBR+FAT32";
        case FLASH_MODE_GPT_FAT32: return "GPT+FAT32";
        case FLASH_MODE_GPT_DUAL:  return "GPT+Dual";
        case FLASH_MODE_GPT_NTFS:  return "GPT+NTFS";
        case FLASH_MODE_RAW_DD:    return "Raw DD";
        default: return "Unknown";
    }
}

const char *flash_mode_description(FlashMode mode) {
    switch (mode) {
        case FLASH_MODE_MBR_FAT32:
            return "MBR partition table, FAT32 filesystem. Supports legacy BIOS and UEFI boot. "
                   "WIM splitting may be required for large install.wim files.";
        case FLASH_MODE_GPT_FAT32:
            return "GPT partition table, FAT32 filesystem (acts as ESP). UEFI boot only. "
                   "WIM splitting may be required for large install.wim files.";
    case FLASH_MODE_GPT_DUAL:
        return "GPT partition table with dual partitions: FAT32 (1 GiB) for boot + NTFS for data. "
               "No WIM splitting needed. UEFI boot only.";
    case FLASH_MODE_GPT_NTFS:
        return "GPT partition table with NTFS for data + embedded UEFI:NTFS shim on ESP. "
               "No WIM splitting needed. UEFI only, Secure Boot compatible. (Requires ~8 MiB ESP)";
    case FLASH_MODE_RAW_DD:
        return "Raw dd write. Wipes entire device and writes ISO directly. "
               "Recommended for Linux and BSD ISOs.";
    default: return "";
    }
}

int partition_wipe(const char *device_path, Error *err) {
    if (exec_cmd(NULL, err, "wipefs", "-a", device_path, NULL) < 0) {
        if (err) error_set(err, ERR_PARTITION_FAILED, "Failed to wipe partition table on %s", device_path);
        return -1;
    }
    usleep(500000);
    if (exec_cmd(NULL, NULL, "blockdev", "--rereadpt", device_path, NULL) < 0) {
    }
    if (err) err->code = ERR_SUCCESS;
    return 0;
}

int partition_create(const char *device_path, FlashMode mode, Error *err) {
    if (partition_wipe(device_path, err) < 0) return -1;

    char devname[256];
    const char *p = strrchr(device_path, '/');
    if (!p) ERR_RETURN(err, ERR_UNKNOWN, "Invalid device path");
    p++;
    snprintf(devname, sizeof(devname), "%s", p);

    switch (mode) {
        case FLASH_MODE_RAW_DD:
            return 0;
        case FLASH_MODE_MBR_FAT32: {
            ExecResult res;
            memset(&res, 0, sizeof(res));
            int ret = exec_cmd(&res, err, "parted", "-s", "-a", "optimal",
                              device_path, "mklabel", "msdos",
                              "mkpart", "primary", "fat32", "1MiB", "100%",
                              "set", "1", "boot", "on", NULL);
            exec_result_free(&res);
            if (ret < 0) ERR_RETURN(err, ERR_PARTITION_FAILED, "Failed to create MBR partition on %s", device_path);
            break;
        }
        case FLASH_MODE_GPT_FAT32: {
            ExecResult res;
            memset(&res, 0, sizeof(res));
            int ret = exec_cmd(&res, err, "parted", "-s", "-a", "optimal",
                              device_path, "mklabel", "gpt",
                              "mkpart", "EFI", "fat32", "1MiB", "100%",
                              "set", "1", "esp", "on", NULL);
            exec_result_free(&res);
            if (ret < 0) ERR_RETURN(err, ERR_PARTITION_FAILED, "Failed to create GPT partition on %s", device_path);
            break;
        }
        case FLASH_MODE_GPT_DUAL: {
            ExecResult res;
            memset(&res, 0, sizeof(res));
            int ret = exec_cmd(&res, err, "parted", "-s", "-a", "optimal",
                              device_path, "mklabel", "gpt",
                              "mkpart", "EFI", "fat32", "1MiB", "1025MiB",
                              "mkpart", "Windows", "ntfs", "1025MiB", "100%",
                              "set", "1", "esp", "on", NULL);
            exec_result_free(&res);
            if (ret < 0) ERR_RETURN(err, ERR_PARTITION_FAILED, "Failed to create dual partition on %s", device_path);
            break;
        }
        case FLASH_MODE_GPT_NTFS: {
            ExecResult res;
            memset(&res, 0, sizeof(res));
            int ret = exec_cmd(&res, err, "parted", "-s", "-a", "optimal",
                              device_path, "mklabel", "gpt",
                              "mkpart", "ESP", "fat32", "1MiB", "9MiB",
                              "mkpart", "Windows", "ntfs", "9MiB", "100%",
                              "set", "1", "esp", "on", NULL);
            exec_result_free(&res);
            if (ret < 0) ERR_RETURN(err, ERR_PARTITION_FAILED, "Failed to create GPT+NTFS partition on %s", device_path);
            break;
        }
    }

    sync();
    usleep(1000000);
    exec_cmd(NULL, NULL, "blockdev", "--rereadpt", device_path, NULL);
    exec_cmd(NULL, NULL, "partprobe", device_path, NULL);
    usleep(1000000);

    char part1[512];
    if (strncmp(devname, "nvme", 4) == 0 || strncmp(devname, "mmcblk", 6) == 0)
        snprintf(part1, sizeof(part1), "%sp1", device_path);
    else
        snprintf(part1, sizeof(part1), "%s1", device_path);

    if (wait_for_device(part1, 10000) < 0) {
        ERR_RETURN(err, ERR_PARTITION_FAILED, "Device %s did not appear after partitioning", part1);
    }

    if (mode == FLASH_MODE_GPT_DUAL || mode == FLASH_MODE_GPT_NTFS) {
        char part2[512];
        if (strncmp(devname, "nvme", 4) == 0 || strncmp(devname, "mmcblk", 6) == 0)
            snprintf(part2, sizeof(part2), "%sp2", device_path);
        else
            snprintf(part2, sizeof(part2), "%s2", device_path);
        if (wait_for_device(part2, 10000) < 0) {
            ERR_RETURN(err, ERR_PARTITION_FAILED, "Device %s did not appear after partitioning", part2);
        }
    }

    if (err) err->code = ERR_SUCCESS;
    return 0;
}
