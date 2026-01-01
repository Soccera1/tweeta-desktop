#include <json-glib/json-glib.h>
#include "json_utils.h"

static GList*
parse_attachments(JsonObject *post_object)
{
    GList *attachments = NULL;
    if (json_object_has_member(post_object, "attachments")) {
        JsonArray *attach_array = json_object_get_array_member(post_object, "attachments");
        for (guint i = 0; i < json_array_get_length(attach_array); i++) {
            JsonNode *attach_node = json_array_get_element(attach_array, i);
            JsonObject *attach_obj = json_node_get_object(attach_node);
            struct Attachment *attach = g_new0(struct Attachment, 1);
            attach->id = g_strdup(json_object_get_string_member(attach_obj, "id"));
            attach->file_url = g_strdup(json_object_get_string_member(attach_obj, "file_url"));
            attach->file_type = g_strdup(json_object_get_string_member(attach_obj, "file_type"));
            attachments = g_list_append(attachments, attach);
        }
    }
    return attachments;
}

GList*
parse_tweets(const gchar *json_data)
{
    JsonParser *parser;
    GError *error = NULL;
    GList *tweets = NULL;

    parser = json_parser_new();
    json_parser_load_from_data(parser, json_data, -1, &error);
    if (error) {
        g_warning("Unable to parse json: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root_node = json_parser_get_root(parser);
    JsonObject *root_object = json_node_get_object(root_node);
    
    if (json_object_has_member(root_object, "posts")) {
        JsonArray *posts = json_object_get_array_member(root_object, "posts");

        for (guint i = 0; i < json_array_get_length(posts); i++) {
            JsonNode *post_node = json_array_get_element(posts, i);
            JsonObject *post_object = json_node_get_object(post_node);
            JsonObject *author_object = json_object_get_object_member(post_object, "author");

            struct Tweet *tweet = g_new0(struct Tweet, 1);
            tweet->content = g_strdup(json_object_get_string_member(post_object, "content"));
            tweet->author_name = g_strdup(json_object_get_string_member(author_object, "name"));
            tweet->author_username = g_strdup(json_object_get_string_member(author_object, "username"));
            if (json_object_has_member(author_object, "avatar") && !json_node_is_null(json_object_get_member(author_object, "avatar"))) {
                tweet->author_avatar = g_strdup(json_object_get_string_member(author_object, "avatar"));
            } else {
                tweet->author_avatar = NULL;
            }

            if (json_object_has_member(post_object, "id")) {
                tweet->id = g_strdup(json_object_get_string_member(post_object, "id"));
            } else {
                tweet->id = NULL;
            }

            if (json_object_has_member(post_object, "fact_check") && !json_node_is_null(json_object_get_member(post_object, "fact_check"))) {
                JsonNode *fc_node = json_object_get_member(post_object, "fact_check");
                if (JSON_NODE_HOLDS_OBJECT(fc_node)) {
                    JsonObject *fact_check = json_node_get_object(fc_node);
                    if (json_object_has_member(fact_check, "note")) {
                        tweet->note = g_strdup(json_object_get_string_member(fact_check, "note"));
                    }
                    if (json_object_has_member(fact_check, "severity")) {
                        tweet->note_severity = g_strdup(json_object_get_string_member(fact_check, "severity"));
                    }
                }
            }

            tweet->attachments = parse_attachments(post_object);

            tweet->liked = FALSE;
            if (json_object_has_member(post_object, "liked_by_user")) {
                JsonNode *node = json_object_get_member(post_object, "liked_by_user");
                if (JSON_NODE_HOLDS_VALUE(node)) {
                    if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                        tweet->liked = json_node_get_boolean(node);
                    else if (json_node_get_value_type(node) == G_TYPE_INT64)
                        tweet->liked = json_node_get_int(node) != 0;
                }
            } else if (json_object_has_member(post_object, "liked")) {
                tweet->liked = json_object_get_boolean_member(post_object, "liked");
            } else if (json_object_has_member(post_object, "is_liked")) {
                tweet->liked = json_object_get_boolean_member(post_object, "is_liked");
            } else if (json_object_has_member(post_object, "user_liked")) {
                tweet->liked = json_object_get_boolean_member(post_object, "user_liked");
            }

            tweet->retweeted = FALSE;
            if (json_object_has_member(post_object, "retweeted_by_user")) {
                JsonNode *node = json_object_get_member(post_object, "retweeted_by_user");
                if (JSON_NODE_HOLDS_VALUE(node)) {
                    if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                        tweet->retweeted = json_node_get_boolean(node);
                    else if (json_node_get_value_type(node) == G_TYPE_INT64)
                        tweet->retweeted = json_node_get_int(node) != 0;
                }
            } else if (json_object_has_member(post_object, "retweeted")) {
                tweet->retweeted = json_object_get_boolean_member(post_object, "retweeted");
            } else if (json_object_has_member(post_object, "is_retweeted")) {
                tweet->retweeted = json_object_get_boolean_member(post_object, "is_retweeted");
            } else if (json_object_has_member(post_object, "user_retweeted")) {
                tweet->retweeted = json_object_get_boolean_member(post_object, "user_retweeted");
            }

            tweet->bookmarked = FALSE;
            if (json_object_has_member(post_object, "bookmarked")) {
                tweet->bookmarked = json_object_get_boolean_member(post_object, "bookmarked");
            } else if (json_object_has_member(post_object, "is_bookmarked")) {
                tweet->bookmarked = json_object_get_boolean_member(post_object, "is_bookmarked");
            } else if (json_object_has_member(post_object, "user_bookmarked")) {
                tweet->bookmarked = json_object_get_boolean_member(post_object, "user_bookmarked");
            }

            if (json_object_has_member(post_object, "likes")) {
                tweet->like_count = json_object_get_int_member(post_object, "likes");
            }

            if (json_object_has_member(post_object, "retweets")) {
                tweet->retweet_count = json_object_get_int_member(post_object, "retweets");
            }

            if (json_object_has_member(post_object, "replies")) {
                tweet->reply_count = json_object_get_int_member(post_object, "replies");
            }

            tweets = g_list_append(tweets, tweet);
        }
    }

    g_object_unref(parser);
    return tweets;
}

static struct Tweet*
parse_single_tweet(JsonObject *post_object)
{
    JsonObject *author_object = json_object_get_object_member(post_object, "author");

    struct Tweet *tweet = g_new0(struct Tweet, 1);
    tweet->content = g_strdup(json_object_get_string_member(post_object, "content"));
    tweet->author_name = g_strdup(json_object_get_string_member(author_object, "name"));
    tweet->author_username = g_strdup(json_object_get_string_member(author_object, "username"));
    if (json_object_has_member(author_object, "avatar") && !json_node_is_null(json_object_get_member(author_object, "avatar"))) {
        tweet->author_avatar = g_strdup(json_object_get_string_member(author_object, "avatar"));
    }

    if (json_object_has_member(post_object, "id")) {
        tweet->id = g_strdup(json_object_get_string_member(post_object, "id"));
    }

    if (json_object_has_member(post_object, "fact_check") && !json_node_is_null(json_object_get_member(post_object, "fact_check"))) {
        JsonNode *fc_node = json_object_get_member(post_object, "fact_check");
        if (JSON_NODE_HOLDS_OBJECT(fc_node)) {
            JsonObject *fact_check = json_node_get_object(fc_node);
            if (json_object_has_member(fact_check, "note")) {
                tweet->note = g_strdup(json_object_get_string_member(fact_check, "note"));
            }
            if (json_object_has_member(fact_check, "severity")) {
                tweet->note_severity = g_strdup(json_object_get_string_member(fact_check, "severity"));
            }
        }
    }

    tweet->attachments = parse_attachments(post_object);

    tweet->liked = FALSE;
    if (json_object_has_member(post_object, "liked_by_user")) {
        JsonNode *node = json_object_get_member(post_object, "liked_by_user");
        if (JSON_NODE_HOLDS_VALUE(node)) {
            if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                tweet->liked = json_node_get_boolean(node);
            else if (json_node_get_value_type(node) == G_TYPE_INT64)
                tweet->liked = json_node_get_int(node) != 0;
        }
    } else if (json_object_has_member(post_object, "liked")) {
        tweet->liked = json_object_get_boolean_member(post_object, "liked");
    }

    tweet->retweeted = FALSE;
    if (json_object_has_member(post_object, "retweeted_by_user")) {
        JsonNode *node = json_object_get_member(post_object, "retweeted_by_user");
        if (JSON_NODE_HOLDS_VALUE(node)) {
            if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                tweet->retweeted = json_node_get_boolean(node);
            else if (json_node_get_value_type(node) == G_TYPE_INT64)
                tweet->retweeted = json_node_get_int(node) != 0;
        }
    } else if (json_object_has_member(post_object, "retweeted")) {
        tweet->retweeted = json_object_get_boolean_member(post_object, "retweeted");
    }

    tweet->bookmarked = FALSE;
    if (json_object_has_member(post_object, "bookmarked")) {
        tweet->bookmarked = json_object_get_boolean_member(post_object, "bookmarked");
    }

    if (json_object_has_member(post_object, "likes")) {
        tweet->like_count = json_object_get_int_member(post_object, "likes");
    }
    if (json_object_has_member(post_object, "retweets")) {
        tweet->retweet_count = json_object_get_int_member(post_object, "retweets");
    }
    if (json_object_has_member(post_object, "replies")) {
        tweet->reply_count = json_object_get_int_member(post_object, "replies");
    }

    return tweet;
}

GList*
parse_tweet_details(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *tweets = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        gchar *main_id = NULL;

        if (json_object_has_member(obj, "tweet")) {
            JsonObject *post_obj = json_object_get_object_member(obj, "tweet");
            main_id = g_strdup(json_object_get_string_member(post_obj, "id"));
        }

        // 1. threadPosts (Parents)
        if (json_object_has_member(obj, "threadPosts")) {
            JsonArray *arr = json_object_get_array_member(obj, "threadPosts");
            for (guint i = 0; i < json_array_get_length(arr); i++) {
                JsonObject *post_obj = json_array_get_object_element(arr, i);
                const gchar *id = json_object_get_string_member(post_obj, "id");
                // Don't add the main tweet again if it's in threadPosts
                if (main_id && g_strcmp0(id, main_id) == 0) continue;
                tweets = g_list_append(tweets, parse_single_tweet(post_obj));
            }
        }

        // 2. The main tweet
        if (json_object_has_member(obj, "tweet")) {
            JsonObject *post_obj = json_object_get_object_member(obj, "tweet");
            tweets = g_list_append(tweets, parse_single_tweet(post_obj));
        }

        // 3. replies
        if (json_object_has_member(obj, "replies")) {
            JsonArray *arr = json_object_get_array_member(obj, "replies");
            for (guint i = 0; i < json_array_get_length(arr); i++) {
                JsonObject *post_obj = json_array_get_object_element(arr, i);
                const gchar *id = json_object_get_string_member(post_obj, "id");
                // Safety check against duplicates
                if (main_id && g_strcmp0(id, main_id) == 0) continue;
                tweets = g_list_append(tweets, parse_single_tweet(post_obj));
            }
        }
        g_free(main_id);
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return tweets;
}

struct Profile*
parse_profile(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    struct Profile *profile = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "profile")) {
            JsonObject *p_obj = json_object_get_object_member(obj, "profile");
            profile = g_new0(struct Profile, 1);
            profile->name = g_strdup(json_object_get_string_member(p_obj, "name"));
            profile->username = g_strdup(json_object_get_string_member(p_obj, "username"));
            profile->bio = g_strdup(json_object_get_string_member(p_obj, "bio"));
            if (json_object_has_member(p_obj, "avatar") && !json_node_is_null(json_object_get_member(p_obj, "avatar"))) {
                profile->avatar = g_strdup(json_object_get_string_member(p_obj, "avatar"));
            }
            profile->follower_count = json_object_get_int_member(p_obj, "follower_count");
            profile->following_count = json_object_get_int_member(p_obj, "following_count");
            profile->post_count = json_object_get_int_member(p_obj, "post_count");
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return profile;
}

GList*
parse_profile_replies(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *tweets = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "replies")) {
            JsonArray *replies = json_object_get_array_member(obj, "replies");
            for (guint i = 0; i < json_array_get_length(replies); i++) {
                JsonNode *reply_node = json_array_get_element(replies, i);
                JsonObject *reply_obj = json_node_get_object(reply_node);
                JsonObject *author_obj = json_object_get_object_member(reply_obj, "author");

                struct Tweet *tweet = g_new0(struct Tweet, 1);
                tweet->content = g_strdup(json_object_get_string_member(reply_obj, "content"));
                tweet->author_name = g_strdup(json_object_get_string_member(author_obj, "name"));
                tweet->author_username = g_strdup(json_object_get_string_member(author_obj, "username"));
                if (json_object_has_member(author_obj, "avatar") && !json_node_is_null(json_object_get_member(author_obj, "avatar"))) {
                    tweet->author_avatar = g_strdup(json_object_get_string_member(author_obj, "avatar"));
                }
                tweet->id = g_strdup(json_object_get_string_member(reply_obj, "id"));
                
                if (json_object_has_member(reply_obj, "fact_check") && !json_node_is_null(json_object_get_member(reply_obj, "fact_check"))) {
                    JsonNode *fc_node = json_object_get_member(reply_obj, "fact_check");
                    if (JSON_NODE_HOLDS_OBJECT(fc_node)) {
                        JsonObject *fact_check = json_node_get_object(fc_node);
                        if (json_object_has_member(fact_check, "note")) {
                            tweet->note = g_strdup(json_object_get_string_member(fact_check, "note"));
                        }
                        if (json_object_has_member(fact_check, "severity")) {
                            tweet->note_severity = g_strdup(json_object_get_string_member(fact_check, "severity"));
                        }
                    }
                }

                tweet->attachments = parse_attachments(reply_obj);

                tweet->liked = FALSE;
                if (json_object_has_member(reply_obj, "liked_by_user")) {
                    JsonNode *node = json_object_get_member(reply_obj, "liked_by_user");
                    if (JSON_NODE_HOLDS_VALUE(node)) {
                        if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                            tweet->liked = json_node_get_boolean(node);
                        else if (json_node_get_value_type(node) == G_TYPE_INT64)
                            tweet->liked = json_node_get_int(node) != 0;
                    }
                } else if (json_object_has_member(reply_obj, "liked")) {
                    tweet->liked = json_object_get_boolean_member(reply_obj, "liked");
                } else if (json_object_has_member(reply_obj, "is_liked")) {
                    tweet->liked = json_object_get_boolean_member(reply_obj, "is_liked");
                } else if (json_object_has_member(reply_obj, "user_liked")) {
                    tweet->liked = json_object_get_boolean_member(reply_obj, "user_liked");
                }

                tweet->retweeted = FALSE;
                if (json_object_has_member(reply_obj, "retweeted_by_user")) {
                    JsonNode *node = json_object_get_member(reply_obj, "retweeted_by_user");
                    if (JSON_NODE_HOLDS_VALUE(node)) {
                        if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                            tweet->retweeted = json_node_get_boolean(node);
                        else if (json_node_get_value_type(node) == G_TYPE_INT64)
                            tweet->retweeted = json_node_get_int(node) != 0;
                    }
                } else if (json_object_has_member(reply_obj, "retweeted")) {
                    tweet->retweeted = json_object_get_boolean_member(reply_obj, "retweeted");
                } else if (json_object_has_member(reply_obj, "is_retweeted")) {
                    tweet->retweeted = json_object_get_boolean_member(reply_obj, "is_retweeted");
                } else if (json_object_has_member(reply_obj, "user_retweeted")) {
                    tweet->retweeted = json_object_get_boolean_member(reply_obj, "user_retweeted");
                }

                tweet->bookmarked = FALSE;
                if (json_object_has_member(reply_obj, "bookmarked")) {
                    tweet->bookmarked = json_object_get_boolean_member(reply_obj, "bookmarked");
                } else if (json_object_has_member(reply_obj, "is_bookmarked")) {
                    tweet->bookmarked = json_object_get_boolean_member(reply_obj, "is_bookmarked");
                } else if (json_object_has_member(reply_obj, "user_bookmarked")) {
                    tweet->bookmarked = json_object_get_boolean_member(reply_obj, "user_bookmarked");
                }

                if (json_object_has_member(reply_obj, "likes")) {
                    tweet->like_count = json_object_get_int_member(reply_obj, "likes");
                }
                if (json_object_has_member(reply_obj, "retweets")) {
                    tweet->retweet_count = json_object_get_int_member(reply_obj, "retweets");
                }
                if (json_object_has_member(reply_obj, "replies")) {
                    tweet->reply_count = json_object_get_int_member(reply_obj, "replies");
                }

                tweets = g_list_append(tweets, tweet);
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return tweets;
}

GList*
parse_users(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *users = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "users")) {
            JsonArray *users_array = json_object_get_array_member(obj, "users");
            for (guint i = 0; i < json_array_get_length(users_array); i++) {
                JsonNode *user_node = json_array_get_element(users_array, i);
                JsonObject *user_obj = json_node_get_object(user_node);
                struct Profile *user = g_new0(struct Profile, 1);
                user->name = g_strdup(json_object_get_string_member(user_obj, "name"));
                user->username = g_strdup(json_object_get_string_member(user_obj, "username"));
                
                if (json_object_has_member(user_obj, "bio") && !json_node_is_null(json_object_get_member(user_obj, "bio"))) {
                    user->bio = g_strdup(json_object_get_string_member(user_obj, "bio"));
                } else {
                    user->bio = NULL;
                }
                if (json_object_has_member(user_obj, "avatar") && !json_node_is_null(json_object_get_member(user_obj, "avatar"))) {
                    user->avatar = g_strdup(json_object_get_string_member(user_obj, "avatar"));
                }
                user->follower_count = json_object_has_member(user_obj, "follower_count") ? json_object_get_int_member(user_obj, "follower_count") : 0;
                users = g_list_append(users, user);
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return users;
}

GList*
parse_notifications(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *notifications = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "notifications")) {
            JsonArray *notif_array = json_object_get_array_member(obj, "notifications");
            for (guint i = 0; i < json_array_get_length(notif_array); i++) {
                JsonNode *notif_node = json_array_get_element(notif_array, i);
                JsonObject *notif_obj = json_node_get_object(notif_node);
                struct Notification *notif = g_new0(struct Notification, 1);
                
                notif->id = g_strdup(json_object_get_string_member(notif_obj, "id"));
                notif->type = g_strdup(json_object_get_string_member(notif_obj, "type"));
                notif->content = g_strdup(json_object_get_string_member(notif_obj, "content"));
                
                if (json_object_has_member(notif_obj, "related_id") && !json_node_is_null(json_object_get_member(notif_obj, "related_id"))) {
                    notif->related_id = g_strdup(json_object_get_string_member(notif_obj, "related_id"));
                }
                
                if (json_object_has_member(notif_obj, "actor_id") && !json_node_is_null(json_object_get_member(notif_obj, "actor_id"))) {
                    notif->actor_id = g_strdup(json_object_get_string_member(notif_obj, "actor_id"));
                }
                
                if (json_object_has_member(notif_obj, "actor_username") && !json_node_is_null(json_object_get_member(notif_obj, "actor_username"))) {
                    notif->actor_username = g_strdup(json_object_get_string_member(notif_obj, "actor_username"));
                }
                
                if (json_object_has_member(notif_obj, "actor_name") && !json_node_is_null(json_object_get_member(notif_obj, "actor_name"))) {
                    notif->actor_name = g_strdup(json_object_get_string_member(notif_obj, "actor_name"));
                }
                
                if (json_object_has_member(notif_obj, "actor_avatar") && !json_node_is_null(json_object_get_member(notif_obj, "actor_avatar"))) {
                    notif->actor_avatar = g_strdup(json_object_get_string_member(notif_obj, "actor_avatar"));
                }
                
                notif->read = json_object_get_boolean_member(notif_obj, "read");
                notif->created_at = g_strdup(json_object_get_string_member(notif_obj, "created_at"));
                
                notifications = g_list_append(notifications, notif);
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return notifications;
}

GList*
parse_conversations(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *conversations = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "conversations")) {
            JsonArray *conv_array = json_object_get_array_member(obj, "conversations");
            for (guint i = 0; i < json_array_get_length(conv_array); i++) {
                JsonNode *conv_node = json_array_get_element(conv_array, i);
                JsonObject *conv_obj = json_node_get_object(conv_node);
                struct Conversation *conv = g_new0(struct Conversation, 1);
                
                conv->id = g_strdup(json_object_get_string_member(conv_obj, "id"));
                conv->type = g_strdup(json_object_get_string_member(conv_obj, "type"));
                
                if (json_object_has_member(conv_obj, "title") && !json_node_is_null(json_object_get_member(conv_obj, "title"))) {
                    conv->title = g_strdup(json_object_get_string_member(conv_obj, "title"));
                }
                
                if (json_object_has_member(conv_obj, "displayName") && !json_node_is_null(json_object_get_member(conv_obj, "displayName"))) {
                    conv->display_name = g_strdup(json_object_get_string_member(conv_obj, "displayName"));
                }
                
                if (json_object_has_member(conv_obj, "displayAvatar") && !json_node_is_null(json_object_get_member(conv_obj, "displayAvatar"))) {
                    conv->display_avatar = g_strdup(json_object_get_string_member(conv_obj, "displayAvatar"));
                }

                if (json_object_has_member(conv_obj, "last_message_content") && !json_node_is_null(json_object_get_member(conv_obj, "last_message_content"))) {
                    conv->last_message_content = g_strdup(json_object_get_string_member(conv_obj, "last_message_content"));
                }

                if (json_object_has_member(conv_obj, "last_message_time") && !json_node_is_null(json_object_get_member(conv_obj, "last_message_time"))) {
                    conv->last_message_time = g_strdup(json_object_get_string_member(conv_obj, "last_message_time"));
                }

                conv->unread_count = json_object_get_int_member(conv_obj, "unread_count");
                
                conversations = g_list_append(conversations, conv);
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return conversations;
}

GList*
parse_messages(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *messages = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "messages")) {
            JsonArray *msg_array = json_object_get_array_member(obj, "messages");
            for (guint i = 0; i < json_array_get_length(msg_array); i++) {
                JsonNode *msg_node = json_array_get_element(msg_array, i);
                JsonObject *msg_obj = json_node_get_object(msg_node);
                struct DirectMessage *msg = g_new0(struct DirectMessage, 1);
                
                msg->id = g_strdup(json_object_get_string_member(msg_obj, "id"));
                msg->conversation_id = g_strdup(json_object_get_string_member(msg_obj, "conversation_id"));
                msg->sender_id = g_strdup(json_object_get_string_member(msg_obj, "sender_id"));
                msg->content = g_strdup(json_object_get_string_member(msg_obj, "content"));
                msg->username = g_strdup(json_object_get_string_member(msg_obj, "username"));
                msg->name = g_strdup(json_object_get_string_member(msg_obj, "name"));
                
                if (json_object_has_member(msg_obj, "avatar") && !json_node_is_null(json_object_get_member(msg_obj, "avatar"))) {
                    msg->avatar = g_strdup(json_object_get_string_member(msg_obj, "avatar"));
                }
                
                msg->created_at = g_strdup(json_object_get_string_member(msg_obj, "created_at"));
                msg->attachments = parse_attachments(msg_obj);
                
                messages = g_list_append(messages, msg);
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return messages;
}

gchar*
construct_dm_payload(const gchar *content)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, content);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    g_object_unref(gen);
    g_object_unref(builder);
    return post_data;
}

