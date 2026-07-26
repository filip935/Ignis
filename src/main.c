#include "core/device.h"
#include "core/iso.h"
#include "core/partition.h"
#include "core/flash.h"
#include "util/error.h"
#include "util/util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <signal.h>

static volatile int cancel_flag = 0;

static void sigint_handler(int sig) {
    (void)sig;
    cancel_flag = 1;
    fprintf(stderr, "\nCancel requested...\n");
}

static int progress_callback(FlashStage stage, int percent, const char *status, void *user_data) {
    (void)stage;
    (void)user_data;

    if (cancel_flag) return -1;

    printf("\r[%3d%%] %-60s", percent, status);
    fflush(stdout);

    if (percent == 100) printf("\n");

    return 0;
}

static void print_devices(void) {
    Device *devices = NULL;
    int count = 0;
    Error err;

    if (device_list(&devices, &count, &err) < 0) {
        fprintf(stderr, "Error listing devices: %s\n", err.message);
        return;
    }

    if (count == 0) {
        printf("No USB devices found.\n");
        free(devices);
        return;
    }

    printf("\nAvailable devices:\n");
    printf("%-12s %-30s %12s  %s\n", "Device", "Model", "Size", "Type");
    printf("%-12s %-30s %12s  %s\n", "------", "-----", "----", "----");

    for (int i = 0; i < count; i++) {
        char size_str[32];
        double size = (double)devices[i].size_bytes;
        const char *unit = "B";
        if (size > 1024*1024*1024ULL) { size /= 1024*1024*1024; unit = "GB"; }
        else if (size > 1024*1024) { size /= 1024*1024; unit = "MB"; }
        else if (size > 1024) { size /= 1024; unit = "KB"; }
        snprintf(size_str, sizeof(size_str), "%.1f %s", size, unit);

        printf("%-12s %-30s %12s  %s\n",
               devices[i].device_path,
               devices[i].model[0] ? devices[i].model : "(unknown)",
               size_str,
               devices[i].is_system_disk ? "SYSTEM" :
                   devices[i].is_removable ? "USB" : "FIXED");
    }

    device_list_free(devices, count);
}

static int select_device_interactive(char *out_path, size_t out_size) {
    Device *devices = NULL;
    int count = 0;
    Error err;

    if (device_list(&devices, &count, &err) < 0) {
        fprintf(stderr, "Error: %s\n", err.message);
        return -1;
    }

    int usb_count = 0;
    for (int i = 0; i < count; i++) {
        if (!devices[i].is_system_disk) usb_count++;
    }

    if (usb_count == 0) {
        printf("No writable USB devices found.\n");
        device_list_free(devices, count);
        return -1;
    }

    printf("\nSelect target USB device:\n");
    int idx = 1;
    for (int i = 0; i < count; i++) {
        if (devices[i].is_system_disk) continue;
        char size_str[32];
        double size = (double)devices[i].size_bytes;
        const char *unit = "B";
        if (size > 1024ULL*1024*1024) { size /= 1024*1024*1024; unit = "GB"; }
        else if (size > 1024*1024) { size /= 1024*1024; unit = "MB"; }
        snprintf(size_str, sizeof(size_str), "%.1f %s", size, unit);
        printf("  %d) %s  %-30s %s\n", idx,
               devices[i].device_path,
               devices[i].model[0] ? devices[i].model : "(unknown)",
               size_str);
        idx++;
    }

    printf("\nSelect device (1-%d): ", usb_count);
    fflush(stdout);
    char input[64];
    if (!fgets(input, sizeof(input), stdin)) {
        device_list_free(devices, count);
        return -1;
    }

    int selection = atoi(input);
    if (selection < 1 || selection > usb_count) {
        printf("Invalid selection.\n");
        device_list_free(devices, count);
        return -1;
    }

    idx = 1;
    for (int i = 0; i < count; i++) {
        if (devices[i].is_system_disk) continue;
        if (idx == selection) {
            snprintf(out_path, out_size, "%s", devices[i].device_path);
            device_list_free(devices, count);
            return 0;
        }
        idx++;
    }

    device_list_free(devices, count);
    return -1;
}

