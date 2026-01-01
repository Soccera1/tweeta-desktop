#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <json-glib/json-glib.h>
#include "types.h"

GList* parse_tweets(const gchar *json_data);
GList* parse_tweet_details(const gchar *json_data);
struct Profile* parse_profile(const gchar *json_data);
GList* parse_profile_replies(const gchar *json_data);
GList* parse_users(const gchar *json_data);
GList* parse_notifications(const gchar *json_data);
GList* parse_conversations(const gchar *json_data);
GList* parse_messages(const gchar *json_data);
GList* parse_admin_users(const gchar *json_data);
GList* parse_admin_posts(const gchar *json_data);
gchar* parse_admin_stats(const gchar *json_data);
gboolean parse_login_response(const gchar *json_data, gchar **token_out, gchar **username_out, gboolean *is_admin_out);
gboolean parse_user_me_response(const gchar *json_data, gboolean *is_admin_out);
gchar* construct_tweet_payload(const gchar *content, const gchar *reply_to_id);
gchar* construct_dm_payload(const gchar *content);

void free_tweet(gpointer data);
void free_tweets(GList *tweets);
void free_user(gpointer data);
void free_users(GList *users);
void free_notification(gpointer data);
void free_notifications(GList *notifications);
void free_conversation(gpointer data);
void free_conversations(GList *conversations);
void free_message(gpointer data);
void free_messages(GList *messages);

#endif // JSON_UTILS_H