gchar*
parse_admin_stats(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    gchar *result = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);

        if (json_object_has_member(obj, "userStats") && json_object_has_member(obj, "postStats")) {
            JsonObject *user_stats = json_object_get_object_member(obj, "userStats");
            JsonObject *post_stats = json_object_get_object_member(obj, "postStats");
            
            result = g_strdup_printf(
                "User Statistics:\n"
                "  Total Users: %" G_GINT64_FORMAT "\n"
                "  Suspended Users: %" G_GINT64_FORMAT "\n"
                "  Admins: %" G_GINT64_FORMAT "\n"
                "  Verified Users: %" G_GINT64_FORMAT "\n\n"
                "Post Statistics:\n"
                "  Total Posts: %" G_GINT64_FORMAT "\n"
                "  Total Likes: %" G_GINT64_FORMAT "\n"
                "  Total Retweets: %" G_GINT64_FORMAT "",
                json_object_get_int_member(user_stats, "total"),
                json_object_get_int_member(user_stats, "suspended"),
                json_object_get_int_member(user_stats, "admins"),
                json_object_get_int_member(user_stats, "verified"),
                json_object_get_int_member(post_stats, "totalPosts"),
                json_object_get_int_member(post_stats, "totalLikes"),
                json_object_get_int_member(post_stats, "totalRetweets")
            );
        }
    } else {
        g_error_free(error);
    }

    g_object_unref(parser);
    return result;
}

