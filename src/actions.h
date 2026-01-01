#ifndef ACTIONS_H
#define ACTIONS_H

#include <gtk/gtk.h>

void start_loading_tweets(GtkListBox *list_box);
void start_loading_notifications(GtkListBox *list_box);
void start_loading_conversations(GtkListBox *list_box);
void start_loading_messages(GtkListBox *list_box, const gchar *conversation_id);
void start_loading_admin_stats();
void start_loading_admin_users(const gchar *search);
void start_loading_admin_posts(const gchar *search);
void load_more_tweets(GtkListBox *list_box, const gchar *before_id);

// Admin action handlers
void perform_admin_verify(const gchar *username, gboolean verify);
void perform_admin_suspend(const gchar *username, const gchar *reason);
void perform_admin_delete_user(const gchar *username);
void perform_admin_delete_post(const gchar *post_id);

void perform_search(const gchar *query);
void update_login_ui();
void perform_logout();
gboolean perform_login(const gchar *username, const gchar *password);
void show_profile(const gchar *username);
void show_tweet(const gchar *tweet_id);

void on_search_activated(GtkEntry *entry, gpointer user_data);
void on_back_clicked(GtkWidget *widget, gpointer user_data);
void on_notifications_clicked(GtkWidget *widget, gpointer user_data);
void on_messages_clicked(GtkWidget *widget, gpointer user_data);
void on_settings_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_clicked(GtkWidget *widget, gpointer user_data);
void on_mark_all_read_clicked(GtkWidget *widget, gpointer user_data);
void on_refresh_clicked(GtkWidget *widget, gpointer user_data);
void on_compose_clicked(GtkWidget *widget, gpointer window);
void on_note_button_clicked(GtkWidget *widget, gpointer user_data);
void update_login_ui();
void on_login_clicked(GtkWidget *widget, gpointer window);
void on_scroll_edge_reached(GtkScrolledWindow *scrolled_window, GtkPositionType pos, gpointer user_data);

gboolean perform_like(const gchar *tweet_id);
gboolean perform_retweet(const gchar *tweet_id);
gboolean perform_bookmark(const gchar *tweet_id, gboolean add);
gboolean perform_reaction(const gchar *tweet_id, const gchar *emoji);
GList* fetch_emojis(void);
void free_emojis(GList *emojis);

#endif // ACTIONS_H
