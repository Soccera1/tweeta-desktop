const std = @import("std");

const c = @import("c.zig").c;
const types = @import("types.zig");

extern fn get_cached_liked(tweet_id: [*c]const c.gchar) c.gboolean;
extern fn get_cached_retweeted(tweet_id: [*c]const c.gchar) c.gboolean;
extern fn get_cached_bookmarked(tweet_id: [*c]const c.gchar) c.gboolean;

const FALSE: c.gboolean = 0;
const TRUE: c.gboolean = 1;

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn warn(comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), c.G_LOG_LEVEL_WARNING, lit(fmt) } ++ args);
}

fn debug(comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), c.G_LOG_LEVEL_DEBUG, lit(fmt) } ++ args);
}

fn alloc(comptime T: type) [*c]T {
    return @ptrCast(@alignCast(c.g_malloc0(@sizeOf(T))));
}

fn objMember(obj: ?*c.JsonObject, key: [*c]const c.gchar) ?*c.JsonNode {
    if (obj == null or key == null or c.json_object_has_member(obj, key) == FALSE) return null;
    return c.json_object_get_member(obj, key);
}

fn hasNonNull(obj: ?*c.JsonObject, key: [*c]const c.gchar) bool {
    const node = objMember(obj, key) orelse return false;
    return c.json_node_is_null(node) == FALSE;
}

fn getArrayMemberIfValid(obj: ?*c.JsonObject, key: [*c]const c.gchar) ?*c.JsonArray {
    const node = objMember(obj, key) orelse return null;
    if (!c.JSON_NODE_HOLDS_ARRAY(node)) return null;
    return c.json_node_get_array(node);
}

fn getObjectMemberIfValid(obj: ?*c.JsonObject, key: [*c]const c.gchar) ?*c.JsonObject {
    const node = objMember(obj, key) orelse return null;
    if (!c.JSON_NODE_HOLDS_OBJECT(node)) return null;
    return c.json_node_get_object(node);
}

fn getObjectElementIfValid(array: ?*c.JsonArray, index: c.guint) ?*c.JsonObject {
    if (array == null) return null;
    const node = c.json_array_get_element(array, index);
    if (node == null or !c.JSON_NODE_HOLDS_OBJECT(node)) return null;
    return c.json_node_get_object(node);
}

fn objectMemberHoldsValue(obj: ?*c.JsonObject, key: [*c]const c.gchar) bool {
    const node = objMember(obj, key) orelse return false;
    return c.JSON_NODE_HOLDS_VALUE(node);
}

fn objectMemberHasNonNullNode(obj: ?*c.JsonObject, key: [*c]const c.gchar) bool {
    const node = objMember(obj, key) orelse return false;
    return c.JSON_NODE_HOLDS_VALUE(node) or c.JSON_NODE_HOLDS_OBJECT(node) or c.JSON_NODE_HOLDS_ARRAY(node);
}

fn dupStringMemberIfValid(obj: ?*c.JsonObject, key: [*c]const c.gchar) [*c]c.gchar {
    if (!objectMemberHoldsValue(obj, key)) return null;
    return c.g_strdup(c.json_object_get_string_member(obj, key));
}

fn boolMember(obj: ?*c.JsonObject, key: [*c]const c.gchar) c.gboolean {
    const node = objMember(obj, key) orelse return FALSE;
    if (!c.JSON_NODE_HOLDS_VALUE(node)) return FALSE;
    if (c.json_node_get_value_type(node) == c.G_TYPE_BOOLEAN) return c.json_node_get_boolean(node);
    return if (c.json_node_get_int(node) != 0) TRUE else FALSE;
}

fn boolMemberInt64Only(obj: ?*c.JsonObject, key: [*c]const c.gchar) c.gboolean {
    const node = objMember(obj, key) orelse return FALSE;
    if (!c.JSON_NODE_HOLDS_VALUE(node)) return FALSE;
    if (c.json_node_get_value_type(node) == c.G_TYPE_BOOLEAN) return c.json_node_get_boolean(node);
    if (c.json_node_get_value_type(node) == c.G_TYPE_INT64) return if (c.json_node_get_int(node) != 0) TRUE else FALSE;
    return FALSE;
}

fn boolMemberDirect(obj: ?*c.JsonObject, key: [*c]const c.gchar) c.gboolean {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return FALSE;
    return c.json_object_get_boolean_member(obj, key);
}

fn boolMemberNodeBoolean(obj: ?*c.JsonObject, key: [*c]const c.gchar) c.gboolean {
    const node = objMember(obj, key) orelse return FALSE;
    if (!c.JSON_NODE_HOLDS_VALUE(node)) return FALSE;
    return c.json_node_get_boolean(node);
}

fn intMember(obj: ?*c.JsonObject, key: [*c]const c.gchar) c_int {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return 0;
    return @intCast(c.json_object_get_int_member(obj, key));
}

fn getFirstArrayMember(obj: ?*c.JsonObject, keys: [*c]const [*c]const c.gchar) ?*c.JsonArray {
    var i: usize = 0;
    while (keys[i] != null) : (i += 1) {
        if (getArrayMemberIfValid(obj, keys[i])) |array| return array;
    }
    return null;
}

fn parseRoot(json_data: [*c]const c.gchar, parser_out: *?*c.JsonParser, warn_prefix: ?[*c]const c.gchar) ?*c.JsonObject {
    if (json_data == null) {
        if (warn_prefix) |prefix| {
            if (c.g_strcmp0(prefix, "parse_tweets") == 0) warn("parse_tweets: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_tweet_details") == 0) warn("parse_tweet_details: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_profile") == 0) warn("parse_profile: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_profile_replies") == 0) warn("parse_profile_replies: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_users") == 0) warn("parse_users: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_notifications") == 0) warn("parse_notifications: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_conversations") == 0) warn("parse_conversations: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_community_details") == 0) warn("parse_community_details: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_conversation_details") == 0) warn("parse_conversation_details: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_messages") == 0) warn("parse_messages: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_admin_users") == 0) warn("parse_admin_users: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_admin_posts") == 0) warn("parse_admin_posts: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_admin_stats") == 0) warn("parse_admin_stats: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_login_response") == 0) warn("parse_login_response: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_user_me_response") == 0) warn("parse_user_me_response: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_communities") == 0) warn("parse_communities: json_data is NULL", .{})
            else if (c.g_strcmp0(prefix, "parse_upload_response") == 0) warn("parse_upload_response: json_data is NULL", .{})
            else warn("%s: json_data is NULL", .{prefix});
        }
        return null;
    }

    const parser = c.json_parser_new();
    var err: [*c]c.GError = null;
    _ = c.json_parser_load_from_data(parser, json_data, -1, &err);
    if (err != null) {
        if (warn_prefix) |prefix| {
            if (c.g_strcmp0(prefix, "parse_tweets") == 0) warn("Unable to parse json: %s", .{err.*.message})
            else if (c.g_strcmp0(prefix, "parse_profile_replies") == 0) warn("parse_profile_replies: unable to parse json: %s", .{err.*.message})
            else warn("%s: unable to parse json: %s", .{ prefix, err.*.message });
        }
        c.g_error_free(err);
        c.g_object_unref(parser);
        return null;
    }

    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) {
        if (warn_prefix) |prefix| {
            if (c.g_strcmp0(prefix, "parse_tweets") == 0) warn("parse_tweets: invalid JSON structure", .{})
            else if (c.g_strcmp0(prefix, "parse_profile_replies") == 0) warn("parse_profile_replies: invalid JSON structure", .{})
            else warn("%s: invalid JSON structure", .{prefix});
        }
        c.g_object_unref(parser);
        return null;
    }

    parser_out.* = parser;
    return c.json_node_get_object(root);
}

