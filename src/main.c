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