gboolean
parse_login_response(const gchar *json_data, gchar **token_out, gchar **username_out, gboolean *is_admin_out)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    gboolean success = FALSE;

    json_parser_load_from_data(parser, json_data, -1, &error);
    
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        
        if (json_object_has_member(obj, "token") && json_object_has_member(obj, "user")) {
            const gchar *token = json_object_get_string_member(obj, "token");
            JsonObject *user_obj = json_object_get_object_member(obj, "user");
            
            if (user_obj && json_object_has_member(user_obj, "username")) {
                const gchar *uname = json_object_get_string_member(user_obj, "username");
                if (token && uname) {
                    *token_out = g_strdup(token);
                    *username_out = g_strdup(uname);
                    
                    if (is_admin_out) {
                        *is_admin_out = FALSE;
                        if (json_object_has_member(user_obj, "admin")) {
                            JsonNode *node = json_object_get_member(user_obj, "admin");
                            if (JSON_NODE_HOLDS_VALUE(node)) {
                                if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                                    *is_admin_out = json_node_get_boolean(node);
                                else
                                    *is_admin_out = (json_node_get_int(node) != 0);
                            }
                        }
                    }
                    success = TRUE;
                }
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return success;
}

gboolean
parse_user_me_response(const gchar *json_data, gboolean *is_admin_out)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    gboolean success = FALSE;

    json_parser_load_from_data(parser, json_data, -1, &error);
    
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        
        if (json_object_has_member(obj, "user")) {
            JsonObject *user_obj = json_object_get_object_member(obj, "user");
            if (is_admin_out) {
                *is_admin_out = FALSE;
                
                if (json_object_has_member(user_obj, "admin")) {
                    JsonNode *node = json_object_get_member(user_obj, "admin");
                    if (JSON_NODE_HOLDS_VALUE(node)) {
                        if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                            *is_admin_out = json_node_get_boolean(node);
                        else
                            *is_admin_out = (json_node_get_int(node) != 0);
                    }
                }
                
                if (!(*is_admin_out) && json_object_has_member(user_obj, "superadmin")) {
                    JsonNode *node = json_object_get_member(user_obj, "superadmin");
                    if (JSON_NODE_HOLDS_VALUE(node)) {
                        if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                            *is_admin_out = json_node_get_boolean(node);
                        else
                            *is_admin_out = (json_node_get_int(node) != 0);
                    }
                }
            }
            success = TRUE;
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return success;
}

gchar*
construct_tweet_payload(const gchar *content, const gchar *reply_to_id)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, content);

    json_builder_set_member_name(builder, "source");
    json_builder_add_string_value(builder, "Tweeta Desktop");

    if (reply_to_id) {
        json_builder_set_member_name(builder, "reply_to");
        json_builder_add_string_value(builder, reply_to_id);
    }

    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    g_object_unref(gen);
    g_object_unref(builder);
    return post_data;
}