fn parseProfileSummaryFromObject(user_obj: ?*c.JsonObject) [*c]types.Profile {
    if (user_obj == null) return null;
    const user = alloc(types.Profile);
    user.*.id = dupStringMemberIfValid(user_obj, "id");
    user.*.name = dupStringMemberIfValid(user_obj, "name");
    user.*.username = dupStringMemberIfValid(user_obj, "username");
    user.*.bio = dupStringMemberIfValid(user_obj, "bio");
    user.*.avatar = dupStringMemberIfValid(user_obj, "avatar");
    user.*.banner = dupStringMemberIfValid(user_obj, "banner");
    user.*.follower_count = intMember(user_obj, "follower_count");
    user.*.following_count = intMember(user_obj, "following_count");
    user.*.post_count = intMember(user_obj, "post_count");
    user.*.avatar_radius = intMember(user_obj, "avatar_radius");
    user.*.author_verified = boolMember(user_obj, "verified");
    user.*.author_gold = boolMember(user_obj, "gold");
    user.*.author_gray = boolMember(user_obj, "gray");
    return user;
}

fn parseProfileListFromArray(array: ?*c.JsonArray) [*c]c.GList {
    var users: [*c]c.GList = null;
    if (array == null) return users;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(array)) : (i += 1) {
        const user_obj = getObjectElementIfValid(array, i) orelse continue;
        const user = parseProfileSummaryFromObject(user_obj);
        if (user == null) continue;
        user.*.is_following = boolMemberNodeBoolean(user_obj, "isFollowing");
        user.*.follows_me = boolMemberNodeBoolean(user_obj, "followsMe");
        user.*.blocked_by_profile = boolMemberNodeBoolean(user_obj, "blockedByProfile");
        user.*.blocked_profile = boolMemberNodeBoolean(user_obj, "blockedProfile");
        users = c.g_list_append(users, user);
    }
    return users;
}

fn parseCommunityFromObject(comm_obj: ?*c.JsonObject) [*c]types.Community {
    if (comm_obj == null) return null;
    const comm = alloc(types.Community);
    comm.*.id = dupStringMemberIfValid(comm_obj, "id");
    comm.*.name = dupStringMemberIfValid(comm_obj, "name");
    comm.*.description = dupStringMemberIfValid(comm_obj, "description");
    comm.*.rules = dupStringMemberIfValid(comm_obj, "rules");
    comm.*.icon_url = dupStringMemberIfValid(comm_obj, "icon_url");
    if (comm.*.icon_url == null) comm.*.icon_url = dupStringMemberIfValid(comm_obj, "icon");
    comm.*.banner_url = dupStringMemberIfValid(comm_obj, "banner_url");
    if (comm.*.banner_url == null) comm.*.banner_url = dupStringMemberIfValid(comm_obj, "banner");
    comm.*.access_mode = dupStringMemberIfValid(comm_obj, "access_mode");
    comm.*.member_count = intMember(comm_obj, "member_count");
    comm.*.is_member = boolMemberDirect(comm_obj, "is_member");
    comm.*.is_admin = boolMemberDirect(comm_obj, "is_admin");
    comm.*.is_moderator = boolMemberDirect(comm_obj, "is_moderator");
    comm.*.tag_enabled = boolMemberDirect(comm_obj, "tag_enabled");
    comm.*.tag_emoji = dupStringMemberIfValid(comm_obj, "tag_emoji");
    comm.*.tag_text = dupStringMemberIfValid(comm_obj, "tag_text");
    return comm;
}

fn parseTweetaListFromObject(list_obj: ?*c.JsonObject) [*c]types.TweetaList {
    if (list_obj == null) return null;
    const list = alloc(types.TweetaList);
    list.*.id = dupStringMemberIfValid(list_obj, "id");
    list.*.user_id = dupStringMemberIfValid(list_obj, "user_id");
    list.*.name = dupStringMemberIfValid(list_obj, "name");
    list.*.description = dupStringMemberIfValid(list_obj, "description");
    list.*.owner_username = dupStringMemberIfValid(list_obj, "owner_username");
    list.*.owner_name = dupStringMemberIfValid(list_obj, "owner_name");
    list.*.member_count = intMember(list_obj, "member_count");
    list.*.follower_count = intMember(list_obj, "follower_count");
    list.*.is_private = boolMember(list_obj, "is_private");
    return list;
}

fn appendTweetaListArray(array: ?*c.JsonArray) [*c]c.GList {
    var lists: [*c]c.GList = null;
    if (array == null) return lists;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(array)) : (i += 1) {
        if (parseTweetaListFromObject(getObjectElementIfValid(array, i))) |list| {
            lists = c.g_list_append(lists, list);
        }
    }
    return lists;
}

