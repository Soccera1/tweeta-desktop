#include <json-glib/json-glib.h>
#include "json_utils.h"

extern gboolean get_cached_liked(const gchar *tweet_id);
extern gboolean get_cached_retweeted(const gchar *tweet_id);
extern gboolean get_cached_bookmarked(const gchar *tweet_id);

static struct Tweet* parse_tweet_from_object(JsonObject *post_object);
static struct Poll* parse_poll(JsonObject *poll_object);
static void free_poll_data(struct Poll *poll);
static gboolean response_has_empty_array_member(const gchar *json_data, const gchar * const *keys);
static struct Profile* parse_profile_summary_from_object(JsonObject *user_obj);
static struct Conversation* parse_conversation_from_object(JsonObject *conv_obj);

static JsonArray*
get_array_member_if_valid(JsonObject *obj, const gchar *key)
{
    JsonNode *node;

    if (!obj || !key || !json_object_has_member(obj, key))
        return NULL;

    node = json_object_get_member(obj, key);
    if (!node || !JSON_NODE_HOLDS_ARRAY(node))
        return NULL;

    return json_node_get_array(node);
}

static JsonObject*
get_object_member_if_valid(JsonObject *obj, const gchar *key)
{
    JsonNode *node;

    if (!obj || !key || !json_object_has_member(obj, key))
        return NULL;

    node = json_object_get_member(obj, key);
    if (!node || !JSON_NODE_HOLDS_OBJECT(node))
        return NULL;

    return json_node_get_object(node);
}

static JsonObject*
get_object_element_if_valid(JsonArray *array, guint index)
{
    JsonNode *node;

    if (!array)
        return NULL;

    node = json_array_get_element(array, index);
    if (!node || !JSON_NODE_HOLDS_OBJECT(node))
        return NULL;

    return json_node_get_object(node);
}

static JsonArray*
get_first_array_member(JsonObject *obj, const gchar * const *keys)
{
    for (guint i = 0; keys[i] != NULL; i++) {
        JsonArray *array = get_array_member_if_valid(obj, keys[i]);
        if (array)
            return array;
    }
    return NULL;
}

static struct Profile*
parse_profile_summary_from_object(JsonObject *user_obj)
{
    struct Profile *user;

    if (!user_obj) {
        return NULL;
    }

    user = g_new0(struct Profile, 1);

    if (json_object_has_member(user_obj, "id") && !json_node_is_null(json_object_get_member(user_obj, "id")))
        user->id = g_strdup(json_object_get_string_member(user_obj, "id"));
    if (json_object_has_member(user_obj, "name") && !json_node_is_null(json_object_get_member(user_obj, "name")))
        user->name = g_strdup(json_object_get_string_member(user_obj, "name"));
    if (json_object_has_member(user_obj, "username") && !json_node_is_null(json_object_get_member(user_obj, "username")))
        user->username = g_strdup(json_object_get_string_member(user_obj, "username"));
    if (json_object_has_member(user_obj, "bio") && !json_node_is_null(json_object_get_member(user_obj, "bio")))
        user->bio = g_strdup(json_object_get_string_member(user_obj, "bio"));
    if (json_object_has_member(user_obj, "avatar") && !json_node_is_null(json_object_get_member(user_obj, "avatar")))
        user->avatar = g_strdup(json_object_get_string_member(user_obj, "avatar"));
    if (json_object_has_member(user_obj, "banner") && !json_node_is_null(json_object_get_member(user_obj, "banner")))
        user->banner = g_strdup(json_object_get_string_member(user_obj, "banner"));

    if (json_object_has_member(user_obj, "follower_count"))
        user->follower_count = json_object_get_int_member(user_obj, "follower_count");
    if (json_object_has_member(user_obj, "following_count"))
        user->following_count = json_object_get_int_member(user_obj, "following_count");
    if (json_object_has_member(user_obj, "post_count"))
        user->post_count = json_object_get_int_member(user_obj, "post_count");
    if (json_object_has_member(user_obj, "avatar_radius"))
        user->avatar_radius = json_object_get_int_member(user_obj, "avatar_radius");

    if (json_object_has_member(user_obj, "verified")) {
        JsonNode *node = json_object_get_member(user_obj, "verified");
        if (node && JSON_NODE_HOLDS_VALUE(node)) {
            if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                user->author_verified = json_node_get_boolean(node);
            else
                user->author_verified = json_node_get_int(node) != 0;
        }
    }
    if (json_object_has_member(user_obj, "gold")) {
        JsonNode *node = json_object_get_member(user_obj, "gold");
        if (node && JSON_NODE_HOLDS_VALUE(node)) {
            if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                user->author_gold = json_node_get_boolean(node);
            else
                user->author_gold = json_node_get_int(node) != 0;
        }
    }
    if (json_object_has_member(user_obj, "gray")) {
        JsonNode *node = json_object_get_member(user_obj, "gray");
        if (node && JSON_NODE_HOLDS_VALUE(node)) {
            if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                user->author_gray = json_node_get_boolean(node);
            else
                user->author_gray = json_node_get_int(node) != 0;
        }
    }

    return user;
}

static struct Community*
parse_community_from_object(JsonObject *comm_obj)
{
    struct Community *comm = g_new0(struct Community, 1);

    if (json_object_has_member(comm_obj, "id"))
        comm->id = g_strdup(json_object_get_string_member(comm_obj, "id"));

    if (json_object_has_member(comm_obj, "name"))
        comm->name = g_strdup(json_object_get_string_member(comm_obj, "name"));

    if (json_object_has_member(comm_obj, "description") &&
        !json_node_is_null(json_object_get_member(comm_obj, "description")))
        comm->description = g_strdup(json_object_get_string_member(comm_obj, "description"));

    if (json_object_has_member(comm_obj, "rules") &&
        !json_node_is_null(json_object_get_member(comm_obj, "rules")))
        comm->rules = g_strdup(json_object_get_string_member(comm_obj, "rules"));

    if (json_object_has_member(comm_obj, "icon_url") &&
        !json_node_is_null(json_object_get_member(comm_obj, "icon_url")))
        comm->icon_url = g_strdup(json_object_get_string_member(comm_obj, "icon_url"));

    if (json_object_has_member(comm_obj, "banner_url") &&
        !json_node_is_null(json_object_get_member(comm_obj, "banner_url")))
        comm->banner_url = g_strdup(json_object_get_string_member(comm_obj, "banner_url"));

    if (json_object_has_member(comm_obj, "access_mode"))
        comm->access_mode = g_strdup(json_object_get_string_member(comm_obj, "access_mode"));

    if (json_object_has_member(comm_obj, "member_count"))
        comm->member_count = json_object_get_int_member(comm_obj, "member_count");

    if (json_object_has_member(comm_obj, "is_member"))
        comm->is_member = json_object_get_boolean_member(comm_obj, "is_member");

    if (json_object_has_member(comm_obj, "is_admin"))
        comm->is_admin = json_object_get_boolean_member(comm_obj, "is_admin");

    if (json_object_has_member(comm_obj, "is_moderator"))
        comm->is_moderator = json_object_get_boolean_member(comm_obj, "is_moderator");

    if (json_object_has_member(comm_obj, "tag_enabled"))
        comm->tag_enabled = json_object_get_boolean_member(comm_obj, "tag_enabled");

    if (json_object_has_member(comm_obj, "tag_emoji") &&
        !json_node_is_null(json_object_get_member(comm_obj, "tag_emoji")))
        comm->tag_emoji = g_strdup(json_object_get_string_member(comm_obj, "tag_emoji"));

    if (json_object_has_member(comm_obj, "tag_text") &&
        !json_node_is_null(json_object_get_member(comm_obj, "tag_text")))
        comm->tag_text = g_strdup(json_object_get_string_member(comm_obj, "tag_text"));

    return comm;
}

