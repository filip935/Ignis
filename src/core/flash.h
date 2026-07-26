#ifndef IGNIS_FLASH_H
#define IGNIS_FLASH_H

#include "../util/error.h"
#include "device.h"
#include "iso.h"
#include "partition.h"
#include "verify.h"

typedef enum {
    STAGE_NONE = 0,
    STAGE_UNMOUNT,
    STAGE_WIPE,
    STAGE_PARTITION,
    STAGE_FORMAT,
    STAGE_MOUNT_TARGET,
    STAGE_MOUNT_ISO,
    STAGE_COPY_FILES,
    STAGE_SPLIT_WIM,
    STAGE_FIX_BOOTLOADER,
    STAGE_INSTALL_BOOTLOADER,
    STAGE_UNMOUNT_FINAL,
    STAGE_VERIFY,
    STAGE_DONE,
    STAGE_FAILED
} FlashStage;

typedef struct {
    FlashMode mode;
    char device_path[256];
    char iso_path[1024];
    Device device;
    IsoInfo iso;
    int (*progress_cb)(FlashStage stage, int percent, const char *status, void *user_data);
    void *progress_user_data;
    volatile int *cancel_requested;
} FlashConfig;

typedef struct {
    int success;
    ErrorCode error_code;
    char error_message[2048];
    FlashStage failed_stage;
    VerifyResult verify;
} FlashResult;

FlashResult flash_run(FlashConfig *config);

#endif