fn parseConversationFromObject(conv_obj: ?*c.JsonObject) [*c]types.Conversation {
    if (conv_obj == null) return null;
    const conv = alloc(types.Conversation);
    conv.*.id = dupStringMemberIfValid(conv_obj, "id");
    conv.*.type = dupStringMemberIfValid(conv_obj, "type");
    conv.*.title = dupStringMemberIfValid(conv_obj, "title");
    conv.*.display_name = dupStringMemberIfValid(conv_obj, "displayName");
    if (conv.*.display_name == null and conv.*.title != null) conv.*.display_name = c.g_strdup(conv.*.title);
    conv.*.display_avatar = dupStringMemberIfValid(conv_obj, "displayAvatar");
    conv.*.last_message_content = dupStringMemberIfValid(conv_obj, "last_message_content");
    conv.*.last_message_time = dupStringMemberIfValid(conv_obj, "last_message_time");
    conv.*.last_message_sender = dupStringMemberIfValid(conv_obj, "last_message_sender");
    conv.*.last_message_sender_name = dupStringMemberIfValid(conv_obj, "lastMessageSenderName");
    conv.*.unread_count = intMember(conv_obj, "unread_count");
    conv.*.participant_count = intMember(conv_obj, "participant_count");
    conv.*.disappearing_enabled = boolMember(conv_obj, "disappearing_enabled");
    conv.*.disappearing_duration = intMember(conv_obj, "disappearing_duration");

    if (getArrayMemberIfValid(conv_obj, "participants")) |participants| {
        var i: c.guint = 0;
        while (i < c.json_array_get_length(participants)) : (i += 1) {
            if (parseProfileSummaryFromObject(getObjectElementIfValid(participants, i))) |participant| {
                conv.*.participants = c.g_list_append(conv.*.participants, participant);
            }
        }
    }
    return conv;
}

fn parseInteractionState(post_object: ?*c.JsonObject, tweet: [*c]types.Tweet) void {
    const liked_keys = [_][*c]const c.gchar{ "liked_by_user", "liked", "is_liked", "user_liked" };
    const retweeted_keys = [_][*c]const c.gchar{ "retweeted_by_user", "retweeted", "is_retweeted", "user_retweeted" };
    const bookmarked_keys = [_][*c]const c.gchar{ "bookmarked_by_user", "bookmarked", "is_bookmarked", "user_bookmarked" };

    var found = false;
    for (liked_keys) |key| {
        if (objMember(post_object, key) != null) {
            tweet.*.liked = if (c.g_strcmp0(key, "liked_by_user") == 0) boolMemberInt64Only(post_object, key) else boolMemberDirect(post_object, key);
            found = true;
            break;
        }
    }
    if (!found) tweet.*.liked = get_cached_liked(tweet.*.id);

    found = false;
    for (retweeted_keys) |key| {
        if (objMember(post_object, key) != null) {
            tweet.*.retweeted = if (c.g_strcmp0(key, "retweeted_by_user") == 0) boolMemberInt64Only(post_object, key) else boolMemberDirect(post_object, key);
            found = true;
            break;
        }
    }
    if (!found) tweet.*.retweeted = get_cached_retweeted(tweet.*.id);

    found = false;
    for (bookmarked_keys) |key| {
        if (objMember(post_object, key) != null) {
            tweet.*.bookmarked = if (c.g_strcmp0(key, "bookmarked_by_user") == 0) boolMemberInt64Only(post_object, key) else boolMemberDirect(post_object, key);
            found = true;
            break;
        }
    }
    if (!found) tweet.*.bookmarked = get_cached_bookmarked(tweet.*.id);
}

fn parseAttachments(post_object: ?*c.JsonObject) [*c]c.GList {
    const attach_array = getArrayMemberIfValid(post_object, "attachments") orelse return null;
    var attachments: [*c]c.GList = null;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(attach_array)) : (i += 1) {
        const attach_obj = getObjectElementIfValid(attach_array, i) orelse continue;
        const attach = alloc(types.Attachment);
        attach.*.id = dupStringMemberIfValid(attach_obj, "id");
        attach.*.file_url = dupStringMemberIfValid(attach_obj, "file_url");
        attach.*.file_type = dupStringMemberIfValid(attach_obj, "file_type");
        attach.*.file_hash = dupStringMemberIfValid(attach_obj, "file_hash");
        attach.*.file_name = dupStringMemberIfValid(attach_obj, "file_name");
        if (objMember(attach_obj, "file_size") != null) attach.*.file_size = c.json_object_get_int_member(attach_obj, "file_size");
        attach.*.is_spoiler = boolMember(attach_obj, "is_spoiler");
        attachments = c.g_list_append(attachments, attach);
    }
    return attachments;
}

fn parsePoll(poll_object: ?*c.JsonObject) [*c]types.Poll {
    if (poll_object == null) return null;
    const poll = alloc(types.Poll);
    poll.*.id = dupStringMemberIfValid(poll_object, "id");
    poll.*.question = dupStringMemberIfValid(poll_object, "question");
    poll.*.kind = dupStringMemberIfValid(poll_object, "kind");
    if (poll.*.kind == null) poll.*.kind = c.g_strdup("single");
    if (objMember(poll_object, "steps")) |steps| {
        if (c.JSON_NODE_HOLDS_ARRAY(steps)) poll.*.steps = c.json_node_copy(steps);
    }
    poll.*.has_user_answers = if (objectMemberHasNonNullNode(poll_object, "userAnswers")) TRUE else FALSE;
    poll.*.user_score = intMember(poll_object, "userScore");
    poll.*.user_total = intMember(poll_object, "userTotal");
    poll.*.is_active = TRUE;
    if (objMember(poll_object, "isExpired") != null) poll.*.is_active = if (boolMemberDirect(poll_object, "isExpired") == FALSE) TRUE else FALSE else if (objMember(poll_object, "is_active") != null) poll.*.is_active = boolMemberDirect(poll_object, "is_active");
    poll.*.expires_at = dupStringMemberIfValid(poll_object, "expires_at");
    if (objMember(poll_object, "totalVotes") != null) poll.*.total_votes = intMember(poll_object, "totalVotes") else poll.*.total_votes = intMember(poll_object, "total_votes");

    if (getArrayMemberIfValid(poll_object, "options")) |options_array| {
        var i: c.guint = 0;
        while (i < c.json_array_get_length(options_array)) : (i += 1) {
            const option_obj = getObjectElementIfValid(options_array, i) orelse continue;
            const option = alloc(types.PollOption);
            option.*.id = dupStringMemberIfValid(option_obj, "id");
            option.*.option_text = dupStringMemberIfValid(option_obj, "option_text");
            option.*.vote_count = intMember(option_obj, "vote_count");
            option.*.percentage = intMember(option_obj, "percentage");
            option.*.voted = boolMemberDirect(option_obj, "voted");
            if (objectMemberHoldsValue(poll_object, "userVote")) {
                const user_vote = c.json_object_get_string_member(poll_object, "userVote");
                if (user_vote != null and option.*.id != null and c.g_strcmp0(user_vote, option.*.id) == 0) {
                    option.*.user_vote = c.g_strdup(user_vote);
                    option.*.voted = TRUE;
                }
            }
            poll.*.options = c.g_list_append(poll.*.options, option);
        }
    }
    return poll;
}

