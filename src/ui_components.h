#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include <gtk/gtk.h>
#include "types.h"

GtkWidget* create_tweet_widget(struct Tweet *tweet);
GtkWidget* create_tweet_widget_full(struct Tweet *tweet, const gchar *op_username);
void populate_tweet_list(GtkListBox *list_box, GList *tweets);
void append_tweets_to_list(GtkListBox *list_box, GList *tweets);
GtkWidget* create_user_widget(struct Profile *user);
void populate_user_list(GtkListBox *list_box, GList *users);
GtkWidget* create_notification_widget(struct Notification *notif);
void populate_notification_list(GtkListBox *list_box, GList *notifications);
void append_notifications_to_list(GtkListBox *list_box, GList *notifications);

GtkWidget* create_conversation_widget(struct Conversation *conv);
void populate_conversation_list(GtkListBox *list_box, GList *conversations);
GtkWidget* create_message_widget(struct DirectMessage *msg);
void populate_message_list(GtkListBox *list_box, GList *messages);

GtkWidget* create_poll_widget(struct Poll *poll, const gchar *tweet_id);

GtkWidget* create_community_widget(struct Community *community);
void populate_community_list(GtkListBox *list_box, GList *communities);

#endif /* UI_COMPONENTS_H */
