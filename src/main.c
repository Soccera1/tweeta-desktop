#include <gtk/gtk.h>
#include <curl/curl.h>
#include <stdlib.h>
#include "globals.h"
#include "session.h"
#include "actions.h"
#include "views.h"
#include "types.h"

int main(int argc, char *argv[]) {
    GtkWidget *window;

    gtk_init(&argc, &argv);
    curl_global_init(CURL_GLOBAL_ALL);

    g_mutex_init(&g_globals_mutex);
    g_interaction_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        ".unread-notification { background-color: rgba(0, 100, 255, 0.1); }"
        ".dim-label { opacity: 0.7; font-size: 0.9em; }"
        ".note-frame { border-radius: 5px; border: 1px solid #ccc; }"
        ".note-warning { background-color: rgba(255, 165, 0, 0.1); border-color: orange; }"
        ".note-danger { background-color: rgba(255, 0, 0, 0.1); border-color: red; }"
        ".note-info { background-color: rgba(0, 191, 255, 0.1); border-color: deepskyblue; }",
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
    if (g_interaction_cache) {
        g_hash_table_unref(g_interaction_cache);
    }
    g_free(g_auth_token);
    g_free(g_current_username);

    return 0;
}