static struct Conversation*
parse_conversation_from_object(JsonObject *conv_obj)
{
    struct Conversation *conv;

    if (!conv_obj) {
        return NULL;
    }

    conv = g_new0(struct Conversation, 1);

    if (json_object_has_member(conv_obj, "id") && !json_node_is_null(json_object_get_member(conv_obj, "id")))
        conv->id = g_strdup(json_object_get_string_member(conv_obj, "id"));
    if (json_object_has_member(conv_obj, "type") && !json_node_is_null(json_object_get_member(conv_obj, "type")))
        conv->type = g_strdup(json_object_get_string_member(conv_obj, "type"));
    if (json_object_has_member(conv_obj, "title") && !json_node_is_null(json_object_get_member(conv_obj, "title")))
        conv->title = g_strdup(json_object_get_string_member(conv_obj, "title"));
    if (json_object_has_member(conv_obj, "displayName") && !json_node_is_null(json_object_get_member(conv_obj, "displayName")))
        conv->display_name = g_strdup(json_object_get_string_member(conv_obj, "displayName"));
    else if (conv->title)
        conv->display_name = g_strdup(conv->title);
    if (json_object_has_member(conv_obj, "displayAvatar") && !json_node_is_null(json_object_get_member(conv_obj, "displayAvatar")))
        conv->display_avatar = g_strdup(json_object_get_string_member(conv_obj, "displayAvatar"));
    if (json_object_has_member(conv_obj, "last_message_content") && !json_node_is_null(json_object_get_member(conv_obj, "last_message_content")))
        conv->last_message_content = g_strdup(json_object_get_string_member(conv_obj, "last_message_content"));
    if (json_object_has_member(conv_obj, "last_message_time") && !json_node_is_null(json_object_get_member(conv_obj, "last_message_time")))
        conv->last_message_time = g_strdup(json_object_get_string_member(conv_obj, "last_message_time"));
    if (json_object_has_member(conv_obj, "last_message_sender") && !json_node_is_null(json_object_get_member(conv_obj, "last_message_sender")))
        conv->last_message_sender = g_strdup(json_object_get_string_member(conv_obj, "last_message_sender"));
    if (json_object_has_member(conv_obj, "lastMessageSenderName") && !json_node_is_null(json_object_get_member(conv_obj, "lastMessageSenderName")))
        conv->last_message_sender_name = g_strdup(json_object_get_string_member(conv_obj, "lastMessageSenderName"));
    if (json_object_has_member(conv_obj, "unread_count"))
        conv->unread_count = json_object_get_int_member(conv_obj, "unread_count");
    if (json_object_has_member(conv_obj, "participant_count"))
        conv->participant_count = json_object_get_int_member(conv_obj, "participant_count");
    if (json_object_has_member(conv_obj, "disappearing_enabled")) {
        JsonNode *node = json_object_get_member(conv_obj, "disappearing_enabled");
        if (node && JSON_NODE_HOLDS_VALUE(node)) {
            if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                conv->disappearing_enabled = json_node_get_boolean(node);
            else
                conv->disappearing_enabled = json_node_get_int(node) != 0;
        }
    }
    if (json_object_has_member(conv_obj, "disappearing_duration"))
        conv->disappearing_duration = json_object_get_int_member(conv_obj, "disappearing_duration");

    if (json_object_has_member(conv_obj, "participants")) {
        JsonArray *participants = json_object_get_array_member(conv_obj, "participants");
        for (guint i = 0; i < json_array_get_length(participants); i++) {
            JsonObject *participant_obj = json_array_get_object_element(participants, i);
            struct Profile *participant = parse_profile_summary_from_object(participant_obj);
            if (participant)
                conv->participants = g_list_append(conv->participants, participant);
        }
    }

    return conv;
}

