#ifndef IGNIS_DEVICE_H
#define IGNIS_DEVICE_H

#include "../util/error.h"
#include <stdint.h>

typedef struct {
    char device_path[256];
    char model[256];
    char serial[128];
    uint64_t size_bytes;
    int is_removable;
    int is_system_disk;
} Device;

int device_list(Device **devices, int *count, Error *err);
void device_list_free(Device *devices, int count);
int device_find_by_path(Device *dev, const char *path, Error *err);
int device_unmount_all(const char *device_path, Error *err);
int device_is_busy(const char *device_path, Error *err);

#endif
