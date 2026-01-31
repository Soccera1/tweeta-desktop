#include "globals.h"

gchar *g_auth_token = NULL;
gchar *g_current_username = NULL;
gboolean g_is_admin = FALSE;
GtkWidget *g_login_button = NULL;
GtkWidget *g_compose_button = NULL;
GtkWidget *g_user_label = NULL;
GtkWidget *g_main_list_box = NULL;
GtkWidget *g_notifications_list = NULL;
GtkWidget *g_conversations_list = NULL;
GtkWidget *g_dm_messages_list = NULL;
GtkWidget *g_dm_title_label = NULL;
GtkWidget *g_dm_entry = NULL;
GtkWidget *g_conversation_list = NULL;
GtkWidget *g_stack = NULL;
GtkWidget *g_back_button = NULL;
GtkWidget *g_admin_button = NULL;

GtkWidget *g_admin_stats_label = NULL;
GtkWidget *g_admin_users_list = NULL;
GtkWidget *g_admin_posts_list = NULL;
GtkWidget *g_admin_users_search = NULL;
GtkWidget *g_admin_posts_search = NULL;

GtkWidget *g_search_entry = NULL;
GtkWidget *g_search_users_list = NULL;
GtkWidget *g_search_tweets_list = NULL;

GtkWidget *g_profile_name_label = NULL;
GtkWidget *g_profile_bio_label = NULL;
GtkWidget *g_profile_stats_label = NULL;
GtkWidget *g_profile_avatar_image = NULL;
GtkWidget *g_profile_tweets_list = NULL;
GtkWidget *g_profile_replies_list = NULL;
GtkWidget *g_follow_button = NULL;
GtkWidget *g_followers_list = NULL;
GtkWidget *g_following_list = NULL;

GtkWidget *g_bookmarks_list = NULL;

TimelineType g_current_timeline_type = TIMELINE_PUBLIC;

GtkWidget *g_communities_list = NULL;
GtkWidget *g_community_tweets_list = NULL;
gchar *g_community_id = NULL;
