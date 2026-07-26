#include "gui_app.h"
#include "../core/device.h"
#include "../core/iso.h"
#include "../core/partition.h"
#include "../core/flash.h"
#include "../util/util.h"
#include <stdarg.h>

static void refresh_device_list(IgnisApp *app);
static void update_ui_on_device_change(IgnisApp *app);
static void update_flash_button_state(IgnisApp *app);
static void on_iso_changed(GtkEditable *ed, gpointer user_data);
static void on_device_changed(GObject *obj, GParamSpec *pspec, gpointer user_data);
static void on_mode_changed(GObject *obj, GParamSpec *pspec, gpointer user_data);
static void on_iso_browse_clicked(GtkButton *button, gpointer user_data);
static void on_refresh_clicked(GtkButton *button, gpointer user_data);
static void on_flash_clicked(GtkButton *button, gpointer user_data);
static void on_iso_dialog_response(GObject *dialog, GAsyncResult *result, gpointer user_data);
static gboolean update_progress_gui(gpointer user_data);
static int flash_progress_cb(FlashStage stage, int percent, const char *status, void *user_data);
static gpointer flash_thread_func(gpointer user_data);
static gboolean flash_completed_idle(gpointer data);

IgnisApp *ignis_app_new(void) {
    IgnisApp *app = g_new0(IgnisApp, 1);
    app->device_list = NULL;
    app->device_count = 0;
    app->flash_running = 0;
    app->cancel_requested = 0;
    app->last_percent = 0;
    app->last_status[0] = '\0';
    app->last_stage = STAGE_NONE;
    app->flash_thread = NULL;
    app->idle_queued = 0;
    return app;
}

void ignis_app_free(IgnisApp *app) {
    if (app->device_list) {
        device_list_free(app->device_list, app->device_count);
    }
    g_free(app);
}

void ignis_app_log(IgnisApp *app, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->log_buffer, &iter);
    gtk_text_buffer_insert(app->log_buffer, &iter, buf, -1);
    gtk_text_buffer_insert(app->log_buffer, &iter, "\n", 1);

    GtkTextMark *mark = gtk_text_buffer_get_insert(app->log_buffer);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(app->log_view), mark, 0.0, FALSE, 0.0, 0.0);
}

static GtkWidget *create_header(void) {
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header),
        gtk_label_new("Ignis \xe2\x80\x94 Windows USB Creator"));
    return header;
}

static GtkWidget *create_iso_section(IgnisApp *app) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    GtkWidget *label = gtk_label_new("Windows ISO:");
    gtk_widget_set_size_request(label, 100, -1);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);

    app->iso_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->iso_entry), "Path to Windows ISO file...");
    gtk_widget_set_hexpand(app->iso_entry, TRUE);

    app->iso_chooser_btn = gtk_button_new_with_label("Browse");
    g_signal_connect(app->iso_chooser_btn, "clicked", G_CALLBACK(on_iso_browse_clicked), app);
    g_signal_connect(app->iso_entry, "changed", G_CALLBACK(on_iso_changed), app);

    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), app->iso_entry);
    gtk_box_append(GTK_BOX(box), app->iso_chooser_btn);
    return box;
}

static GtkWidget *create_device_section(IgnisApp *app) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 3);
    gtk_widget_set_margin_bottom(box, 6);

    GtkWidget *label = gtk_label_new("Target Device:");
    gtk_widget_set_size_request(label, 100, -1);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);

    app->device_dropdown = gtk_drop_down_new(NULL, NULL);
    gtk_widget_set_hexpand(app->device_dropdown, TRUE);

    app->refresh_btn = gtk_button_new_with_label("Refresh");
    g_signal_connect(app->refresh_btn, "clicked", G_CALLBACK(on_refresh_clicked), app);

    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), app->device_dropdown);
    gtk_box_append(GTK_BOX(box), app->refresh_btn);

    g_signal_connect(app->device_dropdown, "notify::selected-item",
                     G_CALLBACK(on_device_changed), app);
    return box;
}

static GtkWidget *create_device_info_box(IgnisApp *app) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(box, 118);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_bottom(box, 6);
    app->device_info_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app->device_info_label), 0.0);
    gtk_box_append(GTK_BOX(box), app->device_info_label);
    return box;
}

