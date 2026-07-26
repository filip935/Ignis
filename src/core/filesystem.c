#include "filesystem.h"
#include "../util/util.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int filesystem_format(const char *partition_path, FlashMode mode, Error *err) {
    switch (mode) {
        case FLASH_MODE_MBR_FAT32:
        case FLASH_MODE_GPT_FAT32: {
            ExecResult res;
            int ret = exec_cmd(&res, err, "mkfs.vfat", "-F32", "-n", "IGNIS", partition_path, NULL);
            exec_result_free(&res);
            if (ret < 0) ERR_RETURN(err, ERR_FORMAT_FAILED, "Failed to format %s as FAT32", partition_path);
            break;
        }
        case FLASH_MODE_GPT_DUAL: {
            ExecResult res;
            int ret = exec_cmd(&res, err, "mkfs.vfat", "-F32", "-n", "WINBOOT", partition_path, NULL);
            exec_result_free(&res);
            if (ret < 0) ERR_RETURN(err, ERR_FORMAT_FAILED, "Failed to format ESP partition as FAT32");
            break;
        }
        default:
            ERR_RETURN(err, ERR_UNKNOWN, "Unknown flash mode");
    }

    if (err) err->code = ERR_SUCCESS;
    return 0;
}

int filesystem_format_ntfs(const char *partition_path, Error *err) {
    ExecResult res;
    int ret = exec_cmd(&res, err, "mkfs.ntfs", "-Q", "-L", "WINDATA", partition_path, NULL);
    exec_result_free(&res);
    if (ret < 0) ERR_RETURN(err, ERR_FORMAT_FAILED, "Failed to format %s as NTFS", partition_path);
    if (err) err->code = ERR_SUCCESS;
    return 0;
}

int filesystem_mount(const char *partition_path, const char *mount_point, Error *err) {
    if (!path_exists(mount_point)) {
        mkdir_p(mount_point, 0755);
    }

    ExecResult res;
    int ret = exec_cmd(&res, err, "mount", partition_path, mount_point, NULL);
    exec_result_free(&res);
    if (ret < 0) {
        ExecResult res2;
        ret = exec_cmd(&res2, err, "mount", "-t", "ntfs-3g",
                       "-o", "force", partition_path, mount_point, NULL);
        exec_result_free(&res2);
    }
    if (ret < 0) ERR_RETURN(err, ERR_MOUNT_FAILED, "Failed to mount %s to %s", partition_path, mount_point);

    if (err) err->code = ERR_SUCCESS;
    return 0;
}

int filesystem_unmount(const char *mount_point, Error *err) {
    if (!path_exists(mount_point)) { if (err) err->code = ERR_SUCCESS; return 0; }
    int ret = exec_cmd(NULL, err, "umount", mount_point, NULL);
    if (ret < 0) {
        ret = exec_cmd(NULL, NULL, "umount", "-l", mount_point, NULL);
    }
    rmdir(mount_point);
    return ret;
}
