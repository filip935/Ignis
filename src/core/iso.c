#define _GNU_SOURCE
#include "iso.h"
#include "../util/util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

int iso_parse(const char *path, IsoInfo *info, Error *err) {
    memset(info, 0, sizeof(*info));
    snprintf(info->iso_path, sizeof(info->iso_path), "%s", path);

    if (!path_exists(path)) ERR_RETURN(err, ERR_ISO_NOT_FOUND, "ISO file not found: %s", path);

    struct stat st;
    if (stat(path, &st) < 0) ERR_RETURN(err, ERR_ISO_NOT_FOUND, "Cannot stat %s: %s", path, strerror(errno));
    if (!S_ISREG(st.st_mode)) ERR_RETURN(err, ERR_ISO_INVALID, "%s is not a regular file", path);

    char *tmp_dir = temp_dir_template("iso-probe");
    if (!tmp_dir) ERR_RETURN(err, ERR_OUT_OF_MEMORY, "Out of memory");

    if (!mkdtemp(tmp_dir)) { free(tmp_dir); ERR_RETURN(err, ERR_UNKNOWN, "Cannot create temp dir: %s", strerror(errno)); }

    if (iso_mount(path, tmp_dir, err) < 0) {
        rmdir(tmp_dir);
        free(tmp_dir);
        return -1;
    }

    char sources_path[1080];
    snprintf(sources_path, sizeof(sources_path), "%s/sources", tmp_dir);

    struct stat sources_st;
    int has_sources = (stat(sources_path, &sources_st) == 0 && S_ISDIR(sources_st.st_mode));

    char wim_path[1080];
    char esd_path[1080];
    snprintf(wim_path, sizeof(wim_path), "%s/sources/install.wim", tmp_dir);
    snprintf(esd_path, sizeof(esd_path), "%s/sources/install.esd", tmp_dir);

    info->has_install_wim = path_exists(wim_path);
    info->has_install_esd = path_exists(esd_path);

    if (has_sources && (info->has_install_wim || info->has_install_esd)) {
        info->type = ISO_TYPE_WINDOWS;

        if (info->has_install_wim) {
            struct stat wim_st;
            stat(wim_path, &wim_st);
            info->install_wim_size = wim_st.st_size;
            info->needs_wim_split = ((unsigned long long)wim_st.st_size > 4000000000ULL);
        }

        char efi_boot_path[1080];
        snprintf(efi_boot_path, sizeof(efi_boot_path), "%s/efi/boot/bootx64.efi", tmp_dir);

        if (path_exists(efi_boot_path)) {
            info->bootx64_efi_exists = 1;
            struct stat efi_st;
            stat(efi_boot_path, &efi_st);
            info->bootx64_efi_zero = (efi_st.st_size == 0);
        }

        snprintf(info->version, sizeof(info->version), "Windows");
        char setup_ini[1080];
        snprintf(setup_ini, sizeof(setup_ini), "%s/sources/setup.exe", tmp_dir);
        if (path_exists(setup_ini)) {
            ExecResult res;
            memset(&res, 0, sizeof(res));
            if (exec_cmd(&res, NULL, "sh", "-c",
                        "strings '", tmp_dir, "/sources/setup.exe' 2>/dev/null | grep -oP 'Microsoft Windows [0-9]+' | head -1",
                        NULL) == 0 && res.stdout_buf) {
                char *nl = strchr(res.stdout_buf, '\n');
                if (nl) *nl = '\0';
                if (res.stdout_buf[0]) {
                    snprintf(info->version, sizeof(info->version), "%s", res.stdout_buf);
                }
            }
            exec_result_free(&res);
        }
    } else {
        char isolinux_path[1080];
        snprintf(isolinux_path, sizeof(isolinux_path), "%s/isolinux/isolinux.bin", tmp_dir);
        char boot_vmlinuz[1080];
        snprintf(boot_vmlinuz, sizeof(boot_vmlinuz), "%s/boot/vmlinuz", tmp_dir);
        char disk_info[1080];
        snprintf(disk_info, sizeof(disk_info), "%s/.disk/info", tmp_dir);
        char liveos[1080];
        snprintf(liveos, sizeof(liveos), "%s/LiveOS", tmp_dir);
        if (path_exists(isolinux_path) || path_exists(boot_vmlinuz) ||
            path_exists(disk_info) || path_exists(liveos)) {
            info->type = ISO_TYPE_LINUX;
            snprintf(info->version, sizeof(info->version), "Linux");
            if (path_exists(disk_info)) {
                ExecResult res;
                memset(&res, 0, sizeof(res));
                if (exec_cmd(&res, NULL, "head", "-1", disk_info, NULL) == 0 && res.stdout_buf) {
                    char *nl = strchr(res.stdout_buf, '\n');
                    if (nl) *nl = '\0';
                    if (res.stdout_buf[0])
                        snprintf(info->version, sizeof(info->version), "%s", res.stdout_buf);
                }
                exec_result_free(&res);
            }
        } else {
            char freebsd_boot[1080];
            snprintf(freebsd_boot, sizeof(freebsd_boot), "%s/boot/loader", tmp_dir);
            char freebsd_dist[1080];
            snprintf(freebsd_dist, sizeof(freebsd_dist), "%s/usr/freebsd-dist", tmp_dir);
            char openbsd_bsd[1080];
            snprintf(openbsd_bsd, sizeof(openbsd_bsd), "%s/bsd", tmp_dir);

            if (path_exists(freebsd_boot) || path_exists(freebsd_dist) || path_exists(openbsd_bsd)) {
                info->type = ISO_TYPE_BSD;
                snprintf(info->version, sizeof(info->version), "BSD");
            } else {
                info->type = ISO_TYPE_UNKNOWN;
                snprintf(info->version, sizeof(info->version), "Unknown ISO");
            }
        }
    }

    info->valid = 1;

    iso_unmount(tmp_dir, NULL);
    rmdir(tmp_dir);
    free(tmp_dir);

    if (err) err->code = ERR_SUCCESS;
    return 0;
}

int iso_mount(const char *path, const char *mount_point, Error *err) {
    if (!path_exists(mount_point)) {
        if (mkdir_p(mount_point, 0755) < 0 && errno != EEXIST)
            ERR_RETURN(err, ERR_MOUNT_FAILED, "Cannot create mount point %s: %s", mount_point, strerror(errno));
    }

    if (exec_cmd(NULL, NULL, "mount", "-o", "loop,ro", path, mount_point, NULL) < 0) {
        ERR_RETURN(err, ERR_ISO_MOUNT_FAILED, "Failed to mount ISO %s to %s", path, mount_point);
    }
    if (err) err->code = ERR_SUCCESS;
    return 0;
}

int iso_unmount(const char *mount_point, Error *err) {
    int ret = exec_cmd(NULL, err, "umount", mount_point, NULL);
    return ret;
}

int iso_copy_files(const char *src_mount, const char *dst_mount, Error *err) {
    ExecResult res;
    int ret = exec_cmd(&res, err, "cp", "-a", src_mount, dst_mount, NULL);
    exec_result_free(&res);
    return ret;
}
