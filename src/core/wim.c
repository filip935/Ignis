#define _GNU_SOURCE
#include "wim.h"
#include "../util/util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

int wim_split_if_needed(const char *source_dir, const char *target_dir, uint64_t wim_size, Error *err) {
    (void)wim_size;

    char source_wim[1024];
    snprintf(source_wim, sizeof(source_wim), "%s/sources/install.wim", source_dir);

    if (!path_exists(source_wim)) {
        if (err) err->code = ERR_SUCCESS;
        return 0;
    }

    struct stat st;
    if (stat(source_wim, &st) < 0) {
        if (err) err->code = ERR_SUCCESS;
        return 0;
    }

    if ((unsigned long long)st.st_size < 4000000000ULL) {
        if (err) err->code = ERR_SUCCESS;
        return 0;
    }

    char target_wim[1024];
    snprintf(target_wim, sizeof(target_wim), "%s/sources/install.wim", target_dir);

    if (path_exists(target_wim)) {
        unlink(target_wim);
    }

    ExecResult res;
    memset(&res, 0, sizeof(res));
    int ret = exec_cmd(&res, err, "wimlib-imagex", "split",
                       source_wim,
                       target_wim,
                       "4000", NULL);
    if (ret < 0) {
        ERR_RETURN(err, ERR_WIM_SPLIT_FAILED,
                  "Failed to split install.wim (size: %lu bytes). wimlib-imagex returned: %s",
                  (unsigned long)st.st_size,
                  res.stderr_buf ? res.stderr_buf : "unknown error");
    }
    exec_result_free(&res);

    if (err) err->code = ERR_SUCCESS;
    return 0;
}

static int find_bootmgfw(const char *mount_point, char *out, size_t out_size) {
    const char *paths[] = {
        "/efi/microsoft/boot/bootmgfw.efi",
        "/EFI/Microsoft/Boot/bootmgfw.efi",
        "/efi/microsoft/boot/bootmgr.efi",
        "/sources/boot.wim",
        "/sources/install.wim",
        "/sources/install.esd",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        char p[1024];
        snprintf(p, sizeof(p), "%s%s", mount_point, paths[i]);
        struct stat st;
        if (stat(p, &st) == 0 && st.st_size > 0) {
            snprintf(out, out_size, "%s", p);
            return 1;
        }
    }
    return 0;
}

static int ensure_efi_file(const char *src, const char *target_path) {
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", target_path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    mkdir_p(dir, 0755);

    struct stat st;
    if (stat(target_path, &st) == 0 && st.st_size > 0) return 0;

    unlink(target_path);
    exec_cmd(NULL, NULL, "cp", src, target_path, NULL);

    if (stat(target_path, &st) == 0 && st.st_size > 0) return 1;
    return 0;
}

int wim_fix_boot_files(const char *target, const char *iso_mnt, Error *err) {
    wim_fix_bootx64(target, NULL);

    if (!iso_mnt || !path_exists(iso_mnt)) {
        if (err) err->code = ERR_SUCCESS;
        return 0;
    }

    const char *pairs[][2] = {
        {"/efi/microsoft/boot/bcd", "/EFI/Microsoft/Boot/BCD"},
        {"/EFI/Microsoft/Boot/bcd", "/EFI/Microsoft/Boot/BCD"},
        {"/EFI/Microsoft/Boot/BCD", "/EFI/Microsoft/Boot/BCD"},
        {"/efi/microsoft/boot/boot.stl", "/EFI/Microsoft/Boot/boot.stl"},
        {"/efi/microsoft/boot/bootmgfw.efi", "/EFI/Microsoft/Boot/bootmgfw.efi"},
        {NULL, NULL}
    };

    for (int i = 0; pairs[i][0]; i++) {
        char src[1024], dst[1024];
        snprintf(src, sizeof(src), "%s%s", iso_mnt, pairs[i][0]);
        snprintf(dst, sizeof(dst), "%s%s", target, pairs[i][1]);
        if (!path_exists(src)) continue;
        struct stat st;
        if (stat(dst, &st) == 0 && st.st_size > 0) continue;

        char dir[1024];
        snprintf(dir, sizeof(dir), "%s", dst);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
        mkdir_p(dir, 0755);

        unlink(dst);
        exec_cmd(NULL, NULL, "cp", src, dst, NULL);
    }

    if (err) err->code = ERR_SUCCESS;
    return 0;
}

int wim_fix_bootx64(const char *mount_point, Error *err) {
    char src[1024] = "";
    find_bootmgfw(mount_point, src, sizeof(src));

    if (src[0] && (strstr(src, ".wim") || strstr(src, ".esd"))) {
        char tmp_dir[] = "/tmp/ignis-efi-fix-XXXXXX";
        if (mkdtemp(tmp_dir)) {
            ExecResult res;
            if (exec_cmd(&res, NULL, "wimlib-imagex", "extract",
                        src, "1",
                        "/Windows/Boot/EFI/bootmgfw.efi",
                        "--dest-dir", tmp_dir, NULL) == 0) {
                char ext[1024];
                snprintf(ext, sizeof(ext), "%s/bootmgfw.efi", tmp_dir);
                if (!path_exists(ext))
                    snprintf(ext, sizeof(ext), "%s/Windows/Boot/EFI/bootmgfw.efi", tmp_dir);
                if (path_exists(ext))
                    snprintf(src, sizeof(src), "%s", ext);
            }
            exec_result_free(&res);
        }
    }

    if (!src[0] || !path_exists(src)) {
        if (err) err->code = ERR_SUCCESS;
        return 0;
    }

    const char *targets[] = {
        "/EFI/BOOT/BOOTX64.EFI",
        "/efi/boot/bootx64.efi",
        NULL
    };
    for (int i = 0; targets[i]; i++) {
        char t[1024];
        snprintf(t, sizeof(t), "%s%s", mount_point, targets[i]);
        ensure_efi_file(src, t);
    }

    if (err) err->code = ERR_SUCCESS;
    return 0;
}