static GtkWidget *create_mode_section(IgnisApp *app) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 3);
    gtk_widget_set_margin_bottom(box, 6);

    GtkWidget *label = gtk_label_new("Flash Mode:");
    gtk_widget_set_size_request(label, 100, -1);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);

    GtkStringList *mode_list = gtk_string_list_new(NULL);
    for (int i = 0; i < flash_mode_count(); i++) {
        gtk_string_list_append(mode_list, flash_mode_name((FlashMode)i));
    }
    app->mode_dropdown = gtk_drop_down_new(G_LIST_MODEL(mode_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->mode_dropdown), 0);
    gtk_widget_set_hexpand(app->mode_dropdown, TRUE);

    app->desc_label = gtk_label_new(flash_mode_description(FLASH_MODE_MBR_FAT32));
    gtk_widget_set_hexpand(app->desc_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(app->desc_label), 0.0);
    gtk_label_set_wrap(GTK_LABEL(app->desc_label), TRUE);
    gtk_widget_set_margin_start(app->desc_label, 6);

    g_signal_connect(app->mode_dropdown, "notify::selected-item",
                     G_CALLBACK(on_mode_changed), app);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_box_append(GTK_BOX(vbox), app->mode_dropdown);
    gtk_box_append(GTK_BOX(vbox), app->desc_label);

    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), vbox);
    return box;
}

static GtkWidget *create_progress_section(IgnisApp *app) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    app->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(app->progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), "Ready");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), 0.0);

    app->status_label = gtk_label_new("Select an ISO file and target device to begin.");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0);

    GtkWidget *action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    app->flash_btn = gtk_button_new_with_label("Flash");
    gtk_widget_set_size_request(app->flash_btn, 120, 40);
    gtk_widget_set_sensitive(app->flash_btn, FALSE);
    g_signal_connect(app->flash_btn, "clicked", G_CALLBACK(on_flash_clicked), app);

    gtk_box_append(GTK_BOX(box), app->progress_bar);
    gtk_box_append(GTK_BOX(box), app->status_label);
    gtk_box_append(GTK_BOX(action_box), app->flash_btn);
    gtk_box_append(GTK_BOX(box), action_box);
    return box;
}

static GtkWidget *create_log_section(IgnisApp *app) {
    GtkWidget *frame = gtk_frame_new("Log");
    gtk_widget_set_margin_start(frame, 12);
    gtk_widget_set_margin_end(frame, 12);
    gtk_widget_set_margin_top(frame, 6);
    gtk_widget_set_margin_bottom(frame, 12);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 150);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    app->log_buffer = gtk_text_buffer_new(NULL);
    app->log_view = gtk_text_view_new_with_buffer(app->log_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app->log_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->log_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->log_view), TRUE);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), app->log_view);
    gtk_frame_set_child(GTK_FRAME(frame), scrolled);
    return frame;
}

void ignis_app_setup_window(IgnisApp *app, GtkApplication *application) {
    app->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(app->window), "Ignis \xe2\x80\x94 Windows USB Creator");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 720, 580);
    gtk_window_set_resizable(GTK_WINDOW(app->window), TRUE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_titlebar(GTK_WINDOW(app->window), create_header());
    gtk_box_append(GTK_BOX(vbox), create_iso_section(app));
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(vbox), create_device_section(app));
    gtk_box_append(GTK_BOX(vbox), create_device_info_box(app));
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(vbox), create_mode_section(app));
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(vbox), create_progress_section(app));
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(vbox), create_log_section(app));

    gtk_window_set_child(GTK_WINDOW(app->window), vbox);
    gtk_widget_set_visible(app->window, TRUE);

    ignis_app_log(app, "Ignis v1.0 started");
    if (geteuid() != 0) {
        ignis_app_log(app, "WARNING: Not running as root. Device operations will fail.");
    }
    refresh_device_list(app);
}

void ignis_show_error(IgnisApp *app, const char *title, const char *message) {
    GtkAlertDialog *alert = gtk_alert_dialog_new("%s", title);
    gtk_alert_dialog_set_detail(alert, message);
    gtk_alert_dialog_show(alert, GTK_WINDOW(app->window));
    g_object_unref(alert);
}

static void on_iso_changed(GtkEditable *ed, gpointer user_data) {
    (void)ed;
    update_flash_button_state((IgnisApp *)user_data);
}

static void on_device_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)obj; (void)pspec;
    update_ui_on_device_change((IgnisApp *)user_data);
    update_flash_button_state((IgnisApp *)user_data);
}

static void on_mode_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)obj; (void)pspec;
    IgnisApp *app = (IgnisApp *)user_data;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->mode_dropdown));
    if (sel < (guint)flash_mode_count()) {
        gtk_label_set_text(GTK_LABEL(app->desc_label), flash_mode_description((FlashMode)sel));
    }
}