fn parseTweetFromObject(post_object: ?*c.JsonObject) [*c]types.Tweet {
    if (post_object == null) return null;
    const tweet = alloc(types.Tweet);
    const author_object = getObjectMemberIfValid(post_object, "author");
    tweet.*.id = dupStringMemberIfValid(post_object, "id");
    tweet.*.content = dupStringMemberIfValid(post_object, "content");
    tweet.*.pinned = boolMember(post_object, "pinned");

    if (author_object) |author| {
        tweet.*.author_id = dupStringMemberIfValid(author, "id");
        tweet.*.author_name = dupStringMemberIfValid(author, "name");
        tweet.*.author_username = dupStringMemberIfValid(author, "username");
        tweet.*.author_avatar = dupStringMemberIfValid(author, "avatar");
        tweet.*.author_verified = boolMember(author, "verified");
        tweet.*.author_gold = boolMember(author, "gold");
        tweet.*.author_gray = boolMember(author, "gray");
    } else {
        tweet.*.author_id = dupStringMemberIfValid(post_object, "user_id");
        tweet.*.author_username = dupStringMemberIfValid(post_object, "username");
        tweet.*.author_name = dupStringMemberIfValid(post_object, "name");
        tweet.*.author_avatar = dupStringMemberIfValid(post_object, "avatar");
    }

    if (getObjectMemberIfValid(post_object, "fact_check")) |fact_check| {
        tweet.*.note = dupStringMemberIfValid(fact_check, "note");
        tweet.*.note_severity = dupStringMemberIfValid(fact_check, "severity");
    }
    tweet.*.edited_at = dupStringMemberIfValid(post_object, "edited_at");
    tweet.*.attachments = parseAttachments(post_object);
    parseInteractionState(post_object, tweet);
    tweet.*.like_count = if (objMember(post_object, "like_count") != null) intMember(post_object, "like_count") else intMember(post_object, "likes");
    tweet.*.retweet_count = if (objMember(post_object, "retweet_count") != null) intMember(post_object, "retweet_count") else intMember(post_object, "retweets");
    tweet.*.reply_count = if (objMember(post_object, "reply_count") != null) intMember(post_object, "reply_count") else intMember(post_object, "replies");
    tweet.*.view_count = intMember(post_object, "view_count");
    tweet.*.quote_count = intMember(post_object, "quote_count");
    tweet.*.reaction_count = intMember(post_object, "reaction_count");
    tweet.*.content_type = dupStringMemberIfValid(post_object, "content_type");
    tweet.*.retweet_created_at = dupStringMemberIfValid(post_object, "retweet_created_at");
    tweet.*.original_post_id = dupStringMemberIfValid(post_object, "original_post_id");
    tweet.*.article_title = dupStringMemberIfValid(post_object, "article_title");
    tweet.*.article_body_markdown = dupStringMemberIfValid(post_object, "article_body_markdown");
    tweet.*.created_at = dupStringMemberIfValid(post_object, "created_at");
    if (getObjectMemberIfValid(post_object, "quoted_tweet")) |quote_obj| tweet.*.quote_tweet = parseTweetFromObject(quote_obj);
    if (getObjectMemberIfValid(post_object, "poll")) |poll_obj| tweet.*.poll = parsePoll(poll_obj);
    return tweet;
}

export fn parse_tweets(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const root_object = parseRoot(json_data, &parser, "parse_tweets") orelse return null;
    defer c.g_object_unref(parser);
    if (objMember(root_object, "error")) |_| {
        const error_msg = c.json_object_get_string_member(root_object, "error");
        warn("API returned error: %s", .{if (error_msg != null) error_msg else lit("(null)")});
        return null;
    }
    var tweets: [*c]c.GList = null;
    const keys = [_][*c]const c.gchar{ "posts", "timeline", "bookmarks", "tweets", "articles", "highlights", null };
    const posts = getFirstArrayMember(root_object, &keys) orelse return null;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(posts)) : (i += 1) {
        const tweet = parseTweetFromObject(getObjectElementIfValid(posts, i));
        if (tweet == null) continue;
        debug("parse_tweets: parsed tweet id=%s, liked=%d, retweeted=%d, bookmarked=%d", .{
            if (tweet.*.id != null) tweet.*.id else lit("(null)"),
            tweet.*.liked,
            tweet.*.retweeted,
            tweet.*.bookmarked,
        });
        tweets = c.g_list_append(tweets, tweet);
    }
    return tweets;
}

export fn tweets_response_is_empty(json_data: [*c]const c.gchar) c.gboolean {
    const keys = [_][*c]const c.gchar{ "posts", "timeline", "bookmarks", "tweets", null };
    return responseHasEmptyArrayMember(json_data, &keys);
}

fn responseHasEmptyArrayMember(json_data: [*c]const c.gchar, keys: [*c]const [*c]const c.gchar) c.gboolean {
    var parser: ?*c.JsonParser = null;
    const root = parseRoot(json_data, &parser, null) orelse return FALSE;
    defer c.g_object_unref(parser);
    if (objMember(root, "error") != null) return FALSE;
    const items = getFirstArrayMember(root, keys) orelse return FALSE;
    return if (c.json_array_get_length(items) == 0) TRUE else FALSE;
}

export fn parse_tweet_details(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, null) orelse return null;
    defer c.g_object_unref(parser);
    var tweets: [*c]c.GList = null;
    var main_id: [*c]c.gchar = null;
    defer c.g_free(main_id);
    if (getObjectMemberIfValid(obj, "tweet")) |post_obj| main_id = dupStringMemberIfValid(post_obj, "id");
    if (getArrayMemberIfValid(obj, "threadPosts")) |arr| appendTweetArrayExcept(&tweets, arr, main_id);
    if (getObjectMemberIfValid(obj, "tweet")) |post_obj| {
        if (parseTweetFromObject(post_obj)) |tweet| tweets = c.g_list_append(tweets, tweet);
    }
    if (getArrayMemberIfValid(obj, "replies")) |arr| appendTweetArrayExcept(&tweets, arr, main_id);
    return tweets;
}

fn appendTweetArrayExcept(tweets: *[*c]c.GList, arr: ?*c.JsonArray, main_id: [*c]const c.gchar) void {
    var i: c.guint = 0;
    while (i < c.json_array_get_length(arr)) : (i += 1) {
        const post_obj = getObjectElementIfValid(arr, i) orelse continue;
        const id = c.json_object_get_string_member(post_obj, "id");
        if (main_id != null and c.g_strcmp0(id, main_id) == 0) continue;
        if (parseTweetFromObject(post_obj)) |tweet| tweets.* = c.g_list_append(tweets.*, tweet);
    }
}

