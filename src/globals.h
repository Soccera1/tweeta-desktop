#ifndef GLOBALS_H
#define GLOBALS_H

#include <gtk/gtk.h>

extern gchar *g_auth_token;
extern gchar *g_current_username;
extern gboolean g_is_admin;
extern GtkWidget *g_login_button;
extern GtkWidget *g_compose_button;
extern GtkWidget *g_user_label;
extern GtkWidget *g_main_list_box;
extern GtkWidget *g_notifications_list;
extern GtkWidget *g_conversations_list;
extern GtkWidget *g_dm_messages_list;
extern GtkWidget *g_dm_title_label;
extern GtkWidget *g_dm_entry;
extern GtkWidget *g_conversation_list;
extern GtkWidget *g_stack;
extern GtkWidget *g_back_button;
extern GtkWidget *g_admin_button;

// Admin widgets
extern GtkWidget *g_admin_stats_label;

// Search widgets
extern GtkWidget *g_search_entry;
extern GtkWidget *g_search_users_list;
extern GtkWidget *g_search_tweets_list;

// Profile widgets
extern GtkWidget *g_profile_name_label;
extern GtkWidget *g_profile_bio_label;
extern GtkWidget *g_profile_stats_label;
extern GtkWidget *g_profile_avatar_image;
extern GtkWidget *g_profile_tweets_list;
extern GtkWidget *g_profile_replies_list;

#endif // GLOBALS_H
