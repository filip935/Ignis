#ifndef IGNIS_FILESYSTEM_H
#define IGNIS_FILESYSTEM_H

#include "../util/error.h"
#include "partition.h"

#define IGNIS_MOUNT_BASE "/tmp/ignis-mnt"

int filesystem_format(const char *partition_path, FlashMode mode, Error *err);
int filesystem_format_ntfs(const char *partition_path, Error *err);
int filesystem_mount(const char *partition_path, const char *mount_point, Error *err);
int filesystem_unmount(const char *mount_point, Error *err);

#endif