export fn parse_profile(json_data: [*c]const c.gchar) [*c]types.Profile {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_profile") orelse return null;
    defer c.g_object_unref(parser);
    const p_obj = getObjectMemberIfValid(obj, "profile") orelse return null;
    const profile = parseProfileSummaryFromObject(p_obj);
    profile.*.location = dupStringMemberIfValid(p_obj, "location");
    profile.*.website = dupStringMemberIfValid(p_obj, "website");
    profile.*.pronouns = dupStringMemberIfValid(p_obj, "pronouns");
    profile.*.theme = dupStringMemberIfValid(p_obj, "theme");
    profile.*.accent_color = dupStringMemberIfValid(p_obj, "accent_color");
    profile.*.label_type = dupStringMemberIfValid(p_obj, "label_type");
    profile.*.label_automated = boolMember(p_obj, "label_automated");
    profile.*.notify_tweets = boolMember(p_obj, "notifyTweets");
    profile.*.is_following = boolMemberNodeBoolean(obj, "isFollowing");
    profile.*.follows_me = boolMemberNodeBoolean(obj, "followsMe");
    profile.*.is_own_profile = boolMemberNodeBoolean(obj, "isOwnProfile");
    profile.*.blocked_by_profile = if (objMember(p_obj, "blockedByProfile") != null) boolMemberNodeBoolean(p_obj, "blockedByProfile") else boolMemberNodeBoolean(obj, "blockedByProfile");
    profile.*.blocked_profile = if (objMember(p_obj, "blockedProfile") != null) boolMemberNodeBoolean(p_obj, "blockedProfile") else boolMemberNodeBoolean(obj, "blockedProfile");
    return profile;
}

export fn parse_profile_replies(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_profile_replies") orelse return null;
    defer c.g_object_unref(parser);
    if (objMember(obj, "error")) |_| {
        const error_msg = c.json_object_get_string_member(obj, "error");
        warn("API returned error: %s", .{if (error_msg != null) error_msg else lit("(null)")});
        return null;
    }
    var tweets: [*c]c.GList = null;
    if (getArrayMemberIfValid(obj, "replies")) |replies| appendTweetArrayExcept(&tweets, replies, null);
    return tweets;
}

export fn profile_replies_response_is_empty(json_data: [*c]const c.gchar) c.gboolean {
    const keys = [_][*c]const c.gchar{ "replies", null };
    return responseHasEmptyArrayMember(json_data, &keys);
}

export fn parse_users(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_users") orelse return null;
    defer c.g_object_unref(parser);
    const keys = [_][*c]const c.gchar{ "users", "followers", "following", "mutuals", "members", "followersYouKnow", "affiliates", null };
    const users_array = getFirstArrayMember(obj, &keys) orelse return null;
    return parseProfileListFromArray(users_array);
}

export fn parse_notifications(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_notifications") orelse return null;
    defer c.g_object_unref(parser);
    const arr = getArrayMemberIfValid(obj, "notifications") orelse return null;
    var notifications: [*c]c.GList = null;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(arr)) : (i += 1) {
        const notif_obj = getObjectElementIfValid(arr, i) orelse continue;
        const notif = alloc(types.Notification);
        notif.*.id = dupStringMemberIfValid(notif_obj, "id");
        notif.*.type = dupStringMemberIfValid(notif_obj, "type");
        notif.*.content = dupStringMemberIfValid(notif_obj, "content");
        notif.*.related_id = dupStringMemberIfValid(notif_obj, "related_id");
        notif.*.actor_id = dupStringMemberIfValid(notif_obj, "actor_id");
        notif.*.actor_username = dupStringMemberIfValid(notif_obj, "actor_username");
        notif.*.actor_name = dupStringMemberIfValid(notif_obj, "actor_name");
        notif.*.actor_avatar = dupStringMemberIfValid(notif_obj, "actor_avatar");
        notif.*.read = boolMemberDirect(notif_obj, "read");
        notif.*.created_at = dupStringMemberIfValid(notif_obj, "created_at");
        notif.*.actor_verified = boolMember(notif_obj, "actor_verified");
        notif.*.actor_gold = boolMember(notif_obj, "actor_gold");
        notifications = c.g_list_append(notifications, notif);
    }
    return notifications;
}

export fn parse_conversations(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_conversations") orelse return null;
    defer c.g_object_unref(parser);
    const arr = getArrayMemberIfValid(obj, "conversations") orelse return null;
    var conversations: [*c]c.GList = null;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(arr)) : (i += 1) {
        if (parseConversationFromObject(getObjectElementIfValid(arr, i))) |conv| conversations = c.g_list_append(conversations, conv);
    }
    return conversations;
}

export fn parse_community_details(json_data: [*c]const c.gchar) [*c]types.Community {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_community_details") orelse return null;
    defer c.g_object_unref(parser);
    return parseCommunityFromObject(getObjectMemberIfValid(obj, "community"));
}

export fn parse_conversation_details(json_data: [*c]const c.gchar) [*c]types.Conversation {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_conversation_details") orelse return null;
    defer c.g_object_unref(parser);
    return parseConversationFromObject(getObjectMemberIfValid(obj, "conversation"));
}

export fn parse_lists_response(json_data: [*c]const c.gchar, owned_out: [*c][*c]c.GList, followed_out: [*c][*c]c.GList) c.gboolean {
    if (owned_out != null) owned_out.* = null;
    if (followed_out != null) followed_out.* = null;
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, null) orelse return FALSE;
    defer c.g_object_unref(parser);
    if (objMember(obj, "error") != null) return FALSE;
    if (owned_out != null) owned_out.* = appendTweetaListArray(getArrayMemberIfValid(obj, "ownedLists"));
    if (followed_out != null) followed_out.* = appendTweetaListArray(getArrayMemberIfValid(obj, "followedLists"));
    return TRUE;
}

export fn parse_list_details_response(json_data: [*c]const c.gchar) [*c]types.TweetaList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_list_details_response") orelse return null;
    defer c.g_object_unref(parser);
    const list_obj = getObjectMemberIfValid(obj, "list") orelse return null;
    const list = parseTweetaListFromObject(list_obj);
    if (list == null) return null;
    list.*.is_following = boolMember(obj, "isFollowing");
    list.*.is_owner = boolMember(obj, "isOwner");
    list.*.members = parseProfileListFromArray(getArrayMemberIfValid(obj, "members"));
    if (getObjectMemberIfValid(list_obj, "owner")) |owner| {
        if (list.*.owner_username == null) list.*.owner_username = dupStringMemberIfValid(owner, "username");
        if (list.*.owner_name == null) list.*.owner_name = dupStringMemberIfValid(owner, "name");
    }
    return list;
}

