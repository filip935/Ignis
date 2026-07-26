#ifndef IGNIS_GUI_H
#define IGNIS_GUI_H

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
} IgnisApp;

IgnisApp *ignis_app_new(void);
void ignis_app_free(IgnisApp *app);
void ignis_app_setup_window(IgnisApp *app, GtkApplication *application);
void ignis_app_show_error(GtkWidget *parent, const char *title, const char *message);
void ignis_app_log(IgnisApp *app, const char *fmt, ...);

#endif
