#ifndef GLOBALS_H
#define GLOBALS_H

#include <gtk/gtk.h>

extern gchar *g_auth_token;
extern gchar *g_current_username;
extern GtkWidget *g_login_button;
extern GtkWidget *g_compose_button;
extern GtkWidget *g_user_label;
extern GtkWidget *g_main_list_box;
extern GtkWidget *g_stack;
extern GtkWidget *g_back_button;

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
