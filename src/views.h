#ifndef VIEWS_H
#define VIEWS_H

#include <gtk/gtk.h>

GtkWidget* create_window(void);
GtkWidget* create_profile_view(void);
GtkWidget* create_search_view(void);
GtkWidget* create_notifications_view(void);
GtkWidget* create_messages_view(void);
GtkWidget* create_dm_messages_view(void);
GtkWidget* create_conversation_view(void);
GtkWidget* create_settings_view(void);
GtkWidget* create_admin_view(void);
GtkWidget* create_bookmarks_view(void);
GtkWidget* create_lists_view(void);
GtkWidget* create_list_details_view(void);
GtkWidget* create_explore_view(void);
GtkWidget* create_communities_view(void);
GtkWidget* create_community_tweets_view(void);
GtkWidget* create_p2p_messages_view(void);

#endif /* VIEWS_H */
