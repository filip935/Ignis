#ifndef NIHILFLASH_UTIL_H
#define NIHILFLASH_UTIL_H

#include "error.h"
#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>
#include <dirent.h>

typedef struct {
    int exit_code;
    char *stdout_buf;
    char *stderr_buf;
    size_t stdout_len;
    size_t stderr_len;
} ExecResult;

int exec_cmd(ExecResult *out, Error *err, const char *cmd, ...);
int exec_cmd_vargs(ExecResult *out, Error *err, const char *cmd, va_list args);
void exec_result_free(ExecResult *res);
int path_exists(const char *path);
int mkdir_p(const char *path, mode_t mode);
char *temp_dir_template(const char *prefix);
int wait_for_device(const char *path, int timeout_ms);

int rm_rf(const char *path);
int copy_file(const char *src, const char *dst);
int cp_count(const char *path);
int cp_progress(const char *src, const char *dst,
                volatile int *cancel,
                void (*progress_cb)(int percent, void *user_data),
                void *progress_user);

#endif
