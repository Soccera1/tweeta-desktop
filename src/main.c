#include <gtk/gtk.h>
#include <curl/curl.h>
#include <stdlib.h>
#include "globals.h"
#include "session.h"
#include "actions.h"
#include "views.h"

int main(int argc, char *argv[]) {
    GtkWidget *window;

    gtk_init(&argc, &argv);
    curl_global_init(CURL_GLOBAL_ALL);

    // CSS Provider
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        ".unread-notification { background-color: rgba(0, 100, 255, 0.1); }"
        ".dim-label { opacity: 0.7; font-size: 0.9em; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    window = create_window();

    load_session();
    update_login_ui();

    gtk_widget_show_all(window);

    start_loading_tweets(GTK_LIST_BOX(g_main_list_box));

    gtk_main();

    curl_global_cleanup();
    g_free(g_auth_token);
    g_free(g_current_username);

    return 0;
}