static void refresh_device_list(IgnisApp *app) {
    if (app->device_list) {
        device_list_free(app->device_list, app->device_count);
        app->device_list = NULL;
        app->device_count = 0;
    }

    Error err;
    if (device_list(&app->device_list, &app->device_count, &err) < 0) {
        ignis_app_log(app, "Error listing devices: %s", err.message);
        GtkStringList *empty = gtk_string_list_new(NULL);
        gtk_string_list_append(empty, "Error listing devices");
        gtk_drop_down_set_model(GTK_DROP_DOWN(app->device_dropdown), G_LIST_MODEL(empty));
        return;
    }

    GtkStringList *model = gtk_string_list_new(NULL);
    for (int i = 0; i < app->device_count; i++) {
        Device *d = &app->device_list[i];
        char size_str[32];
        double size = (double)d->size_bytes / (1024*1024*1024);
        snprintf(size_str, sizeof(size_str), "%.1f GB", size);

        char label[512];
        if (d->is_system_disk) {
            snprintf(label, sizeof(label), "[SYSTEM] %s \xe2\x80\x94 %s \xe2\x80\x94 %s",
                     d->device_path, size_str, d->model);
        } else {
            snprintf(label, sizeof(label), "%s \xe2\x80\x94 %s \xe2\x80\x94 %s",
                     d->device_path, size_str, d->model);
        }
        gtk_string_list_append(model, label);
    }

    gtk_drop_down_set_model(GTK_DROP_DOWN(app->device_dropdown), G_LIST_MODEL(model));

    if (app->device_count > 0) {
        int sel = 0;
        for (int i = 0; i < app->device_count; i++) {
            if (!app->device_list[i].is_system_disk && app->device_list[i].is_removable) {
                sel = i; break;
            }
        }
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->device_dropdown), sel);
    }

    update_ui_on_device_change(app);
    update_flash_button_state(app);
    ignis_app_log(app, "Found %d device(s)", app->device_count);
}

static void update_ui_on_device_change(IgnisApp *app) {
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->device_dropdown));
    if (app->device_list && sel < (guint)app->device_count) {
        Device *d = &app->device_list[sel];
        char info[256];
        if (d->is_system_disk)
            snprintf(info, sizeof(info), "WARNING: System disk! Cannot flash.");
        else if (d->is_removable)
            snprintf(info, sizeof(info), "Removable USB \xe2\x80\x94 %s", d->model);
        else
            snprintf(info, sizeof(info), "Fixed disk \xe2\x80\x94 %s", d->model);
        gtk_label_set_text(GTK_LABEL(app->device_info_label), info);
    }
}

static void update_flash_button_state(IgnisApp *app) {
    if (app->flash_running) {
        gtk_widget_set_sensitive(app->flash_btn, TRUE);
        return;
    }

    const char *iso_text = gtk_editable_get_text(GTK_EDITABLE(app->iso_entry));
    guint dev_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->device_dropdown));

    int valid = 0;
    if (iso_text && iso_text[0] && path_exists(iso_text) &&
        dev_sel < (guint)app->device_count && dev_sel != GTK_INVALID_LIST_POSITION) {
        Device *d = &app->device_list[dev_sel];
        if (!d->is_system_disk) valid = 1;
    }

    gtk_widget_set_sensitive(app->flash_btn, valid);
}

static void on_iso_dialog_response(GObject *dialog, GAsyncResult *result, gpointer user_data) {
    IgnisApp *app = (IgnisApp *)user_data;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(dialog), result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            gtk_editable_set_text(GTK_EDITABLE(app->iso_entry), path);
            g_free(path);
        }
        g_object_unref(file);
    }
    g_object_unref(dialog);
}

static void on_iso_browse_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    IgnisApp *app = (IgnisApp *)user_data;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Windows ISO");

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "ISO files (*.iso)");
    gtk_file_filter_add_suffix(filter, "iso");
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, filter);

    gtk_file_dialog_open(dialog, GTK_WINDOW(app->window), NULL, on_iso_dialog_response, app);
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    IgnisApp *app = (IgnisApp *)user_data;
    ignis_app_log(app, "Refreshing device list...");
    refresh_device_list(app);
}

static int flash_progress_cb(FlashStage stage, int percent, const char *status, void *user_data) {
    IgnisApp *app = (IgnisApp *)user_data;
    if (app->cancel_requested) return -1;
    app->last_stage = stage;
    app->last_percent = percent;
    snprintf(app->last_status, sizeof(app->last_status), "%s", status);
    if (!app->idle_queued) {
        app->idle_queued = 1;
        g_idle_add(update_progress_gui, app);
    }
    return 0;
}

static gboolean update_progress_gui(gpointer user_data) {
    IgnisApp *app = (IgnisApp *)user_data;
    app->idle_queued = 0;
    if (!app->flash_running) return G_SOURCE_REMOVE;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), app->last_percent / 100.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), app->last_status);
    gtk_label_set_text(GTK_LABEL(app->status_label), app->last_status);
    return G_SOURCE_REMOVE;
}

