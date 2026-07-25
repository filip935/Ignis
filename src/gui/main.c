#include "gui_app.h"
#include <stdio.h>
#include <unistd.h>

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)app;
    WinFlashApp *wapp = (WinFlashApp *)user_data;
    winflash_app_setup_window(wapp, app);
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("com.winflash.gui", G_APPLICATION_DEFAULT_FLAGS);
    WinFlashApp *wapp = winflash_app_new();

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), wapp);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    winflash_app_free(wapp);
    g_object_unref(app);
    return status;
}
