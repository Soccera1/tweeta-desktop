#ifndef ACTIONS_H
#define ACTIONS_H

#include <gtk/gtk.h>

void start_loading_tweets(GtkListBox *list_box);
void perform_search(const gchar *query);
void update_login_ui();
void perform_logout();
gboolean perform_login(const gchar *username, const gchar *password);
void show_profile(const gchar *username);

// UI Handlers
void on_search_activated(GtkEntry *entry, gpointer user_data);
void on_back_clicked(GtkWidget *widget, gpointer user_data);
void on_compose_clicked(GtkWidget *widget, gpointer window);
void on_refresh_clicked(GtkWidget *widget, gpointer user_data);
void on_login_clicked(GtkWidget *widget, gpointer window);

#endif // ACTIONS_H