static void
parse_interaction_state(JsonObject *post_object, struct Tweet *tweet)
{
    tweet->liked = FALSE;
    if (json_object_has_member(post_object, "liked_by_user")) {
        JsonNode *node = json_object_get_member(post_object, "liked_by_user");
        if (node && JSON_NODE_HOLDS_VALUE(node)) {
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

    if (!json_object_has_member(post_object, "liked_by_user") &&
        !json_object_has_member(post_object, "liked") &&
        !json_object_has_member(post_object, "is_liked") &&
        !json_object_has_member(post_object, "user_liked")) {
        tweet->liked = get_cached_liked(tweet->id);
    }

    tweet->retweeted = FALSE;
    if (json_object_has_member(post_object, "retweeted_by_user")) {
        JsonNode *node = json_object_get_member(post_object, "retweeted_by_user");
        if (node && JSON_NODE_HOLDS_VALUE(node)) {
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

    if (!json_object_has_member(post_object, "retweeted_by_user") &&
        !json_object_has_member(post_object, "retweeted") &&
        !json_object_has_member(post_object, "is_retweeted") &&
        !json_object_has_member(post_object, "user_retweeted")) {
        tweet->retweeted = get_cached_retweeted(tweet->id);
    }

    tweet->bookmarked = FALSE;
    if (json_object_has_member(post_object, "bookmarked_by_user")) {
        JsonNode *node = json_object_get_member(post_object, "bookmarked_by_user");
        if (node && JSON_NODE_HOLDS_VALUE(node)) {
            if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                tweet->bookmarked = json_node_get_boolean(node);
            else if (json_node_get_value_type(node) == G_TYPE_INT64)
                tweet->bookmarked = json_node_get_int(node) != 0;
        }
    } else if (json_object_has_member(post_object, "bookmarked")) {
        tweet->bookmarked = json_object_get_boolean_member(post_object, "bookmarked");
    } else if (json_object_has_member(post_object, "is_bookmarked")) {
        tweet->bookmarked = json_object_get_boolean_member(post_object, "is_bookmarked");
    } else if (json_object_has_member(post_object, "user_bookmarked")) {
        tweet->bookmarked = json_object_get_boolean_member(post_object, "user_bookmarked");
    }

    if (!json_object_has_member(post_object, "bookmarked_by_user") &&
        !json_object_has_member(post_object, "bookmarked") &&
        !json_object_has_member(post_object, "is_bookmarked") &&
        !json_object_has_member(post_object, "user_bookmarked")) {
        tweet->bookmarked = get_cached_bookmarked(tweet->id);
    }
}

static GList*
parse_attachments(JsonObject *post_object)
{
    GList *attachments = NULL;
    JsonArray *attach_array = get_array_member_if_valid(post_object, "attachments");

    if (!attach_array)
        return NULL;

    for (guint i = 0; i < json_array_get_length(attach_array); i++) {
        JsonObject *attach_obj = get_object_element_if_valid(attach_array, i);
        struct Attachment *attach;

        if (!attach_obj)
            continue;

        attach = g_new0(struct Attachment, 1);
        attach->id = g_strdup(json_object_get_string_member(attach_obj, "id"));
        attach->file_url = g_strdup(json_object_get_string_member(attach_obj, "file_url"));
        attach->file_type = g_strdup(json_object_get_string_member(attach_obj, "file_type"));
        if (json_object_has_member(attach_obj, "file_hash") && !json_node_is_null(json_object_get_member(attach_obj, "file_hash")))
            attach->file_hash = g_strdup(json_object_get_string_member(attach_obj, "file_hash"));
        if (json_object_has_member(attach_obj, "file_name") && !json_node_is_null(json_object_get_member(attach_obj, "file_name")))
            attach->file_name = g_strdup(json_object_get_string_member(attach_obj, "file_name"));
        if (json_object_has_member(attach_obj, "file_size"))
            attach->file_size = json_object_get_int_member(attach_obj, "file_size");
        if (json_object_has_member(attach_obj, "is_spoiler")) {
            JsonNode *sp_node = json_object_get_member(attach_obj, "is_spoiler");
            if (sp_node && JSON_NODE_HOLDS_VALUE(sp_node)) {
                if (json_node_get_value_type(sp_node) == G_TYPE_BOOLEAN)
                    attach->is_spoiler = json_node_get_boolean(sp_node);
                else
                    attach->is_spoiler = json_node_get_int(sp_node) != 0;
            }
        }
        attachments = g_list_append(attachments, attach);
    }

    return attachments;
}

static struct Poll*
parse_poll(JsonObject *poll_object)
{
    if (!poll_object)
        return NULL;

    struct Poll *poll = g_new0(struct Poll, 1);
    
    if (json_object_has_member(poll_object, "id"))
        poll->id = g_strdup(json_object_get_string_member(poll_object, "id"));
    
    if (json_object_has_member(poll_object, "question"))
        poll->question = g_strdup(json_object_get_string_member(poll_object, "question"));
    
    if (json_object_has_member(poll_object, "isExpired")) {
        poll->is_active = !json_object_get_boolean_member(poll_object, "isExpired");
    } else if (json_object_has_member(poll_object, "is_active")) {
        JsonNode *node = json_object_get_member(poll_object, "is_active");
        if (node && JSON_NODE_HOLDS_VALUE(node)) {
            if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                poll->is_active = json_node_get_boolean(node);
            else
                poll->is_active = json_node_get_int(node) != 0;
        }
    }
    
    if (json_object_has_member(poll_object, "expires_at") && 
        !json_node_is_null(json_object_get_member(poll_object, "expires_at")))
        poll->expires_at = g_strdup(json_object_get_string_member(poll_object, "expires_at"));
    
    if (json_object_has_member(poll_object, "totalVotes"))
        poll->total_votes = json_object_get_int_member(poll_object, "totalVotes");
    else if (json_object_has_member(poll_object, "total_votes"))
        poll->total_votes = json_object_get_int_member(poll_object, "total_votes");
    
    {
        JsonArray *options_array = get_array_member_if_valid(poll_object, "options");
        if (options_array) {
            for (guint i = 0; i < json_array_get_length(options_array); i++) {
                JsonObject *option_obj = get_object_element_if_valid(options_array, i);
                struct PollOption *option = g_new0(struct PollOption, 1);

                if (!option_obj) {
                    g_free(option);
                    continue;
                }

                if (json_object_has_member(option_obj, "id"))
                    option->id = g_strdup(json_object_get_string_member(option_obj, "id"));

                if (json_object_has_member(option_obj, "option_text"))
                    option->option_text = g_strdup(json_object_get_string_member(option_obj, "option_text"));

                if (json_object_has_member(option_obj, "vote_count"))
                    option->vote_count = json_object_get_int_member(option_obj, "vote_count");

                if (json_object_has_member(option_obj, "percentage"))
                    option->percentage = json_object_get_int_member(option_obj, "percentage");

                if (json_object_has_member(option_obj, "voted"))
                    option->voted = json_object_get_boolean_member(option_obj, "voted");

                if (json_object_has_member(poll_object, "userVote") && !json_node_is_null(json_object_get_member(poll_object, "userVote"))) {
                    const gchar *user_vote = json_object_get_string_member(poll_object, "userVote");
                    if (user_vote && option->id && g_strcmp0(user_vote, option->id) == 0) {
                        option->user_vote = g_strdup(user_vote);
                        option->voted = TRUE;
                    }
                }

                poll->options = g_list_append(poll->options, option);
            }
        }
    }
    
    return poll;
}

GList*
parse_tweets(const gchar *json_data)
{
    JsonParser *parser;
    GError *error = NULL;
    GList *tweets = NULL;

    if (!json_data) {
        g_warning("parse_tweets: json_data is NULL");
        return NULL;
    }

    parser = json_parser_new();
    json_parser_load_from_data(parser, json_data, -1, &error);
    if (error) {
        g_warning("Unable to parse json: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root_node = json_parser_get_root(parser);
    if (!root_node || !JSON_NODE_HOLDS_OBJECT(root_node)) {
        g_warning("parse_tweets: invalid JSON structure");
        g_object_unref(parser);
        return NULL;
    }

    JsonObject *root_object = json_node_get_object(root_node);

    if (json_object_has_member(root_object, "error")) {
        const gchar *error_msg = json_object_get_string_member(root_object, "error");
        g_warning("API returned error: %s", error_msg ? error_msg : "(null)");
        g_object_unref(parser);
        return NULL;
    }

    JsonArray *posts = NULL;
    const gchar *key = NULL;

    if (json_object_has_member(root_object, "posts")) {
        posts = get_array_member_if_valid(root_object, "posts");
        key = "posts";
    } else if (json_object_has_member(root_object, "timeline")) {
        posts = get_array_member_if_valid(root_object, "timeline");
        key = "timeline";
    } else if (json_object_has_member(root_object, "bookmarks")) {
        posts = get_array_member_if_valid(root_object, "bookmarks");
        key = "bookmarks";
    } else if (json_object_has_member(root_object, "tweets")) {
        posts = get_array_member_if_valid(root_object, "tweets");
        key = "tweets";
    }

    if (posts && key) {
        for (guint i = 0; i < json_array_get_length(posts); i++) {
            JsonObject *post_object = get_object_element_if_valid(posts, i);
            struct Tweet *tweet = parse_tweet_from_object(post_object);

            if (!tweet)
                continue;

            g_debug("parse_tweets: parsed tweet id=%s, liked=%d, retweeted=%d, bookmarked=%d",
                tweet->id ? tweet->id : "(null)", tweet->liked, tweet->retweeted, tweet->bookmarked);
            tweets = g_list_append(tweets, tweet);
        }
    }
    g_object_unref(parser);
    return tweets;
}

gboolean
tweets_response_is_empty(const gchar *json_data)
{
    static const gchar * const tweet_keys[] = {"posts", "timeline", "bookmarks", "tweets", NULL};
    return response_has_empty_array_member(json_data, tweet_keys);
}

static struct Tweet*
parse_tweet_from_object(JsonObject *post_object)
{
    JsonObject *author_object = NULL;

    if (!post_object)
        return NULL;

    author_object = get_object_member_if_valid(post_object, "author");

    struct Tweet *tweet = g_new0(struct Tweet, 1);
    
    if (json_object_has_member(post_object, "id"))
        tweet->id = g_strdup(json_object_get_string_member(post_object, "id"));

    tweet->content = g_strdup(json_object_get_string_member(post_object, "content"));
    if (json_object_has_member(post_object, "pinned")) {
        JsonNode *node = json_object_get_member(post_object, "pinned");
        if (node && JSON_NODE_HOLDS_VALUE(node)) {
            if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                tweet->pinned = json_node_get_boolean(node);
            else
                tweet->pinned = json_node_get_int(node) != 0;
        }
    }

    if (author_object) {
        if (json_object_has_member(author_object, "id") && !json_node_is_null(json_object_get_member(author_object, "id")))
            tweet->author_id = g_strdup(json_object_get_string_member(author_object, "id"));
        tweet->author_name = g_strdup(json_object_get_string_member(author_object, "name"));
        tweet->author_username = g_strdup(json_object_get_string_member(author_object, "username"));
        if (json_object_has_member(author_object, "avatar") && !json_node_is_null(json_object_get_member(author_object, "avatar"))) {
            tweet->author_avatar = g_strdup(json_object_get_string_member(author_object, "avatar"));
        }
        if (json_object_has_member(author_object, "verified")) {
            JsonNode *node = json_object_get_member(author_object, "verified");
            if (node && JSON_NODE_HOLDS_VALUE(node)) {
                if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                    tweet->author_verified = json_node_get_boolean(node);
                else
                    tweet->author_verified = json_node_get_int(node) != 0;
            }
        }
        if (json_object_has_member(author_object, "gold")) {
            JsonNode *node = json_object_get_member(author_object, "gold");
            if (node && JSON_NODE_HOLDS_VALUE(node)) {
                if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                    tweet->author_gold = json_node_get_boolean(node);
                else
                    tweet->author_gold = json_node_get_int(node) != 0;
            }
        }
        if (json_object_has_member(author_object, "gray")) {
            JsonNode *node = json_object_get_member(author_object, "gray");
            if (node && JSON_NODE_HOLDS_VALUE(node)) {
                if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                    tweet->author_gray = json_node_get_boolean(node);
                else
                    tweet->author_gray = json_node_get_int(node) != 0;
            }
        }
    } else {
        if (json_object_has_member(post_object, "user_id") && !json_node_is_null(json_object_get_member(post_object, "user_id")))
            tweet->author_id = g_strdup(json_object_get_string_member(post_object, "user_id"));
        if (json_object_has_member(post_object, "username"))
            tweet->author_username = g_strdup(json_object_get_string_member(post_object, "username"));
        if (json_object_has_member(post_object, "name"))
            tweet->author_name = g_strdup(json_object_get_string_member(post_object, "name"));
        if (json_object_has_member(post_object, "avatar") && !json_node_is_null(json_object_get_member(post_object, "avatar")))
            tweet->author_avatar = g_strdup(json_object_get_string_member(post_object, "avatar"));
    }

    {
        JsonObject *fact_check = get_object_member_if_valid(post_object, "fact_check");
        if (fact_check) {
            if (json_object_has_member(fact_check, "note"))
                tweet->note = g_strdup(json_object_get_string_member(fact_check, "note"));
            if (json_object_has_member(fact_check, "severity"))
                tweet->note_severity = g_strdup(json_object_get_string_member(fact_check, "severity"));
        }
    }

    if (json_object_has_member(post_object, "edited_at") && !json_node_is_null(json_object_get_member(post_object, "edited_at")))
        tweet->edited_at = g_strdup(json_object_get_string_member(post_object, "edited_at"));

    tweet->attachments = parse_attachments(post_object);
    parse_interaction_state(post_object, tweet);

    if (json_object_has_member(post_object, "like_count"))
        tweet->like_count = json_object_get_int_member(post_object, "like_count");
    else if (json_object_has_member(post_object, "likes"))
        tweet->like_count = json_object_get_int_member(post_object, "likes");

    if (json_object_has_member(post_object, "retweet_count"))
        tweet->retweet_count = json_object_get_int_member(post_object, "retweet_count");
    else if (json_object_has_member(post_object, "retweets"))
        tweet->retweet_count = json_object_get_int_member(post_object, "retweets");

    if (json_object_has_member(post_object, "reply_count"))
        tweet->reply_count = json_object_get_int_member(post_object, "reply_count");
    else if (json_object_has_member(post_object, "replies"))
        tweet->reply_count = json_object_get_int_member(post_object, "replies");

    if (json_object_has_member(post_object, "view_count"))
        tweet->view_count = json_object_get_int_member(post_object, "view_count");

    if (json_object_has_member(post_object, "quote_count"))
        tweet->quote_count = json_object_get_int_member(post_object, "quote_count");

    if (json_object_has_member(post_object, "reaction_count"))
        tweet->reaction_count = json_object_get_int_member(post_object, "reaction_count");

    if (json_object_has_member(post_object, "content_type"))
        tweet->content_type = g_strdup(json_object_get_string_member(post_object, "content_type"));

    if (json_object_has_member(post_object, "retweet_created_at") && !json_node_is_null(json_object_get_member(post_object, "retweet_created_at")))
        tweet->retweet_created_at = g_strdup(json_object_get_string_member(post_object, "retweet_created_at"));

    if (json_object_has_member(post_object, "original_post_id") && !json_node_is_null(json_object_get_member(post_object, "original_post_id")))
        tweet->original_post_id = g_strdup(json_object_get_string_member(post_object, "original_post_id"));

    {
        JsonObject *quote_obj = get_object_member_if_valid(post_object, "quoted_tweet");
        if (quote_obj)
            tweet->quote_tweet = parse_tweet_from_object(quote_obj);
    }

    {
        JsonObject *poll_obj = get_object_member_if_valid(post_object, "poll");
        if (poll_obj)
            tweet->poll = parse_poll(poll_obj);
    }

    return tweet;
}

GList*
parse_tweet_details(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *tweets = NULL;

    if (!json_data) {
        g_warning("parse_tweet_details: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        gchar *main_id = NULL;

        if (json_object_has_member(obj, "tweet")) {
            JsonObject *post_obj = get_object_member_if_valid(obj, "tweet");
            if (post_obj)
                main_id = g_strdup(json_object_get_string_member(post_obj, "id"));
        }

        if (json_object_has_member(obj, "threadPosts")) {
            JsonArray *arr = get_array_member_if_valid(obj, "threadPosts");
            if (arr) {
                for (guint i = 0; i < json_array_get_length(arr); i++) {
                    JsonObject *post_obj = get_object_element_if_valid(arr, i);
                    const gchar *id;

                    if (!post_obj)
                        continue;

                    id = json_object_get_string_member(post_obj, "id");
                    if (main_id && g_strcmp0(id, main_id) == 0)
                        continue;

                    tweets = g_list_append(tweets, parse_tweet_from_object(post_obj));
                }
            }
        }

        if (json_object_has_member(obj, "tweet")) {
            JsonObject *post_obj = get_object_member_if_valid(obj, "tweet");
            struct Tweet *tweet = parse_tweet_from_object(post_obj);
            if (tweet)
                tweets = g_list_append(tweets, tweet);
        }

        if (json_object_has_member(obj, "replies")) {
            JsonArray *arr = get_array_member_if_valid(obj, "replies");
            if (arr) {
                for (guint i = 0; i < json_array_get_length(arr); i++) {
                    JsonObject *post_obj = get_object_element_if_valid(arr, i);
                    const gchar *id;

                    if (!post_obj)
                        continue;

                    id = json_object_get_string_member(post_obj, "id");
                    if (main_id && g_strcmp0(id, main_id) == 0)
                        continue;

                    tweets = g_list_append(tweets, parse_tweet_from_object(post_obj));
                }
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

    if (!json_data) {
        g_warning("parse_profile: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "profile")) {
            JsonObject *p_obj = json_object_get_object_member(obj, "profile");
            profile = parse_profile_summary_from_object(p_obj);
            if (json_object_has_member(p_obj, "bio") && !json_node_is_null(json_object_get_member(p_obj, "bio")))
                profile->bio = g_strdup(json_object_get_string_member(p_obj, "bio"));
            if (json_object_has_member(p_obj, "avatar") && !json_node_is_null(json_object_get_member(p_obj, "avatar")))
                profile->avatar = g_strdup(json_object_get_string_member(p_obj, "avatar"));
            if (json_object_has_member(p_obj, "banner") && !json_node_is_null(json_object_get_member(p_obj, "banner")))
                profile->banner = g_strdup(json_object_get_string_member(p_obj, "banner"));
            if (json_object_has_member(p_obj, "location") && !json_node_is_null(json_object_get_member(p_obj, "location")))
                profile->location = g_strdup(json_object_get_string_member(p_obj, "location"));
            if (json_object_has_member(p_obj, "website") && !json_node_is_null(json_object_get_member(p_obj, "website")))
                profile->website = g_strdup(json_object_get_string_member(p_obj, "website"));
            if (json_object_has_member(p_obj, "pronouns") && !json_node_is_null(json_object_get_member(p_obj, "pronouns")))
                profile->pronouns = g_strdup(json_object_get_string_member(p_obj, "pronouns"));
            if (json_object_has_member(p_obj, "theme") && !json_node_is_null(json_object_get_member(p_obj, "theme")))
                profile->theme = g_strdup(json_object_get_string_member(p_obj, "theme"));
            if (json_object_has_member(p_obj, "accent_color") && !json_node_is_null(json_object_get_member(p_obj, "accent_color")))
                profile->accent_color = g_strdup(json_object_get_string_member(p_obj, "accent_color"));
            if (json_object_has_member(p_obj, "label_type") && !json_node_is_null(json_object_get_member(p_obj, "label_type")))
                profile->label_type = g_strdup(json_object_get_string_member(p_obj, "label_type"));
            profile->follower_count = json_object_get_int_member(p_obj, "follower_count");
            profile->following_count = json_object_get_int_member(p_obj, "following_count");
            profile->post_count = json_object_get_int_member(p_obj, "post_count");
            if (json_object_has_member(p_obj, "label_automated")) {
                JsonNode *node = json_object_get_member(p_obj, "label_automated");
                if (node && JSON_NODE_HOLDS_VALUE(node)) {
                    if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                        profile->label_automated = json_node_get_boolean(node);
                    else
                        profile->label_automated = json_node_get_int(node) != 0;
                }
            }
            if (json_object_has_member(p_obj, "notifyTweets")) {
                JsonNode *node = json_object_get_member(p_obj, "notifyTweets");
                if (node && JSON_NODE_HOLDS_VALUE(node)) {
                    if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                        profile->notify_tweets = json_node_get_boolean(node);
                    else
                        profile->notify_tweets = json_node_get_int(node) != 0;
                }
            }

            if (json_object_has_member(obj, "isFollowing")) {
                JsonNode *node = json_object_get_member(obj, "isFollowing");
                if (node && JSON_NODE_HOLDS_VALUE(node))
                    profile->is_following = json_node_get_boolean(node);
            }
            if (json_object_has_member(obj, "followsMe")) {
                JsonNode *node = json_object_get_member(obj, "followsMe");
                if (node && JSON_NODE_HOLDS_VALUE(node))
                    profile->follows_me = json_node_get_boolean(node);
            }
            if (json_object_has_member(obj, "isOwnProfile")) {
                JsonNode *node = json_object_get_member(obj, "isOwnProfile");
                if (node && JSON_NODE_HOLDS_VALUE(node))
                    profile->is_own_profile = json_node_get_boolean(node);
            }
            if (json_object_has_member(p_obj, "blockedByProfile")) {
                JsonNode *node = json_object_get_member(p_obj, "blockedByProfile");
                if (node && JSON_NODE_HOLDS_VALUE(node))
                    profile->blocked_by_profile = json_node_get_boolean(node);
            } else if (json_object_has_member(obj, "blockedByProfile")) {
                JsonNode *node = json_object_get_member(obj, "blockedByProfile");
                if (node && JSON_NODE_HOLDS_VALUE(node))
                    profile->blocked_by_profile = json_node_get_boolean(node);
            }
            if (json_object_has_member(p_obj, "blockedProfile")) {
                JsonNode *node = json_object_get_member(p_obj, "blockedProfile");
                if (node && JSON_NODE_HOLDS_VALUE(node))
                    profile->blocked_profile = json_node_get_boolean(node);
            } else if (json_object_has_member(obj, "blockedProfile")) {
                JsonNode *node = json_object_get_member(obj, "blockedProfile");
                if (node && JSON_NODE_HOLDS_VALUE(node))
                    profile->blocked_profile = json_node_get_boolean(node);
            }
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

    if (!json_data) {
        g_warning("parse_profile_replies: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (error) {
        g_warning("parse_profile_replies: unable to parse json: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_warning("parse_profile_replies: invalid JSON structure");
        g_object_unref(parser);
        return NULL;
    }

    JsonObject *obj = json_node_get_object(root);

    if (json_object_has_member(obj, "error")) {
        const gchar *error_msg = json_object_get_string_member(obj, "error");
        g_warning("API returned error: %s", error_msg ? error_msg : "(null)");
        g_object_unref(parser);
        return NULL;
    }

    if (json_object_has_member(obj, "replies")) {
            JsonArray *replies = get_array_member_if_valid(obj, "replies");
            if (replies) {
            for (guint i = 0; i < json_array_get_length(replies); i++) {
                JsonObject *reply_obj = get_object_element_if_valid(replies, i);
                struct Tweet *tweet = parse_tweet_from_object(reply_obj);
                if (tweet)
                    tweets = g_list_append(tweets, tweet);
            }
            }
        }
    g_object_unref(parser);
    return tweets;
}

gboolean
profile_replies_response_is_empty(const gchar *json_data)
{
    static const gchar * const reply_keys[] = {"replies", NULL};
    return response_has_empty_array_member(json_data, reply_keys);
}

static gboolean
response_has_empty_array_member(const gchar *json_data, const gchar * const *keys)
{
    JsonParser *parser;
    GError *error = NULL;
    gboolean is_empty = FALSE;

    if (!json_data || !keys) {
        return FALSE;
    }

    parser = json_parser_new();
    json_parser_load_from_data(parser, json_data, -1, &error);
    if (error) {
        g_error_free(error);
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode *root_node = json_parser_get_root(parser);
    if (root_node && JSON_NODE_HOLDS_OBJECT(root_node)) {
        JsonObject *root_object = json_node_get_object(root_node);

        if (!json_object_has_member(root_object, "error")) {
            JsonArray *items = get_first_array_member(root_object, keys);
            if (items) {
                is_empty = (json_array_get_length(items) == 0);
            }
        }
    }

    g_object_unref(parser);
    return is_empty;
}

GList*
parse_users(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *users = NULL;

    if (!json_data) {
        g_warning("parse_users: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        static const gchar * const user_keys[] = {"users", "followers", "following", "mutuals", "members", NULL};
        JsonArray *users_array = get_first_array_member(obj, user_keys);
        if (users_array) {
            for (guint i = 0; i < json_array_get_length(users_array); i++) {
                JsonNode *user_node = json_array_get_element(users_array, i);
                JsonObject *user_obj = json_node_get_object(user_node);
                struct Profile *user = parse_profile_summary_from_object(user_obj);

                if (json_object_has_member(user_obj, "isFollowing")) {
                    JsonNode *node = json_object_get_member(user_obj, "isFollowing");
                    if (node && JSON_NODE_HOLDS_VALUE(node))
                        user->is_following = json_node_get_boolean(node);
                }
                if (json_object_has_member(user_obj, "followsMe")) {
                    JsonNode *node = json_object_get_member(user_obj, "followsMe");
                    if (node && JSON_NODE_HOLDS_VALUE(node))
                        user->follows_me = json_node_get_boolean(node);
                }
                if (json_object_has_member(user_obj, "blockedByProfile")) {
                    JsonNode *node = json_object_get_member(user_obj, "blockedByProfile");
                    if (node && JSON_NODE_HOLDS_VALUE(node))
                        user->blocked_by_profile = json_node_get_boolean(node);
                }
                if (json_object_has_member(user_obj, "blockedProfile")) {
                    JsonNode *node = json_object_get_member(user_obj, "blockedProfile");
                    if (node && JSON_NODE_HOLDS_VALUE(node))
                        user->blocked_profile = json_node_get_boolean(node);
                }

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

    if (!json_data) {
        g_warning("parse_notifications: json_data is NULL");
        return NULL;
    }

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
                
                if (json_object_has_member(notif_obj, "actor_verified")) {
                    JsonNode *node = json_object_get_member(notif_obj, "actor_verified");
                    if (node && JSON_NODE_HOLDS_VALUE(node)) {
                        if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                            notif->actor_verified = json_node_get_boolean(node);
                        else
                            notif->actor_verified = json_node_get_int(node) != 0;
                    }
                }
                if (json_object_has_member(notif_obj, "actor_gold")) {
                    JsonNode *node = json_object_get_member(notif_obj, "actor_gold");
                    if (node && JSON_NODE_HOLDS_VALUE(node)) {
                        if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                            notif->actor_gold = json_node_get_boolean(node);
                        else
                            notif->actor_gold = json_node_get_int(node) != 0;
                    }
                }
                
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

    if (!json_data) {
        g_warning("parse_conversations: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "conversations")) {
            JsonArray *conv_array = json_object_get_array_member(obj, "conversations");
            for (guint i = 0; i < json_array_get_length(conv_array); i++) {
                JsonNode *conv_node = json_array_get_element(conv_array, i);
                JsonObject *conv_obj = json_node_get_object(conv_node);
                struct Conversation *conv = parse_conversation_from_object(conv_obj);
                conversations = g_list_append(conversations, conv);
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return conversations;
}

struct Community*
parse_community_details(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    struct Community *community = NULL;

    if (!json_data) {
        g_warning("parse_community_details: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "community") &&
            !json_node_is_null(json_object_get_member(obj, "community"))) {
            JsonObject *community_obj = json_object_get_object_member(obj, "community");
            community = parse_community_from_object(community_obj);
        }
    } else {
        g_error_free(error);
    }

    g_object_unref(parser);
    return community;
}

struct Conversation*
parse_conversation_details(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    struct Conversation *conversation = NULL;

    if (!json_data) {
        g_warning("parse_conversation_details: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "conversation")) {
            JsonObject *conv_obj = json_object_get_object_member(obj, "conversation");
            conversation = parse_conversation_from_object(conv_obj);
        }
    } else {
        g_error_free(error);
    }

    g_object_unref(parser);
    return conversation;
}

GList*
parse_messages(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *messages = NULL;

    if (!json_data) {
        g_warning("parse_messages: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);

        JsonArray *msg_array = NULL;
        if (json_object_has_member(obj, "messages")) {
            msg_array = json_object_get_array_member(obj, "messages");
        } else if (json_object_has_member(obj, "conversation")) {
            JsonObject *conv_obj = json_object_get_object_member(obj, "conversation");
            if (conv_obj && json_object_has_member(conv_obj, "messages"))
                msg_array = json_object_get_array_member(conv_obj, "messages");
        }

        if (msg_array) {
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

                if (json_object_has_member(msg_obj, "message_type") && !json_node_is_null(json_object_get_member(msg_obj, "message_type")))
                    msg->message_type = g_strdup(json_object_get_string_member(msg_obj, "message_type"));

                if (json_object_has_member(msg_obj, "reply_to") && !json_node_is_null(json_object_get_member(msg_obj, "reply_to")))
                    msg->reply_to = g_strdup(json_object_get_string_member(msg_obj, "reply_to"));

                if (json_object_has_member(msg_obj, "reply_to_message") &&
                    !json_node_is_null(json_object_get_member(msg_obj, "reply_to_message"))) {
                    JsonObject *reply_obj = json_object_get_object_member(msg_obj, "reply_to_message");
                    if (reply_obj && json_object_has_member(reply_obj, "content") &&
                        !json_node_is_null(json_object_get_member(reply_obj, "content")))
                        msg->reply_preview = g_strdup(json_object_get_string_member(reply_obj, "content"));
                }

                if (json_object_has_member(msg_obj, "verified")) {
                    JsonNode *node = json_object_get_member(msg_obj, "verified");
                    if (node && JSON_NODE_HOLDS_VALUE(node)) {
                        if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                            msg->verified = json_node_get_boolean(node);
                        else
                            msg->verified = json_node_get_int(node) != 0;
                    }
                }

                if (json_object_has_member(msg_obj, "edited_at") && !json_node_is_null(json_object_get_member(msg_obj, "edited_at")))
                    msg->edited_at = g_strdup(json_object_get_string_member(msg_obj, "edited_at"));

                if (json_object_has_member(msg_obj, "deleted_at") && !json_node_is_null(json_object_get_member(msg_obj, "deleted_at")))
                    msg->is_deleted = TRUE;

                msg->created_at = g_strdup(json_object_get_string_member(msg_obj, "created_at"));
                msg->attachments = parse_attachments(msg_obj);

                if (json_object_has_member(msg_obj, "reactions") &&
                    !json_node_is_null(json_object_get_member(msg_obj, "reactions"))) {
                    JsonArray *reactions = json_object_get_array_member(msg_obj, "reactions");
                    if (reactions && json_array_get_length(reactions) > 0) {
                        GString *summary = g_string_new(NULL);
                        for (guint r = 0; r < json_array_get_length(reactions); r++) {
                            JsonObject *reaction_obj = json_array_get_object_element(reactions, r);
                            const gchar *emoji = json_object_has_member(reaction_obj, "emoji")
                                ? json_object_get_string_member(reaction_obj, "emoji")
                                : "";
                            gint count = json_object_has_member(reaction_obj, "count")
                                ? json_object_get_int_member(reaction_obj, "count")
                                : 0;
                            if (summary->len > 0) {
                                g_string_append(summary, "  ");
                            }
                            g_string_append_printf(summary, "%s %d", emoji, count);
                        }
                        msg->reactions_summary = g_string_free(summary, FALSE);
                    }
                }
                
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

GList*
parse_admin_users(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *users = NULL;

    if (!json_data) {
        g_warning("parse_admin_users: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "users")) {
            JsonArray *arr = json_object_get_array_member(obj, "users");
            for (guint i = 0; i < json_array_get_length(arr); i++) {
                JsonObject *u_obj = json_array_get_object_element(arr, i);
                struct Profile *user = g_new0(struct Profile, 1);
                user->username = g_strdup(json_object_get_string_member(u_obj, "username"));
                user->name = g_strdup(json_object_get_string_member(u_obj, "name"));
                if (json_object_has_member(u_obj, "avatar") && !json_node_is_null(json_object_get_member(u_obj, "avatar")))
                    user->avatar = g_strdup(json_object_get_string_member(u_obj, "avatar"));
                if (json_object_has_member(u_obj, "bio") && !json_node_is_null(json_object_get_member(u_obj, "bio")))
                    user->bio = g_strdup(json_object_get_string_member(u_obj, "bio"));
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
parse_admin_posts(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *tweets = NULL;

    if (!json_data) {
        g_warning("parse_admin_posts: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "posts")) {
            JsonArray *arr = json_object_get_array_member(obj, "posts");
            for (guint i = 0; i < json_array_get_length(arr); i++) {
                JsonObject *p_obj = json_array_get_object_element(arr, i);
                struct Tweet *tweet = g_new0(struct Tweet, 1);
                tweet->id = g_strdup(json_object_get_string_member(p_obj, "id"));
                tweet->content = g_strdup(json_object_get_string_member(p_obj, "content"));
                tweet->author_username = g_strdup(json_object_get_string_member(p_obj, "username"));
                tweet->author_name = g_strdup(json_object_get_string_member(p_obj, "name"));
                if (json_object_has_member(p_obj, "avatar") && !json_node_is_null(json_object_get_member(p_obj, "avatar")))
                    tweet->author_avatar = g_strdup(json_object_get_string_member(p_obj, "avatar"));
                tweets = g_list_append(tweets, tweet);
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return tweets;
}

gchar*
parse_admin_stats(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    gchar *result = NULL;

    if (!json_data) {
        g_warning("parse_admin_stats: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);

        if (json_object_has_member(obj, "stats")) {
            JsonObject *stats = json_object_get_object_member(obj, "stats");
            JsonObject *user_stats = json_object_get_object_member(stats, "users");
            JsonObject *post_stats = json_object_get_object_member(stats, "posts");
            JsonObject *suspension_stats = json_object_get_object_member(stats, "suspensions");
            
            result = g_strdup_printf(
                "User Statistics:\n"
                "  Total Users: %" G_GINT64_FORMAT "\n"
                "  Suspended: %" G_GINT64_FORMAT "\n"
                "  Restricted: %" G_GINT64_FORMAT "\n"
                "  Verified: %" G_GINT64_FORMAT "\n"
                "  Gold: %" G_GINT64_FORMAT "\n"
                "  Gray: %" G_GINT64_FORMAT "\n\n"
                "Post Statistics:\n"
                "  Total Posts: %" G_GINT64_FORMAT "\n\n"
                "Suspension Statistics:\n"
                "  Active: %" G_GINT64_FORMAT "\n"
                "  Restricted: %" G_GINT64_FORMAT "\n"
                "  Suspended: %" G_GINT64_FORMAT "",
                json_object_get_int_member(user_stats, "total"),
                json_object_get_int_member(user_stats, "suspended"),
                json_object_get_int_member(user_stats, "restricted"),
                json_object_get_int_member(user_stats, "verified"),
                json_object_get_int_member(user_stats, "gold"),
                json_object_get_int_member(user_stats, "gray"),
                json_object_get_int_member(post_stats, "total"),
                json_object_get_int_member(suspension_stats, "active"),
                json_object_get_int_member(suspension_stats, "active_restricted"),
                json_object_get_int_member(suspension_stats, "active_suspended")
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

    if (!json_data) {
        g_warning("parse_login_response: json_data is NULL");
        return FALSE;
    }

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
                            if (node && JSON_NODE_HOLDS_VALUE(node)) {
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

    if (!json_data) {
        g_warning("parse_user_me_response: json_data is NULL");
        return FALSE;
    }

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
                    if (node && JSON_NODE_HOLDS_VALUE(node)) {
                        if (json_node_get_value_type(node) == G_TYPE_BOOLEAN)
                            *is_admin_out = json_node_get_boolean(node);
                        else
                            *is_admin_out = (json_node_get_int(node) != 0);
                    }
                }
                
                if (!(*is_admin_out) && json_object_has_member(user_obj, "superadmin")) {
                    JsonNode *node = json_object_get_member(user_obj, "superadmin");
                    if (node && JSON_NODE_HOLDS_VALUE(node)) {
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
construct_tweet_payload(const gchar *content, const gchar *reply_to_id, GList *attachments)
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

    if (attachments) {
        json_builder_set_member_name(builder, "files");
        json_builder_begin_array(builder);
        for (GList *l = attachments; l != NULL; l = l->next) {
            struct Attachment *attach = l->data;
            if (!attach || !attach->file_url) {
                continue;
            }
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "url");
            json_builder_add_string_value(builder, attach->file_url);
            json_builder_set_member_name(builder, "type");
            json_builder_add_string_value(builder, attach->file_type ? attach->file_type : "application/octet-stream");
            json_builder_set_member_name(builder, "name");
            json_builder_add_string_value(builder, "");
            json_builder_set_member_name(builder, "hash");
            json_builder_add_string_value(builder, "");
            json_builder_set_member_name(builder, "size");
            json_builder_add_int_value(builder, 0);
            json_builder_end_object(builder);
        }
        json_builder_end_array(builder);
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
        g_free(attach->file_hash);
        g_free(attach->file_name);
        g_free(attach);
    }
}

void
free_tweet(gpointer data)
{
    struct Tweet *tweet = data;
    if (!tweet) {
        return;
    }
    g_free(tweet->content);
    g_free(tweet->author_id);
    g_free(tweet->author_name);
    g_free(tweet->author_username);
    g_free(tweet->author_avatar);
    g_free(tweet->id);
    g_free(tweet->note);
    g_free(tweet->note_severity);
    g_free(tweet->edited_at);
    g_free(tweet->content_type);
    g_free(tweet->retweet_created_at);
    g_free(tweet->original_post_id);
    if (tweet->quote_tweet) {
        free_tweet(tweet->quote_tweet);
    }
    if (tweet->attachments) {
        g_list_free_full(tweet->attachments, free_attachment);
    }
    if (tweet->poll) {
        free_poll_data(tweet->poll);
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
    if (!user) {
        return;
    }
    g_free(user->id);
    g_free(user->name);
    g_free(user->username);
    g_free(user->bio);
    g_free(user->avatar);
    g_free(user->banner);
    g_free(user->location);
    g_free(user->website);
    g_free(user->pronouns);
    g_free(user->theme);
    g_free(user->accent_color);
    g_free(user->label_type);
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
        g_free(conv->last_message_sender);
        g_free(conv->last_message_sender_name);
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
        g_free(msg->message_type);
        g_free(msg->reply_to);
        g_free(msg->reply_preview);
        g_free(msg->username);
        g_free(msg->name);
        g_free(msg->avatar);
        g_free(msg->created_at);
        g_free(msg->edited_at);
        g_free(msg->reactions_summary);
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

static void
free_poll_option(gpointer data)
{
    struct PollOption *option = data;
    if (option) {
        g_free(option->id);
        g_free(option->option_text);
        g_free(option->user_vote);
        g_free(option);
    }
}

static void
free_poll_data(struct Poll *poll)
{
    if (poll) {
        g_free(poll->id);
        g_free(poll->question);
        g_free(poll->expires_at);
        if (poll->options) {
            g_list_free_full(poll->options, free_poll_option);
        }
        g_free(poll);
    }
}

GList*
parse_communities(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *communities = NULL;

    if (!json_data) {
        g_warning("parse_communities: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "communities")) {
            JsonArray *comm_array = json_object_get_array_member(obj, "communities");
            for (guint i = 0; i < json_array_get_length(comm_array); i++) {
                JsonNode *comm_node = json_array_get_element(comm_array, i);
                JsonObject *comm_obj = json_node_get_object(comm_node);
                struct Community *comm = parse_community_from_object(comm_obj);
                communities = g_list_append(communities, comm);
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return communities;
}

gchar*
parse_upload_response(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    gchar *file_url = NULL;

    if (!json_data) {
        g_warning("parse_upload_response: json_data is NULL");
        return NULL;
    }

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);

        if (json_object_has_member(obj, "file") && json_object_has_member(json_object_get_object_member(obj, "file"), "url")) {
            file_url = g_strdup(json_object_get_string_member(json_object_get_object_member(obj, "file"), "url"));
        } else if (json_object_has_member(obj, "file_url")) {
            file_url = g_strdup(json_object_get_string_member(obj, "file_url"));
        } else if (json_object_has_member(obj, "url")) {
            file_url = g_strdup(json_object_get_string_member(obj, "url"));
        }
    } else {
        g_error_free(error);
    }

    g_object_unref(parser);
    return file_url;
}

void
free_community(gpointer data)
{
    struct Community *comm = data;
    if (comm) {
        g_free(comm->id);
        g_free(comm->name);
        g_free(comm->description);
        g_free(comm->rules);
        g_free(comm->icon_url);
        g_free(comm->banner_url);
        g_free(comm->access_mode);
        g_free(comm->tag_emoji);
        g_free(comm->tag_text);
        g_free(comm);
    }
}

void
free_communities(GList *communities)
{
    g_list_free_full(communities, free_community);
}
