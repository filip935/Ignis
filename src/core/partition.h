#ifndef NIHILFLASH_PARTITION_H
#define NIHILFLASH_PARTITION_H

#include "../util/error.h"
#include <stdint.h>

typedef enum {
    FLASH_MODE_MBR_FAT32,
    FLASH_MODE_GPT_FAT32,
    FLASH_MODE_GPT_DUAL,
    FLASH_MODE_GPT_NTFS,
    FLASH_MODE_RAW_DD
} FlashMode;

const char *flash_mode_name(FlashMode mode);
const char *flash_mode_description(FlashMode mode);

static inline int flash_mode_count(void) { return 5; }

int partition_wipe(const char *device_path, Error *err);
int partition_create(const char *device_path, FlashMode mode, Error *err);

#endif