export fn parse_list_followers_response(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_list_followers_response") orelse return null;
    defer c.g_object_unref(parser);
    return parseProfileListFromArray(getArrayMemberIfValid(obj, "followers"));
}

export fn parse_messages(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_messages") orelse return null;
    defer c.g_object_unref(parser);
    var msg_array = getArrayMemberIfValid(obj, "messages");
    if (msg_array == null) {
        if (getObjectMemberIfValid(obj, "conversation")) |conv_obj| msg_array = getArrayMemberIfValid(conv_obj, "messages");
    }
    const arr = msg_array orelse return null;
    var messages: [*c]c.GList = null;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(arr)) : (i += 1) {
        const msg_obj = getObjectElementIfValid(arr, i) orelse continue;
        const msg = alloc(types.DirectMessage);
        msg.*.id = dupStringMemberIfValid(msg_obj, "id");
        msg.*.conversation_id = dupStringMemberIfValid(msg_obj, "conversation_id");
        msg.*.sender_id = dupStringMemberIfValid(msg_obj, "sender_id");
        msg.*.content = dupStringMemberIfValid(msg_obj, "content");
        msg.*.username = dupStringMemberIfValid(msg_obj, "username");
        msg.*.name = dupStringMemberIfValid(msg_obj, "name");
        msg.*.avatar = dupStringMemberIfValid(msg_obj, "avatar");
        msg.*.message_type = dupStringMemberIfValid(msg_obj, "message_type");
        if (getObjectMemberIfValid(msg_obj, "mpi_payment")) |payment| {
            msg.*.mpi_kind = dupStringMemberIfValid(payment, "kind");
            msg.*.mpi_status = dupStringMemberIfValid(payment, "status");
            msg.*.mpi_net = dupStringMemberIfValid(payment, "net");
            msg.*.mpi_gross = dupStringMemberIfValid(payment, "gross");
            msg.*.mpi_note = dupStringMemberIfValid(payment, "note");
            msg.*.mpi_order_id = dupStringMemberIfValid(payment, "order_id");
            msg.*.mpi_payment_link_url = dupStringMemberIfValid(payment, "payment_link_url");
        }
        msg.*.reply_to = dupStringMemberIfValid(msg_obj, "reply_to");
        if (getObjectMemberIfValid(msg_obj, "reply_to_message")) |reply_obj| msg.*.reply_preview = dupStringMemberIfValid(reply_obj, "content");
        msg.*.verified = boolMember(msg_obj, "verified");
        msg.*.edited_at = dupStringMemberIfValid(msg_obj, "edited_at");
        msg.*.is_deleted = if (hasNonNull(msg_obj, "deleted_at")) TRUE else FALSE;
        msg.*.created_at = dupStringMemberIfValid(msg_obj, "created_at");
        msg.*.attachments = parseAttachments(msg_obj);
        if (getArrayMemberIfValid(msg_obj, "reactions")) |reactions| {
            if (c.json_array_get_length(reactions) > 0) {
                const summary = c.g_string_new(null);
                var r: c.guint = 0;
                while (r < c.json_array_get_length(reactions)) : (r += 1) {
                    const reaction_obj = getObjectElementIfValid(reactions, r) orelse continue;
                    const emoji = if (objectMemberHoldsValue(reaction_obj, "emoji")) c.json_object_get_string_member(reaction_obj, "emoji") else lit("");
                    const count = intMember(reaction_obj, "count");
                    if (summary.*.len > 0) _ = c.g_string_append(summary, "  ");
                    _ = c.g_string_append_printf(summary, "%s %d", emoji, count);
                }
                msg.*.reactions_summary = c.g_string_free(summary, FALSE);
            }
        }
        messages = c.g_list_append(messages, msg);
    }
    return messages;
}

fn builderToData(builder: ?*c.JsonBuilder) [*c]c.gchar {
    const gen = c.json_generator_new();
    defer c.g_object_unref(gen);
    const root = c.json_builder_get_root(builder);
    defer c.json_node_free(root);
    c.json_generator_set_root(gen, root);
    return c.json_generator_to_data(gen, null);
}

export fn construct_dm_payload(content: [*c]const c.gchar) [*c]c.gchar {
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "content");
    _ = c.json_builder_add_string_value(builder, content);
    _ = c.json_builder_end_object(builder);
    return builderToData(builder);
}

export fn parse_admin_users(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_admin_users") orelse return null;
    defer c.g_object_unref(parser);
    const arr = getArrayMemberIfValid(obj, "users") orelse return null;
    var users: [*c]c.GList = null;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(arr)) : (i += 1) {
        const u_obj = getObjectElementIfValid(arr, i) orelse continue;
        const user = alloc(types.Profile);
        user.*.username = dupStringMemberIfValid(u_obj, "username");
        user.*.name = dupStringMemberIfValid(u_obj, "name");
        user.*.avatar = dupStringMemberIfValid(u_obj, "avatar");
        user.*.bio = dupStringMemberIfValid(u_obj, "bio");
        users = c.g_list_append(users, user);
    }
    return users;
}

export fn parse_admin_posts(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_admin_posts") orelse return null;
    defer c.g_object_unref(parser);
    const arr = getArrayMemberIfValid(obj, "posts") orelse return null;
    var tweets: [*c]c.GList = null;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(arr)) : (i += 1) {
        const p_obj = getObjectElementIfValid(arr, i) orelse continue;
        const tweet = alloc(types.Tweet);
        tweet.*.id = dupStringMemberIfValid(p_obj, "id");
        tweet.*.content = dupStringMemberIfValid(p_obj, "content");
        tweet.*.author_username = dupStringMemberIfValid(p_obj, "username");
        tweet.*.author_name = dupStringMemberIfValid(p_obj, "name");
        tweet.*.author_avatar = dupStringMemberIfValid(p_obj, "avatar");
        tweets = c.g_list_append(tweets, tweet);
    }
    return tweets;
}

