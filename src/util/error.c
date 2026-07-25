#include "error.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void error_set(Error *err, ErrorCode code, const char *fmt, ...) {
    if (!err) return;
    err->code = code;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, args);
    va_end(args);
}

const char *error_code_string(ErrorCode code) {
    switch (code) {
        case ERR_SUCCESS: return "Success";
        case ERR_DEVICE_NOT_FOUND: return "Device not found";
        case ERR_DEVICE_NOT_REMOVABLE: return "Device is not removable";
        case ERR_DEVICE_BUSY: return "Device is busy";
        case ERR_DEVICE_IS_SYSTEM: return "Device is the system disk";
        case ERR_DEVICE_NO_PERMISSION: return "No permission to access device";
        case ERR_ISO_INVALID: return "Invalid or unsupported ISO file";
        case ERR_ISO_NOT_FOUND: return "ISO file not found";
        case ERR_ISO_MOUNT_FAILED: return "Failed to mount ISO";
        case ERR_PARTITION_FAILED: return "Partition creation failed";
        case ERR_FORMAT_FAILED: return "Filesystem format failed";
        case ERR_MOUNT_FAILED: return "Mount failed";
        case ERR_COPY_FAILED: return "File copy failed";
        case ERR_WIM_SPLIT_FAILED: return "WIM splitting failed";
        case ERR_BOOTLOADER_FAILED: return "Bootloader installation failed";
        case ERR_VERIFY_FAILED: return "Verification failed";
        case ERR_CANCELLED: return "Operation cancelled";
        case ERR_OUT_OF_MEMORY: return "Out of memory";
        case ERR_PERMISSION_DENIED: return "Permission denied (run as root)";
        case ERR_UNKNOWN: return "Unknown error";
        default: return "Unknown error code";
    }
}
