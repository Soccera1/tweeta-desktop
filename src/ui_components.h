#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include <gtk/gtk.h>
#include "types.h"

GtkWidget* create_tweet_widget(struct Tweet *tweet);
void populate_tweet_list(GtkListBox *list_box, GList *tweets);
void append_tweets_to_list(GtkListBox *list_box, GList *tweets);
GtkWidget* create_user_widget(struct Profile *user);
void populate_user_list(GtkListBox *list_box, GList *users);
GtkWidget* create_notification_widget(struct Notification *notif);
void populate_notification_list(GtkListBox *list_box, GList *notifications);

#endif // UI_COMPONENTS_H