void
free_attachment(gpointer data)
{
    struct Attachment *attach = data;
    if (attach) {
        g_free(attach->id);
        g_free(attach->file_url);
        g_free(attach->file_type);
        g_free(attach);
    }
}

void
free_tweet(gpointer data)
{
    struct Tweet *tweet = data;
    g_free(tweet->content);
    g_free(tweet->author_name);
    g_free(tweet->author_username);
    g_free(tweet->author_avatar);
    g_free(tweet->id);
    g_free(tweet->note);
    g_free(tweet->note_severity);
    if (tweet->attachments) {
        g_list_free_full(tweet->attachments, free_attachment);
    }
    g_free(tweet);
}

void
free_tweets(GList *tweets)
{
    g_list_free_full(tweets, free_tweet);
}

void
free_user(gpointer data)
{
    struct Profile *user = data;
    g_free(user->name);
    g_free(user->username);
    g_free(user->bio);
    g_free(user->avatar);
    g_free(user);
}

void
free_users(GList *users)
{
    g_list_free_full(users, free_user);
}

void
free_notification(gpointer data)
{
    struct Notification *notif = data;
    if (notif) {
        g_free(notif->id);
        g_free(notif->type);
        g_free(notif->content);
        g_free(notif->related_id);
        g_free(notif->actor_id);
        g_free(notif->actor_username);
        g_free(notif->actor_name);
        g_free(notif->actor_avatar);
        g_free(notif->created_at);
        g_free(notif);
    }
}

void
free_notifications(GList *notifications)
{
    g_list_free_full(notifications, free_notification);
}

void
free_conversation(gpointer data)
{
    struct Conversation *conv = data;
    if (conv) {
        g_free(conv->id);
        g_free(conv->type);
        g_free(conv->title);
        g_free(conv->display_name);
        g_free(conv->display_avatar);
        g_free(conv->last_message_content);
        g_free(conv->last_message_time);
        if (conv->participants) {
            g_list_free_full(conv->participants, free_user);
        }
        g_free(conv);
    }
}

void
free_conversations(GList *conversations)
{
    g_list_free_full(conversations, free_conversation);
}

void
free_message(gpointer data)
{
    struct DirectMessage *msg = data;
    if (msg) {
        g_free(msg->id);
        g_free(msg->conversation_id);
        g_free(msg->sender_id);
        g_free(msg->content);
        g_free(msg->username);
        g_free(msg->name);
        g_free(msg->avatar);
        g_free(msg->created_at);
        if (msg->attachments) {
            g_list_free_full(msg->attachments, free_attachment);
        }
        g_free(msg);
    }
}

void
free_messages(GList *messages)
{
    g_list_free_full(messages, free_message);
}
