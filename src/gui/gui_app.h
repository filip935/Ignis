#ifndef WINFLASH_GUI_H
#define WINFLASH_GUI_H

#include "../core/flash.h"
#include "../core/device.h"
#include <gtk/gtk.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *iso_entry;
    GtkWidget *iso_chooser_btn;
    GtkWidget *device_dropdown;
    GtkWidget *refresh_btn;
    GtkWidget *mode_dropdown;
    GtkWidget *desc_label;
    GtkWidget *flash_btn;
    GtkWidget *progress_bar;
    GtkWidget *status_label;
    GtkWidget *log_view;
    GtkTextBuffer *log_buffer;
    GtkWidget *device_info_label;

    Device *device_list;
    int device_count;
    FlashConfig flash_cfg;
    GThread *flash_thread;
    volatile int flash_running;
    volatile int cancel_requested;
    volatile int last_percent;
    char last_status[256];
    FlashStage last_stage;
    volatile int idle_queued;
} WinFlashApp;

WinFlashApp *winflash_app_new(void);
void winflash_app_free(WinFlashApp *app);
void winflash_app_setup_window(WinFlashApp *app, GtkApplication *application);
void winflash_app_show_error(GtkWidget *parent, const char *title, const char *message);
void winflash_app_log(WinFlashApp *app, const char *fmt, ...);

#endif
