#ifndef ACTIONS_H
#define ACTIONS_H

#include <gtk/gtk.h>
#include "types.h"

void start_loading_tweets(GtkListBox *list_box);
void start_loading_notifications(GtkListBox *list_box);
void start_loading_conversations(GtkListBox *list_box);
void start_loading_messages(GtkListBox *list_box, const gchar *conversation_id);
void start_loading_admin_stats(void);
void start_loading_admin_users(const gchar *search);
void start_loading_admin_posts(const gchar *search);
void load_more_tweets(GtkListBox *list_box, const gchar *before_id);

// Admin action handlers
void perform_admin_verify(const gchar *username, gboolean verify);
void perform_admin_suspend(const gchar *username, const gchar *reason);
void perform_admin_delete_user(const gchar *username);
void perform_admin_delete_post(const gchar *post_id);

void perform_search(const gchar *query);
void update_login_ui(void);
void perform_logout(void);
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
void update_login_ui(void);
void on_login_clicked(GtkWidget *widget, gpointer window);
void on_scroll_edge_reached(GtkScrolledWindow *scrolled_window, GtkPositionType pos, gpointer user_data);

gboolean perform_like(const gchar *tweet_id);
gboolean perform_retweet(const gchar *tweet_id);
gboolean perform_bookmark(const gchar *tweet_id, gboolean add);
gboolean perform_reaction(const gchar *tweet_id, const gchar *emoji);
GList* fetch_emojis(void);
void free_emojis(GList *emojis);

// Follow/Unfollow
gboolean perform_follow(const gchar *username, gboolean follow);
void start_loading_followers(const gchar *username);
void start_loading_following(const gchar *username);

// Bookmarks
void start_loading_bookmarks(GtkListBox *list_box);

// Block/Mute
gboolean perform_block(const gchar *username, gboolean block);
gboolean perform_mute(const gchar *username, gboolean mute);
gboolean check_user_blocked(const gchar *username);
gboolean check_user_muted(const gchar *username);

// Timeline
void set_timeline_type(TimelineType type);
TimelineType get_current_timeline_type(void);
void start_loading_timeline(GtkListBox *list_box);

// Polls
gboolean perform_poll_vote(const gchar *tweet_id, const gchar *option_id);
void free_poll(struct Poll *poll);
void free_poll_option(gpointer data);

// Profile Editing
gboolean perform_update_profile(const gchar *username, const gchar *name, const gchar *bio);
gboolean perform_upload_avatar(const gchar *username, const gchar *file_path);
gboolean perform_upload_banner(const gchar *username, const gchar *file_path);

// Media Upload
gchar* perform_media_upload(const gchar *file_path);

// Communities
void start_loading_communities(GtkListBox *list_box);
void start_loading_community_tweets(GtkListBox *list_box, const gchar *community_id);
gboolean perform_join_community(const gchar *community_id);
gboolean perform_leave_community(const gchar *community_id);
void free_community(gpointer data);
void free_communities(GList *communities);

#endif // ACTIONS_H
