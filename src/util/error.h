#ifndef IGNIS_ERROR_H
#define IGNIS_ERROR_H

typedef enum {
    ERR_SUCCESS = 0,
    ERR_DEVICE_NOT_FOUND,
    ERR_DEVICE_NOT_REMOVABLE,
    ERR_DEVICE_BUSY,
    ERR_DEVICE_IS_SYSTEM,
    ERR_DEVICE_NO_PERMISSION,
    ERR_ISO_INVALID,
    ERR_ISO_NOT_FOUND,
    ERR_ISO_MOUNT_FAILED,
    ERR_PARTITION_FAILED,
    ERR_FORMAT_FAILED,
    ERR_MOUNT_FAILED,
    ERR_COPY_FAILED,
    ERR_WIM_SPLIT_FAILED,
    ERR_BOOTLOADER_FAILED,
    ERR_VERIFY_FAILED,
    ERR_CANCELLED,
    ERR_OUT_OF_MEMORY,
    ERR_PERMISSION_DENIED,
    ERR_UNKNOWN
} ErrorCode;

typedef struct {
    ErrorCode code;
    char message[2048];
} Error;

void error_set(Error *err, ErrorCode code, const char *fmt, ...);
const char *error_code_string(ErrorCode code);

#define ERR_RETURN(err, code, fmt, ...) do { \
    if (err) { error_set(err, code, fmt, ##__VA_ARGS__); } \
    return -1; \
} while (0)

#define ERR_RETURN_OK(err) do { \
    if (err) { err->code = ERR_SUCCESS; err->message[0] = '\0'; } \
    return 0; \
} while (0)

#endif
