#ifndef NIHILFLASH_WIM_H
#define NIHILFLASH_WIM_H

#include "../util/error.h"
#include <stdint.h>

int wim_split_if_needed(const char *source_dir, const char *target_dir, uint64_t wim_size, Error *err);
int wim_fix_bootx64(const char *mount_point, Error *err);
int wim_fix_boot_files(const char *target, const char *iso_mnt, Error *err);

#endif