static FlashMode select_mode_interactive(int has_large_wim) {
    printf("\nSelect flash mode:\n");
    for (int i = 0; i < flash_mode_count(); i++) {
        FlashMode mode = (FlashMode)i;
        printf("  %d) %s\n", i + 1, flash_mode_name(mode));
        printf("     %s\n", flash_mode_description(mode));
        if (has_large_wim && mode == FLASH_MODE_MBR_FAT32) {
            printf("     * WARNING: WIM splitting required (install.wim > 4 GiB)\n");
        }
        printf("\n");
    }

    int default_mode = 1;
    if (has_large_wim) default_mode = 3;

    printf("Select mode (1-%d, default %d): ", flash_mode_count(), default_mode);
    fflush(stdout);
    char input[64];
    if (!fgets(input, sizeof(input), stdin)) return (FlashMode)(default_mode - 1);

    int selection = atoi(input);
    if (selection < 1 || selection > flash_mode_count()) selection = default_mode;

    return (FlashMode)(selection - 1);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);

    FlashConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.progress_cb = progress_callback;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("Ignis - Create bootable Windows USB drives on Linux\n");
        printf("\nUsage:\n");
        printf("  ignis [options]\n");
        printf("\nOptions:\n");
        printf("  -h, --help        Show this help\n");
        printf("  -l, --list        List available USB devices\n");
        printf("  -i <iso>          Windows ISO file (required)\n");
        printf("  -d <device>       Target device (e.g. /dev/sdb)\n");
        printf("  -m <mode>         Flash mode: mbr (default), gpt, dual, ntfs\n");
        printf("  -y                Skip confirmation prompts\n");
        printf("\nExamples:\n");
        printf("  sudo ignis                     Interactive mode\n");
        printf("  sudo ignis -i Win10.iso -d /dev/sdb -y\n");
        printf("  sudo ignis --list\n");
        return 0;
    }

    if (argc > 1 && (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "--list") == 0)) {
        print_devices();
        return 0;
    }

    int skip_confirm = 0;
    char *iso_arg = NULL;
    char *dev_arg = NULL;
    char *mode_arg = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) iso_arg = argv[++i];
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) dev_arg = argv[++i];
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) mode_arg = argv[++i];
        else if (strcmp(argv[i], "-y") == 0) skip_confirm = 1;
    }

    Error err;

    if (!iso_arg) {
        printf("Windows ISO path: ");
        fflush(stdout);
        char input[1024];
        if (!fgets(input, sizeof(input), stdin)) return 1;
        size_t len = strlen(input);
        if (len > 0 && input[len-1] == '\n') input[len-1] = '\0';
        iso_arg = input;
    }

    snprintf(cfg.iso_path, sizeof(cfg.iso_path), "%s", iso_arg);
    printf("Parsing ISO: %s\n", iso_arg);

    if (iso_parse(iso_arg, &cfg.iso, &err) < 0) {
        fprintf(stderr, "Error: %s\n", err.message);
        return 1;
    }

    printf("  Windows version: %s\n", cfg.iso.windows_version);
    printf("  install.wim size: %.1f GiB%s\n",
           cfg.iso.install_wim_size / (1024.0*1024*1024),
           cfg.iso.needs_wim_split ? " (needs splitting for FAT32)" : "");
    printf("  UEFI bootloader: %s\n",
           cfg.iso.bootx64_efi_zero ? "PRESENT BUT 0 BYTES (needs fix)" :
           cfg.iso.bootx64_efi_exists ? "OK" : "not found separately (will copy from ISO)");

    if (!dev_arg) {
        if (select_device_interactive(cfg.device_path, sizeof(cfg.device_path)) < 0) {
            fprintf(stderr, "No device selected.\n");
            return 1;
        }
    } else {
        snprintf(cfg.device_path, sizeof(cfg.device_path), "%s", dev_arg);
    }

    if (!mode_arg) {
        cfg.mode = select_mode_interactive(cfg.iso.needs_wim_split);
    } else {
        if (strcasecmp(mode_arg, "gpt") == 0) cfg.mode = FLASH_MODE_GPT_FAT32;
        else if (strcasecmp(mode_arg, "dual") == 0) cfg.mode = FLASH_MODE_GPT_DUAL;
        else if (strcasecmp(mode_arg, "ntfs") == 0) cfg.mode = FLASH_MODE_GPT_NTFS;
        else cfg.mode = FLASH_MODE_MBR_FAT32;
    }

    {
        Device dev;
        if (device_find_by_path(&dev, cfg.device_path, &err) == 0) {
            cfg.device = dev;
            if (dev.is_system_disk) {
                fprintf(stderr, "ERROR: %s is the system disk! Refusing to flash.\n", cfg.device_path);
                return 1;
            }
        }
    }

    printf("\n=== Flash Summary ===\n");
    printf("  ISO:     %s\n", cfg.iso_path);
    printf("  Device:  %s (%s)\n", cfg.device_path, cfg.device.model);
    char size_str[32];
    double size = (double)cfg.device.size_bytes / (1024*1024*1024);
    snprintf(size_str, sizeof(size_str), "%.1f GB", size);
    printf("  Size:    %s\n", size_str);
    printf("  Mode:    %s\n", flash_mode_name(cfg.mode));
    printf("  All data on %s will be DESTROYED!\n", cfg.device_path);
    printf("=====================\n");

    if (!skip_confirm) {
        printf("\nType 'yes' to continue: ");
        fflush(stdout);
        char confirm[64];
        if (!fgets(confirm, sizeof(confirm), stdin)) return 1;
        if (strncmp(confirm, "yes", 3) != 0) {
            printf("Cancelled.\n");
            return 0;
        }
    }

    cfg.cancel_requested = (volatile int *)&cancel_flag;

    printf("\nFlashing...\n");
    FlashResult result = flash_run(&cfg);

    if (result.success) {
        printf("\nSUCCESS! Windows USB created successfully.\n");
        printf("  Files copied: %d\n", result.verify.total_files);
        printf("  bootmgr:      %s\n", result.verify.bootmgr_exists ? "OK" : "MISSING");
        printf("  bootx64.efi:  %s\n", result.verify.bootx64_exists ? "OK" : "MISSING");
        printf("  sources:      %s\n", result.verify.sources_dir_exists ? "OK" : "MISSING");
        return 0;
    } else {
        fprintf(stderr, "\nFAILED at stage %d: %s\n", result.failed_stage, result.error_message);
        return 1;
    }
}