export fn parse_admin_stats(json_data: [*c]const c.gchar) [*c]c.gchar {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_admin_stats") orelse return null;
    defer c.g_object_unref(parser);
    const stats = getObjectMemberIfValid(obj, "stats") orelse return null;
    const user_stats = getObjectMemberIfValid(stats, "users");
    const post_stats = getObjectMemberIfValid(stats, "posts");
    const suspension_stats = getObjectMemberIfValid(stats, "suspensions");
    return c.g_strdup_printf(
        "User Statistics:\n  Total Users: %ld\n  Suspended: %ld\n  Restricted: %ld\n  Verified: %ld\n  Gold: %ld\n  Gray: %ld\n\nPost Statistics:\n  Total Posts: %ld\n\nSuspension Statistics:\n  Active: %ld\n  Restricted: %ld\n  Suspended: %ld",
        c.json_object_get_int_member(user_stats, "total"),
        c.json_object_get_int_member(user_stats, "suspended"),
        c.json_object_get_int_member(user_stats, "restricted"),
        c.json_object_get_int_member(user_stats, "verified"),
        c.json_object_get_int_member(user_stats, "gold"),
        c.json_object_get_int_member(user_stats, "gray"),
        c.json_object_get_int_member(post_stats, "total"),
        c.json_object_get_int_member(suspension_stats, "active"),
        c.json_object_get_int_member(suspension_stats, "active_restricted"),
        c.json_object_get_int_member(suspension_stats, "active_suspended"),
    );
}

export fn parse_login_response(json_data: [*c]const c.gchar, token_out: [*c][*c]c.gchar, username_out: [*c][*c]c.gchar, is_admin_out: [*c]c.gboolean) c.gboolean {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, null) orelse return FALSE;
    defer c.g_object_unref(parser);
    if (objMember(obj, "token") == null or getObjectMemberIfValid(obj, "user") == null) return FALSE;
    const token = c.json_object_get_string_member(obj, "token");
    const user_obj = getObjectMemberIfValid(obj, "user").?;
    const uname = if (objectMemberHoldsValue(user_obj, "username")) c.json_object_get_string_member(user_obj, "username") else null;
    if (token == null or uname == null) return FALSE;
    token_out.* = c.g_strdup(token);
    username_out.* = c.g_strdup(uname);
    if (is_admin_out != null) is_admin_out.* = boolMember(user_obj, "admin");
    return TRUE;
}

export fn parse_user_me_response(json_data: [*c]const c.gchar, is_admin_out: [*c]c.gboolean) c.gboolean {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, null) orelse return FALSE;
    defer c.g_object_unref(parser);
    const user_obj = getObjectMemberIfValid(obj, "user") orelse return FALSE;
    if (is_admin_out != null) {
        is_admin_out.* = boolMember(user_obj, "admin");
        if (is_admin_out.* == FALSE) is_admin_out.* = boolMember(user_obj, "superadmin");
    }
    return TRUE;
}

export fn construct_tweet_payload(content: [*c]const c.gchar, reply_to_id: [*c]const c.gchar, attachments: [*c]c.GList) [*c]c.gchar {
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "content");
    _ = c.json_builder_add_string_value(builder, content);
    _ = c.json_builder_set_member_name(builder, "source");
    _ = c.json_builder_add_string_value(builder, "Tweeta Desktop");
    if (reply_to_id != null) {
        _ = c.json_builder_set_member_name(builder, "reply_to");
        _ = c.json_builder_add_string_value(builder, reply_to_id);
    }
    if (attachments != null) {
        _ = c.json_builder_set_member_name(builder, "files");
        _ = c.json_builder_begin_array(builder);
        var l = attachments;
        while (l != null) : (l = l.*.next) {
            const attach: [*c]types.Attachment = @ptrCast(@alignCast(l.*.data));
            if (attach == null or attach.*.file_url == null) continue;
            _ = c.json_builder_begin_object(builder);
            _ = c.json_builder_set_member_name(builder, "url");
            _ = c.json_builder_add_string_value(builder, attach.*.file_url);
            _ = c.json_builder_set_member_name(builder, "type");
            _ = c.json_builder_add_string_value(builder, if (attach.*.file_type != null) attach.*.file_type else lit("application/octet-stream"));
            _ = c.json_builder_set_member_name(builder, "name");
            _ = c.json_builder_add_string_value(builder, "");
            _ = c.json_builder_set_member_name(builder, "hash");
            _ = c.json_builder_add_string_value(builder, "");
            _ = c.json_builder_set_member_name(builder, "size");
            _ = c.json_builder_add_int_value(builder, 0);
            _ = c.json_builder_end_object(builder);
        }
        _ = c.json_builder_end_array(builder);
    }
    _ = c.json_builder_end_object(builder);
    return builderToData(builder);
}

export fn free_attachment(data: c.gpointer) void {
    const attach: [*c]types.Attachment = @ptrCast(@alignCast(data));
    if (attach != null) {
        c.g_free(attach.*.id);
        c.g_free(attach.*.file_url);
        c.g_free(attach.*.file_type);
        c.g_free(attach.*.file_hash);
        c.g_free(attach.*.file_name);
        c.g_free(attach);
    }
}

export fn free_tweet(data: c.gpointer) void {
    const tweet: [*c]types.Tweet = @ptrCast(@alignCast(data));
    if (tweet == null) return;
    c.g_free(tweet.*.content);
    c.g_free(tweet.*.author_id);
    c.g_free(tweet.*.author_name);
    c.g_free(tweet.*.author_username);
    c.g_free(tweet.*.author_avatar);
    c.g_free(tweet.*.id);
    c.g_free(tweet.*.note);
    c.g_free(tweet.*.note_severity);
    c.g_free(tweet.*.edited_at);
    c.g_free(tweet.*.content_type);
    c.g_free(tweet.*.retweet_created_at);
    c.g_free(tweet.*.original_post_id);
    c.g_free(tweet.*.article_title);
    c.g_free(tweet.*.article_body_markdown);
    c.g_free(tweet.*.created_at);
    if (tweet.*.quote_tweet != null) free_tweet(tweet.*.quote_tweet);
    if (tweet.*.attachments != null) c.g_list_free_full(tweet.*.attachments, free_attachment);
    if (tweet.*.poll != null) freePollData(tweet.*.poll);
    c.g_free(tweet);
}

export fn free_tweets(tweets: [*c]c.GList) void {
    c.g_list_free_full(tweets, free_tweet);
}

export fn free_user(data: c.gpointer) void {
    const user: [*c]types.Profile = @ptrCast(@alignCast(data));
    if (user == null) return;
    c.g_free(user.*.id);
    c.g_free(user.*.name);
    c.g_free(user.*.username);
    c.g_free(user.*.bio);
    c.g_free(user.*.avatar);
    c.g_free(user.*.banner);
    c.g_free(user.*.location);
    c.g_free(user.*.website);
    c.g_free(user.*.pronouns);
    c.g_free(user.*.theme);
    c.g_free(user.*.accent_color);
    c.g_free(user.*.label_type);
    c.g_free(user);
}

