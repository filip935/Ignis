#define _GNU_SOURCE
#include "device.h"
#include "../util/util.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>
#include <mntent.h>
#include <errno.h>

#define SYS_BLOCK "/sys/block"
#define DEV_DIR "/dev"

static int read_sysfs_string(const char *path, char *buf, size_t buf_size) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, buf_size, f)) { fclose(f); return -1; }
    fclose(f);
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == ' ')) buf[--len] = '\0';
    return 0;
}

static int is_system_disk(const char *devname) {
    char path[512];
    snprintf(path, sizeof(path), "/sys/block/%s", devname);

    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return 0;

    struct mntent *ent;
    int is_sys = 0;
    while ((ent = getmntent(f)) != NULL) {
        if (strcmp(ent->mnt_dir, "/") == 0 || strcmp(ent->mnt_dir, "/boot") == 0) {
            if (strstr(ent->mnt_fsname, devname)) {
                is_sys = 1;
                break;
            }
        }
    }
    endmntent(f);
    return is_sys;
}

int device_list(Device **devices, int *count, Error *err) {
    DIR *dir = opendir(SYS_BLOCK);
    if (!dir) ERR_RETURN(err, ERR_UNKNOWN, "Cannot open %s: %s", SYS_BLOCK, strerror(errno));

    int cap = 32;
    int n = 0;
    *devices = malloc(cap * sizeof(Device));
    if (!*devices) { closedir(dir); ERR_RETURN(err, ERR_OUT_OF_MEMORY, "Out of memory"); }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR && entry->d_type != DT_LNK) continue;
        if (entry->d_name[0] == '.') continue;

        if (strncmp(entry->d_name, "sd", 2) != 0 && strncmp(entry->d_name, "nvme", 4) != 0 && strncmp(entry->d_name, "mmcblk", 6) != 0)
            continue;

        if (strchr(entry->d_name, 'p') != NULL && strncmp(entry->d_name, "nvme", 4) == 0) continue;
        if (strchr(entry->d_name, 'p') != NULL && strncmp(entry->d_name, "mmcblk", 6) == 0) continue;

        char removable_path[512];
        snprintf(removable_path, sizeof(removable_path), "/sys/block/%s/removable", entry->d_name);
        char removable_str[8];
        int removable = 0;
        if (read_sysfs_string(removable_path, removable_str, sizeof(removable_str)) == 0) {
            removable = (removable_str[0] == '1');
        }

        char size_path[512];
        snprintf(size_path, sizeof(size_path), "/sys/block/%s/size", entry->d_name);
        char size_str[32];
        uint64_t sectors = 0;
        if (read_sysfs_string(size_path, size_str, sizeof(size_str)) == 0) {
            sectors = strtoull(size_str, NULL, 10);
        }
        uint64_t size_bytes = sectors * 512;

        if (size_bytes < 2ULL * 1024 * 1024 * 1024) continue;

        Device *d = &(*devices)[n];
        memset(d, 0, sizeof(Device));
        snprintf(d->device_path, sizeof(d->device_path), "%s/%s", DEV_DIR, entry->d_name);
        d->size_bytes = size_bytes;
        d->is_removable = removable;
        d->is_system_disk = is_system_disk(entry->d_name);

        char model_path[512];
        snprintf(model_path, sizeof(model_path), "/sys/block/%s/device/model", entry->d_name);
        read_sysfs_string(model_path, d->model, sizeof(d->model));

        char vendor_path[512];
        snprintf(vendor_path, sizeof(vendor_path), "/sys/block/%s/device/vendor", entry->d_name);
        char vendor[64] = "";
        read_sysfs_string(vendor_path, vendor, sizeof(vendor));
        if (vendor[0] && d->model[0]) {
            char tmp[256];
            snprintf(tmp, sizeof(tmp), "%s %s", vendor, d->model);
            snprintf(d->model, sizeof(d->model), "%s", tmp);
        }

        snprintf(model_path, sizeof(model_path), "/sys/block/%s/serial", entry->d_name);
        read_sysfs_string(model_path, d->serial, sizeof(d->serial));

        n++;
        if (n >= cap) {
            cap *= 2;
            Device *tmp = realloc(*devices, cap * sizeof(Device));
            if (!tmp) { free(*devices); closedir(dir); ERR_RETURN(err, ERR_OUT_OF_MEMORY, "Out of memory"); }
            *devices = tmp;
        }
    }
    closedir(dir);
    *count = n;
    if (err) err->code = ERR_SUCCESS;
    return 0;
}

void device_list_free(Device *devices, int count) {
    (void)count;
    free(devices);
}

int device_find_by_path(Device *dev, const char *path, Error *err) {
    char resolved[256];
    if (!realpath(path, resolved)) {
        ERR_RETURN(err, ERR_DEVICE_NOT_FOUND, "Cannot resolve path %s: %s", path, strerror(errno));
    }

    Device *list = NULL;
    int count = 0;
    if (device_list(&list, &count, err) < 0) return -1;

    int found = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(list[i].device_path, resolved) == 0) {
            *dev = list[i];
            found = 1;
            break;
        }
    }

    free(list);
    if (!found) ERR_RETURN(err, ERR_DEVICE_NOT_FOUND, "Device %s not found", path);
    return 0;
}

int device_unmount_all(const char *device_path, Error *err) {
    char devname[256];
    const char *p = strrchr(device_path, '/');
    if (!p) ERR_RETURN(err, ERR_UNKNOWN, "Invalid device path: %s", device_path);
    p++;
    snprintf(devname, sizeof(devname), "%s", p);

    FILE *f = setmntent("/proc/mounts", "r");
    if (!f) return 0;

    struct mntent *ent;
    int ret = 0;
    while ((ent = getmntent(f)) != NULL) {
        if (strstr(ent->mnt_fsname, devname)) {
            if (umount(ent->mnt_dir) < 0 && umount2(ent->mnt_dir, MNT_FORCE) < 0) {
                ret = -1;
                if (err && err->code == ERR_SUCCESS)
                    error_set(err, ERR_DEVICE_BUSY, "Cannot unmount %s: %s", ent->mnt_dir, strerror(errno));
            }
        }
    }
    endmntent(f);
    return ret;
}

int device_is_busy(const char *device_path, Error *err) {
    char devname[256];
    const char *p = strrchr(device_path, '/');
    if (!p) { if (err) err->code = ERR_SUCCESS; return 0; }
    p++;
    snprintf(devname, sizeof(devname), "%s", p);

    FILE *f = setmntent("/proc/mounts", "r");
    if (!f) return 0;

    struct mntent *ent;
    while ((ent = getmntent(f)) != NULL) {
        if (strstr(ent->mnt_fsname, devname)) {
            endmntent(f);
            return 1;
        }
    }
    endmntent(f);
    return 0;
}