typedef struct {
    IgnisApp *app;
    FlashResult result;
} FlashThreadData;

static void flash_completed(IgnisApp *app, FlashResult *result) {
    app->flash_running = 0;
    gtk_widget_set_sensitive(app->flash_btn, TRUE);
    gtk_button_set_label(GTK_BUTTON(app->flash_btn), "Flash");

    if (result->success) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), 1.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), "Complete!");
        gtk_label_set_text(GTK_LABEL(app->status_label), "Windows USB created successfully!");
        ignis_app_log(app, "SUCCESS: Windows USB created!");
        ignis_app_log(app, "  Files: %d  bootmgr: %s  bootx64: %s  sources: %s",
            result->verify.total_files,
            result->verify.bootmgr_exists ? "OK" : "MISSING",
            result->verify.bootx64_exists ? "OK" : "MISSING",
            result->verify.sources_dir_exists ? "OK" : "MISSING");
    } else {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), "Failed");
        gtk_label_set_text(GTK_LABEL(app->status_label), "Flash failed!");
        ignis_app_log(app, "FAILED: %s", result->error_message);
    }
}

static gboolean flash_completed_idle(gpointer data) {
    FlashThreadData *td = (FlashThreadData *)data;
    flash_completed(td->app, &td->result);
    g_free(td);
    return G_SOURCE_REMOVE;
}

static gpointer flash_thread_func(gpointer user_data) {
    FlashThreadData *data = (FlashThreadData *)user_data;
    data->result = flash_run(&data->app->flash_cfg);
    g_idle_add(flash_completed_idle, data);
    return NULL;
}

static void on_flash_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    IgnisApp *app = (IgnisApp *)user_data;

    if (app->flash_running) {
        app->cancel_requested = 1;
        gtk_button_set_label(GTK_BUTTON(app->flash_btn), "Cancelling...");
        gtk_widget_set_sensitive(app->flash_btn, FALSE);
        ignis_app_log(app, "Cancelling flash...");
        return;
    }

    const char *iso_path = gtk_editable_get_text(GTK_EDITABLE(app->iso_entry));
    if (!iso_path || !iso_path[0]) {
        ignis_show_error(app, "No ISO selected", "Please select a Windows ISO file first.");
        return;
    }
    if (!path_exists(iso_path)) {
        ignis_show_error(app, "ISO not found", "The specified ISO file does not exist.");
        return;
    }

    guint dev_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->device_dropdown));
    if (dev_sel >= (guint)app->device_count || dev_sel == GTK_INVALID_LIST_POSITION) {
        ignis_show_error(app, "No device selected", "Please select a target device.");
        return;
    }

    Device *dev = &app->device_list[dev_sel];
    if (dev->is_system_disk) {
        ignis_show_error(app, "System disk",
                           "Cannot flash the system disk. Select a different device.");
        return;
    }

    if (geteuid() != 0) {
        ignis_show_error(app, "Permission denied",
                           "Ignis must be run as root for flashing.\nUse: sudo ignis-gui");
        return;
    }

    IsoInfo iso;
    Error err;
    if (iso_parse(iso_path, &iso, &err) < 0) {
        ignis_show_error(app, "Invalid ISO", err.message);
        return;
    }

    guint mode_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->mode_dropdown));
    if (mode_sel >= (guint)flash_mode_count()) mode_sel = 0;

    app->cancel_requested = 0;
    memset(&app->flash_cfg, 0, sizeof(app->flash_cfg));
    snprintf(app->flash_cfg.iso_path, sizeof(app->flash_cfg.iso_path), "%s", iso_path);
    snprintf(app->flash_cfg.device_path, sizeof(app->flash_cfg.device_path), "%s", dev->device_path);
    app->flash_cfg.mode = (FlashMode)mode_sel;
    app->flash_cfg.device = *dev;
    app->flash_cfg.iso = iso;
    app->flash_cfg.progress_cb = flash_progress_cb;
    app->flash_cfg.progress_user_data = app;
    app->flash_cfg.cancel_requested = &app->cancel_requested;

    gtk_button_set_label(GTK_BUTTON(app->flash_btn), "Cancel");
    app->flash_running = 1;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), "Starting...");
    gtk_label_set_text(GTK_LABEL(app->status_label), "Preparing...");

    FlashThreadData *data = g_new(FlashThreadData, 1);
    data->app = app;
    memset(&data->result, 0, sizeof(data->result));

    ignis_app_log(app, "Starting flash: ISO=%s Device=%s Mode=%s",
        iso_path, dev->device_path, flash_mode_name((FlashMode)mode_sel));

    app->flash_thread = g_thread_new("flash-worker", flash_thread_func, data);
    g_thread_unref(app->flash_thread);
}