export fn free_users(users: [*c]c.GList) void {
    c.g_list_free_full(users, free_user);
}

export fn free_notification(data: c.gpointer) void {
    const notif: [*c]types.Notification = @ptrCast(@alignCast(data));
    if (notif == null) return;
    c.g_free(notif.*.id);
    c.g_free(notif.*.type);
    c.g_free(notif.*.content);
    c.g_free(notif.*.related_id);
    c.g_free(notif.*.actor_id);
    c.g_free(notif.*.actor_username);
    c.g_free(notif.*.actor_name);
    c.g_free(notif.*.actor_avatar);
    c.g_free(notif.*.created_at);
    c.g_free(notif);
}

export fn free_notifications(notifications: [*c]c.GList) void {
    c.g_list_free_full(notifications, free_notification);
}

export fn free_conversation(data: c.gpointer) void {
    const conv: [*c]types.Conversation = @ptrCast(@alignCast(data));
    if (conv == null) return;
    c.g_free(conv.*.id);
    c.g_free(conv.*.type);
    c.g_free(conv.*.title);
    c.g_free(conv.*.display_name);
    c.g_free(conv.*.display_avatar);
    c.g_free(conv.*.last_message_content);
    c.g_free(conv.*.last_message_time);
    c.g_free(conv.*.last_message_sender);
    c.g_free(conv.*.last_message_sender_name);
    if (conv.*.participants != null) c.g_list_free_full(conv.*.participants, free_user);
    c.g_free(conv);
}

export fn free_conversations(conversations: [*c]c.GList) void {
    c.g_list_free_full(conversations, free_conversation);
}

export fn free_message(data: c.gpointer) void {
    const msg: [*c]types.DirectMessage = @ptrCast(@alignCast(data));
    if (msg == null) return;
    c.g_free(msg.*.id);
    c.g_free(msg.*.conversation_id);
    c.g_free(msg.*.sender_id);
    c.g_free(msg.*.content);
    c.g_free(msg.*.message_type);
    c.g_free(msg.*.reply_to);
    c.g_free(msg.*.reply_preview);
    c.g_free(msg.*.username);
    c.g_free(msg.*.name);
    c.g_free(msg.*.avatar);
    c.g_free(msg.*.created_at);
    c.g_free(msg.*.edited_at);
    c.g_free(msg.*.reactions_summary);
    c.g_free(msg.*.mpi_kind);
    c.g_free(msg.*.mpi_status);
    c.g_free(msg.*.mpi_net);
    c.g_free(msg.*.mpi_gross);
    c.g_free(msg.*.mpi_note);
    c.g_free(msg.*.mpi_order_id);
    c.g_free(msg.*.mpi_payment_link_url);
    if (msg.*.attachments != null) c.g_list_free_full(msg.*.attachments, free_attachment);
    c.g_free(msg);
}

export fn free_messages(messages: [*c]c.GList) void {
    c.g_list_free_full(messages, free_message);
}

fn freePollOption(data: c.gpointer) callconv(.c) void {
    const option: [*c]types.PollOption = @ptrCast(@alignCast(data));
    if (option == null) return;
    c.g_free(option.*.id);
    c.g_free(option.*.option_text);
    c.g_free(option.*.user_vote);
    c.g_free(option);
}

fn freePollData(poll: [*c]types.Poll) void {
    if (poll == null) return;
    c.g_free(poll.*.id);
    c.g_free(poll.*.question);
    c.g_free(poll.*.kind);
    c.g_free(poll.*.expires_at);
    if (poll.*.steps != null) c.json_node_free(poll.*.steps);
    if (poll.*.options != null) c.g_list_free_full(poll.*.options, freePollOption);
    c.g_free(poll);
}

export fn parse_communities(json_data: [*c]const c.gchar) [*c]c.GList {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_communities") orelse return null;
    defer c.g_object_unref(parser);
    const arr = getArrayMemberIfValid(obj, "communities") orelse return null;
    var communities: [*c]c.GList = null;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(arr)) : (i += 1) {
        if (parseCommunityFromObject(getObjectElementIfValid(arr, i))) |comm| communities = c.g_list_append(communities, comm);
    }
    return communities;
}

export fn parse_upload_response(json_data: [*c]const c.gchar) [*c]c.gchar {
    var parser: ?*c.JsonParser = null;
    const obj = parseRoot(json_data, &parser, "parse_upload_response") orelse return null;
    defer c.g_object_unref(parser);
    if (getObjectMemberIfValid(obj, "file")) |file| {
        if (objectMemberHoldsValue(file, "url")) return c.g_strdup(c.json_object_get_string_member(file, "url"));
    }
    if (objectMemberHoldsValue(obj, "file_url")) return c.g_strdup(c.json_object_get_string_member(obj, "file_url"));
    if (objectMemberHoldsValue(obj, "url")) return c.g_strdup(c.json_object_get_string_member(obj, "url"));
    return null;
}

export fn free_community(data: c.gpointer) void {
    const comm: [*c]types.Community = @ptrCast(@alignCast(data));
    if (comm == null) return;
    c.g_free(comm.*.id);
    c.g_free(comm.*.name);
    c.g_free(comm.*.description);
    c.g_free(comm.*.rules);
    c.g_free(comm.*.icon_url);
    c.g_free(comm.*.banner_url);
    c.g_free(comm.*.access_mode);
    c.g_free(comm.*.tag_emoji);
    c.g_free(comm.*.tag_text);
    c.g_free(comm);
}

export fn free_communities(communities: [*c]c.GList) void {
    c.g_list_free_full(communities, free_community);
}

export fn free_tweeta_list(data: c.gpointer) void {
    const list: [*c]types.TweetaList = @ptrCast(@alignCast(data));
    if (list == null) return;
    c.g_free(list.*.id);
    c.g_free(list.*.user_id);
    c.g_free(list.*.name);
    c.g_free(list.*.description);
    c.g_free(list.*.owner_username);
    c.g_free(list.*.owner_name);
    if (list.*.members != null) free_users(list.*.members);
    if (list.*.followers != null) free_users(list.*.followers);
    c.g_free(list);
}

export fn free_tweeta_lists(lists: [*c]c.GList) void {
    c.g_list_free_full(lists, free_tweeta_list);
}
