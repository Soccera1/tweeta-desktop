#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <json-glib/json-glib.h>
#include "types.h"

GList* parse_tweets(const gchar *json_data);
struct Profile* parse_profile(const gchar *json_data);
GList* parse_profile_replies(const gchar *json_data);
GList* parse_users(const gchar *json_data);
gboolean parse_login_response(const gchar *json_data, gchar **token_out, gchar **username_out);
gchar* construct_tweet_payload(const gchar *content, const gchar *reply_to_id);

void free_tweet(gpointer data);
void free_tweets(GList *tweets);
void free_user(gpointer data);
void free_users(GList *users);

#endif // JSON_UTILS_H
