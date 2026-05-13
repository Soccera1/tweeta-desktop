const std = @import("std");
const c = @import("c.zig").c;
const cstr = @import("cstr.zig");
const api = @import("api.zig");
const constants = @import("constants.zig");
const g = @import("globals_import.zig");
const types = @import("types.zig");

const TRUE: c.gboolean = 1;
const FALSE: c.gboolean = 0;
const ProfileEditWidgets = extern struct {
    name_entry: [*c]c.GtkWidget,
    bio_entry: [*c]c.GtkWidget,
    location_entry: [*c]c.GtkWidget,
    website_entry: [*c]c.GtkWidget,
    pronouns_entry: [*c]c.GtkWidget,
    theme_combo: [*c]c.GtkWidget,
    accent_entry: [*c]c.GtkWidget,
    label_combo: [*c]c.GtkWidget,
    label_automated_check: [*c]c.GtkWidget,
    avatar_radius_spin: [*c]c.GtkWidget,
    include_avatar_radius: c.gboolean,
};
var g_p2p_current_contact: [*c]c.gchar = null;
var g_p2p_mutex: c.GMutex = std.mem.zeroes(c.GMutex);
var load_tweets_mutex: c.GMutex = std.mem.zeroes(c.GMutex);
var active_tweets_request_id: c.guint = 0;
var load_notifications_mutex: c.GMutex = std.mem.zeroes(c.GMutex);
var active_notifications_request_id: c.guint = 0;
const notifications_page_size: c.gint = 20;
var load_conversations_mutex: c.GMutex = std.mem.zeroes(c.GMutex);
var active_conversations_request_id: c.guint = 0;
var load_messages_mutex: c.GMutex = std.mem.zeroes(c.GMutex);
var active_messages_request_id: c.guint = 0;

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn textOr(value: [*c]const c.gchar) [*c]const c.gchar {
    return if (value != null) value else lit("");
}

fn unused(args: anytype) void {
    inline for (args) |arg| _ = arg;
}

fn logWarning(comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), c.G_LOG_LEVEL_WARNING, lit(fmt) } ++ args);
}

fn logDebug(comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), c.G_LOG_LEVEL_DEBUG, lit(fmt) } ++ args);
}

fn setStack(name: [*c]const c.gchar) void {
    if (g.g_stack != null) c.gtk_stack_set_visible_child_name(@ptrCast(@alignCast(g.g_stack)), name);
}

fn labelSet(widget: [*c]c.GtkWidget, text: [*c]const c.gchar) void {
    if (widget != null) c.gtk_label_set_text(@ptrCast(@alignCast(widget)), text);
}

fn buttonSet(widget: [*c]c.GtkWidget, text: [*c]const c.gchar) void {
    if (widget != null) c.gtk_button_set_label(@ptrCast(@alignCast(widget)), text);
}

fn asWidget(value: anytype) [*c]c.GtkWidget {
    return @ptrCast(@alignCast(value));
}

fn container(value: anytype) [*c]c.GtkContainer {
    return @ptrCast(@alignCast(value));
}

fn listBox(value: anytype) [*c]c.GtkListBox {
    return @ptrCast(@alignCast(value));
}

fn asEntry(value: anytype) [*c]c.GtkEntry {
    return @ptrCast(@alignCast(value));
}

fn asDialog(value: anytype) [*c]c.GtkDialog {
    return @ptrCast(@alignCast(value));
}

fn asWindow(value: anytype) [*c]c.GtkWindow {
    return @ptrCast(@alignCast(value));
}

fn widgetWindow(widget: [*c]c.GtkWidget) [*c]c.GtkWindow {
    const toplevel = c.gtk_widget_get_toplevel(widget);
    return if (toplevel != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(toplevel)), c.gtk_window_get_type()) != FALSE) @ptrCast(@alignCast(toplevel)) else null;
}

fn asGrid(value: anytype) [*c]c.GtkGrid {
    return @ptrCast(@alignCast(value));
}

fn asBox(value: anytype) [*c]c.GtkBox {
    return @ptrCast(@alignCast(value));
}

fn cb(function: anytype) c.GCallback {
    return @ptrCast(&function);
}

fn connect(widget: [*c]c.GtkWidget, signal: [*c]const c.gchar, function: anytype, data: c.gpointer) void {
    _ = c.g_signal_connect_data(widget, signal, cb(function), data, null, c.G_CONNECT_DEFAULT);
}

fn freeClosureData(data: c.gpointer, closure: ?*c.GClosure) callconv(.c) void {
    unused(.{closure});
    c.g_free(data);
}

fn getUsernameSafe() [*c]c.gchar {
    c.g_mutex_lock(&g.g_globals_mutex);
    const out = if (g.g_current_username != null) c.g_strdup(g.g_current_username) else null;
    c.g_mutex_unlock(&g.g_globals_mutex);
    return out;
}

fn getAuthTokenSafe() [*c]c.gchar {
    c.g_mutex_lock(&g.g_globals_mutex);
    const out = if (g.g_auth_token != null) c.g_strdup(g.g_auth_token) else null;
    c.g_mutex_unlock(&g.g_globals_mutex);
    return out;
}

fn updateAdminImpersonationStatusLabel() void {
    if (g.g_admin_impersonation_status_label == null) return;
    c.g_mutex_lock(&g.g_globals_mutex);
    const current_username = if (g.g_current_username != null) c.g_strdup(g.g_current_username) else null;
    const admin_username = if (g.g_impersonation_admin_username != null) c.g_strdup(g.g_impersonation_admin_username) else null;
    const is_impersonating = g.g_is_impersonating;
    c.g_mutex_unlock(&g.g_globals_mutex);
    defer c.g_free(current_username);
    defer c.g_free(admin_username);
    if (is_impersonating != FALSE and current_username != null) {
        const text = c.g_strdup_printf("Impersonating @%s. Admin session preserved%s%s.", current_username, if (admin_username != null) lit(" from @") else lit(""), if (admin_username != null) admin_username else lit(""));
        defer c.g_free(text);
        labelSet(g.g_admin_impersonation_status_label, text);
    } else {
        labelSet(g.g_admin_impersonation_status_label, "Admin session active.");
    }
}

fn simpleRequest(url: [*c]const c.gchar, payload: [*c]const c.gchar, method: [*c]const c.gchar) c.gboolean {
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    const ok = api.fetch_url(url, &chunk, payload, method);
    c.g_free(chunk.memory);
    return ok;
}

fn requestWithResponse(url: [*c]const c.gchar, payload: [*c]const c.gchar, method: [*c]const c.gchar, response_out: ?*[*c]c.gchar) c.gboolean {
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    var ok: c.gboolean = FALSE;
    if (response_out) |out| out.* = null;
    if (api.fetch_url(url, &chunk, payload, method) != FALSE) {
        ok = if (chunk.memory != null and c.strstr(chunk.memory, "\"error\"") == null) TRUE else FALSE;
        if (response_out) |out| {
            out.* = chunk.memory;
            chunk.memory = null;
        }
    }
    c.g_free(chunk.memory);
    return ok;
}

fn builderPayload(builder: [*c]c.JsonBuilder) [*c]c.gchar {
    const generator = c.json_generator_new();
    defer c.g_object_unref(generator);
    const root = c.json_builder_get_root(builder);
    defer c.json_node_free(root);
    c.json_generator_set_root(generator, root);
    return c.json_generator_to_data(generator, null);
}

fn requestBuilder(url: [*c]const c.gchar, method: [*c]const c.gchar, builder: [*c]c.JsonBuilder) c.gboolean {
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    return simpleRequest(url, payload, method);
}

fn requestBuilderWithResponse(url: [*c]const c.gchar, method: [*c]const c.gchar, builder: [*c]c.JsonBuilder, response_out: ?*[*c]c.gchar) c.gboolean {
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    return requestWithResponse(url, payload, method, response_out);
}

fn freeAsyncData(data: [*c]types.AsyncData) void {
    if (data == null) return;
    if (data.*.tweets != null) api.free_tweets(data.*.tweets);
    if (data.*.users != null) api.free_users(data.*.users);
    if (data.*.notifications != null) api.free_notifications(data.*.notifications);
    if (data.*.conversations != null) api.free_conversations(data.*.conversations);
    if (data.*.messages != null) api.free_messages(data.*.messages);
    if (data.*.communities != null) api.free_communities(data.*.communities);
    if (data.*.lists != null) api.free_tweeta_lists(data.*.lists);
    if (data.*.conversation != null) api.free_conversation(data.*.conversation);
    if (data.*.list != null) api.free_tweeta_list(data.*.list);
    if (data.*.profile != null) api.free_user(data.*.profile);
    c.g_free(data.*.username);
    c.g_free(data.*.query);
    c.g_free(data.*.conversation_id);
    c.g_free(data.*.community_id);
    c.g_free(data.*.json_data);
    c.g_free(data.*.before_id);
    c.g_free(data);
}

fn responseHasSuccessFlag(json_data: [*c]const c.gchar, state_key: [*c]const c.gchar, state_value: ?*c.gboolean) c.gboolean {
    if (state_value) |out| out.* = FALSE;
    if (json_data == null) return FALSE;
    const parser = c.json_parser_new();
    defer c.g_object_unref(parser);
    var err: ?*c.GError = null;
    if (c.json_parser_load_from_data(parser, json_data, -1, &err) == FALSE) {
        if (err) |e| c.g_error_free(e);
        return FALSE;
    }
    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) return FALSE;
    const obj = c.json_node_get_object(root);
    if (obj == null) return FALSE;
    if (state_key != null and state_value != null and c.json_object_has_member(obj, state_key) != FALSE) {
        state_value.?.* = c.json_object_get_boolean_member(obj, state_key);
    }
    return if (c.json_object_has_member(obj, "success") != FALSE and c.json_object_get_boolean_member(obj, "success") != FALSE) TRUE else FALSE;
}

fn responseBooleanMember(json_data: [*c]const c.gchar, key: [*c]const c.gchar, value_out: *c.gboolean) c.gboolean {
    if (json_data == null or key == null) return FALSE;
    const parser = c.json_parser_new();
    defer c.g_object_unref(parser);
    var err: ?*c.GError = null;
    if (c.json_parser_load_from_data(parser, json_data, -1, &err) == FALSE) {
        if (err) |e| c.g_error_free(e);
        return FALSE;
    }
    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) return FALSE;
    const obj = c.json_node_get_object(root);
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return FALSE;
    value_out.* = c.json_object_get_boolean_member(obj, key);
    return TRUE;
}

fn hasAdminSession() bool {
    c.g_mutex_lock(&g.g_globals_mutex);
    const ok = g.g_is_admin != FALSE or (g.g_is_impersonating != FALSE and g.g_impersonation_admin_token != null);
    c.g_mutex_unlock(&g.g_globals_mutex);
    return ok;
}

fn getAdminAuthTokenSafe() [*c]c.gchar {
    c.g_mutex_lock(&g.g_globals_mutex);
    const token = if (g.g_is_impersonating != FALSE and g.g_impersonation_admin_token != null) c.g_strdup(g.g_impersonation_admin_token) else if (g.g_auth_token != null) c.g_strdup(g.g_auth_token) else null;
    c.g_mutex_unlock(&g.g_globals_mutex);
    return token;
}

fn adminRequestWithResponse(url: [*c]const c.gchar, payload: [*c]const c.gchar, method: [*c]const c.gchar, response_out: ?*[*c]c.gchar) c.gboolean {
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    const admin_token = getAdminAuthTokenSafe();
    defer c.g_free(admin_token);
    if (api.fetch_url_with_auth_token(url, &chunk, payload, method, admin_token) == FALSE) return FALSE;
    if (response_out) |out| {
        out.* = chunk.memory;
        chunk.memory = null;
    }
    c.g_free(chunk.memory);
    return TRUE;
}

fn adminSimpleRequest(url: [*c]const c.gchar, payload: [*c]const c.gchar, method: [*c]const c.gchar) c.gboolean {
    return adminRequestWithResponse(url, payload, method, null);
}

fn adminRequestErrorFromResponse(url: [*c]const c.gchar, payload: [*c]const c.gchar, method: [*c]const c.gchar, error_out: *[*c]c.gchar) c.gboolean {
    error_out.* = null;
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (adminRequestWithResponse(url, payload, method, &response) == FALSE) return FALSE;
    error_out.* = extractErrorMessage(response);
    return if (error_out.* == null) TRUE else FALSE;
}

fn performAdminDeleteItem(url: [*c]const c.gchar, error_title: [*c]const c.gchar, refresh_fn: ?fn () callconv(.c) void) void {
    if (url == null) return;
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (adminRequestErrorFromResponse(url, null, "DELETE", &error_message) == FALSE) {
        showModalMessage(
            c.GTK_MESSAGE_ERROR,
            if (error_title != null) error_title else "Delete failed.",
            if (error_message != null) error_message else "The request could not be sent.",
        );
    } else if (refresh_fn) |refresh| {
        refresh();
    }
}

fn entryText(widget: [*c]c.GtkWidget) [*c]const c.gchar {
    if (widget == null) return null;
    return c.gtk_entry_get_text(@ptrCast(@alignCast(widget)));
}

fn tweetUrlForList(list_box: [*c]c.GtkListBox, before_id: [*c]const c.gchar) [*c]c.gchar {
    const feed_type: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(list_box)), "feed_type"));
    var base_owned: [*c]c.gchar = null;
    var base: [*c]const c.gchar = if (g.g_auth_token == null) constants.PUBLIC_TWEETS_URL else if (g.g_current_timeline_type == types.TimelineType.TIMELINE_FOLLOWING) constants.FOLLOWING_TIMELINE_URL else constants.TIMELINE_URL;
    var escape_before = false;
    if (c.g_strcmp0(feed_type, "public") == 0) {
        base = constants.PUBLIC_TWEETS_URL;
    } else if (c.g_strcmp0(feed_type, "bookmarks") == 0) {
        base = constants.BOOKMARKS_LIST_URL;
    } else if (c.g_strcmp0(feed_type, "community") == 0 or c.g_strcmp0(feed_type, "community_tweets") == 0) {
        c.g_mutex_lock(&g.g_globals_mutex);
        const community_id = if (g.g_community_id != null) c.g_strdup(g.g_community_id) else null;
        c.g_mutex_unlock(&g.g_globals_mutex);
        defer c.g_free(community_id);
        if (community_id == null) return null;
        base_owned = c.g_strdup_printf(constants.COMMUNITY_TWEETS_URL, @as([*c]const c.gchar, @ptrCast(community_id)));
        base = base_owned;
    } else if (c.g_strcmp0(feed_type, "profile_posts") == 0) {
        const username: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(list_box)), "current_profile_user"));
        if (username != null) {
            base_owned = c.g_strdup_printf(constants.PROFILE_POSTS_URL, username);
            base = base_owned;
            escape_before = true;
        }
    } else if (c.g_strcmp0(feed_type, "profile_replies") == 0) {
        const username: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(list_box)), "current_profile_user"));
        if (username != null) {
            base_owned = c.g_strdup_printf(constants.PROFILE_REPLIES_URL, username);
            base = base_owned;
        }
    }
    var escaped_before: [*c]c.gchar = null;
    if (escape_before and before_id != null and before_id[0] != 0) escaped_before = c.g_uri_escape_string(before_id, null, TRUE);
    const cursor = if (escaped_before != null) escaped_before else before_id;
    const url = if (cursor != null and cursor[0] != 0) c.g_strdup_printf("%s?before=%s", base, cursor) else c.g_strdup(base);
    c.g_free(escaped_before);
    c.g_free(base_owned);
    return url;
}

fn removeLoadingMoreLabel(list_box: [*c]c.GtkListBox) void {
    const children = c.gtk_container_get_children(container(list_box));
    const last = c.g_list_last(children);
    if (last != null) {
        const candidate = asWidget(last.*.data);
        if (candidate != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(candidate)), c.gtk_label_get_type()) != FALSE) {
            const text = c.gtk_label_get_text(@ptrCast(@alignCast(candidate)));
            if (c.g_strcmp0(text, "Loading more...") == 0) c.gtk_widget_destroy(asWidget(last.*.data));
        }
    }
    c.g_list_free(children);
}

fn appendEndOfListLabel(list_box: [*c]c.GtkListBox) void {
    const label = c.gtk_label_new("You've reached the end.");
    c.gtk_widget_set_halign(label, c.GTK_ALIGN_CENTER);
    c.gtk_widget_set_margin_top(label, 12);
    c.gtk_widget_set_margin_bottom(label, 12);
    c.gtk_widget_set_opacity(label, 0.7);
    c.gtk_widget_show(label);
    c.gtk_list_box_insert(list_box, label, -1);
}

fn tweetPaginationCursor(list_box: [*c]c.GtkListBox, tweet: [*c]types.Tweet) [*c]c.gchar {
    if (tweet == null) return null;
    if (list_box == @as([*c]c.GtkListBox, @ptrCast(@alignCast(g.g_profile_tweets_list)))) {
        const sort_date = if (tweet.*.content_type != null and c.g_strcmp0(tweet.*.content_type, "retweet") == 0 and tweet.*.retweet_created_at != null and tweet.*.retweet_created_at[0] != 0)
            tweet.*.retweet_created_at
        else
            tweet.*.created_at;
        return if (sort_date != null and sort_date[0] != 0) c.g_strdup(sort_date) else null;
    }
    return if (tweet.*.id != null and tweet.*.id[0] != 0) c.g_strdup(tweet.*.id) else null;
}

fn loadTweetsInto(list_box: [*c]c.GtkListBox, url: [*c]const c.gchar) void {
    if (list_box == null or url == null) return;
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        const feed_type: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(list_box)), "feed_type"));
        const tweets = if (c.g_strcmp0(feed_type, "profile_replies") == 0) api.parse_profile_replies(chunk.memory) else api.parse_tweets(chunk.memory);
        api.populate_tweet_list(list_box, tweets);
        api.free_tweets(tweets);
    } else {
        setListBoxStatus(list_box, "Failed to load tweets.");
    }
    c.g_free(chunk.memory);
}

fn appendTweetsInto(list_box: [*c]c.GtkListBox, url: [*c]const c.gchar) void {
    if (list_box == null or url == null) return;
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        const feed_type: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(list_box)), "feed_type"));
        const tweets = if (c.g_strcmp0(feed_type, "profile_replies") == 0) api.parse_profile_replies(chunk.memory) else api.parse_tweets(chunk.memory);
        removeLoadingMoreLabel(list_box);
        if (tweets != null) {
            api.append_tweets_to_list(list_box, tweets);
        } else {
            c.g_object_set_data(@ptrCast(@alignCast(list_box)), "last_id", null);
            appendEndOfListLabel(list_box);
        }
        api.free_tweets(tweets);
    } else {
        removeLoadingMoreLabel(list_box);
    }
    c.g_free(chunk.memory);
}

fn onTweetsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));

    c.g_mutex_lock(&load_tweets_mutex);
    const is_active = async_data.*.request_id == active_tweets_request_id;
    c.g_mutex_unlock(&load_tweets_mutex);

    if (!is_active) {
        freeAsyncData(async_data);
        return FALSE;
    }

    c.g_object_set_data(@ptrCast(@alignCast(async_data.*.list_box)), "loading_more", null);

    if (async_data.*.success != FALSE and async_data.*.tweets != null) {
        if (async_data.*.is_append != FALSE) {
            removeLoadingMoreLabel(async_data.*.list_box);
            api.append_tweets_to_list(async_data.*.list_box, async_data.*.tweets);
        } else {
            api.populate_tweet_list(async_data.*.list_box, async_data.*.tweets);
        }

        const last = c.g_list_last(async_data.*.tweets);
        if (last != null) {
            const last_tweet: [*c]types.Tweet = @ptrCast(@alignCast(last.*.data));
            const cursor = tweetPaginationCursor(async_data.*.list_box, last_tweet);
            if (cursor != null) {
                c.g_object_set_data_full(@ptrCast(@alignCast(async_data.*.list_box)), "last_id", cursor, c.g_free);
            } else {
                c.g_object_set_data(@ptrCast(@alignCast(async_data.*.list_box)), "last_id", null);
            }
        } else {
            c.g_object_set_data(@ptrCast(@alignCast(async_data.*.list_box)), "last_id", null);
        }

        api.free_tweets(async_data.*.tweets);
        async_data.*.tweets = null;
    } else if (async_data.*.success != FALSE and async_data.*.is_append != FALSE) {
        removeLoadingMoreLabel(async_data.*.list_box);
        c.g_object_set_data(@ptrCast(@alignCast(async_data.*.list_box)), "last_id", null);
        appendEndOfListLabel(async_data.*.list_box);
    } else {
        if (async_data.*.is_append == FALSE) {
            setListBoxStatus(async_data.*.list_box, "Failed to load tweets.");
        } else {
            removeLoadingMoreLabel(async_data.*.list_box);
        }
    }

    freeAsyncData(async_data);
    return FALSE;
}

fn fetchTweetsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    var empty_response = FALSE;
    const url = tweetUrlForList(async_data.*.list_box, async_data.*.before_id);

    const feed_type: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(async_data.*.list_box)), "feed_type"));
    if (url != null and api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        if (c.g_strcmp0(feed_type, "profile_replies") == 0) {
            async_data.*.tweets = api.parse_profile_replies(chunk.memory);
            empty_response = api.profile_replies_response_is_empty(chunk.memory);
        } else {
            async_data.*.tweets = api.parse_tweets(chunk.memory);
            empty_response = api.tweets_response_is_empty(chunk.memory);
        }
        async_data.*.success = if (async_data.*.tweets != null or empty_response != FALSE) TRUE else FALSE;
    } else {
        async_data.*.success = FALSE;
    }

    c.g_free(chunk.memory);
    c.g_free(url);
    _ = c.g_idle_add(@ptrCast(&onTweetsLoaded), async_data);
    return null;
}

fn onNotificationsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));

    c.g_mutex_lock(&load_notifications_mutex);
    const is_active = async_data.*.request_id == active_notifications_request_id;
    c.g_mutex_unlock(&load_notifications_mutex);

    if (!is_active) {
        freeAsyncData(async_data);
        return FALSE;
    }

    c.g_object_set_data(@ptrCast(@alignCast(async_data.*.list_box)), "loading_more", null);

    if (async_data.*.success != FALSE and async_data.*.notifications != null) {
        if (async_data.*.is_append != FALSE) {
            removeLoadingMoreLabel(async_data.*.list_box);
            api.append_notifications_to_list(async_data.*.list_box, async_data.*.notifications);
        } else {
            api.populate_notification_list(async_data.*.list_box, async_data.*.notifications);
        }

        const last = c.g_list_last(async_data.*.notifications);
        if (last != null) {
            const last_notification: [*c]types.Notification = @ptrCast(@alignCast(last.*.data));
            c.g_object_set_data_full(@ptrCast(@alignCast(async_data.*.list_box)), "last_id", c.g_strdup(last_notification.*.id), c.g_free);
        } else {
            c.g_object_set_data(@ptrCast(@alignCast(async_data.*.list_box)), "last_id", null);
        }

        if (async_data.*.has_more == FALSE) {
            c.g_object_set_data(@ptrCast(@alignCast(async_data.*.list_box)), "last_id", null);
            if (async_data.*.is_append != FALSE) appendEndOfListLabel(async_data.*.list_box);
        }

        api.free_notifications(async_data.*.notifications);
        async_data.*.notifications = null;
    } else if (async_data.*.success != FALSE and async_data.*.is_append != FALSE) {
        removeLoadingMoreLabel(async_data.*.list_box);
        c.g_object_set_data(@ptrCast(@alignCast(async_data.*.list_box)), "last_id", null);
        appendEndOfListLabel(async_data.*.list_box);
    } else {
        if (async_data.*.is_append == FALSE) {
            setListBoxStatus(async_data.*.list_box, if (async_data.*.success != FALSE) "No notifications." else "Failed to load notifications.");
        } else {
            removeLoadingMoreLabel(async_data.*.list_box);
        }
    }

    freeAsyncData(async_data);
    return FALSE;
}

fn fetchNotificationsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = if (async_data.*.before_id != null)
        c.g_strdup_printf("%s?limit=%d&before=%s", constants.NOTIFICATIONS_URL, notifications_page_size, async_data.*.before_id)
    else
        c.g_strdup_printf("%s?limit=%d", constants.NOTIFICATIONS_URL, notifications_page_size);
    defer c.g_free(url);

    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.notifications = api.parse_notifications(chunk.memory);
        async_data.*.has_more = if (async_data.*.notifications != null and c.g_list_length(async_data.*.notifications) >= notifications_page_size) TRUE else FALSE;
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onNotificationsLoaded), async_data);
    return null;
}

fn onConversationsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));

    c.g_mutex_lock(&load_conversations_mutex);
    const is_active = async_data.*.request_id == active_conversations_request_id;
    c.g_mutex_unlock(&load_conversations_mutex);

    if (!is_active) {
        freeAsyncData(async_data);
        return FALSE;
    }

    if (async_data.*.success != FALSE and async_data.*.conversations != null) {
        api.populate_conversation_list(async_data.*.list_box, async_data.*.conversations);
        api.free_conversations(async_data.*.conversations);
        async_data.*.conversations = null;
    } else {
        setListBoxStatus(async_data.*.list_box, if (async_data.*.success != FALSE) "No conversations." else "Failed to load conversations.");
    }

    freeAsyncData(async_data);
    return FALSE;
}

fn fetchConversationsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(constants.DM_CONVERSATIONS_URL, &chunk, null, "GET") != FALSE) {
        async_data.*.conversations = api.parse_conversations(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onConversationsLoaded), async_data);
    return null;
}

fn onMessagesLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));

    c.g_mutex_lock(&load_messages_mutex);
    const is_active = async_data.*.request_id == active_messages_request_id;
    c.g_mutex_unlock(&load_messages_mutex);

    if (!is_active) {
        freeAsyncData(async_data);
        return FALSE;
    }

    if (async_data.*.conversation != null) {
        if (g.g_dm_title_label != null) {
            c.gtk_label_set_text(
                @ptrCast(@alignCast(g.g_dm_title_label)),
                if (async_data.*.conversation.*.display_name != null) async_data.*.conversation.*.display_name else if (async_data.*.conversation.*.title != null) async_data.*.conversation.*.title else "Messages",
            );
        }
        if (g.g_dm_info_label != null) {
            const info = buildDmConversationInfo(async_data.*.conversation);
            defer c.g_free(info);
            c.gtk_label_set_text(@ptrCast(@alignCast(g.g_dm_info_label)), info);
        }
        c.g_object_set_data_full(@ptrCast(@alignCast(g.g_dm_messages_list)), "conversation_detail", async_data.*.conversation, api.free_conversation);
        async_data.*.conversation = null;
    }

    if (async_data.*.success != FALSE and async_data.*.messages != null) {
        api.populate_message_list(async_data.*.list_box, async_data.*.messages);
        api.free_messages(async_data.*.messages);
        async_data.*.messages = null;
    } else {
        setListBoxStatus(async_data.*.list_box, if (async_data.*.success != FALSE) "No messages." else "Failed to load messages.");
    }

    freeAsyncData(async_data);
    return FALSE;
}

fn fetchMessagesThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.DM_MESSAGES_URL, async_data.*.conversation_id);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.conversation = api.parse_conversation_details(chunk.memory);
        async_data.*.messages = api.parse_messages(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onMessagesLoaded), async_data);
    return null;
}

fn renderTweetThread(list: [*c]c.GtkListBox, tweets: [*c]c.GList, tweet_id: [*c]const c.gchar) void {
    clearListBox(list);
    const first_t: [*c]types.Tweet = if (tweets != null) @ptrCast(@alignCast(tweets.*.data)) else null;
    const op_username = if (first_t != null) first_t.*.author_username else null;
    var main_tweet_reached: c.gboolean = FALSE;
    var l = tweets;
    while (l != null) : (l = l.*.next) {
        const tweet: [*c]types.Tweet = @ptrCast(@alignCast(l.*.data));
        const is_main = if (tweet != null and c.g_strcmp0(tweet.*.id, tweet_id) == 0) TRUE else FALSE;
        if (is_main != FALSE) {
            if (main_tweet_reached == FALSE and l != tweets) {
                c.gtk_list_box_insert(list, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), -1);
            }
            main_tweet_reached = TRUE;
        } else if (main_tweet_reached != FALSE and l != tweets) {
            const prev_l: [*c]c.GList = @ptrCast(@alignCast(l.*.prev));
            if (prev_l != null) {
                const prev_t: [*c]types.Tweet = @ptrCast(@alignCast(prev_l.*.data));
                if (prev_t != null and c.g_strcmp0(prev_t.*.id, tweet_id) == 0) {
                    const header = c.gtk_label_new("Replies");
                    c.gtk_widget_set_margin_top(header, 10);
                    c.gtk_widget_set_margin_bottom(header, 5);
                    c.gtk_widget_set_halign(header, c.GTK_ALIGN_START);
                    c.gtk_widget_set_margin_start(header, 10);
                    const attrs = c.pango_attr_list_new();
                    c.pango_attr_list_insert(attrs, c.pango_attr_weight_new(c.PANGO_WEIGHT_BOLD));
                    c.gtk_label_set_attributes(@ptrCast(@alignCast(header)), attrs);
                    c.pango_attr_list_unref(attrs);
                    c.gtk_widget_show(header);
                    c.gtk_list_box_insert(list, header, -1);
                }
            }
        }
        const current_op = if (l == tweets or is_main != FALSE) null else op_username;
        const tweet_widget = api.create_tweet_widget_full(tweet, current_op);
        if (is_main != FALSE) {
            const context = c.gtk_widget_get_style_context(tweet_widget);
            c.gtk_style_context_add_class(context, "main-tweet");
        }
        c.gtk_widget_show_all(tweet_widget);
        c.gtk_list_box_insert(list, tweet_widget, -1);
    }
}

fn onTweetLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (g.g_conversation_list == null) {
        freeAsyncData(async_data);
        return FALSE;
    }
    const list = listBox(g.g_conversation_list);
    if (async_data.*.success != FALSE and async_data.*.tweets != null) {
        renderTweetThread(list, async_data.*.tweets, async_data.*.query);
        api.free_tweets(async_data.*.tweets);
        async_data.*.tweets = null;
    } else {
        setListBoxStatus(list, "Tweet not found or error loading.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchTweetThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.TWEET_DETAILS_URL, async_data.*.query);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.tweets = api.parse_tweet_details(chunk.memory);
        async_data.*.success = if (async_data.*.tweets != null) TRUE else FALSE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onTweetLoaded), async_data);
    return null;
}

fn setTweetListLastId(list_box: [*c]c.GtkListBox, tweets: [*c]c.GList, use_cursor: bool) void {
    if (list_box == null) return;
    const last = c.g_list_last(tweets);
    if (last != null) {
        const last_tweet: [*c]types.Tweet = @ptrCast(@alignCast(last.*.data));
        const cursor = if (use_cursor) tweetPaginationCursor(list_box, last_tweet) else if (last_tweet != null and last_tweet.*.id != null) c.g_strdup(last_tweet.*.id) else null;
        if (cursor != null) {
            c.g_object_set_data_full(@ptrCast(@alignCast(list_box)), "last_id", cursor, c.g_free);
        } else {
            c.g_object_set_data(@ptrCast(@alignCast(list_box)), "last_id", null);
        }
    } else {
        c.g_object_set_data(@ptrCast(@alignCast(list_box)), "last_id", null);
    }
}

fn onProfileLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));

    if (async_data.*.success != FALSE and async_data.*.profile != null) {
        if (g.g_active_profile != null) {
            api.free_user(g.g_active_profile);
            g.g_active_profile = null;
        }
        g.g_active_profile = async_data.*.profile;
        async_data.*.profile = null;
        const profile = g.g_active_profile;

        setProfileActionUsername(profile.*.username);
        setProfileActionUserId(profile.*.id);

        const stats = c.g_strdup_printf("%d Followers · %d Following · %d Posts", profile.*.follower_count, profile.*.following_count, profile.*.post_count);
        const username_label = if (profile.*.username != null) c.g_strdup_printf("@%s", profile.*.username) else c.g_strdup("");
        const status = buildProfileStatusText(profile);
        const details = buildProfileDetailsText(profile);
        defer c.g_free(stats);
        defer c.g_free(username_label);
        defer c.g_free(status);
        defer c.g_free(details);
        labelSet(g.g_profile_name_label, if (profile.*.name != null) profile.*.name else "Unknown");
        labelSet(g.g_profile_username_label, username_label);
        labelSet(g.g_profile_bio_label, textOr(profile.*.bio));
        labelSet(g.g_profile_status_label, status);
        labelSet(g.g_profile_details_label, details);
        labelSet(g.g_profile_stats_label, stats);
        updateProfileBadges(profile);

        if (g.g_profile_banner_image != null) {
            c.gtk_image_clear(@ptrCast(@alignCast(g.g_profile_banner_image)));
            if (profile.*.banner != null and profile.*.banner[0] != 0) {
                c.gtk_widget_show(g.g_profile_banner_image);
                api.load_avatar(g.g_profile_banner_image, profile.*.banner, 640);
            } else {
                c.gtk_widget_hide(g.g_profile_banner_image);
            }
        }
        if (g.g_profile_avatar_image != null) {
            c.gtk_image_set_from_icon_name(@ptrCast(@alignCast(g.g_profile_avatar_image)), "avatar-default", c.GTK_ICON_SIZE_DND);
            if (profile.*.avatar != null) api.load_avatar(g.g_profile_avatar_image, profile.*.avatar, 80);
        }

        if (g.g_follow_button != null) {
            if (profile.*.is_own_profile == FALSE and profile.*.username != null and profile.*.username[0] != 0 and profile.*.blocked_by_profile == FALSE and profile.*.blocked_profile == FALSE and g.g_auth_token != null) {
                setObjectStringData(@ptrCast(@alignCast(g.g_follow_button)), "username", profile.*.username);
                setButtonBoolData(@ptrCast(@alignCast(g.g_follow_button)), "is_following", profile.*.is_following);
                buttonSet(@ptrCast(@alignCast(g.g_follow_button)), if (profile.*.is_following != FALSE) "Unfollow" else "Follow");
                c.gtk_widget_show(g.g_follow_button);
            } else c.gtk_widget_hide(g.g_follow_button);
        }
        if (g.g_profile_edit_button != null) {
            if (profile.*.is_own_profile != FALSE and g.g_auth_token != null) c.gtk_widget_show(g.g_profile_edit_button) else c.gtk_widget_hide(g.g_profile_edit_button);
        }
        if (g.g_profile_notify_button != null) {
            if (profile.*.is_own_profile == FALSE and g.g_auth_token != null and profile.*.username != null and profile.*.username[0] != 0 and profile.*.is_following != FALSE and profile.*.blocked_by_profile == FALSE and profile.*.blocked_profile == FALSE) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_notify_button)), "username", profile.*.username);
                buttonSet(@ptrCast(@alignCast(g.g_profile_notify_button)), if (profile.*.notify_tweets != FALSE) "Alerts On" else "Alerts Off");
                c.gtk_widget_show(g.g_profile_notify_button);
            } else c.gtk_widget_hide(g.g_profile_notify_button);
        }
        if (g.g_profile_block_button != null) {
            if (profile.*.is_own_profile == FALSE and g.g_auth_token != null and profile.*.id != null and profile.*.username != null and profile.*.username[0] != 0) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_block_button)), "user_id", profile.*.id);
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_block_button)), "username", profile.*.username);
                buttonSet(@ptrCast(@alignCast(g.g_profile_block_button)), if (profile.*.blocked_profile != FALSE) "Unblock" else "Block");
                c.gtk_widget_show(g.g_profile_block_button);
            } else c.gtk_widget_hide(g.g_profile_block_button);
        }
        if (g.g_profile_mute_button != null) {
            if (profile.*.is_own_profile == FALSE and g.g_auth_token != null and profile.*.id != null and profile.*.username != null and profile.*.username[0] != 0) {
                const muted = check_user_muted(profile.*.username);
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_mute_button)), "user_id", profile.*.id);
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_mute_button)), "username", profile.*.username);
                setButtonBoolData(@ptrCast(@alignCast(g.g_profile_mute_button)), "muted_state", muted);
                buttonSet(@ptrCast(@alignCast(g.g_profile_mute_button)), if (muted != FALSE) "Unmute" else "Mute");
                c.gtk_widget_show(g.g_profile_mute_button);
            } else c.gtk_widget_hide(g.g_profile_mute_button);
        }
        if (g.g_profile_report_button != null) {
            if (profile.*.is_own_profile == FALSE and g.g_auth_token != null and profile.*.id != null) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_report_button)), "user_id", profile.*.id);
                c.gtk_widget_show(g.g_profile_report_button);
            } else c.gtk_widget_hide(g.g_profile_report_button);
        }
        if (g.g_profile_affiliate_button != null) {
            if (profile.*.is_own_profile == FALSE and g.g_auth_token != null and profile.*.username != null and profile.*.username[0] != 0) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_affiliate_button)), "username", profile.*.username);
                c.gtk_widget_show(g.g_profile_affiliate_button);
            } else c.gtk_widget_hide(g.g_profile_affiliate_button);
        }
        if (g.g_profile_shop_button != null) {
            if (profile.*.username != null and profile.*.username[0] != 0) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_shop_button)), "username", profile.*.username);
                c.gtk_widget_show(g.g_profile_shop_button);
            } else c.gtk_widget_hide(g.g_profile_shop_button);
        }
        if (g.g_profile_donate_button != null) {
            if (profile.*.is_own_profile == FALSE and g.g_auth_token != null and profile.*.username != null and profile.*.username[0] != 0) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_donate_button)), "username", profile.*.username);
                c.gtk_widget_show(g.g_profile_donate_button);
            } else c.gtk_widget_hide(g.g_profile_donate_button);
        }
        if (g.g_profile_algorithm_button != null) {
            if (profile.*.username != null and profile.*.username[0] != 0) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_algorithm_button)), "username", profile.*.username);
                c.gtk_widget_show(g.g_profile_algorithm_button);
            } else c.gtk_widget_hide(g.g_profile_algorithm_button);
        }
        if (g.g_profile_spam_score_button != null) {
            if (profile.*.username != null and profile.*.username[0] != 0) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_spam_score_button)), "username", profile.*.username);
                c.gtk_widget_show(g.g_profile_spam_score_button);
            } else c.gtk_widget_hide(g.g_profile_spam_score_button);
        }
        if (g.g_profile_analytics_button != null) {
            if (profile.*.username != null and profile.*.username[0] != 0) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_analytics_button)), "username", profile.*.username);
                c.gtk_widget_show(g.g_profile_analytics_button);
            } else c.gtk_widget_hide(g.g_profile_analytics_button);
        }
        if (g.g_profile_common_followers_button != null) {
            if (profile.*.is_own_profile == FALSE and g.g_auth_token != null and profile.*.username != null and profile.*.username[0] != 0) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_common_followers_button)), "username", profile.*.username);
                c.gtk_widget_show(g.g_profile_common_followers_button);
            } else c.gtk_widget_hide(g.g_profile_common_followers_button);
        }
        if (g.g_profile_top_posts_button != null) {
            if (profile.*.username != null and profile.*.username[0] != 0) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_top_posts_button)), "username", profile.*.username);
                c.gtk_widget_show(g.g_profile_top_posts_button);
            } else c.gtk_widget_hide(g.g_profile_top_posts_button);
        }
        if (g.g_profile_communities_button != null) {
            if (profile.*.id != null and profile.*.id[0] != 0) {
                setObjectStringData(@ptrCast(@alignCast(g.g_profile_communities_button)), "user_id", profile.*.id);
                c.gtk_widget_show(g.g_profile_communities_button);
            } else c.gtk_widget_hide(g.g_profile_communities_button);
        }
        if (g.g_profile_delete_avatar_button != null) {
            if (profile.*.is_own_profile != FALSE and g.g_auth_token != null) c.gtk_widget_show(g.g_profile_delete_avatar_button) else c.gtk_widget_hide(g.g_profile_delete_avatar_button);
        }
        if (g.g_profile_delete_banner_button != null) {
            if (profile.*.is_own_profile != FALSE and g.g_auth_token != null) c.gtk_widget_show(g.g_profile_delete_banner_button) else c.gtk_widget_hide(g.g_profile_delete_banner_button);
        }

        if (async_data.*.tweets != null and g.g_profile_tweets_list != null) {
            const list = listBox(g.g_profile_tweets_list);
            api.populate_tweet_list(list, async_data.*.tweets);
            setTweetListLastId(list, async_data.*.tweets, true);
            api.free_tweets(async_data.*.tweets);
            async_data.*.tweets = null;
        }

        if (profile.*.username != null and profile.*.username[0] != 0) {
            start_loading_followers(profile.*.username);
            start_loading_following(profile.*.username);
            start_loading_profile_media(profile.*.username);
            start_loading_profile_highlights(profile.*.username);
            start_loading_profile_mutuals(profile.*.username);
            start_loading_profile_followers_you_know(profile.*.username);
            start_loading_profile_affiliates(profile.*.username);
        }
    } else {
        labelSet(g.g_profile_name_label, "Error loading profile");
    }

    freeAsyncData(async_data);
    return FALSE;
}

fn fetchProfileThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.PROFILE_URL, async_data.*.username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.profile = api.parse_profile(chunk.memory);
        async_data.*.tweets = api.parse_tweets(chunk.memory);
        async_data.*.success = if (async_data.*.profile != null) TRUE else FALSE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onProfileLoaded), async_data);
    return null;
}

fn onProfileRepliesLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.tweets != null and g.g_profile_replies_list != null) {
        const list = listBox(g.g_profile_replies_list);
        api.populate_tweet_list(list, async_data.*.tweets);
        setTweetListLastId(list, async_data.*.tweets, false);
        api.free_tweets(async_data.*.tweets);
        async_data.*.tweets = null;
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchProfileRepliesThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.PROFILE_REPLIES_URL, async_data.*.username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.tweets = api.parse_profile_replies(chunk.memory);
        async_data.*.success = if (async_data.*.tweets != null) TRUE else FALSE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onProfileRepliesLoaded), async_data);
    return null;
}

fn fetchGet(url: [*c]const c.gchar) types.MemoryStruct {
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") == FALSE) {
        c.g_free(chunk.memory);
        chunk.memory = null;
        chunk.size = 0;
    }
    return chunk;
}

fn loadUsersInto(list_box: [*c]c.GtkListBox, url: [*c]const c.gchar) void {
    if (list_box == null or url == null) return;
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory != null) {
        const users = api.parse_users(chunk.memory);
        api.populate_user_list(list_box, users);
        api.free_users(users);
    }
}

fn loadCommunitiesInto(list_box: [*c]c.GtkListBox, url: [*c]const c.gchar) void {
    if (list_box == null or url == null) return;
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory != null) {
        const communities = api.parse_communities(chunk.memory);
        api.populate_community_list(list_box, communities);
        api.free_communities(communities);
    } else {
        setListBoxStatus(list_box, "Failed to load communities");
    }
}

fn setListBoxStatus(list_box: [*c]c.GtkListBox, text: [*c]const c.gchar) void {
    clearListBox(list_box);
    _ = appendListLabel(list_box, text);
}

fn setObjectStringData(object: [*c]c.GtkWidget, key: [*c]const c.gchar, value: [*c]const c.gchar) void {
    c.g_object_set_data_full(@ptrCast(@alignCast(object)), key, if (value != null) c.g_strdup(value) else null, c.g_free);
}

fn objectStringData(object: [*c]c.GtkWidget, key: [*c]const c.gchar) [*c]const c.gchar {
    if (object == null) return null;
    return @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(object)), key));
}

fn clearListBox(list_box: [*c]c.GtkListBox) void {
    if (list_box == null) return;
    const children = c.gtk_container_get_children(container(list_box));
    var iter = children;
    while (iter != null) : (iter = iter.*.next) {
        c.gtk_widget_destroy(asWidget(iter.*.data));
    }
    c.g_list_free(children);
}

fn appendListLabel(list_box: [*c]c.GtkListBox, text: [*c]const c.gchar) [*c]c.GtkWidget {
    const row = c.gtk_label_new(if (text != null) text else "");
    c.gtk_label_set_xalign(@ptrCast(@alignCast(row)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(row)), TRUE);
    c.gtk_list_box_insert(list_box, row, -1);
    c.gtk_widget_show_all(asWidget(list_box));
    return row;
}

fn createListRow(list: [*c]types.TweetaList, followed_section: bool) [*c]c.GtkWidget {
    const row = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 4);
    const top = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    const title = c.g_strdup_printf("%s%s", if (list.*.name != null) list.*.name else lit("Untitled list"), if (list.*.is_private != FALSE) lit(" (Private)") else lit(""));
    defer c.g_free(title);
    const open = c.gtk_button_new_with_label(title);
    c.gtk_button_set_relief(@ptrCast(@alignCast(open)), c.GTK_RELIEF_NONE);
    c.gtk_widget_set_halign(open, c.GTK_ALIGN_START);
    setObjectStringData(open, "list_id", list.*.id);
    connect(open, "clicked", on_list_follow_clicked, @ptrFromInt(2));
    c.gtk_box_pack_start(asBox(top), open, FALSE, FALSE, 0);

    if (followed_section) {
        const unfollow = c.gtk_button_new_with_label("Unfollow");
        setObjectStringData(unfollow, "list_id", list.*.id);
        connect(unfollow, "clicked", on_list_follow_clicked, @ptrFromInt(0));
        c.gtk_box_pack_end(asBox(top), unfollow, FALSE, FALSE, 0);
    }

    const meta_text = c.g_strdup_printf(
        "%d members · %d followers%s%s%s%s%s",
        list.*.member_count,
        list.*.follower_count,
        if (list.*.owner_username != null) lit(" · @") else lit(""),
        if (list.*.owner_username != null) list.*.owner_username else lit(""),
        if (list.*.description != null and list.*.description[0] != 0) lit("\n") else lit(""),
        if (list.*.description != null) list.*.description else lit(""),
        lit(""),
    );
    defer c.g_free(meta_text);
    const meta = c.gtk_label_new(meta_text);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(meta)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(meta)), TRUE);

    c.gtk_box_pack_start(asBox(row), top, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(row), meta, FALSE, FALSE, 0);
    c.gtk_container_set_border_width(container(row), 8);
    return row;
}

fn populateListsBox(list_box_widget: [*c]c.GtkWidget, lists: [*c]c.GList, followed_section: bool) void {
    if (list_box_widget == null) return;
    const box_ = listBox(list_box_widget);
    clearListBox(box_);
    if (lists == null) {
        _ = appendListLabel(box_, if (followed_section) lit("No followed lists.") else lit("No owned lists."));
        return;
    }
    var item = lists;
    while (item != null) : (item = item.*.next) {
        const list: [*c]types.TweetaList = @ptrCast(@alignCast(item.*.data));
        if (list == null) continue;
        const row = createListRow(list, followed_section);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(box_, row, -1);
    }
}

fn jsonString(obj: ?*c.JsonObject, key: [*c]const c.gchar) [*c]const c.gchar {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return lit("");
    const node = c.json_object_get_member(obj, key);
    if (node == null or c.json_node_is_null(node) != FALSE) return lit("");
    return c.json_object_get_string_member(obj, key);
}

fn jsonDouble(obj: ?*c.JsonObject, key: [*c]const c.gchar) f64 {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return 0.0;
    const node = c.json_object_get_member(obj, key);
    if (node == null or c.json_node_is_null(node) != FALSE) return 0.0;
    return c.json_object_get_double_member(obj, key);
}

fn jsonBool(obj: ?*c.JsonObject, key: [*c]const c.gchar) bool {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return false;
    const node = c.json_object_get_member(obj, key);
    if (node == null or c.json_node_is_null(node) != FALSE or !c.JSON_NODE_HOLDS_VALUE(node)) return false;
    if (c.json_node_get_value_type(node) == c.G_TYPE_BOOLEAN) return c.json_node_get_boolean(node) != FALSE;
    return c.json_node_get_int(node) != 0;
}

fn jsonInt(obj: ?*c.JsonObject, key: [*c]const c.gchar) c.gint64 {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return 0;
    const node = c.json_object_get_member(obj, key);
    if (node == null or c.json_node_is_null(node) != FALSE) return 0;
    return c.json_object_get_int_member(obj, key);
}

fn parseRootObject(json_data: [*c]const c.gchar, parser_out: *?*c.JsonParser) ?*c.JsonObject {
    parser_out.* = null;
    if (json_data == null) return null;
    const parser = c.json_parser_new();
    var err: [*c]c.GError = null;
    if (c.json_parser_load_from_data(parser, json_data, -1, &err) == FALSE) {
        if (err != null) c.g_error_free(err);
        c.g_object_unref(parser);
        return null;
    }
    parser_out.* = parser;
    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) return null;
    return c.json_node_get_object(root);
}

fn parseAdminRootObject(json_data: [*c]const c.gchar, parser_out: *?*c.JsonParser, list: [*c]c.GtkListBox, failed_status: [*c]const c.gchar) ?*c.JsonObject {
    parser_out.* = null;
    const parser = c.json_parser_new();
    var err: [*c]c.GError = null;
    if (json_data == null or c.json_parser_load_from_data(parser, json_data, -1, &err) == FALSE) {
        if (err != null) c.g_error_free(err);
        c.g_object_unref(parser);
        setListBoxStatus(list, failed_status);
        return null;
    }
    parser_out.* = parser;
    clearListBox(list);
    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) return null;
    return c.json_node_get_object(root);
}

fn jsonObjectMember(obj: ?*c.JsonObject, key: [*c]const c.gchar) ?*c.JsonObject {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return null;
    const node = c.json_object_get_member(obj, key);
    if (node == null or !c.JSON_NODE_HOLDS_OBJECT(node)) return null;
    return c.json_object_get_object_member(obj, key);
}

fn jsonArrayMember(obj: ?*c.JsonObject, key: [*c]const c.gchar) ?*c.JsonArray {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return null;
    const node = c.json_object_get_member(obj, key);
    if (node == null or !c.JSON_NODE_HOLDS_ARRAY(node)) return null;
    return c.json_object_get_array_member(obj, key);
}

fn createAdminRow(title: [*c]const c.gchar, body: [*c]const c.gchar, actions: [*c]c.GtkWidget) [*c]c.GtkWidget {
    const frame = c.gtk_frame_new(null);
    const boxw = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 6);
    c.gtk_container_set_border_width(container(boxw), 10);
    const title_label = c.gtk_label_new(null);
    const escaped = c.g_markup_escape_text(if (title != null) title else "", -1);
    defer c.g_free(escaped);
    const markup = c.g_strdup_printf("<b>%s</b>", if (escaped != null) escaped else lit(""));
    defer c.g_free(markup);
    c.gtk_label_set_markup(@ptrCast(@alignCast(title_label)), markup);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(title_label)), 0.0);
    const body_label = c.gtk_label_new(if (body != null) body else "");
    c.gtk_label_set_xalign(@ptrCast(@alignCast(body_label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(body_label)), TRUE);
    c.gtk_box_pack_start(asBox(boxw), title_label, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), body_label, FALSE, FALSE, 0);
    if (actions != null) c.gtk_box_pack_start(asBox(boxw), actions, FALSE, FALSE, 0);
    c.gtk_container_add(container(frame), boxw);
    return frame;
}

fn jsonArrayLength(array: ?*c.JsonArray) c.guint {
    return if (array != null) c.json_array_get_length(array) else 0;
}

fn jsonObjectToDisplayLines(obj: ?*c.JsonObject) [*c]c.gchar {
    const out = c.g_string_new(null);
    if (obj == null) return c.g_string_free(out, FALSE);

    const members = c.json_object_get_members(obj);
    defer c.g_list_free(members);
    var iter = members;
    while (iter != null) : (iter = iter.*.next) {
        const name: [*c]const c.gchar = @ptrCast(iter.*.data);
        const node = c.json_object_get_member(obj, name);
        const label = c.g_strdup(if (name != null) name else lit(""));
        defer c.g_free(label);
        var p = label;
        while (p[0] != 0) : (p += 1) {
            if (p[0] == '_') p[0] = ' ';
        }
        if (label[0] != 0) label[0] = @intCast(c.g_ascii_toupper(label[0]));

        if (node != null and c.JSON_NODE_HOLDS_VALUE(node)) {
            const value_type = c.json_node_get_value_type(node);
            if (value_type == c.G_TYPE_BOOLEAN) {
                _ = c.g_string_append_printf(out, "%s: %s\n", label, if (c.json_node_get_boolean(node) != FALSE) lit("yes") else lit("no"));
            } else if (value_type == c.G_TYPE_INT64 or value_type == c.G_TYPE_INT or value_type == c.G_TYPE_UINT64 or value_type == c.G_TYPE_UINT) {
                _ = c.g_string_append_printf(out, "%s: %ld\n", label, c.json_node_get_int(node));
            } else if (value_type == c.G_TYPE_DOUBLE or value_type == c.G_TYPE_FLOAT) {
                _ = c.g_string_append_printf(out, "%s: %.3g\n", label, c.json_node_get_double(node));
            } else {
                const value = c.json_node_get_string(node);
                _ = c.g_string_append_printf(out, "%s: %s\n", label, if (value != null) value else lit(""));
            }
        } else if (node != null and c.JSON_NODE_HOLDS_OBJECT(node)) {
            const nested = jsonObjectToDisplayLines(c.json_node_get_object(node));
            defer c.g_free(nested);
            _ = c.g_string_append_printf(out, "%s:\n%s", label, nested);
        } else if (node != null and c.JSON_NODE_HOLDS_ARRAY(node)) {
            const array = c.json_node_get_array(node);
            const len = c.json_array_get_length(array);
            _ = c.g_string_append_printf(out, "%s: %u item%s\n", label, len, if (len == 1) lit("") else lit("s"));
        }
    }
    if (out.*.len > 0 and out.*.str[out.*.len - 1] == '\n') {
        _ = c.g_string_truncate(out, out.*.len - 1);
    }
    return c.g_string_free(out, FALSE);
}

fn jsonNodeToDisplayText(node: ?*c.JsonNode) [*c]c.gchar {
    if (node == null) return c.g_strdup("");
    if (c.JSON_NODE_HOLDS_OBJECT(node)) {
        return jsonObjectToDisplayLines(c.json_node_get_object(node));
    }
    if (c.JSON_NODE_HOLDS_ARRAY(node)) {
        const out = c.g_string_new(null);
        const array = c.json_node_get_array(node);
        var i: c.guint = 0;
        while (i < c.json_array_get_length(array)) : (i += 1) {
            const text = jsonNodeToDisplayText(c.json_array_get_element(array, i));
            defer c.g_free(text);
            if (text != null and text[0] != 0) {
                _ = c.g_string_append_printf(out, "%u. %s\n", i + 1, text);
            }
        }
        if (out.*.len > 0 and out.*.str[out.*.len - 1] == '\n') {
            _ = c.g_string_truncate(out, out.*.len - 1);
        }
        return c.g_string_free(out, FALSE);
    }
    if (c.JSON_NODE_HOLDS_VALUE(node)) {
        const value_type = c.json_node_get_value_type(node);
        if (value_type == c.G_TYPE_BOOLEAN) return c.g_strdup(if (c.json_node_get_boolean(node) != FALSE) "yes" else "no");
        if (value_type == c.G_TYPE_INT64 or value_type == c.G_TYPE_INT or value_type == c.G_TYPE_UINT64 or value_type == c.G_TYPE_UINT) return c.g_strdup_printf("%ld", c.json_node_get_int(node));
        if (value_type == c.G_TYPE_DOUBLE or value_type == c.G_TYPE_FLOAT) return c.g_strdup_printf("%.3g", c.json_node_get_double(node));
        const value = c.json_node_get_string(node);
        return c.g_strdup(if (value != null) value else "");
    }
    return c.g_strdup("");
}

fn requestJsonPayload(url: [*c]const c.gchar, payload: [*c]const c.gchar, method: [*c]const c.gchar, refresh: fn () callconv(.c) void) void {
    if (url == null or g.g_auth_token == null) return;
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, payload, method, &response) != FALSE) {
        refresh();
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Request failed.", if (response != null) response else null);
    }
}

fn adminUrlForId(base_url: [*c]const c.gchar, id: [*c]const c.gchar) [*c]c.gchar {
    const escaped = c.g_uri_escape_string(id, null, FALSE);
    defer c.g_free(escaped);
    return c.g_strdup_printf("%s/%s", base_url, escaped);
}

fn refreshAdminEmojisOnly() callconv(.c) void {
    start_loading_admin_emojis();
}

fn refreshAdminBadgesOnly() callconv(.c) void {
    start_loading_admin_badges();
}

fn refreshAdminDmsOnly() callconv(.c) void {
    start_loading_admin_dms(entryText(g.g_admin_dms_search));
    clearListBox(@ptrCast(@alignCast(g.g_admin_dm_admin_messages_list)));
}

fn refreshAdminShopOnly() callconv(.c) void {
    start_loading_admin_shop(entryText(g.g_admin_shop_search));
}

fn onAdminDeleteEmojiClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const url = adminUrlForId(constants.ADMIN_EMOJIS_URL, objectStringData(widget, "item_id"));
    defer c.g_free(url);
    performAdminDeleteItem(url, "Failed to delete emoji.", refreshAdminEmojisOnly);
}

fn onAdminDeleteBadgeClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const url = adminUrlForId(constants.ADMIN_BADGES_URL, objectStringData(widget, "item_id"));
    defer c.g_free(url);
    performAdminDeleteItem(url, "Failed to delete badge.", refreshAdminBadgesOnly);
}

fn onAdminDeleteConversationClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const url = adminUrlForId(constants.ADMIN_DMS_URL, objectStringData(widget, "item_id"));
    defer c.g_free(url);
    performAdminDeleteItem(url, "Failed to delete conversation.", refreshAdminDmsOnly);
}

fn onAdminDeleteDmMessageClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const id = objectStringData(widget, "item_id");
    const escaped = c.g_uri_escape_string(id, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf("%s/messages/%s", constants.ADMIN_DMS_URL, escaped);
    defer c.g_free(url);
    const conversation_id = objectStringData(@ptrCast(@alignCast(g.g_admin_dm_admin_messages_list)), "conversation_id");
    performAdminDeleteItem(url, "Failed to delete DM message.", null);
    if (conversation_id != null) start_loading_admin_dm_messages(conversation_id);
}

fn onAdminDeleteShopProductClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const url = adminUrlForId(constants.ADMIN_SHOP_PRODUCTS_URL, objectStringData(widget, "item_id"));
    defer c.g_free(url);
    performAdminDeleteItem(url, "Failed to delete shop product.", refreshAdminShopOnly);
}

fn performAdminReportResolution(report_id: [*c]const c.gchar, action: [*c]const c.gchar, ban_action: [*c]const c.gchar) void {
    if (report_id == null or action == null) return;
    const escaped = c.g_uri_escape_string(report_id, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf("%s/%s/resolve", constants.ADMIN_REPORTS_URL, escaped);
    defer c.g_free(url);
    const payload = if (ban_action != null)
        c.g_strdup_printf("{\"action\":\"%s\",\"banAction\":\"%s\"}", action, ban_action)
    else
        c.g_strdup_printf("{\"action\":\"%s\"}", action);
    defer c.g_free(payload);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (adminRequestErrorFromResponse(url, payload, "POST", &error_message) != FALSE) {
        start_loading_admin_reports();
        start_loading_admin_suspensions();
        start_loading_admin_posts(entryText(g.g_admin_posts_search));
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Failed to resolve report.", if (error_message != null) error_message else "The request could not be sent.");
    }
}

export fn on_admin_report_ignore_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{widget});
    performAdminReportResolution(@ptrCast(user_data), "ignore", null);
}

export fn on_admin_report_delete_post_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{widget});
    performAdminReportResolution(@ptrCast(user_data), "delete_post", null);
}

export fn on_admin_report_ban_user_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{widget});
    const parts = c.g_strsplit(@ptrCast(user_data), "|", 2);
    defer c.g_strfreev(parts);
    if (parts != null and parts[0] != null and parts[1] != null) {
        performAdminReportResolution(parts[0], "ban_user", parts[1]);
    }
}

fn onAdminReportActionClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    performAdminReportResolution(objectStringData(widget, "report_id"), objectStringData(widget, "action"), objectStringData(widget, "ban_action"));
}

fn onAdminLiftClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const user_id = objectStringData(widget, "user_id");
    const lift_action = objectStringData(widget, "lift_action");
    if (user_id == null or lift_action == null) return;
    const escaped = c.g_uri_escape_string(user_id, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf("%s/%s/suspend", constants.ADMIN_USERS_URL, escaped);
    defer c.g_free(url);
    const payload = c.g_strdup_printf("{\"action\":\"lift\",\"lift\":[\"%s\"]}", lift_action);
    defer c.g_free(payload);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (adminRequestErrorFromResponse(url, payload, "POST", &error_message) != FALSE) {
        start_loading_admin_suspensions();
        start_loading_admin_users(entryText(g.g_admin_users_search));
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Failed to lift moderation action.", if (error_message != null) error_message else "The request could not be sent.");
    }
}

fn liftButton(user_id: [*c]const c.gchar, lift_action: [*c]const c.gchar) [*c]c.GtkWidget {
    const button = c.gtk_button_new_with_label("Lift");
    setObjectStringData(button, "user_id", user_id);
    setObjectStringData(button, "lift_action", lift_action);
    _ = c.g_signal_connect_data(button, "clicked", @ptrCast(&onAdminLiftClicked), null, null, c.G_CONNECT_DEFAULT);
    return button;
}

fn adminButton(label_text: [*c]const c.gchar, callback: c.GCallback, item_id: [*c]const c.gchar) [*c]c.GtkWidget {
    const button = c.gtk_button_new_with_label(label_text);
    setObjectStringData(button, "item_id", item_id);
    _ = c.g_signal_connect_data(button, "clicked", callback, null, null, c.G_CONNECT_DEFAULT);
    return button;
}

fn reportActionButton(label_text: [*c]const c.gchar, report_id: [*c]const c.gchar, action: [*c]const c.gchar, ban_action: [*c]const c.gchar) [*c]c.GtkWidget {
    const button = c.gtk_button_new_with_label(label_text);
    setObjectStringData(button, "report_id", report_id);
    setObjectStringData(button, "action", action);
    setObjectStringData(button, "ban_action", ban_action);
    _ = c.g_signal_connect_data(button, "clicked", @ptrCast(&onAdminReportActionClicked), null, null, c.G_CONNECT_DEFAULT);
    return button;
}

fn populateAdminReportsFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_admin_reports_list == null) return;
    const list = listBox(g.g_admin_reports_list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseAdminRootObject(json_data, &parser, list, "Failed to load reports.") orelse return;
    const reports = jsonArrayMember(root, "reports");
    if (jsonArrayLength(reports) == 0) {
        _ = appendListLabel(list, "No reports.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(reports)) : (i += 1) {
        const item = c.json_array_get_object_element(reports, i);
        const reporter = jsonObjectMember(item, "reporter");
        const reported = jsonObjectMember(item, "reported");
        const report_id = jsonString(item, "id");
        const reported_type = jsonString(item, "reported_type");
        const title = c.g_strdup_printf("%s report on %s", jsonString(item, "reason"), if (reported_type[0] != 0) reported_type else lit("item"));
        defer c.g_free(title);
        const target = if (c.g_strcmp0(reported_type, "user") == 0) jsonString(reported, "username") else jsonString(reported, "content");
        const body = c.g_strdup_printf(
            "Reporter: @%s\nTarget: %s\nStatus: %s\nCreated: %s",
            if (jsonString(reporter, "username")[0] != 0) jsonString(reporter, "username") else lit("unknown"),
            if (target[0] != 0) target else if (c.g_strcmp0(reported_type, "user") == 0) lit("unknown user") else lit("unknown post"),
            jsonString(item, "status"),
            jsonString(item, "created_at"),
        );
        defer c.g_free(body);
        const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
        c.gtk_box_pack_start(asBox(actions), reportActionButton("Ignore", report_id, "ignore", null), FALSE, FALSE, 0);
        if (c.g_strcmp0(reported_type, "post") == 0) {
            c.gtk_box_pack_start(asBox(actions), reportActionButton("Delete Post", report_id, "delete_post", null), FALSE, FALSE, 0);
        } else if (c.g_strcmp0(reported_type, "user") == 0) {
            c.gtk_box_pack_start(asBox(actions), reportActionButton("Suspend", report_id, "ban_user", "suspend"), FALSE, FALSE, 0);
            c.gtk_box_pack_start(asBox(actions), reportActionButton("Restrict", report_id, "ban_user", "restrict"), FALSE, FALSE, 0);
            c.gtk_box_pack_start(asBox(actions), reportActionButton("Shadowban", report_id, "ban_user", "shadowban"), FALSE, FALSE, 0);
        }
        const row = createAdminRow(title, body, actions);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn populateAdminSuspensionsFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_admin_suspensions_list == null) return;
    const list = listBox(g.g_admin_suspensions_list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseAdminRootObject(json_data, &parser, list, "Failed to load suspensions.") orelse return;
    const suspensions = jsonArrayMember(root, "suspensions");
    if (jsonArrayLength(suspensions) == 0) {
        _ = appendListLabel(list, "No active suspensions.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(suspensions)) : (i += 1) {
        const item = c.json_array_get_object_element(suspensions, i);
        const action = jsonString(item, "action");
        const title = c.g_strdup_printf("@%s", jsonString(item, "username"));
        defer c.g_free(title);
        const body = c.g_strdup_printf(
            "Action: %s\nReason: %s\nBy: @%s\nCreated: %s\nExpires: %s",
            if (action[0] != 0) action else lit("unknown"),
            jsonString(item, "reason"),
            jsonString(item, "suspended_by_username"),
            jsonString(item, "created_at"),
            if (jsonString(item, "expires_at")[0] != 0) jsonString(item, "expires_at") else lit("Permanent"),
        );
        defer c.g_free(body);
        const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
        c.gtk_box_pack_start(asBox(actions), liftButton(jsonString(item, "user_id"), if (action[0] != 0) action else lit("suspend")), FALSE, FALSE, 0);
        const row = createAdminRow(title, body, actions);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn populateAdminLogsFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_admin_logs_list == null) return;
    const list = listBox(g.g_admin_logs_list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseAdminRootObject(json_data, &parser, list, "Failed to load moderation logs.") orelse return;
    const logs = jsonArrayMember(root, "logs");
    if (jsonArrayLength(logs) == 0) {
        _ = appendListLabel(list, "No moderation logs.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(logs)) : (i += 1) {
        const item = c.json_array_get_object_element(logs, i);
        const details = jsonNodeToDisplayText(if (item != null and c.json_object_has_member(item, "details") != FALSE) c.json_object_get_member(item, "details") else null);
        defer c.g_free(details);
        const title = c.g_strdup_printf("%s by @%s", jsonString(item, "action"), jsonString(item, "moderator_username"));
        defer c.g_free(title);
        const body = c.g_strdup_printf(
            "Target: %s %s\nCreated: %s\nDetails: %s",
            jsonString(item, "target_type"),
            jsonString(item, "target_id"),
            jsonString(item, "created_at"),
            if (details != null and details[0] != 0) details else lit("{}"),
        );
        defer c.g_free(body);
        const row = createAdminRow(title, body, null);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn populateAdminBlocksFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_admin_blocks_list == null) return;
    const list = listBox(g.g_admin_blocks_list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseAdminRootObject(json_data, &parser, list, "Failed to load blocks.") orelse return;
    const blocks = jsonArrayMember(root, "blocks");
    if (jsonArrayLength(blocks) == 0) {
        _ = appendListLabel(list, "No blocking relationships.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(blocks)) : (i += 1) {
        const item = c.json_array_get_object_element(blocks, i);
        const title = c.g_strdup_printf("@%s blocked @%s", jsonString(item, "blocker_username"), jsonString(item, "blocked_username"));
        defer c.g_free(title);
        const body = c.g_strdup_printf("Created: %s", jsonString(item, "created_at"));
        defer c.g_free(body);
        const row = createAdminRow(title, body, null);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn populateAdminEmojisFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_admin_emojis_list == null) return;
    const list = listBox(g.g_admin_emojis_list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseAdminRootObject(json_data, &parser, list, "Failed to load emojis.") orelse return;
    const emojis = jsonArrayMember(root, "emojis");
    if (jsonArrayLength(emojis) == 0) {
        _ = appendListLabel(list, "No custom emojis.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(emojis)) : (i += 1) {
        const item = c.json_array_get_object_element(emojis, i);
        const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
        c.gtk_box_pack_start(asBox(actions), adminButton("Delete", @ptrCast(&onAdminDeleteEmojiClicked), jsonString(item, "id")), FALSE, FALSE, 0);
        const title = c.g_strdup_printf(":%s:", jsonString(item, "name"));
        defer c.g_free(title);
        const body = c.g_strdup_printf("URL: %s\nCreated: %s", jsonString(item, "file_url"), jsonString(item, "created_at"));
        defer c.g_free(body);
        const row = createAdminRow(title, body, actions);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn populateAdminBadgesFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_admin_badges_list == null) return;
    const list = listBox(g.g_admin_badges_list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseAdminRootObject(json_data, &parser, list, "Failed to load badges.") orelse return;
    const badges = jsonArrayMember(root, "badges");
    if (jsonArrayLength(badges) == 0) {
        _ = appendListLabel(list, "No badges.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(badges)) : (i += 1) {
        const item = c.json_array_get_object_element(badges, i);
        const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
        c.gtk_box_pack_start(asBox(actions), adminButton("Delete", @ptrCast(&onAdminDeleteBadgeClicked), jsonString(item, "id")), FALSE, FALSE, 0);
        const body = c.g_strdup_printf("Action: %s\nColor: %s\nDescription: %s", jsonString(item, "action_type"), jsonString(item, "color"), jsonString(item, "description"));
        defer c.g_free(body);
        const row = createAdminRow(jsonString(item, "name"), body, actions);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn populateAdminDmsFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_admin_dms_list == null) return;
    const list = listBox(g.g_admin_dms_list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseAdminRootObject(json_data, &parser, list, "Failed to load conversations.") orelse return;
    const conversations = jsonArrayMember(root, "conversations");
    if (jsonArrayLength(conversations) == 0) {
        _ = appendListLabel(list, "No conversations.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(conversations)) : (i += 1) {
        const item = c.json_array_get_object_element(conversations, i);
        const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
        c.gtk_box_pack_start(asBox(actions), adminButton("Delete Conversation", @ptrCast(&onAdminDeleteConversationClicked), jsonString(item, "id")), FALSE, FALSE, 0);
        const title = c.g_strdup_printf("Conversation %s", jsonString(item, "id"));
        defer c.g_free(title);
        const body = c.g_strdup_printf("Participants: %s\nMessages: %d\nLast message: %s", jsonString(item, "participants"), @as(c_int, @intCast(jsonInt(item, "message_count"))), jsonString(item, "last_message_at"));
        defer c.g_free(body);
        const row = createAdminRow(title, body, actions);
        setObjectStringData(row, "conversation_id", jsonString(item, "id"));
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn populateAdminDmMessagesFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_admin_dm_admin_messages_list == null) return;
    const list = listBox(g.g_admin_dm_admin_messages_list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseAdminRootObject(json_data, &parser, list, "Failed to load DM messages.") orelse return;
    const messages = jsonArrayMember(root, "messages");
    if (jsonArrayLength(messages) == 0) {
        _ = appendListLabel(list, "No messages in this conversation.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(messages)) : (i += 1) {
        const item = c.json_array_get_object_element(messages, i);
        const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
        c.gtk_box_pack_start(asBox(actions), adminButton("Delete Message", @ptrCast(&onAdminDeleteDmMessageClicked), jsonString(item, "id")), FALSE, FALSE, 0);
        const title = c.g_strdup_printf("@%s", jsonString(item, "username"));
        defer c.g_free(title);
        const body = c.g_strdup_printf("%s\nCreated: %s", jsonString(item, "content"), jsonString(item, "created_at"));
        defer c.g_free(body);
        const row = createAdminRow(title, body, actions);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn populateAdminShopProductsFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_admin_shop_products_list == null) return;
    const list = listBox(g.g_admin_shop_products_list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseAdminRootObject(json_data, &parser, list, "Failed to load shop products.") orelse return;
    const products = jsonArrayMember(root, "products");
    if (jsonArrayLength(products) == 0) {
        _ = appendListLabel(list, "No shop products.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(products)) : (i += 1) {
        const item = c.json_array_get_object_element(products, i);
        const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
        c.gtk_box_pack_start(asBox(actions), adminButton("Delete Product", @ptrCast(&onAdminDeleteShopProductClicked), jsonString(item, "id")), FALSE, FALSE, 0);
        const title = c.g_strdup_printf("%s by @%s", jsonString(item, "title"), jsonString(item, "owner_username"));
        defer c.g_free(title);
        const body = c.g_strdup_printf("Price: %s INR\nType: %s\nCreated: %s\nDescription: %s", jsonString(item, "price_inr"), jsonString(item, "content_type"), jsonString(item, "created_at"), jsonString(item, "description"));
        defer c.g_free(body);
        const row = createAdminRow(title, body, actions);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn populateAdminShopPurchasesFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_admin_shop_purchases_list == null) return;
    const list = listBox(g.g_admin_shop_purchases_list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseAdminRootObject(json_data, &parser, list, "Failed to load shop purchases.") orelse return;
    const purchases = jsonArrayMember(root, "purchases");
    if (jsonArrayLength(purchases) == 0) {
        _ = appendListLabel(list, "No purchases.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(purchases)) : (i += 1) {
        const item = c.json_array_get_object_element(purchases, i);
        const body = c.g_strdup_printf("Buyer: @%s\nSeller: @%s\nPrice: %s INR\nCreated: %s", jsonString(item, "buyer_username"), jsonString(item, "seller_username"), jsonString(item, "price"), jsonString(item, "created_at"));
        defer c.g_free(body);
        const row = createAdminRow(jsonString(item, "product_title"), body, null);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn onRemoveMutedWordClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const word_id = objectStringData(widget, "word_id");
    if (word_id == null or word_id[0] == 0) return;
    const url = c.g_strdup_printf(constants.MUTED_WORD_URL, word_id);
    defer c.g_free(url);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (requestErrorFromResponse(url, null, "DELETE", &error_message) != FALSE) {
        start_loading_muted_words();
    } else {
        if (error_message == null) error_message = c.g_strdup("The mute request could not be sent.");
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not remove muted word.", error_message);
    }
}

fn populateMutedWordsFromJson(list_widget: [*c]c.GtkWidget, json_data: [*c]const c.gchar) void {
    if (list_widget == null) return;
    const list_box = listBox(list_widget);
    clearListBox(list_box);
    var parser: [*c]c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    if (root == null) {
        _ = appendListLabel(list_box, "Failed to load muted words.");
        return;
    }
    const words = jsonArrayMember(root, "words");
    if (words == null or c.json_array_get_length(words) == 0) {
        _ = appendListLabel(list_box, "No muted words.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(words)) : (i += 1) {
        const word_obj = c.json_array_get_object_element(words, i);
        const id = jsonString(word_obj, "id");
        const word = jsonString(word_obj, "word");
        const created_at = jsonString(word_obj, "created_at");
        const row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 8);
        const label_text = c.g_strdup_printf("%s%s%s", word, if (created_at[0] != 0) lit(" · ") else lit(""), if (created_at[0] != 0) created_at else lit(""));
        defer c.g_free(label_text);
        const label = c.gtk_label_new(label_text);
        c.gtk_label_set_xalign(@ptrCast(@alignCast(label)), 0.0);
        c.gtk_box_pack_start(asBox(row), label, TRUE, TRUE, 0);
        const remove = c.gtk_button_new_with_label("Remove");
        setObjectStringData(remove, "word_id", id);
        connect(remove, "clicked", onRemoveMutedWordClicked, null);
        c.gtk_box_pack_end(asBox(row), remove, FALSE, FALSE, 0);
        c.gtk_container_set_border_width(container(row), 6);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list_box, row, -1);
    }
}

fn loadMutedWordsInto(list_widget: [*c]c.GtkWidget) void {
    const chunk = fetchGet(constants.MUTED_WORDS_URL);
    defer c.g_free(chunk.memory);
    populateMutedWordsFromJson(list_widget, chunk.memory);
}

fn onRemoveForYouInterestClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const topic = objectStringData(widget, "topic");
    if (topic == null or topic[0] == 0 or g.g_auth_token == null) return;
    const escaped = c.g_uri_escape_string(topic, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf(constants.FOR_YOU_INTEREST_URL, escaped);
    defer c.g_free(url);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (requestErrorFromResponse(url, null, "DELETE", &error_message) != FALSE) start_loading_for_you_interests() else showModalMessage(c.GTK_MESSAGE_ERROR, "Could not remove interest.", error_message);
}

fn populateForYouInterestsFromJson(list_widget: [*c]c.GtkWidget, json_data: [*c]const c.gchar) void {
    if (list_widget == null) return;
    const list_box = listBox(list_widget);
    clearListBox(list_box);
    var parser: [*c]c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const topics = if (root != null and c.json_object_has_member(root, "topics") != FALSE) c.json_object_get_array_member(root, "topics") else null;
    if (topics == null) {
        _ = appendListLabel(list_box, "Failed to load interests.");
        return;
    }
    if (c.json_array_get_length(topics) == 0) {
        _ = appendListLabel(list_box, "No learned interests yet.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(topics)) : (i += 1) {
        const item = c.json_array_get_object_element(topics, i);
        const topic = jsonString(item, "topic");
        const row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 8);
        const text = c.g_strdup_printf("%s · %.2f", topic, jsonDouble(item, "weight"));
        defer c.g_free(text);
        const label = c.gtk_label_new(text);
        c.gtk_label_set_xalign(@ptrCast(@alignCast(label)), 0.0);
        c.gtk_box_pack_start(asBox(row), label, TRUE, TRUE, 0);
        const remove = c.gtk_button_new_with_label("Remove");
        setObjectStringData(remove, "topic", topic);
        connect(remove, "clicked", onRemoveForYouInterestClicked, null);
        c.gtk_box_pack_end(asBox(row), remove, FALSE, FALSE, 0);
        c.gtk_container_set_border_width(container(row), 6);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list_box, row, -1);
    }
}

fn loadForYouInterestsInto(list_widget: [*c]c.GtkWidget) void {
    const chunk = fetchGet(constants.FOR_YOU_INTERESTS_URL);
    defer c.g_free(chunk.memory);
    populateForYouInterestsFromJson(list_widget, chunk.memory);
}

fn onDeleteScheduledPostClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const post_id = objectStringData(widget, "scheduled_id");
    if (post_id == null or post_id[0] == 0 or g.g_auth_token == null) return;
    const url = c.g_strdup_printf(constants.SCHEDULED_POST_URL, post_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, null, "DELETE", &response) != FALSE) {
        start_loading_scheduled_posts();
    } else {
        const error_message = extractErrorMessage(response);
        defer c.g_free(error_message);
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not cancel scheduled post.", error_message);
    }
}

fn populateScheduledPostsFromJson(list_widget: [*c]c.GtkWidget, json_data: [*c]const c.gchar) void {
    if (list_widget == null) return;
    const list_box = listBox(list_widget);
    clearListBox(list_box);
    var parser: [*c]c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const posts = if (root != null and c.json_object_has_member(root, "scheduledPosts") != FALSE) c.json_object_get_array_member(root, "scheduledPosts") else null;
    if (posts == null) {
        _ = appendListLabel(list_box, "Failed to load scheduled posts.");
        return;
    }
    if (c.json_array_get_length(posts) == 0) {
        _ = appendListLabel(list_box, "No scheduled posts.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(posts)) : (i += 1) {
        const post = c.json_array_get_object_element(posts, i);
        const boxw = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
        c.gtk_container_set_border_width(container(boxw), 8);
        const content = c.gtk_label_new(jsonString(post, "content"));
        c.gtk_label_set_xalign(@ptrCast(@alignCast(content)), 0.0);
        c.gtk_label_set_line_wrap(@ptrCast(@alignCast(content)), TRUE);
        const status = jsonString(post, "status");
        const scheduled_for = jsonString(post, "scheduled_for");
        const meta_text = c.g_strdup_printf("%s%s%s", scheduled_for, if (status[0] != 0) lit(" · ") else lit(""), if (status[0] != 0) status else lit(""));
        defer c.g_free(meta_text);
        const meta = c.gtk_label_new(meta_text);
        c.gtk_label_set_xalign(@ptrCast(@alignCast(meta)), 0.0);
        c.gtk_widget_set_opacity(meta, 0.75);
        const cancel = c.gtk_button_new_with_label("Cancel");
        setObjectStringData(cancel, "scheduled_id", jsonString(post, "id"));
        connect(cancel, "clicked", onDeleteScheduledPostClicked, null);
        c.gtk_box_pack_start(asBox(boxw), content, FALSE, FALSE, 0);
        c.gtk_box_pack_start(asBox(boxw), meta, FALSE, FALSE, 0);
        c.gtk_box_pack_start(asBox(boxw), cancel, FALSE, FALSE, 0);
        c.gtk_widget_show_all(boxw);
        c.gtk_list_box_insert(list_box, boxw, -1);
    }
}

fn loadScheduledPostsInto(list_widget: [*c]c.GtkWidget) void {
    const chunk = fetchGet(constants.SCHEDULED_POSTS_URL);
    defer c.g_free(chunk.memory);
    populateScheduledPostsFromJson(list_widget, chunk.memory);
}

fn onAccountRequestActionClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    const request_id = objectStringData(widget, "request_id");
    const action: [*c]const c.gchar = @ptrCast(user_data);
    if (request_id == null or request_id[0] == 0 or action == null) return;
    var url: [*c]c.gchar = null;
    if (c.g_strcmp0(action, "follow-approve") == 0) {
        url = c.g_strdup_printf(constants.PROFILE_FOLLOW_REQUEST_APPROVE_URL, request_id);
    } else if (c.g_strcmp0(action, "follow-deny") == 0) {
        url = c.g_strdup_printf(constants.PROFILE_FOLLOW_REQUEST_DENY_URL, request_id);
    } else if (c.g_strcmp0(action, "affiliate-approve") == 0) {
        url = c.g_strdup_printf(constants.PROFILE_AFFILIATE_REQUEST_APPROVE_URL, request_id);
    } else if (c.g_strcmp0(action, "affiliate-deny") == 0) {
        url = c.g_strdup_printf(constants.PROFILE_AFFILIATE_REQUEST_DENY_URL, request_id);
    }
    if (url == null) return;
    defer c.g_free(url);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (requestErrorFromResponse(url, "{}", "POST", &error_message) != FALSE) start_loading_account_requests() else showModalMessage(c.GTK_MESSAGE_ERROR, "Request update failed.", error_message);
}

fn accountRequestRow(item: ?*c.JsonObject, kind: [*c]const c.gchar) [*c]c.GtkWidget {
    const boxw = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    c.gtk_container_set_border_width(container(boxw), 8);
    const username = jsonString(item, "username");
    const name = jsonString(item, "name");
    const bio = jsonString(item, "bio");
    const created_at = jsonString(item, "created_at");
    const title_text = c.g_strdup_printf("%s%s%s", if (name[0] != 0) name else lit("@"), if (name[0] != 0) lit(" @") else lit(""), username);
    defer c.g_free(title_text);
    const body_text = c.g_strdup_printf("%s%s%s", bio, if (created_at[0] != 0) lit("\nRequested ") else lit(""), if (created_at[0] != 0) created_at else lit(""));
    defer c.g_free(body_text);
    const title_label = c.gtk_label_new(title_text);
    const body = c.gtk_label_new(body_text);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(title_label)), 0.0);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(body)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(body)), TRUE);
    c.gtk_widget_set_opacity(body, 0.75);
    const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    const approve = c.gtk_button_new_with_label("Approve");
    const deny = c.gtk_button_new_with_label("Deny");
    const approve_action = c.g_strdup_printf("%s-approve", kind);
    const deny_action = c.g_strdup_printf("%s-deny", kind);
    setObjectStringData(approve, "request_id", jsonString(item, "id"));
    setObjectStringData(deny, "request_id", jsonString(item, "id"));
    _ = c.g_signal_connect_data(approve, "clicked", cb(onAccountRequestActionClicked), approve_action, freeClosureData, c.G_CONNECT_DEFAULT);
    _ = c.g_signal_connect_data(deny, "clicked", cb(onAccountRequestActionClicked), deny_action, freeClosureData, c.G_CONNECT_DEFAULT);
    c.gtk_box_pack_start(asBox(actions), approve, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(actions), deny, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), title_label, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), body, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), actions, FALSE, FALSE, 0);
    return boxw;
}

fn populateAccountRequestList(list_widget: [*c]c.GtkWidget, json_data: [*c]const c.gchar, empty_text: [*c]const c.gchar, kind: [*c]const c.gchar) void {
    if (list_widget == null) return;
    const list_box = listBox(list_widget);
    clearListBox(list_box);
    var parser: [*c]c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const requests = if (root != null and c.json_object_has_member(root, "requests") != FALSE) c.json_object_get_array_member(root, "requests") else null;
    if (requests == null or c.json_array_get_length(requests) == 0) {
        _ = appendListLabel(list_box, empty_text);
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(requests)) : (i += 1) {
        const row = accountRequestRow(c.json_array_get_object_element(requests, i), kind);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list_box, row, -1);
    }
}

fn showShopPurchase(purchase_id: [*c]const c.gchar) void {
    if (purchase_id == null or purchase_id[0] == 0 or g.g_auth_token == null) return;
    const url = c.g_strdup_printf(constants.SHOP_PURCHASE_URL, purchase_id);
    defer c.g_free(url);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Purchase unavailable.", null);
        return;
    }
    const error_message = extractErrorMessage(chunk.memory);
    if (error_message != null) {
        defer c.g_free(error_message);
        showModalMessage(c.GTK_MESSAGE_ERROR, "Purchase unavailable.", error_message);
        return;
    }
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(chunk.memory, &parser);
    const purchase = if (root != null and c.json_object_has_member(root, "purchase") != FALSE) c.json_object_get_object_member(root, "purchase") else null;
    if (purchase == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Purchase unavailable.", "The server response could not be read.");
        return;
    }
    const body = c.g_strdup_printf("Type: %s\n\n%s", jsonString(purchase, "content_type"), jsonString(purchase, "content"));
    defer c.g_free(body);
    showModalMessage(c.GTK_MESSAGE_INFO, jsonString(purchase, "title"), body);
}

fn onShopOpenPurchaseClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    showShopPurchase(objectStringData(widget, "purchase_id"));
}

fn onShopBuyClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const product_id = objectStringData(widget, "product_id");
    if (product_id == null or product_id[0] == 0 or g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to buy shop products.");
        return;
    }
    const url = c.g_strdup_printf(constants.SHOP_PRODUCT_BUY_URL, product_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, "{}", "POST", &response) == FALSE) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Purchase could not start.", if (response != null) response else null);
        return;
    }
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(response, &parser);
    const payment_url = jsonString(root, "paymentUrl");
    const order_id = jsonString(root, "orderId");
    if (payment_url[0] != 0) _ = c.gtk_show_uri_on_window(widgetWindow(widget), payment_url, c.GDK_CURRENT_TIME, null);
    if (order_id[0] == 0) return;

    const dialog = c.gtk_dialog_new_with_buttons(
        "Confirm Purchase",
        null,
        c.GTK_DIALOG_MODAL,
        "_Cancel",
        @as(c_int, c.GTK_RESPONSE_CANCEL),
        "_Confirm",
        @as(c_int, c.GTK_RESPONSE_ACCEPT),
        @as([*c]const c.gchar, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "MyPayIndia transaction id");
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), c.gtk_label_new("Complete payment in the opened page, then paste the transaction id."), FALSE, FALSE, 8);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const confirm_url = c.g_strdup_printf(constants.SHOP_PRODUCT_CONFIRM_URL, product_id);
        defer c.g_free(confirm_url);
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "orderId");
        _ = c.json_builder_add_string_value(builder, order_id);
        _ = c.json_builder_set_member_name(builder, "transactionId");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(entry));
        _ = c.json_builder_end_object(builder);
        var confirm_response: [*c]c.gchar = null;
        defer c.g_free(confirm_response);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        if (requestWithResponse(confirm_url, payload, "POST", &confirm_response) != FALSE) {
            var confirm_parser: ?*c.JsonParser = null;
            defer if (confirm_parser != null) c.g_object_unref(confirm_parser);
            const confirm_root = parseRootObject(confirm_response, &confirm_parser);
            const purchase = if (confirm_root != null and c.json_object_has_member(confirm_root, "purchase") != FALSE) c.json_object_get_object_member(confirm_root, "purchase") else null;
            if (purchase != null) showShopPurchase(jsonString(purchase, "id"));
        } else {
            const confirm_error = extractErrorMessage(confirm_response);
            defer c.g_free(confirm_error);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Purchase confirmation failed.", confirm_error);
        }
    }
    c.gtk_widget_destroy(dialog);
}

fn onShopDeleteProductClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const product_id = objectStringData(widget, "product_id");
    if (product_id == null or product_id[0] == 0) return;
    const url = c.g_strdup_printf(constants.SHOP_PRODUCT_URL, product_id);
    defer c.g_free(url);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (requestErrorFromResponse(url, null, "DELETE", &error_message) != FALSE) {
        start_loading_my_shop();
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not delete product.", error_message);
    }
}

export fn on_shop_delete_product_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onShopDeleteProductClicked(widget, user_data);
}

fn shopProductRow(product: ?*c.JsonObject, owner_view: bool) [*c]c.GtkWidget {
    const boxw = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    c.gtk_container_set_border_width(container(boxw), 8);
    const title = c.gtk_label_new(null);
    const escaped_title = c.g_markup_escape_text(jsonString(product, "title"), -1);
    defer c.g_free(escaped_title);
    const title_markup = c.g_strdup_printf("<b>%s</b>", escaped_title);
    defer c.g_free(title_markup);
    c.gtk_label_set_markup(@ptrCast(@alignCast(title)), title_markup);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(title)), 0.0);
    const body_text = c.g_strdup_printf("@%s · INR %s\n%s", jsonString(product, "owner_username"), jsonString(product, "price_inr"), jsonString(product, "description"));
    defer c.g_free(body_text);
    const body = c.gtk_label_new(body_text);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(body)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(body)), TRUE);
    const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    const product_id = jsonString(product, "id");
    const purchase_id = jsonString(product, "purchase_id");
    const purchased = product != null and c.json_object_has_member(product, "purchased") != FALSE and c.json_object_get_boolean_member(product, "purchased") != FALSE;
    if (purchased and purchase_id[0] != 0) {
        const open = c.gtk_button_new_with_label("Open Purchase");
        setObjectStringData(open, "purchase_id", purchase_id);
        connect(open, "clicked", onShopOpenPurchaseClicked, null);
        c.gtk_box_pack_start(asBox(actions), open, FALSE, FALSE, 0);
    } else if (!owner_view) {
        const buy = c.gtk_button_new_with_label("Buy");
        setObjectStringData(buy, "product_id", product_id);
        connect(buy, "clicked", onShopBuyClicked, null);
        c.gtk_box_pack_start(asBox(actions), buy, FALSE, FALSE, 0);
    }
    if (owner_view) {
        const edit = c.gtk_button_new_with_label("Edit");
        const delete_btn = c.gtk_button_new_with_label("Delete");
        setObjectStringData(edit, "product_id", product_id);
        setObjectStringData(delete_btn, "product_id", product_id);
        connect(edit, "clicked", on_create_shop_product_clicked, @ptrFromInt(1));
        connect(delete_btn, "clicked", onShopDeleteProductClicked, null);
        c.gtk_box_pack_start(asBox(actions), edit, FALSE, FALSE, 0);
        c.gtk_box_pack_start(asBox(actions), delete_btn, FALSE, FALSE, 0);
    }
    c.gtk_box_pack_start(asBox(boxw), title, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), body, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), actions, FALSE, FALSE, 0);
    return boxw;
}

fn populateShopProducts(list_widget: [*c]c.GtkWidget, json_data: [*c]const c.gchar, owner_view: bool) void {
    if (list_widget == null) return;
    const list_box = listBox(list_widget);
    clearListBox(list_box);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const products = if (root != null and c.json_object_has_member(root, "products") != FALSE) c.json_object_get_array_member(root, "products") else null;
    if (products == null or c.json_array_get_length(products) == 0) {
        _ = appendListLabel(list_box, "No shop products.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(products)) : (i += 1) {
        const row = shopProductRow(c.json_array_get_object_element(products, i), owner_view);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list_box, row, -1);
    }
}

fn onCommunityRevokeInviteClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const community_id = objectStringData(widget, "community_id");
    const invite_id = objectStringData(widget, "invite_id");
    if (community_id == null or invite_id == null) return;
    const url = c.g_strdup_printf(constants.COMMUNITY_INVITE_URL, community_id, invite_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, null, "DELETE", &response) != FALSE) {
        showModalMessage(c.GTK_MESSAGE_INFO, "Invite revoked.", null);
    } else {
        const err = extractErrorMessage(response);
        defer c.g_free(err);
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not revoke invite.", err);
    }
}

fn onComposeFileSelected(chooser: [*c]c.GtkFileChooserButton, user_data: c.gpointer) callconv(.c) void {
    const ctx: [*c]types.UploadContext = @ptrCast(@alignCast(user_data));
    if (ctx == null) return;
    const filename = c.gtk_file_chooser_get_filename(@ptrCast(@alignCast(chooser)));
    logDebug("on_file_selected: filename=%s", .{if (filename != null) filename else lit("(null)")});
    if (filename == null) {
        logDebug("on_file_selected: no file selected", .{});
        return;
    }
    c.g_clear_pointer(@ptrCast(&ctx.*.remote_url), c.g_free);
    c.g_clear_pointer(@ptrCast(&ctx.*.remote_type), c.g_free);
    c.g_free(ctx.*.file_path);
    ctx.*.file_path = filename;
    const basename = c.g_path_get_basename(filename);
    defer c.g_free(basename);
    const label_text = c.g_strdup_printf("Selected: %s", basename);
    defer c.g_free(label_text);
    c.gtk_label_set_text(@ptrCast(@alignCast(ctx.*.file_label)), label_text);
    c.g_free(ctx.*.file_type);
    ctx.*.file_type = api.detect_mime_type(filename);
    logDebug("on_file_selected: detected mime_type=%s for file=%s", .{ if (ctx.*.file_type != null) ctx.*.file_type else lit("(null)"), filename });
}

fn mediaSearchResultUrl(item: ?*c.JsonObject, tenor: bool) [*c]const c.gchar {
    if (item == null) return null;
    if (!tenor and jsonString(item, "url")[0] != 0) return jsonString(item, "url");
    if (tenor) {
        const formats = jsonObjectMember(item, "media_formats") orelse return null;
        const gif = jsonObjectMember(formats, "gif") orelse jsonObjectMember(formats, "tinygif") orelse return null;
        if (jsonString(gif, "url")[0] != 0) return jsonString(gif, "url");
    }
    return null;
}

fn mediaSearchResultLabel(item: ?*c.JsonObject, tenor: bool) [*c]c.gchar {
    if (tenor) {
        const title = jsonString(item, "content_description");
        return c.g_strdup(if (title[0] != 0) title else "GIF result");
    }
    const description = jsonString(item, "description");
    const user = jsonObjectMember(item, "user");
    const name = jsonString(user, "name");
    return c.g_strdup_printf(
        "%s%s%s",
        if (description[0] != 0) description else lit("Unsplash image"),
        if (name[0] != 0) lit("\nPhoto: ") else lit(""),
        if (name[0] != 0) name else lit(""),
    );
}

fn onMediaSearchClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    const ctx: [*c]types.UploadContext = @ptrCast(@alignCast(user_data));
    if (ctx == null) return;
    const tenor = c.g_object_get_data(@ptrCast(@alignCast(widget)), "tenor") != null;
    const dialog = c.gtk_dialog_new_with_buttons(
        if (tenor) "Search GIFs" else "Search Images",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        c.GTK_RESPONSE_CLOSE,
        "_Search",
        c.GTK_RESPONSE_APPLY,
        "_Use Selected",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    c.gtk_window_set_default_size(@ptrCast(@alignCast(dialog)), 560, 520);
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    const search_row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    const entry = c.gtk_search_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), if (tenor) "Search GIFs" else "Search photos");
    c.gtk_box_pack_start(asBox(search_row), entry, TRUE, TRUE, 0);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), search_row, FALSE, FALSE, 6);
    const scroll = c.gtk_scrolled_window_new(null, null);
    const list = c.gtk_list_box_new();
    c.gtk_list_box_set_selection_mode(@ptrCast(@alignCast(list)), c.GTK_SELECTION_SINGLE);
    c.gtk_container_add(container(scroll), list);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), scroll, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);

    var response: c.gint = undefined;
    while (true) {
        response = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
        if (response != c.GTK_RESPONSE_APPLY) break;
        clearListBox(@ptrCast(@alignCast(list)));
        const query = entryTextOrEmpty(entry);
        if (query[0] != 0) {
            const escaped = c.g_uri_escape_string(query, null, TRUE);
            defer c.g_free(escaped);
            const base_url: [*c]const c.gchar = if (tenor) @ptrCast(constants.TENOR_SEARCH_URL.ptr) else @ptrCast(constants.UNSPLASH_SEARCH_URL.ptr);
            const url = c.g_strdup_printf("%s?q=%s&limit=12", base_url, escaped);
            defer c.g_free(url);
            const chunk = fetchGet(url);
            defer c.g_free(chunk.memory);
            var parser: ?*c.JsonParser = null;
            defer if (parser != null) c.g_object_unref(parser);
            const root = parseRootObject(chunk.memory, &parser);
            const results = jsonArrayMember(root, "results");
            if (root == null) {
                _ = appendListLabel(@ptrCast(@alignCast(list)), "Search failed.");
            } else if (results != null and c.json_array_get_length(results) > 0) {
                var i: c.guint = 0;
                while (i < c.json_array_get_length(results)) : (i += 1) {
                    const item = c.json_array_get_object_element(results, i);
                    const media_url = mediaSearchResultUrl(item, tenor);
                    if (media_url == null) continue;
                    const row = c.gtk_list_box_row_new();
                    const label_text = mediaSearchResultLabel(item, tenor);
                    defer c.g_free(label_text);
                    const row_label = c.gtk_label_new(label_text);
                    c.gtk_label_set_xalign(@ptrCast(@alignCast(row_label)), 0.0);
                    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(row_label)), TRUE);
                    c.gtk_container_add(container(row), row_label);
                    setObjectStringData(row, "media_url", media_url);
                    setObjectStringData(row, "media_type", if (tenor) "image/gif" else "image/jpeg");
                    c.gtk_list_box_insert(@ptrCast(@alignCast(list)), row, -1);
                }
            } else {
                _ = appendListLabel(@ptrCast(@alignCast(list)), "No results.");
            }
            c.gtk_widget_show_all(list);
        }
    }

    if (response == c.GTK_RESPONSE_ACCEPT) {
        const row = c.gtk_list_box_get_selected_row(@ptrCast(@alignCast(list)));
        const media_url = if (row != null) objectStringData(@ptrCast(@alignCast(row)), "media_url") else null;
        const media_type = if (row != null) objectStringData(@ptrCast(@alignCast(row)), "media_type") else null;
        if (media_url != null) {
            c.g_clear_pointer(@ptrCast(&ctx.*.file_path), c.g_free);
            c.g_clear_pointer(@ptrCast(&ctx.*.file_type), c.g_free);
            c.g_free(ctx.*.remote_url);
            c.g_free(ctx.*.remote_type);
            ctx.*.remote_url = c.g_strdup(media_url);
            ctx.*.remote_type = c.g_strdup(if (media_type != null) media_type else "image/jpeg");
            c.gtk_label_set_text(@ptrCast(@alignCast(ctx.*.file_label)), if (tenor) "Selected GIF from Tenor" else "Selected image from Unsplash");
        }
    }
    c.gtk_widget_destroy(dialog);
}

fn queryUrl(base: [*c]const c.gchar, key: [*c]const c.gchar, search: [*c]const c.gchar) [*c]c.gchar {
    if (search == null or search[0] == 0) return c.g_strdup(base);
    const escaped = c.g_uri_escape_string(search, null, FALSE);
    defer c.g_free(escaped);
    return c.g_strdup_printf("%s?%s=%s", base, key, escaped);
}

fn loadSettingsLists() void {
    if (g.g_auth_token == null) return;
    start_loading_muted_words();
    start_loading_muted_conversations();
    start_loading_for_you_interests();
    start_loading_scheduled_posts();
    start_loading_my_shop();
    start_loading_delegates();
    start_loading_account_requests();
}

fn exploreUrlForIndex(index: c.gint) [*c]const c.gchar {
    return switch (index) {
        1 => constants.EXPLORE_BEST_OF_WEEK_URL,
        2 => constants.EXPLORE_MOST_BOOKMARKED_URL,
        3 => constants.EXPLORE_MOST_DISCUSSED_URL,
        4 => constants.EXPLORE_LONGEST_THREADS_URL,
        5 => constants.EXPLORE_WITH_MEDIA_URL,
        6 => constants.EXPLORE_WITH_POLLS_URL,
        7 => constants.EXPLORE_TRENDING_USERS_URL,
        8 => constants.EXPLORE_SUGGESTED_USERS_URL,
        9 => constants.EXPLORE_DIRECTORY_URL,
        10 => constants.EXPLORE_TOP_HASHTAGS_URL,
        11 => constants.EXPLORE_DIGEST_URL,
        12 => constants.EXPLORE_LEADERBOARD_URL,
        13 => constants.EXPLORE_STATS_URL,
        else => constants.TRENDS_URL,
    };
}

fn onMutedConversationOpenClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const tweet_id = widgetStringData(widget, "tweet_id");
    if (tweet_id != null and tweet_id[0] != 0) api.show_tweet(tweet_id);
}

export fn on_open_muted_conversation_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onMutedConversationOpenClicked(widget, user_data);
}

fn populateMutedConversations(json_data: [*c]const c.gchar) void {
    if (g.g_muted_conversations_list == null) return;
    const list = listBox(g.g_muted_conversations_list);
    clearListBox(list);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const conversations = jsonArrayMember(root, "conversations");
    if (conversations == null or c.json_array_get_length(conversations) == 0) {
        _ = appendListLabel(list, "No muted conversations.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(conversations)) : (i += 1) {
        const conv = c.json_array_get_object_element(conversations, i);
        const post_id = jsonString(conv, "post_id");
        const created_at = jsonString(conv, "created_at");
        const row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 8);
        const text = c.g_strdup_printf("%s%s%s", post_id, if (created_at[0] != 0) lit(" · ") else lit(""), if (created_at[0] != 0) created_at else lit(""));
        defer c.g_free(text);
        const label_ = c.gtk_label_new(text);
        const open = c.gtk_button_new_with_label("Open");
        const unmute = c.gtk_button_new_with_label("Unmute");
        c.gtk_label_set_xalign(@ptrCast(@alignCast(label_)), 0.0);
        c.gtk_box_pack_start(asBox(row), label_, TRUE, TRUE, 0);
        setObjectStringData(open, "tweet_id", post_id);
        setObjectStringData(unmute, "tweet_id", post_id);
        connect(open, "clicked", onMutedConversationOpenClicked, null);
        connect(unmute, "clicked", on_mute_conversation_clicked, null);
        c.gtk_box_pack_end(asBox(row), unmute, FALSE, FALSE, 0);
        c.gtk_box_pack_end(asBox(row), open, FALSE, FALSE, 0);
        c.gtk_container_set_border_width(container(row), 6);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn onMutedWordsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.json_data != null) {
        populateMutedWordsFromJson(g.g_muted_words_list, async_data.*.json_data);
    } else if (g.g_muted_words_list != null) {
        setListBoxStatus(listBox(g.g_muted_words_list), "Failed to load muted words.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchMutedWordsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    async_data.*.success = api.fetch_url(constants.MUTED_WORDS_URL, &chunk, null, "GET");
    if (async_data.*.success != FALSE) async_data.*.json_data = c.g_strdup(chunk.memory);
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onMutedWordsLoaded), async_data);
    return null;
}

fn onMutedConversationsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.json_data != null) {
        populateMutedConversations(async_data.*.json_data);
    } else if (g.g_muted_conversations_list != null) {
        setListBoxStatus(listBox(g.g_muted_conversations_list), "Failed to load muted conversations.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchMutedConversationsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    async_data.*.success = api.fetch_url(constants.MUTED_CONVERSATIONS_URL, &chunk, null, "GET");
    if (async_data.*.success != FALSE) async_data.*.json_data = c.g_strdup(chunk.memory);
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onMutedConversationsLoaded), async_data);
    return null;
}

fn onForYouInterestsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.json_data != null) {
        populateForYouInterestsFromJson(g.g_for_you_interests_list, async_data.*.json_data);
    } else if (g.g_for_you_interests_list != null) {
        setListBoxStatus(listBox(g.g_for_you_interests_list), "Failed to load interests.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchForYouInterestsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    async_data.*.success = api.fetch_url(constants.FOR_YOU_INTERESTS_URL, &chunk, null, "GET");
    if (async_data.*.success != FALSE) async_data.*.json_data = c.g_strdup(chunk.memory);
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onForYouInterestsLoaded), async_data);
    return null;
}

fn onScheduledPostsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.json_data != null) {
        populateScheduledPostsFromJson(g.g_scheduled_posts_list, async_data.*.json_data);
    } else if (g.g_scheduled_posts_list != null) {
        setListBoxStatus(listBox(g.g_scheduled_posts_list), "Failed to load scheduled posts.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchScheduledPostsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    async_data.*.success = api.fetch_url(constants.SCHEDULED_POSTS_URL, &chunk, null, "GET");
    if (async_data.*.success != FALSE) async_data.*.json_data = c.g_strdup(chunk.memory);
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onScheduledPostsLoaded), async_data);
    return null;
}

fn onDelegateSwitchClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const owner_id = widgetStringData(widget, "owner_id");
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (owner_id == null or owner_id[0] == 0) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Delegate account unavailable.", null);
        return;
    }
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "ownerId");
    _ = c.json_builder_add_string_value(builder, owner_id);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    if (authSwitchRequest(constants.AUTH_SWITCH_DELEGATE_URL, payload, &error_message) != FALSE) {
        showModalMessage(c.GTK_MESSAGE_INFO, "Switched delegate account.", null);
        start_loading_delegates();
        if (g.g_main_list_box != null) start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not switch delegate account.", error_message);
    }
}

fn onDelegateActionClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    const delegate_id = widgetStringData(widget, "delegate_id");
    const action: [*c]const c.gchar = @ptrCast(user_data);
    if (delegate_id == null or action == null) return;
    var ok: c.gboolean = FALSE;
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (c.g_strcmp0(action, "accept") == 0) {
        ok = delegateActionWithError(constants.DELEGATE_ACCEPT_URL, delegate_id, "POST", "{}", &error_message);
    } else if (c.g_strcmp0(action, "decline") == 0) {
        ok = delegateActionWithError(constants.DELEGATE_DECLINE_URL, delegate_id, "POST", "{}", &error_message);
    } else if (c.g_strcmp0(action, "remove") == 0) {
        ok = delegateActionWithError(constants.DELEGATE_REMOVE_URL, delegate_id, "DELETE", null, &error_message);
    }
    if (ok != FALSE) start_loading_delegates() else showModalMessage(c.GTK_MESSAGE_ERROR, "Delegate update failed.", error_message);
}

fn delegateRow(item: ?*c.JsonObject, mode: [*c]const c.gchar) [*c]c.GtkWidget {
    const boxw = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const username = jsonString(item, "username");
    const name = jsonString(item, "name");
    const title = c.g_strdup_printf(
        "%s%s%s",
        if (name[0] != 0) name else lit("@"),
        if (name[0] != 0) lit(" @") else lit(""),
        username,
    );
    defer c.g_free(title);
    const accepted_at = jsonString(item, "accepted_at");
    const body = c.g_strdup_printf(
        "Status: %s%s%s%s%s",
        jsonString(item, "status"),
        if (accepted_at[0] != 0) lit(" · accepted ") else lit(""),
        if (accepted_at[0] != 0) accepted_at else lit(""),
        if (jsonString(item, "created_at")[0] != 0) lit(" · created ") else lit(""),
        if (jsonString(item, "created_at")[0] != 0) jsonString(item, "created_at") else lit(""),
    );
    defer c.g_free(body);
    c.gtk_container_set_border_width(container(boxw), 8);
    const title_label = c.gtk_label_new(title);
    const body_label = c.gtk_label_new(body);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(title_label)), 0.0);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(body_label)), 0.0);
    c.gtk_widget_set_opacity(body_label, 0.75);
    c.gtk_box_pack_start(asBox(boxw), title_label, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), body_label, FALSE, FALSE, 0);
    const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    if (c.g_strcmp0(mode, "incoming") == 0) {
        const accept = c.gtk_button_new_with_label("Accept");
        const decline = c.gtk_button_new_with_label("Decline");
        setObjectStringData(accept, "delegate_id", jsonString(item, "id"));
        setObjectStringData(decline, "delegate_id", jsonString(item, "id"));
        connect(accept, "clicked", onDelegateActionClicked, @ptrCast(@constCast(lit("accept"))));
        connect(decline, "clicked", onDelegateActionClicked, @ptrCast(@constCast(lit("decline"))));
        c.gtk_box_pack_start(asBox(actions), accept, FALSE, FALSE, 0);
        c.gtk_box_pack_start(asBox(actions), decline, FALSE, FALSE, 0);
    } else if (c.g_strcmp0(mode, "delegation") == 0) {
        const switch_btn = c.gtk_button_new_with_label("Switch");
        const remove = c.gtk_button_new_with_label("Remove");
        setObjectStringData(switch_btn, "owner_id", jsonString(item, "owner_id"));
        setObjectStringData(remove, "delegate_id", jsonString(item, "id"));
        connect(switch_btn, "clicked", onDelegateSwitchClicked, null);
        connect(remove, "clicked", onDelegateActionClicked, @ptrCast(@constCast(lit("remove"))));
        c.gtk_box_pack_start(asBox(actions), switch_btn, FALSE, FALSE, 0);
        c.gtk_box_pack_start(asBox(actions), remove, FALSE, FALSE, 0);
    } else {
        const remove = c.gtk_button_new_with_label(if (c.g_strcmp0(mode, "sent") == 0) "Cancel" else "Remove");
        setObjectStringData(remove, "delegate_id", jsonString(item, "id"));
        connect(remove, "clicked", onDelegateActionClicked, @ptrCast(@constCast(lit("remove"))));
        c.gtk_box_pack_start(asBox(actions), remove, FALSE, FALSE, 0);
    }
    c.gtk_box_pack_start(asBox(boxw), actions, FALSE, FALSE, 0);
    return boxw;
}

fn populateDelegateBucket(list_widget: [*c]c.GtkWidget, items: ?*c.JsonArray, empty_text: [*c]const c.gchar, mode: [*c]const c.gchar) void {
    if (list_widget == null) return;
    const box_ = listBox(list_widget);
    clearListBox(box_);
    if (items == null or c.json_array_get_length(items) == 0) {
        _ = appendListLabel(box_, empty_text);
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(items)) : (i += 1) {
        const row = delegateRow(c.json_array_get_object_element(items, i), mode);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(box_, row, -1);
    }
}

fn populateDelegates(json_data: [*c]const c.gchar) void {
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    if (root != null and c.json_object_has_member(root, "error") == FALSE) {
        populateDelegateBucket(g.g_delegates_list, jsonArrayMember(root, "delegates"), "No delegates can post as you.", "remove");
        populateDelegateBucket(g.g_delegations_list, jsonArrayMember(root, "delegations"), "You are not a delegate for anyone.", "delegation");
        populateDelegateBucket(g.g_delegate_invitations_list, jsonArrayMember(root, "invitations"), "No pending invitations.", "incoming");
        populateDelegateBucket(g.g_delegate_sent_list, jsonArrayMember(root, "sentInvitations"), "No sent invitations.", "sent");
    } else {
        if (g.g_delegates_list != null) setListBoxStatus(listBox(g.g_delegates_list), "Failed to load delegates.");
        if (g.g_delegations_list != null) setListBoxStatus(listBox(g.g_delegations_list), "Failed to load delegations.");
        if (g.g_delegate_invitations_list != null) setListBoxStatus(listBox(g.g_delegate_invitations_list), "Failed to load invitations.");
        if (g.g_delegate_sent_list != null) setListBoxStatus(listBox(g.g_delegate_sent_list), "Failed to load sent invitations.");
    }
}

fn createExploreTextRow(title: [*c]const c.gchar, body: [*c]const c.gchar) [*c]c.GtkWidget {
    const boxw = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 4);
    const title_label = c.gtk_label_new(if (title != null) title else "");
    const body_label = c.gtk_label_new(if (body != null) body else "");
    c.gtk_label_set_xalign(@ptrCast(@alignCast(title_label)), 0.0);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(body_label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(body_label)), TRUE);
    c.gtk_widget_set_opacity(body_label, 0.78);
    c.gtk_box_pack_start(asBox(boxw), title_label, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), body_label, FALSE, FALSE, 0);
    c.gtk_container_set_border_width(container(boxw), 8);
    return boxw;
}

fn populateExploreHashtags(hashtags: ?*c.JsonArray) void {
    if (hashtags == null or c.json_array_get_length(hashtags) == 0) {
        setListBoxStatus(listBox(g.g_explore_list), "No hashtags.");
        return;
    }
    var i: c.guint = 0;
    while (i < c.json_array_get_length(hashtags)) : (i += 1) {
        const item = c.json_array_get_object_element(hashtags, i);
        const title = c.g_strdup_printf("#%s", jsonString(item, "name"));
        defer c.g_free(title);
        const body = c.g_strdup_printf("%ld posts · %ld recent", jsonInt(item, "tweet_count"), jsonInt(item, "recent_count"));
        defer c.g_free(body);
        const row = createExploreTextRow(title, body);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(listBox(g.g_explore_list), row, -1);
    }
}

fn populateExploreStats(stats: ?*c.JsonObject) void {
    if (stats == null) {
        setListBoxStatus(listBox(g.g_explore_list), "Stats unavailable.");
        return;
    }
    const body = c.g_strdup_printf(
        "Users: %ld\nPosts: %ld\nLikes: %ld\nPosts today: %ld\nNew users today: %ld",
        jsonInt(stats, "total_users"),
        jsonInt(stats, "total_posts"),
        jsonInt(stats, "total_likes"),
        jsonInt(stats, "posts_today"),
        jsonInt(stats, "new_users_today"),
    );
    defer c.g_free(body);
    const row = createExploreTextRow("Network stats", body);
    c.gtk_widget_show_all(row);
    c.gtk_list_box_insert(listBox(g.g_explore_list), row, -1);
}

fn populateExploreDigest(digest: ?*c.JsonObject) void {
    if (digest == null) {
        setListBoxStatus(listBox(g.g_explore_list), "Digest unavailable.");
        return;
    }
    const author = jsonObjectMember(digest, "top_author");
    const body = c.g_strdup_printf(
        "Posts in 24h: %ld\nNew users in 24h: %ld\nTop author: %s",
        jsonInt(digest, "total_posts_24h"),
        jsonInt(digest, "new_users_24h"),
        if (author != null) jsonString(author, "username") else lit("none"),
    );
    defer c.g_free(body);
    const row = createExploreTextRow("Daily digest", body);
    c.gtk_widget_show_all(row);
    c.gtk_list_box_insert(listBox(g.g_explore_list), row, -1);
}

fn populateExploreLeaderboard(leaderboard: ?*c.JsonObject) void {
    const by_posts = jsonArrayMember(leaderboard, "by_posts");
    const by_followers = jsonArrayMember(leaderboard, "by_followers");
    if ((by_posts == null or c.json_array_get_length(by_posts) == 0) and (by_followers == null or c.json_array_get_length(by_followers) == 0)) {
        setListBoxStatus(listBox(g.g_explore_list), "Leaderboard unavailable.");
        return;
    }
    var i: c.guint = 0;
    while (by_posts != null and i < c.json_array_get_length(by_posts)) : (i += 1) {
        const user = c.json_array_get_object_element(by_posts, i);
        const title = c.g_strdup_printf("Posts: @%s", jsonString(user, "username"));
        defer c.g_free(title);
        const body = c.g_strdup_printf("%ld posts", jsonInt(user, "post_count"));
        defer c.g_free(body);
        const row = createExploreTextRow(title, body);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(listBox(g.g_explore_list), row, -1);
    }
    i = 0;
    while (by_followers != null and i < c.json_array_get_length(by_followers)) : (i += 1) {
        const user = c.json_array_get_object_element(by_followers, i);
        const title = c.g_strdup_printf("Followers: @%s", jsonString(user, "username"));
        defer c.g_free(title);
        const body = c.g_strdup_printf("%ld followers", jsonInt(user, "follower_count"));
        defer c.g_free(body);
        const row = createExploreTextRow(title, body);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(listBox(g.g_explore_list), row, -1);
    }
}

fn booleanSetting(url: [*c]const c.gchar, member: [*c]const c.gchar, state: c.gboolean) void {
    if (g.g_auth_token == null) return;
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, member);
    _ = c.json_builder_add_boolean_value(builder, state);
    _ = c.json_builder_end_object(builder);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestBuilderWithResponse(url, "POST", builder, &response) == FALSE) {
        const error_message = extractErrorMessage(response);
        defer c.g_free(error_message);
        showModalMessage(c.GTK_MESSAGE_ERROR, "Setting update failed.", if (error_message != null) error_message else "Request failed.");
    }
}

fn updateCommunityTag(community_id: [*c]const c.gchar) void {
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to update your community tag.");
        return;
    }
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "community_id");
    if (community_id != null and community_id[0] != 0) {
        _ = c.json_builder_add_string_value(builder, community_id);
    } else {
        _ = c.json_builder_add_null_value(builder);
    }
    _ = c.json_builder_end_object(builder);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestBuilderWithResponse(constants.PROFILE_SETTINGS_COMMUNITY_TAG_URL, "POST", builder, &response) != FALSE) {
        showModalMessage(c.GTK_MESSAGE_INFO, "Community tag updated.", null);
    } else {
        const error_message = extractErrorMessage(response);
        defer c.g_free(error_message);
        showModalMessage(c.GTK_MESSAGE_ERROR, "Community tag update failed.", if (error_message != null) error_message else "Request failed.");
    }
}

fn textViewText(view: [*c]c.GtkWidget) [*c]c.gchar {
    if (view == null) return c.g_strdup("");
    const buffer = c.gtk_text_view_get_buffer(@ptrCast(@alignCast(view)));
    var start: c.GtkTextIter = undefined;
    var end: c.GtkTextIter = undefined;
    c.gtk_text_buffer_get_bounds(buffer, &start, &end);
    return c.gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

fn entryTextOrEmpty(entry: [*c]c.GtkWidget) [*c]const c.gchar {
    if (entry == null) return lit("");
    const text = c.gtk_entry_get_text(@ptrCast(@alignCast(entry)));
    return if (text != null) text else lit("");
}

fn widgetStringData(w: [*c]c.GtkWidget, key: [*c]const c.gchar) [*c]const c.gchar {
    if (w == null) return null;
    return @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(w)), key));
}

fn showTextDialog(parent: [*c]c.GtkWindow, title: [*c]const c.gchar, text: [*c]const c.gchar) void {
    const dialog = c.gtk_dialog_new_with_buttons(
        title,
        parent,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        c.GTK_RESPONSE_CLOSE,
        @as(?*anyopaque, null),
    );
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    const scrolled = c.gtk_scrolled_window_new(null, null);
    c.gtk_widget_set_size_request(scrolled, 560, 360);
    const label = c.gtk_label_new(text);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(label)), 0.0);
    c.gtk_label_set_yalign(@ptrCast(@alignCast(label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(label)), TRUE);
    c.gtk_label_set_selectable(@ptrCast(@alignCast(label)), TRUE);
    c.gtk_container_add(@ptrCast(@alignCast(scrolled)), label);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), scrolled, TRUE, TRUE, 8);
    c.gtk_widget_show_all(dialog);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}

fn showModalMessage(kind: c.GtkMessageType, primary: [*c]const c.gchar, secondary: [*c]const c.gchar) void {
    const message = c.gtk_message_dialog_new(null, c.GTK_DIALOG_MODAL, kind, c.GTK_BUTTONS_CLOSE, "%s", textOr(primary));
    if (secondary != null and secondary[0] != 0) {
        c.gtk_message_dialog_format_secondary_text(@ptrCast(@alignCast(message)), "%s", secondary);
    }
    _ = c.gtk_dialog_run(asDialog(message));
    c.gtk_widget_destroy(message);
}

fn colorButtonHexValue(chooser: ?*c.GtkColorChooser) [*c]c.gchar {
    var rgba: c.GdkRGBA = undefined;
    c.gtk_color_chooser_get_rgba(chooser, &rgba);
    return c.g_strdup_printf(
        "#%02x%02x%02x",
        @as(c.guint, @intFromFloat(@min(@max(rgba.red, 0.0), 1.0) * 255.0 + 0.5)),
        @as(c.guint, @intFromFloat(@min(@max(rgba.green, 0.0), 1.0) * 255.0 + 0.5)),
        @as(c.guint, @intFromFloat(@min(@max(rgba.blue, 0.0), 1.0) * 255.0 + 0.5)),
    );
}

fn onColorButtonSet(button: [*c]c.GtkColorButton, user_data: c.gpointer) callconv(.c) void {
    const entry_widget: [*c]c.GtkEntry = @ptrCast(@alignCast(user_data));
    const hex = colorButtonHexValue(@ptrCast(@alignCast(button)));
    defer c.g_free(hex);
    c.gtk_entry_set_text(entry_widget, hex);
}

fn onColorEntryChanged(entry_widget: [*c]c.GtkEntry, user_data: c.gpointer) callconv(.c) void {
    const chooser: ?*c.GtkColorChooser = @ptrCast(@alignCast(user_data));
    var rgba: c.GdkRGBA = undefined;
    const text = c.gtk_entry_get_text(entry_widget);
    if (text != null and c.gdk_rgba_parse(&rgba, text) != FALSE) {
        c.gtk_color_chooser_set_rgba(chooser, &rgba);
    }
}

fn createColorEntryRow(entry_widget: [*c]c.GtkWidget, initial_value: [*c]const c.gchar) [*c]c.GtkWidget {
    const row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    const button = c.gtk_color_button_new();
    var rgba: c.GdkRGBA = undefined;
    c.gtk_box_pack_start(asBox(row), entry_widget, TRUE, TRUE, 0);
    c.gtk_box_pack_start(asBox(row), button, FALSE, FALSE, 0);
    if (initial_value != null and c.gdk_rgba_parse(&rgba, initial_value) != FALSE) {
        c.gtk_color_chooser_set_rgba(@ptrCast(@alignCast(button)), &rgba);
    }
    connect(button, "color-set", onColorButtonSet, entry_widget);
    connect(entry_widget, "changed", onColorEntryChanged, button);
    return row;
}

fn formatAlgorithmStats(root: ?*c.JsonObject) callconv(.c) [*c]c.gchar {
    const impact = jsonObjectMember(root, "algorithm_impact");
    const rating = if (impact != null and jsonString(impact, "rating")[0] != 0) jsonString(impact, "rating") else lit("Unknown");
    return c.g_strdup_printf(
        "Rating: %s\nOverall multiplier: %.3g\nReputation multiplier: %.3g\nAccount age multiplier: %.3g\n\nSpam score: %.3g\nBlocked by: %ld\nMuted by: %ld\nAccount age: %ld days\nFollowers: %ld\nFollowing: %ld\nPosts: %ld\n\nVerified: %s\nGold: %s\nSuper tweeter: %s",
        rating,
        jsonDouble(impact, "overall_multiplier"),
        jsonDouble(impact, "reputation_multiplier"),
        jsonDouble(impact, "account_age_multiplier"),
        jsonDouble(root, "spam_score"),
        jsonInt(root, "blocked_by_count"),
        jsonInt(root, "muted_by_count"),
        jsonInt(root, "account_age_days"),
        jsonInt(root, "follower_count"),
        jsonInt(root, "following_count"),
        jsonInt(root, "post_count"),
        if (jsonBool(root, "verified")) lit("yes") else lit("no"),
        if (jsonBool(root, "gold")) lit("yes") else lit("no"),
        if (jsonBool(root, "super_tweeter")) lit("yes") else lit("no"),
    );
}

fn formatSpamScore(root: ?*c.JsonObject) callconv(.c) [*c]c.gchar {
    const out = c.g_string_new(null);
    const metrics = jsonObjectMember(root, "accountMetrics");
    const indicators = jsonArrayMember(root, "indicators");
    const message = jsonString(root, "message");
    _ = c.g_string_append_printf(out, "Score: %.1f%%\n%s\n\n", jsonDouble(root, "spamPercentage"), if (message[0] != 0) message else lit("No summary available."));
    if (metrics != null) {
        _ = c.g_string_append_printf(
            out,
            "Account age: %ld days\nFollowers: %ld\nFollowing: %ld\nFollow ratio: %s\nPosts: %ld\nReplies: %ld\nPosts last hour: %ld\nPosts last day: %ld\nReplies last day: %ld",
            jsonInt(metrics, "accountAgeDays"),
            jsonInt(metrics, "followerCount"),
            jsonInt(metrics, "followingCount"),
            jsonString(metrics, "followRatio"),
            jsonInt(metrics, "totalPosts"),
            jsonInt(metrics, "totalReplies"),
            jsonInt(metrics, "postsLastHour"),
            jsonInt(metrics, "postsLastDay"),
            jsonInt(metrics, "repliesLastDay"),
        );
    }
    if (indicators != null and c.json_array_get_length(indicators) > 0) {
        _ = c.g_string_append(out, "\n\nIndicators:");
        var i: c.guint = 0;
        while (i < c.json_array_get_length(indicators) and i < 6) : (i += 1) {
            const indicator = c.json_array_get_object_element(indicators, i);
            const display = jsonString(indicator, "displayName");
            _ = c.g_string_append_printf(out, "\n- %s: %s (%s)", if (display[0] != 0) display else jsonString(indicator, "name"), jsonString(indicator, "contribution"), jsonString(indicator, "status"));
            if (jsonString(indicator, "details")[0] != 0) {
                _ = c.g_string_append_printf(out, "\n  %s", jsonString(indicator, "details"));
            }
        }
    }
    return c.g_string_free(out, FALSE);
}

fn showProfileJsonSummary(title: [*c]const c.gchar, url: [*c]const c.gchar, formatter: *const fn (?*c.JsonObject) callconv(.c) [*c]c.gchar) void {
    if (url == null) return;
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Profile data unavailable.", null);
        return;
    }
    const error_message = extractErrorMessage(chunk.memory);
    if (error_message != null) {
        defer c.g_free(error_message);
        showModalMessage(c.GTK_MESSAGE_ERROR, "Profile data unavailable.", error_message);
        return;
    }
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(chunk.memory, &parser);
    if (root == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Profile data unavailable.", "The server response could not be read.");
        return;
    }
    const text = formatter(root);
    defer c.g_free(text);
    showModalMessage(c.GTK_MESSAGE_INFO, title, text);
}

fn getWebBaseUrl() [*c]c.gchar {
    if (c.g_str_has_suffix(constants.API_BASE_URL, "/api") != FALSE) {
        return c.g_strndup(constants.API_BASE_URL, cstr.len(constants.API_BASE_URL) - 4);
    }
    return c.g_strdup(constants.API_BASE_URL);
}

fn openWebPath(window: [*c]c.GtkWindow, path: [*c]const c.gchar) void {
    const base = getWebBaseUrl();
    defer c.g_free(base);
    const url = c.g_strdup_printf("%s%s", base, if (path != null) path else lit("/"));
    defer c.g_free(url);
    _ = c.gtk_show_uri_on_window(window, url, c.GDK_CURRENT_TIME, null);
}

fn applyLoginLikeResponse(json_data: [*c]const c.gchar) c.gboolean {
    var token: [*c]c.gchar = null;
    var parsed_username: [*c]c.gchar = null;
    var is_admin: c.gboolean = FALSE;
    if (api.parse_login_response(json_data, &token, &parsed_username, &is_admin) == FALSE) return FALSE;
    c.g_mutex_lock(&g.g_globals_mutex);
    c.g_free(g.g_auth_token);
    c.g_free(g.g_current_username);
    g.g_auth_token = token;
    g.g_current_username = parsed_username;
    g.g_is_admin = is_admin;
    c.g_free(g.g_impersonation_admin_token);
    c.g_free(g.g_impersonation_admin_username);
    g.g_impersonation_admin_token = null;
    g.g_impersonation_admin_username = null;
    g.g_impersonation_admin_is_admin = FALSE;
    g.g_is_impersonating = FALSE;
    c.g_mutex_unlock(&g.g_globals_mutex);
    const me_chunk = fetchGet(constants.AUTH_ME_URL);
    defer c.g_free(me_chunk.memory);
    if (me_chunk.memory != null) {
        var me_is_admin: c.gboolean = is_admin;
        if (api.parse_user_me_response(me_chunk.memory, &me_is_admin) != FALSE) {
            c.g_mutex_lock(&g.g_globals_mutex);
            g.g_is_admin = me_is_admin;
            is_admin = me_is_admin;
            c.g_mutex_unlock(&g.g_globals_mutex);
        }
    }
    api.save_session(token, parsed_username, is_admin);
    return TRUE;
}

fn applyAuthSwitchResponse(json_data: [*c]const c.gchar, error_out: ?*[*c]c.gchar) c.gboolean {
    if (error_out) |out| out.* = null;
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    if (root != null and jsonBool(root, "success") and c.json_object_has_member(root, "token") != FALSE and c.json_object_has_member(root, "user") != FALSE) {
        const user = jsonObjectMember(root, "user");
        const token = jsonString(root, "token");
        const username = jsonString(user, "username");
        if (token[0] != 0 and username[0] != 0) {
            c.g_mutex_lock(&g.g_globals_mutex);
            c.g_free(g.g_auth_token);
            c.g_free(g.g_current_username);
            g.g_auth_token = c.g_strdup(token);
            g.g_current_username = c.g_strdup(username);
            c.g_mutex_unlock(&g.g_globals_mutex);
            api.save_session(g.g_auth_token, g.g_current_username, g.g_is_admin);
            update_login_ui();
            return TRUE;
        }
    } else if (root != null and error_out != null and c.json_object_has_member(root, "error") != FALSE) {
        error_out.?.* = c.g_strdup(jsonString(root, "error"));
    }
    if (error_out) |out| {
        if (out.* == null) out.* = c.g_strdup("The account switch response could not be read.");
    }
    return FALSE;
}

fn delegateAction(url_format: [*c]const c.gchar, delegate_id: [*c]const c.gchar, method: [*c]const c.gchar, payload: [*c]const c.gchar) c.gboolean {
    const url = c.g_strdup_printf(url_format, delegate_id);
    defer c.g_free(url);
    return simpleRequest(url, payload, method);
}

fn delegateActionWithError(url_format: [*c]const c.gchar, delegate_id: [*c]const c.gchar, method: [*c]const c.gchar, payload: [*c]const c.gchar, error_out: ?*[*c]c.gchar) c.gboolean {
    if (error_out) |out| out.* = null;
    if (g.g_auth_token == null) {
        if (error_out) |out| out.* = c.g_strdup("You must be logged in to manage delegates.");
        return FALSE;
    }
    const url = c.g_strdup_printf(url_format, delegate_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, payload, method, &response) != FALSE) return TRUE;
    if (error_out) |out| {
        const server_error = extractErrorMessage(response);
        out.* = if (server_error != null) server_error else c.g_strdup("Delegate request failed.");
    }
    return FALSE;
}

fn authSwitchRequest(url: [*c]const c.gchar, payload: [*c]const c.gchar, error_out: ?*[*c]c.gchar) c.gboolean {
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, payload, "POST", &response) == FALSE) return FALSE;
    return applyAuthSwitchResponse(response, error_out);
}

fn profileThemeIndex(theme: [*c]const c.gchar) c.gint {
    if (c.g_strcmp0(theme, "light") == 0) return 1;
    if (c.g_strcmp0(theme, "dark") == 0) return 2;
    return 0;
}

fn profileLabelIndex(label_type: [*c]const c.gchar) c.gint {
    if (c.g_strcmp0(label_type, "parody") == 0) return 1;
    if (c.g_strcmp0(label_type, "fan") == 0) return 2;
    if (c.g_strcmp0(label_type, "commentary") == 0) return 3;
    return 0;
}

fn profileThemeValue(combo: [*c]c.GtkWidget) [*c]const c.gchar {
    return switch (c.gtk_combo_box_get_active(@ptrCast(@alignCast(combo)))) {
        1 => "light",
        2 => "dark",
        else => "auto",
    };
}

fn profileLabelValue(combo: [*c]c.GtkWidget) [*c]const c.gchar {
    return switch (c.gtk_combo_box_get_active(@ptrCast(@alignCast(combo)))) {
        1 => "parody",
        2 => "fan",
        3 => "commentary",
        else => null,
    };
}

fn detectMimeType(path: [*c]const c.gchar) [*c]const c.gchar {
    if (path == null) return "application/octet-stream";
    if (c.g_str_has_suffix(path, ".png") != FALSE) return "image/png";
    if (c.g_str_has_suffix(path, ".jpg") != FALSE or c.g_str_has_suffix(path, ".jpeg") != FALSE) return "image/jpeg";
    if (c.g_str_has_suffix(path, ".gif") != FALSE) return "image/gif";
    if (c.g_str_has_suffix(path, ".webp") != FALSE) return "image/webp";
    if (c.g_str_has_suffix(path, ".mp4") != FALSE) return "video/mp4";
    return "application/octet-stream";
}

fn performAddNote(tweet_id: [*c]const c.gchar, note: [*c]const c.gchar, severity: [*c]const c.gchar) c.gboolean {
    const url = c.g_strdup_printf("%s/admin/fact-check/%s", constants.API_BASE_URL, tweet_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "note");
    _ = c.json_builder_add_string_value(builder, note);
    _ = c.json_builder_set_member_name(builder, "severity");
    _ = c.json_builder_add_string_value(builder, if (severity != null and severity[0] != 0) severity else "warning");
    _ = c.json_builder_end_object(builder);
    return requestBuilder(url, "POST", builder);
}

fn formatPasskeySummary(json_data: [*c]const c.gchar) [*c]c.gchar {
    const summary = c.g_string_new(null);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const passkeys = jsonArrayMember(root, "passkeys");
    if (root != null and passkeys != null and c.json_array_get_length(passkeys) > 0) {
        var i: c.guint = 0;
        while (i < c.json_array_get_length(passkeys)) : (i += 1) {
            const passkey = c.json_array_get_object_element(passkeys, i);
            const name = jsonString(passkey, "name");
            const created = jsonString(passkey, "createdAt");
            const last = jsonString(passkey, "lastUsed");
            _ = c.g_string_append_printf(
                summary,
                "%u. %s\nID: %s\nCreated: %s\nLast used: %s\n\n",
                i + 1,
                if (name[0] != 0) name else lit("Passkey"),
                jsonString(passkey, "id"),
                if (created[0] != 0) created else lit("Unknown"),
                if (last[0] != 0) last else lit("Never"),
            );
        }
    } else if (root != null) {
        _ = c.g_string_append(summary, "No passkeys are registered for this account.");
    } else {
        _ = c.g_string_append(summary, "Could not read passkeys.");
    }
    return c.g_string_free(summary, FALSE);
}

fn formatPushSummary(status_json: [*c]const c.gchar, vapid_json: [*c]const c.gchar) [*c]c.gchar {
    var status_parser: ?*c.JsonParser = null;
    defer if (status_parser != null) c.g_object_unref(status_parser);
    const status_root = parseRootObject(status_json, &status_parser);
    var vapid_parser: ?*c.JsonParser = null;
    defer if (vapid_parser != null) c.g_object_unref(vapid_parser);
    const vapid_root = parseRootObject(vapid_json, &vapid_parser);
    const public_key = jsonString(vapid_root, "publicKey");
    return c.g_strdup_printf(
        "Status: %s\nSubscriptions: %d\nVAPID key: %s",
        if (jsonBool(status_root, "enabled")) lit("Enabled") else lit("Disabled"),
        @as(c_int, @intCast(jsonInt(status_root, "count"))),
        if (public_key[0] != 0) public_key else lit("Unavailable"),
    );
}

fn extractErrorMessage(json_data: [*c]const c.gchar) [*c]c.gchar {
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const message = jsonString(root, "error");
    return if (message[0] != 0) c.g_strdup(message) else null;
}

fn requestErrorFromResponse(url: [*c]const c.gchar, payload: [*c]const c.gchar, method: [*c]const c.gchar, error_out: *[*c]c.gchar) c.gboolean {
    error_out.* = null;
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    const ok = requestWithResponse(url, payload, method, &response);
    if (response != null) error_out.* = extractErrorMessage(response);
    return if (ok != FALSE and error_out.* == null) TRUE else FALSE;
}

fn lookupAdminUserIdentifier(identifier: [*c]const c.gchar, user_id_out: *[*c]c.gchar, username_out: *[*c]c.gchar, error_out: *[*c]c.gchar) c.gboolean {
    user_id_out.* = null;
    username_out.* = null;
    error_out.* = null;
    if (identifier == null or identifier[0] == 0) {
        error_out.* = c.g_strdup("User identifier is required.");
        return FALSE;
    }
    const escaped = c.g_uri_escape_string(identifier, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf("%s/%s", constants.ADMIN_USERS_URL, escaped);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    const ok = adminRequestWithResponse(url, null, "GET", &response);
    defer c.g_free(response);
    if (ok == FALSE) {
        error_out.* = c.g_strdup("The user lookup request failed.");
        return FALSE;
    }
    const server_error = extractErrorMessage(response);
    if (server_error != null) {
        error_out.* = server_error;
        return FALSE;
    }
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(response, &parser);
    const user = jsonObjectMember(root, "user");
    const user_id = jsonString(user, "id");
    if (user_id[0] == 0) {
        error_out.* = c.g_strdup("User not found.");
        return FALSE;
    }
    user_id_out.* = c.g_strdup(user_id);
    const username = jsonString(user, "username");
    username_out.* = if (username[0] != 0) c.g_strdup(username) else null;
    return TRUE;
}

fn jsonBuilderAddTargetValue(builder: [*c]c.JsonBuilder, raw: [*c]const c.gchar) c.gboolean {
    if (raw == null or raw[0] == 0) return FALSE;
    if (c.g_strcmp0(raw, "all") == 0) {
        _ = c.json_builder_add_string_value(builder, "all");
        return TRUE;
    }
    const parts = c.g_strsplit_set(raw, ",\n", -1);
    defer c.g_strfreev(parts);
    var count: usize = 0;
    var i: usize = 0;
    while (parts[i] != null) : (i += 1) {
        const trimmed = c.g_strstrip(parts[i]);
        if (trimmed != null and trimmed[0] != 0) count += 1;
    }
    if (count == 0) return FALSE;
    if (count == 1) {
        i = 0;
        while (parts[i] != null) : (i += 1) {
            const trimmed = c.g_strstrip(parts[i]);
            if (trimmed != null and trimmed[0] != 0) {
                _ = c.json_builder_add_string_value(builder, trimmed);
                return TRUE;
            }
        }
    }
    _ = c.json_builder_begin_array(builder);
    i = 0;
    while (parts[i] != null) : (i += 1) {
        const trimmed = c.g_strstrip(parts[i]);
        if (trimmed != null and trimmed[0] != 0) _ = c.json_builder_add_string_value(builder, trimmed);
    }
    _ = c.json_builder_end_array(builder);
    return TRUE;
}

fn normalizeJsonObjectPayload(json_text: [*c]const c.gchar, normalized_out: *[*c]c.gchar, error_out: *[*c]c.gchar) c.gboolean {
    normalized_out.* = null;
    error_out.* = null;
    if (json_text == null or json_text[0] == 0) {
        error_out.* = c.g_strdup("A JSON object payload is required.");
        return FALSE;
    }
    const parser = c.json_parser_new();
    defer c.g_object_unref(parser);
    var parse_error: ?*c.GError = null;
    if (c.json_parser_load_from_data(parser, json_text, -1, &parse_error) == FALSE) {
        defer if (parse_error != null) c.g_error_free(parse_error);
        error_out.* = c.g_strdup(if (parse_error != null) parse_error.?.*.message else "Invalid JSON payload.");
        return FALSE;
    }
    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) {
        error_out.* = c.g_strdup("Payload must be a JSON object.");
        return FALSE;
    }
    const generator = c.json_generator_new();
    defer c.g_object_unref(generator);
    c.json_generator_set_root(generator, root);
    normalized_out.* = c.json_generator_to_data(generator, null);
    return TRUE;
}

fn formatModerationHistoryResponse(json_data: [*c]const c.gchar) [*c]c.gchar {
    const text = c.g_string_new(null);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const items = jsonArrayMember(root, "suspensions");
    if (items != null) {
        var i: c.guint = 0;
        while (i < c.json_array_get_length(items)) : (i += 1) {
            const item = c.json_array_get_object_element(items, i);
            const expires = jsonString(item, "expires_at");
            _ = c.g_string_append_printf(text, "%s - %s\nSeverity: %s\nStatus: %s\nCreated: %s\nExpires: %s\nNotes: %s\n\n", jsonString(item, "action"), jsonString(item, "reason"), jsonString(item, "severity"), jsonString(item, "status"), jsonString(item, "created_at"), if (expires[0] != 0) expires else lit("Permanent"), jsonString(item, "notes"));
        }
    }
    if (text.*.len == 0) _ = c.g_string_append(text, "No moderation history.");
    return c.g_string_free(text, FALSE);
}

fn formatBlockingCausesResponse(json_data: [*c]const c.gchar) [*c]c.gchar {
    const text = c.g_string_new(null);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const items = jsonArrayMember(root, "causes");
    if (items != null) {
        var i: c.guint = 0;
        while (i < c.json_array_get_length(items)) : (i += 1) {
            const item = c.json_array_get_object_element(items, i);
            const count = jsonInt(item, "count");
            _ = c.g_string_append_printf(text, "%ld block%s from post %s\nCreated: %s\n%s\n\n", count, if (count == 1) lit("") else lit("s"), jsonString(item, "source_tweet_id"), jsonString(item, "created_at"), jsonString(item, "content"));
        }
    }
    if (text.*.len == 0) _ = c.g_string_append(text, "No block causes.");
    return c.g_string_free(text, FALSE);
}

fn formatValidateAccountsResponse(json_data: [*c]const c.gchar) [*c]c.gchar {
    const text = c.g_string_new(null);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const items = jsonArrayMember(root, "validUsers");
    if (items != null) {
        var i: c.guint = 0;
        while (i < c.json_array_get_length(items)) : (i += 1) {
            const user = c.json_array_get_object_element(items, i);
            const badges = c.g_string_new(null);
            if (jsonBool(user, "verified")) _ = c.g_string_append(badges, "Verified");
            if (jsonBool(user, "gold")) {
                if (badges.*.len != 0) _ = c.g_string_append(badges, ", ");
                _ = c.g_string_append(badges, "Gold");
            }
            if (jsonBool(user, "gray")) {
                if (badges.*.len != 0) _ = c.g_string_append(badges, ", ");
                _ = c.g_string_append(badges, "Gray");
            }
            _ = c.g_string_append_printf(text, "%s (@%s)\nID: %s%s%s\n\n", if (jsonString(user, "name")[0] != 0) jsonString(user, "name") else lit("Unknown"), jsonString(user, "username"), jsonString(user, "id"), if (badges.*.len != 0) lit("\n") else lit(""), badges.*.str);
            _ = c.g_string_free(badges, TRUE);
        }
    }
    if (text.*.len == 0) _ = c.g_string_append(text, "No valid accounts.");
    return c.g_string_free(text, FALSE);
}

fn formatCommunityModLogResponse(json_data: [*c]const c.gchar) [*c]c.gchar {
    const text = c.g_string_new(null);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    const log = jsonArrayMember(root, "log");
    if (log != null and c.json_array_get_length(log) > 0) {
        var i: c.guint = 0;
        while (i < c.json_array_get_length(log)) : (i += 1) {
            const item = c.json_array_get_object_element(log, i);
            _ = c.g_string_append_printf(text, "%s by @%s", jsonString(item, "action"), jsonString(item, "actor_username"));
            if (jsonString(item, "target_username")[0] != 0) {
                _ = c.g_string_append_printf(text, " -> @%s", jsonString(item, "target_username"));
            }
            if (jsonString(item, "reason")[0] != 0) {
                _ = c.g_string_append_printf(text, "\nReason: %s", jsonString(item, "reason"));
            }
            _ = c.g_string_append(text, "\n\n");
        }
    } else if (root != null) {
        _ = c.g_string_append(text, "No moderation actions yet.");
    } else {
        _ = c.g_string_append(text, "Could not read moderation log.");
    }
    return c.g_string_free(text, FALSE);
}

fn getEntryDialogText(parent: [*c]c.GtkWidget, title: [*c]const c.gchar, placeholder: [*c]const c.gchar, accept_label: [*c]const c.gchar) [*c]c.gchar {
    const dlg = c.gtk_dialog_new_with_buttons(
        title,
        widgetWindow(parent),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        @as(c_int, c.GTK_RESPONSE_CANCEL),
        accept_label,
        @as(c_int, c.GTK_RESPONSE_ACCEPT),
        @as([*c]const c.gchar, null),
    );
    const input = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(input)), placeholder);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dlg))))), input, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dlg);
    var out: [*c]c.gchar = null;
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dlg))) == c.GTK_RESPONSE_ACCEPT) {
        const text = c.gtk_entry_get_text(@ptrCast(@alignCast(input)));
        if (text != null and text[0] != 0) out = c.g_strdup(text);
    }
    c.gtk_widget_destroy(dlg);
    return out;
}

fn showReportDialog(parent: [*c]c.GtkWidget, reported_type: [*c]const c.gchar, reported_id: [*c]const c.gchar) void {
    if (reported_type == null or reported_id == null) return;
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to report content.");
        return;
    }
    const dialog = c.gtk_dialog_new_with_buttons(
        "Report",
        widgetWindow(parent),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        @as(c_int, c.GTK_RESPONSE_CANCEL),
        "_Report",
        @as(c_int, c.GTK_RESPONSE_ACCEPT),
        @as([*c]const c.gchar, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(container(grid), 10);
    const reason_combo = c.gtk_combo_box_text_new();
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(reason_combo)), "spam", "Spam");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(reason_combo)), "harassment", "Harassment");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(reason_combo)), "hate", "Hate or abuse");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(reason_combo)), "impersonation", "Impersonation");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(reason_combo)), "illegal", "Illegal content");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(reason_combo)), "other", "Other");
    c.gtk_combo_box_set_active(@ptrCast(@alignCast(reason_combo)), 0);
    const info_view = c.gtk_text_view_new();
    c.gtk_text_view_set_wrap_mode(@ptrCast(@alignCast(info_view)), c.GTK_WRAP_WORD_CHAR);
    const info_scroll = c.gtk_scrolled_window_new(null, null);
    c.gtk_widget_set_size_request(info_scroll, 360, 120);
    c.gtk_container_add(container(info_scroll), info_view);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Reason:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), reason_combo, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Details:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), info_scroll, 1, 1, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const reason = c.gtk_combo_box_get_active_id(@ptrCast(@alignCast(reason_combo)));
        const details = textViewText(info_view);
        defer c.g_free(details);
        var error_message: [*c]c.gchar = null;
        defer c.g_free(error_message);
        if (perform_report(reported_type, reported_id, reason, details, &error_message) != FALSE) {
            showModalMessage(c.GTK_MESSAGE_INFO, "Report submitted.", "Thanks. Moderators will review it.");
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Report failed.", if (error_message != null) error_message else null);
        }
    }
    c.gtk_widget_destroy(dialog);
}

fn activeDmConversationId() [*c]const c.gchar {
    if (g.g_dm_messages_list == null) return null;
    return @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_dm_messages_list)), "conversation_id"));
}

fn activeDmConversationDetail() [*c]types.Conversation {
    if (g.g_dm_messages_list == null) return null;
    return @ptrCast(@alignCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_dm_messages_list)), "conversation_detail")));
}

fn buildDmConversationInfo(conversation: [*c]const types.Conversation) [*c]c.gchar {
    const info = c.g_string_new(null);
    if (conversation == null) return c.g_string_free(info, FALSE);
    if (conversation.*.participant_count > 0) {
        _ = c.g_string_append_printf(info, "%d participant%s", conversation.*.participant_count, if (conversation.*.participant_count == 1) lit("") else lit("s"));
    }
    if (conversation.*.participants != null) {
        const names = c.g_string_new(null);
        var item = conversation.*.participants;
        while (item != null) : (item = item.*.next) {
            const participant: [*c]types.Profile = @ptrCast(@alignCast(item.*.data));
            const display = if (participant.*.name != null) participant.*.name else participant.*.username;
            if (display == null or display[0] == 0) continue;
            if (names.*.len > 0) _ = c.g_string_append(names, ", ");
            _ = c.g_string_append(names, display);
        }
        if (names.*.len > 0) {
            if (info.*.len > 0) _ = c.g_string_append(info, " | ");
            _ = c.g_string_append(info, names.*.str);
        }
        _ = c.g_string_free(names, TRUE);
    }
    if (conversation.*.disappearing_enabled != FALSE) {
        if (info.*.len > 0) _ = c.g_string_append(info, " | ");
        if (conversation.*.disappearing_duration > 0) {
            _ = c.g_string_append_printf(info, "Disappearing: %ds", conversation.*.disappearing_duration);
        } else {
            _ = c.g_string_append(info, "Disappearing enabled");
        }
    }
    return c.g_string_free(info, FALSE);
}

fn dmContextString(key: [*c]const c.gchar) [*c]const c.gchar {
    if (g.g_dm_messages_list == null) return null;
    return @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_dm_messages_list)), key));
}

fn updateDmComposeStatusLabel() void {
    if (g.g_dm_messages_list == null) return;
    const status_label: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_dm_messages_list)), "composer_status_label")));
    if (status_label == null) return;
    const reply_to = dmContextString("reply_to_id");
    const reply_preview = dmContextString("reply_preview");
    const file_path = dmContextString("pending_file_path");
    const status = c.g_string_new(null);
    defer _ = c.g_string_free(status, TRUE);
    if (reply_to != null) {
        _ = c.g_string_append(status, "Replying");
        if (reply_preview != null and reply_preview[0] != 0) {
            const trimmed = c.g_strdup(reply_preview);
            defer c.g_free(trimmed);
            if (cstr.len(trimmed) > 48) trimmed[48] = 0;
            _ = c.g_string_append_printf(status, " to: %s", trimmed);
        }
    }
    if (file_path != null) {
        if (status.*.len > 0) _ = c.g_string_append(status, " | ");
        const basename = c.g_path_get_basename(file_path);
        defer c.g_free(basename);
        _ = c.g_string_append_printf(status, "Attachment: %s", textOr(basename));
    }
    c.gtk_label_set_text(@ptrCast(@alignCast(status_label)), status.*.str);
    c.gtk_widget_set_visible(status_label, if (status.*.len > 0) TRUE else FALSE);
}

fn clearDmComposeContext() void {
    if (g.g_dm_messages_list == null) return;
    const object: [*c]c.GObject = @ptrCast(@alignCast(g.g_dm_messages_list));
    c.g_object_set_data_full(object, "reply_to_id", null, c.g_free);
    c.g_object_set_data_full(object, "reply_preview", null, c.g_free);
    c.g_object_set_data_full(object, "pending_file_path", null, c.g_free);
    c.g_object_set_data_full(object, "pending_file_type", null, c.g_free);
    updateDmComposeStatusLabel();
}

fn activeCommunityId() [*c]const c.gchar {
    return @ptrCast(g.g_community_id);
}

fn setProfileActionUsername(username: [*c]const c.gchar) void {
    const buttons = [_][*c]c.GtkWidget{
        @ptrCast(@alignCast(g.g_follow_button)),
        @ptrCast(@alignCast(g.g_profile_notify_button)),
        @ptrCast(@alignCast(g.g_profile_block_button)),
        @ptrCast(@alignCast(g.g_profile_mute_button)),
        @ptrCast(@alignCast(g.g_profile_affiliate_button)),
        @ptrCast(@alignCast(g.g_profile_shop_button)),
        @ptrCast(@alignCast(g.g_profile_algorithm_button)),
        @ptrCast(@alignCast(g.g_profile_spam_score_button)),
        @ptrCast(@alignCast(g.g_profile_analytics_button)),
        @ptrCast(@alignCast(g.g_profile_common_followers_button)),
        @ptrCast(@alignCast(g.g_profile_top_posts_button)),
        @ptrCast(@alignCast(g.g_profile_communities_button)),
        @ptrCast(@alignCast(g.g_profile_delete_avatar_button)),
        @ptrCast(@alignCast(g.g_profile_delete_banner_button)),
    };
    for (buttons) |button| {
        if (button != null) setObjectStringData(button, "username", username);
    }
}

fn setProfileActionUserId(user_id: [*c]const c.gchar) void {
    const buttons = [_][*c]c.GtkWidget{
        @ptrCast(@alignCast(g.g_profile_report_button)),
        @ptrCast(@alignCast(g.g_profile_communities_button)),
    };
    for (buttons) |button| {
        if (button != null) setObjectStringData(button, "user_id", user_id);
    }
}

fn clearBoxChildren(box_widget: [*c]c.GtkWidget) void {
    if (box_widget == null) return;
    const children = c.gtk_container_get_children(container(box_widget));
    var iter = children;
    while (iter != null) : (iter = iter.*.next) {
        c.gtk_widget_destroy(@ptrCast(@alignCast(iter.*.data)));
    }
    c.g_list_free(children);
}

fn appendProfileBadge(parent: [*c]c.GtkWidget, text: [*c]const c.gchar, color: [*c]const c.gchar) void {
    const badge = c.gtk_label_new(null);
    const markup = c.g_strdup_printf("<span foreground='white' background='%s' size='small' weight='bold'> %s </span>", color, text);
    defer c.g_free(markup);
    c.gtk_label_set_markup(@ptrCast(@alignCast(badge)), markup);
    c.gtk_box_pack_start(asBox(parent), badge, FALSE, FALSE, 0);
    c.gtk_widget_show(badge);
}

fn updateProfileBadges(profile: [*c]const types.Profile) void {
    if (g.g_profile_badges_box == null) return;
    clearBoxChildren(@ptrCast(@alignCast(g.g_profile_badges_box)));
    if (profile == null) return;
    if (profile.*.author_gold != FALSE) {
        appendProfileBadge(@ptrCast(@alignCast(g.g_profile_badges_box)), "Gold", "#c88900");
    } else if (profile.*.author_gray != FALSE) {
        appendProfileBadge(@ptrCast(@alignCast(g.g_profile_badges_box)), "Gray", "#6c757d");
    } else if (profile.*.author_verified != FALSE) {
        appendProfileBadge(@ptrCast(@alignCast(g.g_profile_badges_box)), "Verified", "#1d9bf0");
    }
    if (profile.*.label_type != null and profile.*.label_type[0] != 0) {
        const label = c.g_strdup(profile.*.label_type);
        defer c.g_free(label);
        label[0] = @intCast(c.g_ascii_toupper(label[0]));
        appendProfileBadge(@ptrCast(@alignCast(g.g_profile_badges_box)), label, "#495057");
    }
    if (profile.*.label_automated != FALSE) {
        appendProfileBadge(@ptrCast(@alignCast(g.g_profile_badges_box)), "Automated", "#198754");
    }
}

fn buildProfileStatusText(profile: [*c]const types.Profile) [*c]c.gchar {
    const status = c.g_string_new(null);
    if (profile == null) return c.g_string_free(status, FALSE);
    if (profile.*.follows_me != FALSE) _ = c.g_string_append(status, "Follows you");
    if (profile.*.blocked_by_profile != FALSE) {
        if (status.*.len > 0) _ = c.g_string_append(status, " | ");
        _ = c.g_string_append(status, "This account has blocked you");
    }
    if (profile.*.blocked_profile != FALSE) {
        if (status.*.len > 0) _ = c.g_string_append(status, " | ");
        _ = c.g_string_append(status, "You have blocked this account");
    }
    if (profile.*.notify_tweets != FALSE) {
        if (status.*.len > 0) _ = c.g_string_append(status, " | ");
        _ = c.g_string_append(status, "Tweet notifications on");
    }
    return c.g_string_free(status, FALSE);
}

fn buildProfileDetailsText(profile: [*c]const types.Profile) [*c]c.gchar {
    const details = c.g_string_new(null);
    if (profile == null) return c.g_string_free(details, FALSE);
    if (profile.*.pronouns != null and profile.*.pronouns[0] != 0) _ = c.g_string_append(details, profile.*.pronouns);
    if (profile.*.location != null and profile.*.location[0] != 0) {
        if (details.*.len > 0) _ = c.g_string_append(details, " | ");
        _ = c.g_string_append(details, profile.*.location);
    }
    if (profile.*.website != null and profile.*.website[0] != 0) {
        if (details.*.len > 0) _ = c.g_string_append(details, " | ");
        _ = c.g_string_append(details, profile.*.website);
    }
    return c.g_string_free(details, FALSE);
}

fn setButtonBoolData(button: [*c]c.GtkWidget, key: [*c]const c.gchar, value: c.gboolean) void {
    const state: [*c]c.gboolean = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(c.gboolean))));
    state.* = value;
    c.g_object_set_data_full(@ptrCast(@alignCast(button)), key, state, c.g_free);
}

fn buttonBoolData(button: [*c]c.GtkWidget, key: [*c]const c.gchar) ?[*c]c.gboolean {
    return @ptrCast(@alignCast(c.g_object_get_data(@ptrCast(@alignCast(button)), key)));
}

fn showListDialog(title: [*c]const c.gchar, width: c_int, height: c_int) [*c]c.GtkWidget {
    const dialog = c.gtk_dialog_new();
    c.gtk_window_set_title(@ptrCast(@alignCast(dialog)), textOr(title));
    c.gtk_window_set_modal(@ptrCast(@alignCast(dialog)), TRUE);
    c.gtk_window_set_default_size(@ptrCast(@alignCast(dialog)), width, height);
    _ = c.gtk_dialog_add_button(@ptrCast(@alignCast(dialog)), "_Close", c.GTK_RESPONSE_CLOSE);
    const scroll = c.gtk_scrolled_window_new(null, null);
    const list = c.gtk_list_box_new();
    c.gtk_list_box_set_selection_mode(@ptrCast(@alignCast(list)), c.GTK_SELECTION_NONE);
    c.gtk_container_add(container(scroll), list);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), scroll, TRUE, TRUE, 0);
    return dialog;
}

fn dialogList(dialog: [*c]c.GtkWidget) [*c]c.GtkListBox {
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    const children = c.gtk_container_get_children(container(content));
    defer c.g_list_free(children);
    if (children == null) return null;
    const scroll: [*c]c.GtkWidget = @ptrCast(@alignCast(children.*.data));
    const inner = c.gtk_bin_get_child(@ptrCast(@alignCast(scroll)));
    return @ptrCast(@alignCast(inner));
}

fn getCacheDirectory() [*c]c.gchar {
    return c.g_build_filename(c.g_get_user_cache_dir(), "tweeta-desktop", @as([*c]const c.gchar, null));
}

fn calculateDirectorySize(path: [*c]const c.gchar) c.guint64 {
    var total: c.guint64 = 0;
    const dir = c.g_dir_open(path, 0, null);
    if (dir == null) return 0;
    defer c.g_dir_close(dir);
    while (true) {
        const filename = c.g_dir_read_name(dir);
        if (filename == null) break;
        const full_path = c.g_build_filename(path, filename, @as([*c]const c.gchar, null));
        defer c.g_free(full_path);
        if (c.g_file_test(full_path, c.G_FILE_TEST_IS_DIR) != FALSE) {
            total += calculateDirectorySize(full_path);
        } else {
            var stat_buf: c.GStatBuf = undefined;
            if (c.g_stat(full_path, &stat_buf) == 0 and stat_buf.st_size > 0) {
                total += @intCast(stat_buf.st_size);
            }
        }
    }
    return total;
}

fn clearDirectory(path: [*c]const c.gchar) void {
    const dir = c.g_dir_open(path, 0, null);
    if (dir == null) return;
    defer c.g_dir_close(dir);
    while (true) {
        const filename = c.g_dir_read_name(dir);
        if (filename == null) break;
        const full_path = c.g_build_filename(path, filename, @as([*c]const c.gchar, null));
        defer c.g_free(full_path);
        if (c.g_file_test(full_path, c.G_FILE_TEST_IS_DIR) != FALSE) {
            const file = c.g_file_new_for_path(full_path);
            if (file != null) {
                _ = c.g_file_trash(file, null, null);
                c.g_object_unref(file);
            }
        } else {
            _ = c.remove(full_path);
        }
    }
}

export fn update_login_ui() void {
    c.g_mutex_lock(&g.g_globals_mutex);
    const logged_in = g.g_auth_token != null;
    const username = if (g.g_current_username != null) g.g_current_username else lit("");
    const admin = g.g_is_admin;
    const impersonating = g.g_is_impersonating;
    c.g_mutex_unlock(&g.g_globals_mutex);
    buttonSet(g.g_header_auth_button, if (logged_in) "Logout" else "Login");
    buttonSet(g.g_settings_auth_button, if (logged_in) "Logout" else "Login");
    if (logged_in) {
        const label = if (impersonating != FALSE)
            c.g_strdup_printf("Impersonating @%s", username)
        else
            c.g_strdup_printf("Logged in as @%s", username);
        const settings_label = c.g_strdup_printf("Logged in as: @%s", username);
        defer c.g_free(label);
        defer c.g_free(settings_label);
        labelSet(g.g_user_label, label);
        labelSet(g.g_settings_username_label, settings_label);
        if (g.g_compose_button != null) c.gtk_widget_set_sensitive(g.g_compose_button, TRUE);
        if (g.g_change_password_button != null) c.gtk_widget_set_sensitive(g.g_change_password_button, TRUE);
        if (g.g_admin_button != null) {
            if (admin != FALSE or impersonating != FALSE) c.gtk_widget_show(g.g_admin_button) else c.gtk_widget_hide(g.g_admin_button);
        }
        refresh_notification_badge();
    } else {
        labelSet(g.g_user_label, "Not logged in");
        labelSet(g.g_settings_username_label, "Not logged in");
        if (g.g_compose_button != null) c.gtk_widget_set_sensitive(g.g_compose_button, FALSE);
        if (g.g_change_password_button != null) c.gtk_widget_set_sensitive(g.g_change_password_button, FALSE);
        if (g.g_admin_button != null) c.gtk_widget_hide(g.g_admin_button);
        buttonSet(g.g_notifications_button, "Alerts");
        if (g.g_notifications_button != null) c.gtk_widget_set_tooltip_text(g.g_notifications_button, "Notifications");
    }
    update_settings_username_display();
    updateAdminImpersonationStatusLabel();
}

export fn perform_logout() void {
    api.clear_session();
    if (g.g_active_profile != null) {
        api.free_user(g.g_active_profile);
        g.g_active_profile = null;
    }
    c.g_mutex_lock(&g.g_globals_mutex);
    c.g_free(g.g_auth_token);
    c.g_free(g.g_current_username);
    g.g_auth_token = null;
    g.g_current_username = null;
    g.g_is_admin = FALSE;
    c.g_free(g.g_impersonation_admin_token);
    c.g_free(g.g_impersonation_admin_username);
    g.g_impersonation_admin_token = null;
    g.g_impersonation_admin_username = null;
    g.g_impersonation_admin_is_admin = FALSE;
    g.g_is_impersonating = FALSE;
    c.g_mutex_unlock(&g.g_globals_mutex);
    update_login_ui();
}

export fn perform_login(username: [*c]const c.gchar, password: [*c]const c.gchar) c.gboolean {
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "username");
    _ = c.json_builder_add_string_value(builder, username);
    _ = c.json_builder_set_member_name(builder, "password");
    _ = c.json_builder_add_string_value(builder, password);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    defer c.g_free(chunk.memory);
    if (api.fetch_url(constants.LOGIN_URL, &chunk, payload, "POST") == FALSE) return FALSE;
    logDebug("perform_login: fetch_url succeeded, response: %s", .{if (chunk.memory != null) chunk.memory else lit("(null)")});
    return applyLoginLikeResponse(chunk.memory);
}

export fn start_loading_tweets(list_box: [*c]c.GtkListBox) void {
    if (list_box == null) return;
    c.g_mutex_lock(&load_tweets_mutex);
    active_tweets_request_id += 1;
    const current_request_id = active_tweets_request_id;
    c.g_mutex_unlock(&load_tweets_mutex);

    setListBoxStatus(list_box, "Loading tweets...");
    c.g_object_set_data(@ptrCast(@alignCast(list_box)), "last_id", null);

    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.list_box = list_box;
    data.*.request_id = current_request_id;
    data.*.is_append = FALSE;
    if (list_box == @as([*c]c.GtkListBox, @ptrCast(@alignCast(g.g_profile_tweets_list))) or list_box == @as([*c]c.GtkListBox, @ptrCast(@alignCast(g.g_profile_replies_list)))) {
        data.*.username = c.g_strdup(@ptrCast(c.g_object_get_data(@ptrCast(@alignCast(list_box)), "current_profile_user")));
    }
    _ = c.g_thread_new("tweet-loader", fetchTweetsThread, data);
}

export fn load_more_tweets(list_box: [*c]c.GtkListBox, before_id: [*c]const c.gchar) void {
    c.g_mutex_lock(&load_tweets_mutex);
    const current_request_id = active_tweets_request_id;
    c.g_mutex_unlock(&load_tweets_mutex);

    const loading = c.gtk_label_new("Loading more...");
    c.gtk_widget_show(loading);
    c.gtk_list_box_insert(list_box, loading, -1);

    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.list_box = list_box;
    data.*.request_id = current_request_id;
    data.*.is_append = TRUE;
    data.*.before_id = c.g_strdup(before_id);
    if (list_box == @as([*c]c.GtkListBox, @ptrCast(@alignCast(g.g_profile_tweets_list))) or list_box == @as([*c]c.GtkListBox, @ptrCast(@alignCast(g.g_profile_replies_list)))) {
        data.*.username = c.g_strdup(@ptrCast(c.g_object_get_data(@ptrCast(@alignCast(list_box)), "current_profile_user")));
    }
    _ = c.g_thread_new("tweet-loader", fetchTweetsThread, data);
}

export fn start_loading_timeline(list_box: [*c]c.GtkListBox) void {
    if (list_box == null) return;
    c.g_mutex_lock(&load_tweets_mutex);
    active_tweets_request_id += 1;
    const current_request_id = active_tweets_request_id;
    c.g_mutex_unlock(&load_tweets_mutex);

    setListBoxStatus(list_box, "Loading timeline...");
    c.g_object_set_data(@ptrCast(@alignCast(list_box)), "last_id", null);

    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.list_box = list_box;
    data.*.request_id = current_request_id;
    data.*.is_append = FALSE;
    _ = c.g_thread_new("timeline-loader", fetchTweetsThread, data);
}

export fn perform_post_tweet(content: [*c]const c.gchar, reply_to_id: [*c]const c.gchar, attachments: [*c]c.GList) c.gboolean {
    const payload = api.construct_tweet_payload(content, reply_to_id, attachments);
    defer c.g_free(payload);
    return simpleRequest(constants.POST_TWEET_URL, payload, "POST");
}

export fn perform_quote_tweet(content: [*c]const c.gchar, quote_tweet_id: [*c]const c.gchar) c.gboolean {
    if (content == null or content[0] == 0 or quote_tweet_id == null or quote_tweet_id[0] == 0) return FALSE;
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "content");
    _ = c.json_builder_add_string_value(builder, content);
    _ = c.json_builder_set_member_name(builder, "source");
    _ = c.json_builder_add_string_value(builder, "Tweeta Desktop");
    _ = c.json_builder_set_member_name(builder, "quote_tweet_id");
    _ = c.json_builder_add_string_value(builder, quote_tweet_id);
    _ = c.json_builder_end_object(builder);
    return requestBuilder(constants.POST_TWEET_URL, "POST", builder);
}

export fn perform_like(tweet_id: [*c]const c.gchar) c.gboolean {
    const url = c.g_strdup_printf(constants.LIKE_TWEET_URL, tweet_id);
    defer c.g_free(url);
    logDebug("perform_like: tweet_id=%s, url=%s", .{ tweet_id, url });
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    defer c.g_free(chunk.memory);
    if (api.fetch_url(url, &chunk, "{}", "POST") == FALSE) {
        logWarning("perform_like: fetch_url failed", .{});
        return FALSE;
    }
    logDebug("perform_like: fetch_url succeeded, response: %s", .{if (chunk.memory != null) chunk.memory else lit("(null)")});
    if (chunk.memory == null or c.strstr(chunk.memory, "\"error\"") != null) {
        if (chunk.memory != null) logWarning("perform_like: API returned error: %s", .{chunk.memory});
        return FALSE;
    }
    var liked: c.gboolean = FALSE;
    if (responseHasSuccessFlag(chunk.memory, "liked", &liked) != FALSE) update_interaction_cache(tweet_id, liked, -1, -1);
    return TRUE;
}

export fn perform_retweet(tweet_id: [*c]const c.gchar) c.gboolean {
    const url = c.g_strdup_printf(constants.RETWEET_URL, tweet_id);
    defer c.g_free(url);
    logDebug("perform_retweet: tweet_id=%s, url=%s", .{ tweet_id, url });
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    defer c.g_free(chunk.memory);
    if (api.fetch_url(url, &chunk, "{}", "POST") == FALSE) {
        logWarning("perform_retweet: fetch_url failed", .{});
        return FALSE;
    }
    logDebug("perform_retweet: fetch_url succeeded, response: %s", .{if (chunk.memory != null) chunk.memory else lit("(null)")});
    if (chunk.memory == null or c.strstr(chunk.memory, "\"error\"") != null) {
        if (chunk.memory != null) logWarning("perform_retweet: API returned error: %s", .{chunk.memory});
        return FALSE;
    }
    var retweeted: c.gboolean = FALSE;
    if (responseHasSuccessFlag(chunk.memory, "retweeted", &retweeted) != FALSE) update_interaction_cache(tweet_id, -1, retweeted, -1);
    return TRUE;
}

export fn perform_bookmark(tweet_id: [*c]const c.gchar, add: c.gboolean) c.gboolean {
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "postId");
    _ = c.json_builder_add_string_value(builder, tweet_id);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    const url: [*c]const c.gchar = if (add != FALSE) @ptrCast(constants.BOOKMARK_ADD_URL.ptr) else @ptrCast(constants.BOOKMARK_REMOVE_URL.ptr);
    logDebug("perform_bookmark: tweet_id=%s, add=%d, url=%s", .{ tweet_id, add, url });
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    defer c.g_free(chunk.memory);
    if (api.fetch_url(url, &chunk, payload, "POST") == FALSE) {
        logWarning("perform_bookmark: fetch_url failed", .{});
        return FALSE;
    }
    logDebug("perform_bookmark: fetch_url succeeded, response: %s", .{if (chunk.memory != null) chunk.memory else lit("(null)")});
    if (chunk.memory == null or c.strstr(chunk.memory, "\"error\"") != null) {
        if (chunk.memory != null) logWarning("perform_bookmark: API returned error: %s", .{chunk.memory});
        return FALSE;
    }
    var bookmarked: c.gboolean = FALSE;
    if (responseHasSuccessFlag(chunk.memory, "bookmarked", &bookmarked) != FALSE) update_interaction_cache(tweet_id, -1, -1, bookmarked);
    return TRUE;
}

export fn perform_reaction(tweet_id: [*c]const c.gchar, emoji: [*c]const c.gchar) c.gboolean {
    const url = c.g_strdup_printf(constants.REACTION_URL, tweet_id);
    defer c.g_free(url);
    logDebug("perform_reaction: tweet_id=%s, emoji=%s, url=%s", .{ tweet_id, emoji, url });
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "emoji");
    _ = c.json_builder_add_string_value(builder, emoji);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    defer c.g_free(chunk.memory);
    if (api.fetch_url(url, &chunk, payload, "POST") == FALSE) {
        logWarning("perform_reaction: fetch_url failed", .{});
        return FALSE;
    }
    logDebug("perform_reaction: fetch_url succeeded, response: %s", .{if (chunk.memory != null) chunk.memory else lit("(null)")});
    if (chunk.memory == null or c.strstr(chunk.memory, "\"error\"") != null) {
        if (chunk.memory != null) logWarning("perform_reaction: API returned error: %s", .{chunk.memory});
        return FALSE;
    }
    return TRUE;
}

export fn perform_edit_tweet(tweet_id: [*c]const c.gchar, new_content: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or tweet_id == null or new_content == null) return FALSE;
    const url = c.g_strdup_printf(constants.TWEET_EDIT_URL, tweet_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "content");
    _ = c.json_builder_add_string_value(builder, new_content);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    return requestWithResponse(url, payload, "PUT", &response);
}

export fn perform_delete_tweet(tweet_id: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or tweet_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.TWEET_DELETE_URL, tweet_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    return requestWithResponse(url, null, "DELETE", &response);
}

export fn fetch_tweet_edit_history_text(tweet_id: [*c]const c.gchar) [*c]c.gchar {
    if (g.g_auth_token == null or tweet_id == null) return null;
    const url = c.g_strdup_printf(constants.TWEET_EDIT_HISTORY_URL, tweet_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, null, "GET", &response) == FALSE) return null;
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const obj = parseRootObject(response, &parser);
    if (obj == null) return null;
    const text = c.g_string_new(null);
    if (c.json_object_has_member(obj, "history") != FALSE) {
        const history = c.json_object_get_array_member(obj, "history");
        var i: c.guint = 0;
        while (i < c.json_array_get_length(history)) : (i += 1) {
            const item = c.json_array_get_object_element(history, i);
            const content = if (c.json_object_has_member(item, "content") != FALSE) c.json_object_get_string_member(item, "content") else lit("");
            const edited_at = if (c.json_object_has_member(item, "edited_at") != FALSE) c.json_object_get_string_member(item, "edited_at") else lit("");
            const is_current = c.json_object_has_member(item, "is_current") != FALSE and c.json_object_get_boolean_member(item, "is_current") != FALSE;
            c.g_string_append_printf(
                text,
                "%s%s\n%s\n\n",
                if (edited_at != null and edited_at[0] != 0) edited_at else lit("Unknown time"),
                if (is_current) lit(" (current)") else lit(""),
                if (content != null) content else lit(""),
            );
        }
    }
    return c.g_string_free(text, FALSE);
}

export fn fetch_tweet_reactions_text(tweet_id: [*c]const c.gchar) [*c]c.gchar {
    if (tweet_id == null) return null;
    const url = c.g_strdup_printf(constants.TWEET_REACTIONS_URL, tweet_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, null, "GET", &response) == FALSE) return null;
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const obj = parseRootObject(response, &parser);
    if (obj == null) return null;
    const text = c.g_string_new(null);
    if (c.json_object_has_member(obj, "reactions") != FALSE) {
        const reactions = c.json_object_get_array_member(obj, "reactions");
        var i: c.guint = 0;
        while (i < c.json_array_get_length(reactions)) : (i += 1) {
            const item = c.json_array_get_object_element(reactions, i);
            const emoji = if (c.json_object_has_member(item, "emoji") != FALSE) c.json_object_get_string_member(item, "emoji") else lit("?");
            const name = if (c.json_object_has_member(item, "name") != FALSE) c.json_object_get_string_member(item, "name") else null;
            const username = if (c.json_object_has_member(item, "username") != FALSE) c.json_object_get_string_member(item, "username") else null;
            if (text.*.len > 0) _ = c.g_string_append_c(text, '\n');
            c.g_string_append_printf(text, "%s  %s", if (emoji != null) emoji else lit("?"), if (name != null and name[0] != 0) name else lit("Unknown"));
            if (username != null and username[0] != 0) {
                c.g_string_append_printf(text, " (@%s)", username);
            }
        }
    }
    if (text.*.len == 0) _ = c.g_string_append(text, "No reactions yet.");
    return c.g_string_free(text, FALSE);
}

fn freeEmoji(data: c.gpointer) callconv(.c) void {
    const emoji: [*c]types.Emoji = @ptrCast(@alignCast(data));
    if (emoji == null) return;
    c.g_free(emoji.*.id);
    c.g_free(emoji.*.name);
    c.g_free(emoji.*.file_url);
    c.g_free(emoji.*.file_hash);
    c.g_free(emoji.*.created_by);
    c.g_free(emoji);
}

export fn fetch_emojis() [*c]c.GList {
    const chunk = fetchGet(constants.EMOJIS_URL);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) return null;
    const parser = c.json_parser_new();
    defer c.g_object_unref(parser);
    var err: ?*c.GError = null;
    if (c.json_parser_load_from_data(parser, chunk.memory, -1, &err) == FALSE) {
        if (err != null) c.g_error_free(err);
        return null;
    }
    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) return null;
    const obj = c.json_node_get_object(root);
    if (c.json_object_has_member(obj, "emojis") == FALSE) return null;
    const arr = c.json_object_get_array_member(obj, "emojis");
    var emojis: [*c]c.GList = null;
    var i: c.guint = 0;
    while (i < c.json_array_get_length(arr)) : (i += 1) {
        const e_obj = c.json_array_get_object_element(arr, i);
        const emoji: [*c]types.Emoji = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.Emoji))));
        emoji.*.id = if (c.json_object_has_member(e_obj, "id") != FALSE) c.g_strdup(c.json_object_get_string_member(e_obj, "id")) else null;
        emoji.*.name = if (c.json_object_has_member(e_obj, "name") != FALSE) c.g_strdup(c.json_object_get_string_member(e_obj, "name")) else null;
        emoji.*.file_url = if (c.json_object_has_member(e_obj, "file_url") != FALSE) c.g_strdup(c.json_object_get_string_member(e_obj, "file_url")) else null;
        if (c.json_object_has_member(e_obj, "file_hash") != FALSE and c.json_node_is_null(c.json_object_get_member(e_obj, "file_hash")) == FALSE) {
            emoji.*.file_hash = c.g_strdup(c.json_object_get_string_member(e_obj, "file_hash"));
        }
        if (c.json_object_has_member(e_obj, "created_by") != FALSE and c.json_node_is_null(c.json_object_get_member(e_obj, "created_by")) == FALSE) {
            emoji.*.created_by = c.g_strdup(c.json_object_get_string_member(e_obj, "created_by"));
        }
        emojis = c.g_list_append(emojis, emoji);
    }
    return emojis;
}

export fn free_emojis(emojis: [*c]c.GList) void {
    c.g_list_free_full(emojis, freeEmoji);
}

export fn perform_report(reported_type: [*c]const c.gchar, reported_id: [*c]const c.gchar, reason: [*c]const c.gchar, additional_info: [*c]const c.gchar, error_out: [*c][*c]c.gchar) c.gboolean {
    if (error_out != null) error_out.* = null;
    if (g.g_auth_token == null) {
        if (error_out != null) error_out.* = c.g_strdup("You must be logged in to report content.");
        return FALSE;
    }
    if (reported_type == null or reported_id == null or reason == null or reason[0] == 0) {
        if (error_out != null) error_out.* = c.g_strdup("Choose a report reason.");
        return FALSE;
    }

    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "reported_type");
    _ = c.json_builder_add_string_value(builder, reported_type);
    _ = c.json_builder_set_member_name(builder, "reported_id");
    _ = c.json_builder_add_string_value(builder, reported_id);
    _ = c.json_builder_set_member_name(builder, "reason");
    _ = c.json_builder_add_string_value(builder, reason);
    if (additional_info != null and additional_info[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "additional_info");
        _ = c.json_builder_add_string_value(builder, additional_info);
    }
    _ = c.json_builder_end_object(builder);

    const generator = c.json_generator_new();
    defer c.g_object_unref(generator);
    const root = c.json_builder_get_root(builder);
    defer c.json_node_free(root);
    c.json_generator_set_root(generator, root);
    const payload = c.json_generator_to_data(generator, null);
    defer c.g_free(payload);

    var response: [*c]c.gchar = null;
    if (requestWithResponse(constants.REPORT_CREATE_URL, payload, "POST", &response) == FALSE) {
        if (error_out != null) error_out.* = c.g_strdup("The report request could not be sent.");
        return FALSE;
    }
    defer c.g_free(response);
    const error_message = extractErrorMessage(response);
    if (error_message != null) {
        if (error_out != null) {
            error_out.* = error_message;
        } else {
            c.g_free(error_message);
        }
        return FALSE;
    }
    return TRUE;
}

export fn perform_follow(username: [*c]const c.gchar, follow: c.gboolean) c.gboolean {
    if (g.g_auth_token == null or username == null) return FALSE;
    if (api.is_valid_username(username) == FALSE) {
        logWarning("Invalid username format: %s", .{username});
        return FALSE;
    }
    const url = c.g_strdup_printf(constants.PROFILE_FOLLOW_URL, username);
    defer c.g_free(url);
    return simpleRequest(url, "{}", if (follow != FALSE) "POST" else "DELETE");
}

export fn perform_profile_notify_tweets(username: [*c]const c.gchar, notify: c.gboolean) c.gboolean {
    if (g.g_auth_token == null or username == null) return FALSE;
    const url = c.g_strdup_printf(constants.PROFILE_NOTIFY_TWEETS_URL, username);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "notify");
    _ = c.json_builder_add_boolean_value(builder, notify);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    return requestWithResponse(url, payload, "POST", &response);
}

export fn perform_delete_profile_avatar(username: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or username == null) return FALSE;
    const url = c.g_strdup_printf(constants.PROFILE_DELETE_AVATAR_URL, username);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    return requestWithResponse(url, null, "DELETE", &response);
}

export fn perform_delete_profile_banner(username: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or username == null) return FALSE;
    const url = c.g_strdup_printf(constants.PROFILE_DELETE_BANNER_URL, username);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    return requestWithResponse(url, null, "DELETE", &response);
}

export fn perform_toggle_pin_tweet(tweet_id: [*c]const c.gchar, pin: c.gboolean) c.gboolean {
    if (g.g_auth_token == null or tweet_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.PROFILE_PIN_GLOBAL_URL, tweet_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    return requestWithResponse(url, "{}", if (pin != FALSE) "POST" else "DELETE", &response);
}

export fn perform_toggle_highlight_tweet(tweet_id: [*c]const c.gchar, highlighted: c.gboolean) c.gboolean {
    const username = getUsernameSafe();
    defer c.g_free(username);
    if (username == null or tweet_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.PROFILE_HIGHLIGHT_URL, username, tweet_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "highlighted");
    _ = c.json_builder_add_boolean_value(builder, highlighted);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    return requestErrorFromResponse(url, payload, "POST", &error_message);
}

export fn perform_tweet_reply_restriction(tweet_id: [*c]const c.gchar, value: [*c]const c.gchar) c.gboolean {
    if (tweet_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.TWEET_REPLY_RESTRICTION_URL, tweet_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "reply_restriction");
    if (value != null and value[0] != 0) {
        _ = c.json_builder_add_string_value(builder, value);
    } else {
        _ = c.json_builder_add_null_value(builder);
    }
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    return requestErrorFromResponse(url, payload, "PATCH", &error_message);
}

export fn perform_tweet_outline(tweet_id: [*c]const c.gchar, value: [*c]const c.gchar) c.gboolean {
    if (tweet_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.TWEET_OUTLINE_URL, tweet_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "outline");
    if (value != null and value[0] != 0) {
        _ = c.json_builder_add_string_value(builder, value);
    } else {
        _ = c.json_builder_add_null_value(builder);
    }
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    return requestErrorFromResponse(url, payload, "PATCH", &error_message);
}

export fn can_reply_to_tweet(tweet_id: [*c]const c.gchar) c.gboolean {
    if (tweet_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.TWEET_CAN_REPLY_URL, tweet_id);
    defer c.g_free(url);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(chunk.memory, &parser);
    return if (root != null and c.json_object_has_member(root, "canReply") != FALSE) c.json_object_get_boolean_member(root, "canReply") else TRUE;
}

export fn perform_dm_mark_read(conversation_id: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or conversation_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.DM_MARK_READ_URL, conversation_id);
    defer c.g_free(url);
    return simpleRequest(url, "", "PATCH");
}

export fn perform_dm_message_reaction(message_id: [*c]const c.gchar, emoji: [*c]const c.gchar) c.gboolean {
    if (message_id == null or emoji == null or emoji[0] == 0) return FALSE;
    const url = c.g_strdup_printf(constants.DM_MESSAGE_REACTIONS_URL, message_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "emoji");
    _ = c.json_builder_add_string_value(builder, emoji);
    _ = c.json_builder_end_object(builder);
    return requestBuilder(url, "POST", builder);
}

export fn perform_dm_message_edit(message_id: [*c]const c.gchar, content: [*c]const c.gchar) c.gboolean {
    if (message_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.DM_MESSAGE_EDIT_URL, message_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "content");
    _ = c.json_builder_add_string_value(builder, textOr(content));
    _ = c.json_builder_end_object(builder);
    return requestBuilder(url, "PUT", builder);
}

export fn perform_dm_message_delete(message_id: [*c]const c.gchar) c.gboolean {
    if (message_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.DM_MESSAGE_DELETE_URL, message_id);
    defer c.g_free(url);
    return simpleRequest(url, null, "DELETE");
}

export fn perform_dm_payment_refresh(message_id: [*c]const c.gchar) c.gboolean {
    if (message_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.MPI_PAYMENT_BY_MESSAGE_URL, message_id);
    defer c.g_free(url);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    return if (chunk.memory != null) TRUE else FALSE;
}

export fn perform_dm_payment_pay(message_id: [*c]const c.gchar) c.gboolean {
    if (message_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.MPI_REQUEST_PAY_URL, message_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, "", "POST", &response) == FALSE) return FALSE;
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(response, &parser);
    const payment_url = jsonString(root, "paymentUrl");
    const order_id = jsonString(root, "orderId");
    if (payment_url[0] != 0) _ = c.gtk_show_uri_on_window(null, payment_url, 0, null);
    if (order_id[0] == 0) return TRUE;
    const dialog = c.gtk_dialog_new();
    c.gtk_window_set_title(@ptrCast(@alignCast(dialog)), "Confirm payment");
    c.gtk_window_set_modal(@ptrCast(@alignCast(dialog)), TRUE);
    _ = c.gtk_dialog_add_button(@ptrCast(@alignCast(dialog)), "_Cancel", c.GTK_RESPONSE_CANCEL);
    _ = c.gtk_dialog_add_button(@ptrCast(@alignCast(dialog)), "_Confirm", c.GTK_RESPONSE_ACCEPT);
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "Transaction ID");
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), c.gtk_label_new("Complete payment in the opened page, then paste the transaction id."), FALSE, FALSE, 8);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    var ok = TRUE;
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const confirm_url = c.g_strdup_printf(constants.MPI_REQUEST_CONFIRM_URL, message_id);
        defer c.g_free(confirm_url);
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "orderId");
        _ = c.json_builder_add_string_value(builder, order_id);
        _ = c.json_builder_set_member_name(builder, "transactionId");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(entry));
        _ = c.json_builder_end_object(builder);
        ok = requestBuilder(confirm_url, "POST", builder);
    }
    c.gtk_widget_destroy(dialog);
    return ok;
}

fn performDmBuilder(url: [*c]const c.gchar, method: [*c]const c.gchar, builder: [*c]c.JsonBuilder) c.gboolean {
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    const ok = requestErrorFromResponse(url, payload, method, &error_message);
    if (ok != FALSE) {
        const conversation_id = activeDmConversationId();
        if (conversation_id != null and g.g_dm_messages_list != null) start_loading_messages(@ptrCast(@alignCast(g.g_dm_messages_list)), conversation_id);
    }
    return ok;
}

export fn on_dm_entry_changed(editable: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const text = c.gtk_entry_get_text(@ptrCast(@alignCast(editable)));
    const was_typing = c.g_object_get_data(@ptrCast(@alignCast(editable)), "typing_active") != null;
    const is_typing = text != null and text[0] != 0;
    if (is_typing != was_typing) {
        const conversation_id = activeDmConversationId();
        if (conversation_id != null and g.g_auth_token != null) {
            const typing_url = c.g_strdup_printf(if (is_typing) constants.DM_TYPING_URL else constants.DM_TYPING_STOP_URL, conversation_id);
            defer c.g_free(typing_url);
            _ = simpleRequest(typing_url, "{}", "POST");
        }
        c.g_object_set_data(@ptrCast(@alignCast(editable)), "typing_active", if (is_typing) @as(c.gpointer, @ptrFromInt(1)) else null);
    }
    const conversation_id = activeDmConversationId();
    if (conversation_id == null or g.g_auth_token == null) return;
    const draft_url = c.g_strdup_printf(constants.DM_DRAFT_URL, conversation_id);
    defer c.g_free(draft_url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "content");
    _ = c.json_builder_add_string_value(builder, textOr(text));
    _ = c.json_builder_end_object(builder);
    _ = requestBuilder(draft_url, "POST", builder);
}

export fn on_dm_attach_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    if (g.g_dm_messages_list == null) return;
    const toplevel = c.gtk_widget_get_toplevel(widget);
    const parent: [*c]c.GtkWindow = if (toplevel != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(toplevel)), c.gtk_window_get_type()) != FALSE) @ptrCast(@alignCast(toplevel)) else null;
    const dialog = c.gtk_file_chooser_dialog_new(
        "Attach File",
        parent,
        c.GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Attach",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const filename = c.gtk_file_chooser_get_filename(@ptrCast(@alignCast(dialog)));
        if (filename != null) {
            const mime_type = api.detect_mime_type(filename);
            const object: [*c]c.GObject = @ptrCast(@alignCast(g.g_dm_messages_list));
            c.g_object_set_data_full(object, "pending_file_path", filename, c.g_free);
            c.g_object_set_data_full(object, "pending_file_type", mime_type, c.g_free);
            updateDmComposeStatusLabel();
        }
    }
    c.gtk_widget_destroy(dialog);
}

export fn on_dm_clear_context_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    clearDmComposeContext();
}

export fn on_dm_title_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const conversation_id = activeDmConversationId();
    const conversation = activeDmConversationDetail();
    if (conversation_id == null or conversation == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Rename Conversation",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_text(@ptrCast(@alignCast(entry)), if (conversation.*.title != null) conversation.*.title else "");
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, TRUE, TRUE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) != c.GTK_RESPONSE_ACCEPT) {
        c.gtk_widget_destroy(dialog);
        return;
    }
    const url = c.g_strdup_printf(constants.DM_UPDATE_TITLE_URL, conversation_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "title");
    _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(entry));
    _ = c.json_builder_end_object(builder);
    _ = performDmBuilder(url, "PATCH", builder);
    c.gtk_widget_destroy(dialog);
}

export fn on_dm_leave_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const conversation_id = activeDmConversationId();
    const conversation = activeDmConversationDetail();
    const current_username = getUsernameSafe();
    defer c.g_free(current_username);
    if (conversation_id != null and conversation != null and current_username != null) {
        var link = conversation.*.participants;
        while (link != null) : (link = link.*.next) {
            const participant: [*c]types.Profile = @ptrCast(@alignCast(link.*.data));
            if (participant != null and participant.*.username != null and c.g_strcmp0(participant.*.username, current_username) == 0 and participant.*.id != null) {
                const url = c.g_strdup_printf(constants.DM_REMOVE_PARTICIPANT_URL, conversation_id, participant.*.id);
                defer c.g_free(url);
                var response: [*c]c.gchar = null;
                defer c.g_free(response);
                if (requestWithResponse(url, null, "DELETE", &response) != FALSE) {
                    setStack("messages");
                    if (g.g_conversations_list != null) start_loading_conversations(@ptrCast(@alignCast(g.g_conversations_list)));
                }
                break;
            }
        }
    }
}

export fn on_dm_add_people_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const conversation_id = activeDmConversationId();
    if (conversation_id == null) return;
    const raw = getEntryDialogText(widget, "Add Participants", "username1, username2", "_Add");
    defer c.g_free(raw);
    if (raw == null or raw[0] == 0) return;
    const url = c.g_strdup_printf(constants.DM_ADD_PARTICIPANTS_URL, conversation_id);
    defer c.g_free(url);
    const parts = c.g_strsplit(raw, ",", -1);
    defer c.g_strfreev(parts);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "usernames");
    _ = c.json_builder_begin_array(builder);
    var i: usize = 0;
    while (parts[i] != null) : (i += 1) {
        _ = c.g_strstrip(parts[i]);
        if (parts[i][0] != 0) _ = c.json_builder_add_string_value(builder, parts[i]);
    }
    _ = c.json_builder_end_array(builder);
    _ = c.json_builder_end_object(builder);
    _ = performDmBuilder(url, "POST", builder);
}

export fn on_dm_disappearing_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const conversation_id = activeDmConversationId();
    if (conversation_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Disappearing Messages",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const enabled = c.gtk_check_button_new_with_label("Enable disappearing messages");
    const duration = c.gtk_spin_button_new_with_range(5, 86400, 5);
    const conversation = activeDmConversationDetail();
    c.gtk_toggle_button_set_active(
        @ptrCast(@alignCast(enabled)),
        if (conversation != null) conversation.*.disappearing_enabled else FALSE,
    );
    c.gtk_spin_button_set_value(
        @ptrCast(@alignCast(duration)),
        if (conversation != null and conversation.*.disappearing_duration > 0) @floatFromInt(conversation.*.disappearing_duration) else 60,
    );
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), enabled, FALSE, FALSE, 8);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), duration, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const url = c.g_strdup_printf(constants.DM_DISAPPEARING_URL, conversation_id);
        defer c.g_free(url);
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "enabled");
        _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(enabled))));
        _ = c.json_builder_set_member_name(builder, "duration");
        _ = c.json_builder_add_int_value(builder, c.gtk_spin_button_get_value_as_int(@ptrCast(@alignCast(duration))));
        _ = c.json_builder_end_object(builder);
        _ = performDmBuilder(url, "PATCH", builder);
    }
    c.gtk_widget_destroy(dialog);
}

export fn on_dm_request_payment_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const conversation_id = activeDmConversationId();
    if (conversation_id == null) return;
    const window = widgetWindow(widget);
    const dialog = c.gtk_dialog_new_with_buttons(
        "Request payment",
        window,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Request",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 12);
    const amount = c.gtk_spin_button_new_with_range(1, 100000, 1);
    const note = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(note)), "Optional note");
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Amount (₹)"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), amount, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Note"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), note, 1, 1, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "conversationId");
        _ = c.json_builder_add_string_value(builder, conversation_id);
        _ = c.json_builder_set_member_name(builder, "amount");
        _ = c.json_builder_add_double_value(builder, c.gtk_spin_button_get_value(@ptrCast(@alignCast(amount))));
        const note_text = entryTextOrEmpty(note);
        if (note_text[0] != 0) {
            _ = c.json_builder_set_member_name(builder, "note");
            _ = c.json_builder_add_string_value(builder, note_text);
        }
        _ = c.json_builder_end_object(builder);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        var chunk: types.MemoryStruct = .{ .memory = null, .size = 0 };
        if (api.fetch_url(constants.MPI_REQUEST_URL, &chunk, payload, "POST") != FALSE) {
            const parser = c.json_parser_new();
            var success: c.gboolean = FALSE;
            if (chunk.memory != null and c.json_parser_load_from_data(parser, chunk.memory, -1, null) != FALSE) {
                const obj = c.json_node_get_object(c.json_parser_get_root(parser));
                success = if (obj != null and c.json_object_has_member(obj, "success") != FALSE and c.json_object_get_boolean_member(obj, "success") != FALSE) TRUE else FALSE;
            }
            c.g_object_unref(parser);
            if (success != FALSE) {
                if (g.g_dm_messages_list != null) start_loading_messages(@ptrCast(@alignCast(g.g_dm_messages_list)), conversation_id);
            } else {
                var error_message = extractErrorMessage(chunk.memory);
                if (error_message == null) error_message = c.g_strdup("The request could not be created.");
                const error_dialog = c.gtk_message_dialog_new(window, c.GTK_DIALOG_MODAL, c.GTK_MESSAGE_ERROR, c.GTK_BUTTONS_OK, "%s", "Payment request failed.");
                c.gtk_message_dialog_format_secondary_text(@ptrCast(@alignCast(error_dialog)), "%s", error_message);
                _ = c.gtk_dialog_run(@ptrCast(@alignCast(error_dialog)));
                c.gtk_widget_destroy(error_dialog);
                c.g_free(error_message);
            }
            c.g_free(chunk.memory);
        } else {
            const error_dialog = c.gtk_message_dialog_new(window, c.GTK_DIALOG_MODAL, c.GTK_MESSAGE_ERROR, c.GTK_BUTTONS_OK, "%s", "Payment request failed.");
            _ = c.gtk_dialog_run(@ptrCast(@alignCast(error_dialog)));
            c.gtk_widget_destroy(error_dialog);
        }
    }
    c.gtk_widget_destroy(dialog);
}

export fn perform_block(username: [*c]const c.gchar, block: c.gboolean) c.gboolean {
    if (g.g_auth_token == null or username == null) return FALSE;
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "userId");
    _ = c.json_builder_add_string_value(builder, username);
    _ = c.json_builder_end_object(builder);
    return requestBuilder(if (block != FALSE) constants.BLOCK_USER_URL else constants.UNBLOCK_USER_URL, "POST", builder);
}

export fn perform_mute(username: [*c]const c.gchar, mute: c.gboolean) c.gboolean {
    if (g.g_auth_token == null or username == null) return FALSE;
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "userId");
    _ = c.json_builder_add_string_value(builder, username);
    _ = c.json_builder_end_object(builder);
    return requestBuilder(if (mute != FALSE) constants.MUTE_USER_URL else constants.UNMUTE_USER_URL, "POST", builder);
}

export fn check_user_blocked(username: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or username == null) return FALSE;
    const url = c.g_strdup_printf(constants.CHECK_BLOCK_URL, username);
    defer c.g_free(url);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(chunk.memory, &parser);
    return if (root != null and c.json_object_has_member(root, "blocked") != FALSE and c.json_object_get_boolean_member(root, "blocked") != FALSE) TRUE else FALSE;
}

export fn check_user_muted(username: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or username == null) return FALSE;
    const url = c.g_strdup_printf(constants.CHECK_MUTE_URL, username);
    defer c.g_free(url);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(chunk.memory, &parser);
    return if (root != null and c.json_object_has_member(root, "muted") != FALSE and c.json_object_get_boolean_member(root, "muted") != FALSE) TRUE else FALSE;
}

export fn set_timeline_type(type_: types.TimelineType) void {
    g.g_current_timeline_type = type_;
}

export fn get_current_timeline_type() types.TimelineType {
    return g.g_current_timeline_type;
}

export fn perform_poll_vote(tweet_id: [*c]const c.gchar, option_id: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or tweet_id == null or option_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.POLL_VOTE_URL, tweet_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "optionId");
    _ = c.json_builder_add_string_value(builder, option_id);
    _ = c.json_builder_end_object(builder);
    return requestBuilder(url, "POST", builder);
}

export fn perform_poll_multi_vote(tweet_id: [*c]const c.gchar, answers: ?*c.JsonNode, message_out: [*c][*c]c.gchar) c.gboolean {
    if (message_out != null) message_out.* = null;
    if (g.g_auth_token == null or tweet_id == null or answers == null) return FALSE;
    const url = c.g_strdup_printf(constants.TWEET_POLL_MULTI_VOTE_URL, tweet_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "answers");
    _ = c.json_builder_add_value(builder, c.json_node_copy(answers));
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    defer c.g_free(chunk.memory);
    var success = FALSE;
    if (api.fetch_url(url, &chunk, payload, "POST") != FALSE) {
        var parser: ?*c.JsonParser = null;
        defer if (parser != null) c.g_object_unref(parser);
        const root = parseRootObject(chunk.memory, &parser);
        if (root != null) {
            if (c.json_object_has_member(root, "success") != FALSE and c.json_object_get_boolean_member(root, "success") != FALSE) {
                success = TRUE;
                if (message_out != null and c.json_object_has_member(root, "score") != FALSE and c.json_node_is_null(c.json_object_get_member(root, "score")) == FALSE and c.json_object_has_member(root, "total") != FALSE and c.json_node_is_null(c.json_object_get_member(root, "total")) == FALSE) {
                    message_out.* = c.g_strdup_printf("Quiz submitted. Score: %d/%d", @as(c_int, @intCast(c.json_object_get_int_member(root, "score"))), @as(c_int, @intCast(c.json_object_get_int_member(root, "total"))));
                }
            } else if (message_out != null and c.json_object_has_member(root, "error") != FALSE) {
                message_out.* = c.g_strdup(c.json_object_get_string_member(root, "error"));
            }
        }
    }
    if (success == FALSE and message_out != null and message_out.* == null) {
        message_out.* = c.g_strdup("Could not submit poll answers.");
    }
    return success;
}

export fn free_poll(poll: [*c]types.Poll) void {
    if (poll == null) return;
    c.g_free(poll.*.id);
    c.g_free(poll.*.question);
    c.g_free(poll.*.kind);
    c.g_free(poll.*.expires_at);
    if (poll.*.steps != null) c.json_node_free(poll.*.steps);
    if (poll.*.options != null) c.g_list_free_full(poll.*.options, free_poll_option);
    c.g_free(poll);
}

export fn free_poll_option(data: c.gpointer) void {
    const option: [*c]types.PollOption = @ptrCast(@alignCast(data));
    if (option == null) return;
    c.g_free(option.*.id);
    c.g_free(option.*.option_text);
    c.g_free(option.*.user_vote);
    c.g_free(option);
}

export fn perform_update_profile(username: [*c]const c.gchar, name: [*c]const c.gchar, bio: [*c]const c.gchar, location: [*c]const c.gchar, website: [*c]const c.gchar, pronouns: [*c]const c.gchar, theme: [*c]const c.gchar, accent_color: [*c]const c.gchar, label_type: [*c]const c.gchar, label_automated: c.gboolean, include_avatar_radius: c.gboolean, avatar_radius: c.gint) c.gboolean {
    if (g.g_auth_token == null or username == null) return FALSE;
    const url = c.g_strdup_printf(constants.UPDATE_PROFILE_URL, username);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "name");
    _ = c.json_builder_add_string_value(builder, textOr(name));
    _ = c.json_builder_set_member_name(builder, "bio");
    _ = c.json_builder_add_string_value(builder, textOr(bio));
    _ = c.json_builder_set_member_name(builder, "location");
    _ = c.json_builder_add_string_value(builder, textOr(location));
    _ = c.json_builder_set_member_name(builder, "website");
    _ = c.json_builder_add_string_value(builder, textOr(website));
    _ = c.json_builder_set_member_name(builder, "pronouns");
    _ = c.json_builder_add_string_value(builder, textOr(pronouns));
    _ = c.json_builder_set_member_name(builder, "theme");
    _ = c.json_builder_add_string_value(builder, if (theme != null) theme else "auto");
    _ = c.json_builder_set_member_name(builder, "accent_color");
    _ = c.json_builder_add_string_value(builder, textOr(accent_color));
    _ = c.json_builder_set_member_name(builder, "label_type");
    if (label_type != null) {
        _ = c.json_builder_add_string_value(builder, label_type);
    } else {
        _ = c.json_builder_add_null_value(builder);
    }
    _ = c.json_builder_set_member_name(builder, "label_automated");
    _ = c.json_builder_add_boolean_value(builder, label_automated);
    if (include_avatar_radius != FALSE) {
        _ = c.json_builder_set_member_name(builder, "avatar_radius");
        _ = c.json_builder_add_int_value(builder, avatar_radius);
    }
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    return simpleRequest(url, payload, "PUT");
}

export fn perform_upload_avatar(username: [*c]const c.gchar, file_path: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or username == null or file_path == null) return FALSE;
    const url = c.g_strdup_printf(constants.UPDATE_AVATAR_URL, username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    defer c.g_free(chunk.memory);
    return api.fetch_url_with_file(url, &chunk, file_path, "image");
}

export fn perform_upload_banner(username: [*c]const c.gchar, file_path: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or username == null or file_path == null) return FALSE;
    const url = c.g_strdup_printf(constants.UPDATE_BANNER_URL, username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    defer c.g_free(chunk.memory);
    return api.fetch_url_with_file(url, &chunk, file_path, "image");
}

export fn perform_media_upload(file_path: [*c]const c.gchar) [*c]c.gchar {
    logDebug("perform_media_upload: starting upload for file_path=%s", .{if (file_path != null) file_path else lit("(null)")});
    if (g.g_auth_token == null or file_path == null) {
        logWarning("perform_media_upload: failed - auth_token=%s, file_path=%s", .{ if (g.g_auth_token != null) lit("(set)") else lit("(null)"), if (file_path != null) file_path else lit("(null)") });
        return null;
    }
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    defer c.g_free(chunk.memory);
    logDebug("perform_media_upload: calling fetch_url_with_file for UPLOAD_URL", .{});
    if (api.fetch_url_with_file(constants.UPLOAD_URL, &chunk, file_path, "file") == FALSE) {
        logWarning("perform_media_upload: fetch_url_with_file failed", .{});
        return null;
    }
    logDebug("perform_media_upload: upload succeeded, response=%s", .{if (chunk.memory != null) chunk.memory else lit("(null)")});
    if (chunk.memory == null) {
        logWarning("fetch_url_with_file succeeded but chunk.memory is NULL", .{});
        return null;
    }
    const file_url = api.parse_upload_response(chunk.memory);
    logDebug("perform_media_upload: extracted file_url=%s", .{if (file_url != null) file_url else lit("(null)")});
    logDebug("perform_media_upload: returning file_url=%s", .{if (file_url != null) file_url else lit("(null)")});
    return file_url;
}

fn performAdminMediaUpload(file_path: [*c]const c.gchar) [*c]c.gchar {
    if (file_path == null) return null;
    const admin_token = getAdminAuthTokenSafe();
    defer c.g_free(admin_token);
    if (admin_token == null) return null;
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    defer c.g_free(chunk.memory);
    if (api.fetch_url_with_file_auth_token(constants.UPLOAD_URL, &chunk, file_path, "file", admin_token) == FALSE) return null;
    return api.parse_upload_response(chunk.memory);
}

export fn perform_join_community(community_id: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or community_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.COMMUNITY_JOIN_URL, community_id);
    defer c.g_free(url);
    return simpleRequest(url, "{}", "POST");
}

export fn perform_leave_community(community_id: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or community_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.COMMUNITY_LEAVE_URL, community_id);
    defer c.g_free(url);
    return simpleRequest(url, "{}", "POST");
}

export fn perform_create_community(name: [*c]const c.gchar, description: [*c]const c.gchar, rules: [*c]const c.gchar, access_mode: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or name == null or name[0] == 0) return FALSE;
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "name");
    _ = c.json_builder_add_string_value(builder, name);
    _ = c.json_builder_set_member_name(builder, "description");
    _ = c.json_builder_add_string_value(builder, textOr(description));
    _ = c.json_builder_set_member_name(builder, "rules");
    _ = c.json_builder_add_string_value(builder, textOr(rules));
    _ = c.json_builder_set_member_name(builder, "access_mode");
    _ = c.json_builder_add_string_value(builder, if (access_mode != null) access_mode else "open");
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    return requestWithResponse(constants.COMMUNITIES_LIST_URL, payload, "POST", &response);
}

export fn perform_update_community(community_id: [*c]const c.gchar, name: [*c]const c.gchar, description: [*c]const c.gchar, rules: [*c]const c.gchar, access_mode: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or community_id == null or name == null or name[0] == 0) return FALSE;
    const url = c.g_strdup_printf(constants.COMMUNITY_DETAILS_URL, community_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "name");
    _ = c.json_builder_add_string_value(builder, name);
    _ = c.json_builder_set_member_name(builder, "description");
    _ = c.json_builder_add_string_value(builder, textOr(description));
    _ = c.json_builder_set_member_name(builder, "rules");
    _ = c.json_builder_add_string_value(builder, textOr(rules));
    _ = c.json_builder_set_member_name(builder, "access_mode");
    _ = c.json_builder_add_string_value(builder, if (access_mode != null) access_mode else "open");
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    return requestWithResponse(url, payload, "PATCH", &response);
}

export fn perform_delete_community(community_id: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or community_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.COMMUNITY_DETAILS_URL, community_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    return requestWithResponse(url, null, "DELETE", &response);
}

export fn perform_update_community_access_mode(community_id: [*c]const c.gchar, access_mode: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or community_id == null or access_mode == null) return FALSE;
    const url = c.g_strdup_printf(constants.COMMUNITY_ACCESS_MODE_URL, community_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "access_mode");
    _ = c.json_builder_add_string_value(builder, access_mode);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    return requestWithResponse(url, payload, "PATCH", &response);
}

export fn update_interaction_cache(tweet_id: [*c]const c.gchar, liked: c.gboolean, retweeted: c.gboolean, bookmarked: c.gboolean) void {
    if (tweet_id == null or g.g_interaction_cache == null) return;
    var state: [*c]types.InteractionState = @ptrCast(@alignCast(c.g_hash_table_lookup(g.g_interaction_cache, tweet_id)));
    if (state == null) {
        state = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.InteractionState))));
        _ = c.g_hash_table_insert(g.g_interaction_cache, c.g_strdup(tweet_id), state);
    }
    if (liked != -1) state.*.liked = liked;
    if (retweeted != -1) state.*.retweeted = retweeted;
    if (bookmarked != -1) state.*.bookmarked = bookmarked;
}

export fn get_cached_liked(tweet_id: [*c]const c.gchar) c.gboolean {
    if (tweet_id == null or g.g_interaction_cache == null) return FALSE;
    const state: [*c]types.InteractionState = @ptrCast(@alignCast(c.g_hash_table_lookup(g.g_interaction_cache, tweet_id)));
    return if (state != null) state.*.liked else FALSE;
}

export fn get_cached_retweeted(tweet_id: [*c]const c.gchar) c.gboolean {
    if (tweet_id == null or g.g_interaction_cache == null) return FALSE;
    const state: [*c]types.InteractionState = @ptrCast(@alignCast(c.g_hash_table_lookup(g.g_interaction_cache, tweet_id)));
    return if (state != null) state.*.retweeted else FALSE;
}

export fn get_cached_bookmarked(tweet_id: [*c]const c.gchar) c.gboolean {
    if (tweet_id == null or g.g_interaction_cache == null) return FALSE;
    const state: [*c]types.InteractionState = @ptrCast(@alignCast(c.g_hash_table_lookup(g.g_interaction_cache, tweet_id)));
    return if (state != null) state.*.bookmarked else FALSE;
}

export fn on_theme_changed(combo: [*c]c.GtkComboBox, user_data: c.gpointer) void {
    unused(.{user_data});
    g.g_theme_preference = c.gtk_combo_box_get_active(combo);
    const settings = c.gtk_settings_get_default();
    if (settings != null) {
        switch (g.g_theme_preference) {
            0 => c.g_object_set(settings, "gtk-application-prefer-dark-theme", FALSE, @as(?*anyopaque, null)),
            1 => c.g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, @as(?*anyopaque, null)),
            else => c.g_object_set(settings, "gtk-application-prefer-dark-theme", FALSE, @as(?*anyopaque, null)),
        }
    }
    logDebug("Theme changed to: %d", .{g.g_theme_preference});
}

export fn on_compact_mode_toggled(switch_widget: [*c]c.GtkSwitch, state: c.gboolean, user_data: c.gpointer) void {
    unused(.{ switch_widget, user_data });
    g.g_compact_mode_enabled = state;
    logDebug("Compact mode: %s", .{if (state != FALSE) lit("enabled") else lit("disabled")});
}

export fn on_notifications_enabled_toggled(switch_widget: [*c]c.GtkSwitch, state: c.gboolean, user_data: c.gpointer) void {
    unused(.{ switch_widget, user_data });
    g.g_notifications_enabled = state;
    if (g.g_sound_notifications_switch != null) {
        c.gtk_widget_set_sensitive(@ptrCast(@alignCast(g.g_sound_notifications_switch)), state);
    }
    if (g.g_dm_notifications_switch != null) {
        c.gtk_widget_set_sensitive(@ptrCast(@alignCast(g.g_dm_notifications_switch)), state);
    }
    logDebug("Notifications: %s", .{if (state != FALSE) lit("enabled") else lit("disabled")});
}

export fn refresh_cache_size_display() void {
    if (g.g_cache_size_label == null) return;
    const cache_dir = getCacheDirectory();
    defer c.g_free(cache_dir);
    const size = calculateDirectorySize(cache_dir);
    const text = if (size < 1024)
        c.g_strdup_printf("Cache size: %lu bytes", size)
    else if (size < 1024 * 1024)
        c.g_strdup_printf("Cache size: %.2f KB", @as(f64, @floatFromInt(size)) / 1024.0)
    else
        c.g_strdup_printf("Cache size: %.2f MB", @as(f64, @floatFromInt(size)) / (1024.0 * 1024.0));
    defer c.g_free(text);
    labelSet(g.g_cache_size_label, text);
}

export fn update_settings_username_display() void {
    if (g.g_settings_username_label == null) return;
    const username = getUsernameSafe();
    defer c.g_free(username);
    if (username != null) {
        const text = c.g_strdup_printf("Logged in as: @%s", username);
        defer c.g_free(text);
        labelSet(g.g_settings_username_label, text);
    } else {
        labelSet(g.g_settings_username_label, "Not logged in");
    }
}

export fn refresh_notification_badge() void {
    var unread_count: c.gint = 0;
    if (g.g_notifications_button == null) return;
    if (g.g_auth_token == null) {
        buttonSet(g.g_notifications_button, "Alerts");
        c.gtk_widget_set_tooltip_text(g.g_notifications_button, "Notifications");
        return;
    }
    if (g.g_auth_token != null) {
        const chunk = fetchGet(constants.NOTIFICATIONS_UNREAD_COUNT_URL);
        defer c.g_free(chunk.memory);
        if (chunk.memory != null) {
            const parser = c.json_parser_new();
            defer c.g_object_unref(parser);
            var err: ?*c.GError = null;
            if (c.json_parser_load_from_data(parser, chunk.memory, -1, &err) != FALSE) {
                const root = c.json_parser_get_root(parser);
                if (root != null and c.JSON_NODE_HOLDS_OBJECT(root)) {
                    const obj = c.json_node_get_object(root);
                    if (c.json_object_has_member(obj, "count") != FALSE) {
                        unread_count = @intCast(c.json_object_get_int_member(obj, "count"));
                    }
                }
            }
            if (err != null) c.g_error_free(err);
        }
    }
    const label = if (unread_count > 0) c.g_strdup_printf("Alerts (%d)", unread_count) else c.g_strdup("Alerts");
    defer c.g_free(label);
    c.gtk_widget_set_tooltip_text(g.g_notifications_button, if (unread_count > 0) label else "No unread notifications");
    buttonSet(g.g_notifications_button, label);
}

export fn mark_notification_read(notification_id: [*c]const c.gchar) c.gboolean {
    if (g.g_auth_token == null or notification_id == null) return FALSE;
    const url = c.g_strdup_printf(constants.NOTIFICATION_READ_URL, notification_id);
    defer c.g_free(url);
    const ok = simpleRequest(url, "", "PATCH");
    if (ok != FALSE) refresh_notification_badge();
    return ok;
}

export fn show_profile(username: [*c]const c.gchar) void {
    if (username == null or username[0] == 0 or g.g_stack == null) return;
    setStack("profile");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);

    labelSet(g.g_profile_name_label, "Loading...");
    labelSet(g.g_profile_username_label, "");
    labelSet(g.g_profile_bio_label, "");
    labelSet(g.g_profile_status_label, "");
    labelSet(g.g_profile_details_label, "");
    labelSet(g.g_profile_stats_label, "");
    updateProfileBadges(null);
    if (g.g_profile_banner_image != null) {
        c.gtk_image_clear(@ptrCast(@alignCast(g.g_profile_banner_image)));
        c.gtk_widget_hide(g.g_profile_banner_image);
    }
    for ([_][*c]c.GtkWidget{
        @ptrCast(@alignCast(g.g_follow_button)),
        @ptrCast(@alignCast(g.g_profile_notify_button)),
        @ptrCast(@alignCast(g.g_profile_edit_button)),
        @ptrCast(@alignCast(g.g_profile_block_button)),
        @ptrCast(@alignCast(g.g_profile_mute_button)),
        @ptrCast(@alignCast(g.g_profile_report_button)),
        @ptrCast(@alignCast(g.g_profile_delete_avatar_button)),
        @ptrCast(@alignCast(g.g_profile_delete_banner_button)),
        @ptrCast(@alignCast(g.g_profile_analytics_button)),
        @ptrCast(@alignCast(g.g_profile_common_followers_button)),
        @ptrCast(@alignCast(g.g_profile_top_posts_button)),
    }) |button| if (button != null) c.gtk_widget_hide(button);

    setProfileActionUsername(username);
    if (g.g_profile_tweets_list != null) {
        setObjectStringData(@ptrCast(@alignCast(g.g_profile_tweets_list)), "current_profile_user", username);
        c.g_object_set_data(@ptrCast(@alignCast(g.g_profile_tweets_list)), "last_id", null);
        api.populate_tweet_list(@ptrCast(@alignCast(g.g_profile_tweets_list)), null);
    }
    if (g.g_profile_replies_list != null) {
        setObjectStringData(@ptrCast(@alignCast(g.g_profile_replies_list)), "current_profile_user", username);
        c.g_object_set_data(@ptrCast(@alignCast(g.g_profile_replies_list)), "last_id", null);
        api.populate_tweet_list(@ptrCast(@alignCast(g.g_profile_replies_list)), null);
    }
    if (g.g_profile_media_list != null) {
        setObjectStringData(@ptrCast(@alignCast(g.g_profile_media_list)), "current_profile_user", username);
        c.g_object_set_data(@ptrCast(@alignCast(g.g_profile_media_list)), "last_id", null);
        api.populate_tweet_list(@ptrCast(@alignCast(g.g_profile_media_list)), null);
    }
    if (g.g_profile_highlights_list != null) {
        setObjectStringData(@ptrCast(@alignCast(g.g_profile_highlights_list)), "current_profile_user", username);
        c.g_object_set_data(@ptrCast(@alignCast(g.g_profile_highlights_list)), "last_id", null);
        api.populate_tweet_list(@ptrCast(@alignCast(g.g_profile_highlights_list)), null);
    }
    if (g.g_profile_mutuals_list != null) api.populate_user_list(@ptrCast(@alignCast(g.g_profile_mutuals_list)), null);
    if (g.g_profile_followers_you_know_list != null) api.populate_user_list(@ptrCast(@alignCast(g.g_profile_followers_you_know_list)), null);
    if (g.g_profile_affiliates_list != null) api.populate_user_list(@ptrCast(@alignCast(g.g_profile_affiliates_list)), null);

    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data != null) {
        data.*.username = c.g_strdup(username);
        _ = c.g_thread_new("profile-loader", fetchProfileThread, data);
    }

    const reply_data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (reply_data != null) {
        reply_data.*.username = c.g_strdup(username);
        _ = c.g_thread_new("profile-reply-loader", fetchProfileRepliesThread, reply_data);
    }
}

export fn show_tweet(tweet_id: [*c]const c.gchar) void {
    if (tweet_id == null) return;
    setStack("conversation");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
    if (g.g_conversation_list == null) return;
    const list = listBox(g.g_conversation_list);
    clearListBox(list);
    _ = appendListLabel(list, "Loading tweet...");

    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.query = c.g_strdup(tweet_id);
    _ = c.g_thread_new("tweet-detail-loader", fetchTweetThread, data);
}

fn onSearchUsersLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.users != null) {
        api.populate_user_list(async_data.*.list_box, async_data.*.users);
        api.free_users(async_data.*.users);
        async_data.*.users = null;
    } else {
        setListBoxStatus(async_data.*.list_box, "No users found.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn onSearchTweetsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.tweets != null) {
        api.populate_tweet_list(async_data.*.list_box, async_data.*.tweets);
        api.free_tweets(async_data.*.tweets);
        async_data.*.tweets = null;
    } else {
        setListBoxStatus(async_data.*.list_box, "No tweets found.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchSearchUsersThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const escaped = c.g_uri_escape_string(async_data.*.query, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf("%s?q=%s", constants.SEARCH_USERS_URL, escaped);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.users = api.parse_users(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onSearchUsersLoaded), async_data);
    return null;
}

fn fetchSearchTweetsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const escaped = c.g_uri_escape_string(async_data.*.query, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf("%s?q=%s", constants.SEARCH_POSTS_URL, escaped);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.tweets = api.parse_tweets(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onSearchTweetsLoaded), async_data);
    return null;
}

export fn perform_search(query: [*c]const c.gchar) void {
    setStack("search");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
    if (query == null or query[0] == 0) return;
    if (g.g_search_users_list != null) {
        const users_list = listBox(g.g_search_users_list);
        api.populate_user_list(users_list, null);
        _ = appendListLabel(users_list, "Searching users...");
        const data_users: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
        if (data_users != null) {
            data_users.*.list_box = users_list;
            data_users.*.query = c.g_strdup(query);
            _ = c.g_thread_new("search-users-loader", fetchSearchUsersThread, data_users);
        }
    }
    if (g.g_search_tweets_list != null) {
        const tweets_list = listBox(g.g_search_tweets_list);
        api.populate_tweet_list(tweets_list, null);
        _ = appendListLabel(tweets_list, "Searching tweets...");
        const data_tweets: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
        if (data_tweets != null) {
            data_tweets.*.list_box = tweets_list;
            data_tweets.*.query = c.g_strdup(query);
            _ = c.g_thread_new("search-tweets-loader", fetchSearchTweetsThread, data_tweets);
        }
    }
}

fn onListRemoveMemberClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    unused(.{user_data});
    const user_id = widgetStringData(widget, "user_id");
    if (g.g_active_list == null or g.g_active_list.*.id == null or user_id == null) return;
    const url = c.g_strdup_printf(constants.LIST_MEMBER_URL, g.g_active_list.*.id, user_id);
    defer c.g_free(url);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (listJsonRequest(url, null, "DELETE", &error_message) != FALSE) {
        show_list_details(g.g_active_list.*.id);
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not remove member.", error_message);
    }
}

fn populateListMembersWithActions(members: [*c]c.GList) void {
    if (g.g_list_members_list == null) return;
    const members_box = listBox(g.g_list_members_list);
    clearListBox(members_box);
    if (members == null) {
        _ = appendListLabel(members_box, "No members.");
        return;
    }
    var item = members;
    while (item != null) : (item = item.*.next) {
        const user: [*c]types.Profile = @ptrCast(@alignCast(item.*.data));
        if (user == null) continue;
        const row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
        const user_widget = api.create_user_widget(user);
        c.gtk_box_pack_start(asBox(row), user_widget, TRUE, TRUE, 0);
        if (g.g_active_list != null and g.g_active_list.*.is_owner != FALSE and user.*.id != null) {
            const remove = c.gtk_button_new_with_label("Remove");
            setObjectStringData(remove, "user_id", user.*.id);
            connect(remove, "clicked", onListRemoveMemberClicked, null);
            c.gtk_box_pack_end(asBox(row), remove, FALSE, FALSE, 0);
        }
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(members_box, row, -1);
    }
}

fn onListDetailsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (g.g_active_list != null) {
        api.free_tweeta_list(g.g_active_list);
        g.g_active_list = null;
    }
    if (async_data.*.success == FALSE or async_data.*.list == null) {
        labelSet(g.g_list_title_label, "List unavailable");
        labelSet(g.g_list_details_label, "");
        if (g.g_list_tweets_list != null) setListBoxStatus(listBox(g.g_list_tweets_list), "Failed to load list.");
        freeAsyncData(async_data);
        return FALSE;
    }
    g.g_active_list = async_data.*.list;
    async_data.*.list = null;
    const active = g.g_active_list;

    labelSet(g.g_list_title_label, if (active.*.name != null) active.*.name else "Untitled list");
    const detail_text = c.g_strdup_printf(
        "%s%d members · %d followers%s%s%s",
        if (active.*.is_private != FALSE) lit("Private · ") else lit(""),
        active.*.member_count,
        active.*.follower_count,
        if (active.*.owner_username != null) lit(" · @") else lit(""),
        if (active.*.owner_username != null) active.*.owner_username else lit(""),
        if (active.*.description != null) lit("") else lit(""),
    );
    defer c.g_free(detail_text);
    labelSet(g.g_list_details_label, detail_text);

    if (g.g_list_follow_button != null) {
        setObjectStringData(@ptrCast(@alignCast(g.g_list_follow_button)), "list_id", active.*.id);
        buttonSet(g.g_list_follow_button, if (active.*.is_following != FALSE) "Unfollow" else "Follow");
        c.gtk_widget_set_visible(@ptrCast(@alignCast(g.g_list_follow_button)), if (g.g_auth_token != null and active.*.is_owner == FALSE) TRUE else FALSE);
    }
    if (g.g_list_edit_button != null) c.gtk_widget_set_visible(@ptrCast(@alignCast(g.g_list_edit_button)), active.*.is_owner);
    if (g.g_list_delete_button != null) c.gtk_widget_set_visible(@ptrCast(@alignCast(g.g_list_delete_button)), active.*.is_owner);
    if (g.g_list_add_member_button != null) c.gtk_widget_set_visible(@ptrCast(@alignCast(g.g_list_add_member_button)), active.*.is_owner);

    if (g.g_list_tweets_list != null) {
        if (async_data.*.tweets != null) {
            api.populate_tweet_list(listBox(g.g_list_tweets_list), async_data.*.tweets);
        } else {
            setListBoxStatus(listBox(g.g_list_tweets_list), "No tweets from list members.");
        }
    }
    populateListMembersWithActions(active.*.members);
    if (g.g_list_followers_list != null) {
        if (active.*.followers != null) {
            api.populate_user_list(listBox(g.g_list_followers_list), active.*.followers);
        } else {
            setListBoxStatus(listBox(g.g_list_followers_list), "No followers.");
        }
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchListDetailsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };

    var url = c.g_strdup_printf(constants.LIST_DETAILS_URL, async_data.*.query);
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.list = api.parse_list_details_response(chunk.memory);
        c.g_free(chunk.memory);
        chunk.memory = null;
        chunk.size = 0;
    }
    c.g_free(url);

    url = c.g_strdup_printf(constants.LIST_TWEETS_URL, async_data.*.query);
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.tweets = api.parse_tweets(chunk.memory);
        c.g_free(chunk.memory);
        chunk.memory = null;
        chunk.size = 0;
    }
    c.g_free(url);

    url = c.g_strdup_printf(constants.LIST_FOLLOWERS_URL, async_data.*.query);
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        if (async_data.*.list != null) async_data.*.list.*.followers = api.parse_list_followers_response(chunk.memory);
        c.g_free(chunk.memory);
        chunk.memory = null;
        chunk.size = 0;
    }
    c.g_free(url);

    async_data.*.success = if (async_data.*.list != null) TRUE else FALSE;
    _ = c.g_idle_add(@ptrCast(&onListDetailsLoaded), async_data);
    return null;
}

export fn show_list_details(list_id: [*c]const c.gchar) void {
    if (list_id == null) return;
    setStack("list_details");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
    labelSet(g.g_list_title_label, "Loading list...");
    labelSet(g.g_list_details_label, "");
    if (g.g_list_tweets_list != null) setListBoxStatus(listBox(g.g_list_tweets_list), "Loading tweets...");
    if (g.g_list_members_list != null) setListBoxStatus(listBox(g.g_list_members_list), "Loading members...");
    if (g.g_list_followers_list != null) setListBoxStatus(listBox(g.g_list_followers_list), "Loading followers...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.query = c.g_strdup(list_id);
    _ = c.g_thread_new("list-details-loader", fetchListDetailsThread, data);
}

export fn on_search_activated(entry: [*c]c.GtkEntry, user_data: c.gpointer) void {
    unused(.{user_data});
    perform_search(c.gtk_entry_get_text(entry));
}

export fn on_back_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const current_view = if (g.g_stack != null) c.gtk_stack_get_visible_child_name(@ptrCast(@alignCast(g.g_stack))) else null;
    if (c.g_strcmp0(current_view, "dm_messages") == 0) {
        setStack("messages");
    } else if (c.g_strcmp0(current_view, "list_details") == 0) {
        setStack("lists");
    } else {
        setStack("timeline");
        if (g.g_back_button != null) c.gtk_widget_hide(g.g_back_button);
    }
}

export fn on_notifications_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        const error_dialog = c.gtk_message_dialog_new(
            widgetWindow(widget),
            c.GTK_DIALOG_DESTROY_WITH_PARENT,
            c.GTK_MESSAGE_ERROR,
            c.GTK_BUTTONS_CLOSE,
            "You must be logged in to view notifications.",
        );
        _ = c.gtk_dialog_run(@ptrCast(@alignCast(error_dialog)));
        c.gtk_widget_destroy(error_dialog);
        return;
    }
    setStack("notifications");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
    if (g.g_notifications_list != null) start_loading_notifications(@ptrCast(@alignCast(g.g_notifications_list)));
}

export fn on_messages_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        const error_dialog = c.gtk_message_dialog_new(
            widgetWindow(widget),
            c.GTK_DIALOG_DESTROY_WITH_PARENT,
            c.GTK_MESSAGE_ERROR,
            c.GTK_BUTTONS_CLOSE,
            "You must be logged in to view messages.",
        );
        _ = c.gtk_dialog_run(@ptrCast(@alignCast(error_dialog)));
        c.gtk_widget_destroy(error_dialog);
        return;
    }
    setStack("messages");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
    if (g.g_conversations_list != null) start_loading_conversations(@ptrCast(@alignCast(g.g_conversations_list)));
}

export fn on_settings_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    setStack("settings");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
    update_settings_username_display();
    refresh_cache_size_display();
    loadSettingsLists();
}

export fn on_admin_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (!hasAdminSession()) return;
    setStack("admin");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
    start_loading_admin_stats();
    start_loading_admin_users(null);
    start_loading_admin_posts(null);
    start_loading_admin_suspensions();
    start_loading_admin_reports();
    start_loading_admin_logs(null);
    start_loading_admin_blocks();
    start_loading_admin_emojis();
    start_loading_admin_badges();
    start_loading_admin_dms(null);
    start_loading_admin_shop(null);
    start_loading_admin_communities();
    updateAdminImpersonationStatusLabel();
}

export fn on_refresh_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const current_view = if (g.g_stack != null) c.gtk_stack_get_visible_child_name(@ptrCast(@alignCast(g.g_stack))) else null;
    if (c.g_strcmp0(current_view, "notifications") == 0) {
        if (g.g_notifications_list != null) start_loading_notifications(@ptrCast(@alignCast(g.g_notifications_list)));
    } else if (c.g_strcmp0(current_view, "messages") == 0) {
        if (g.g_conversations_list != null) start_loading_conversations(@ptrCast(@alignCast(g.g_conversations_list)));
    } else if (c.g_strcmp0(current_view, "dm_messages") == 0) {
        if (g.g_dm_messages_list != null) {
            const conv_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_dm_messages_list)), "conversation_id"));
            if (conv_id != null) start_loading_messages(@ptrCast(@alignCast(g.g_dm_messages_list)), conv_id);
        }
    } else if (c.g_strcmp0(current_view, "lists") == 0) {
        start_loading_lists();
    } else if (c.g_strcmp0(current_view, "list_details") == 0 and g.g_active_list != null and g.g_active_list.*.id != null) {
        api.show_list_details(g.g_active_list.*.id);
    } else if (c.g_strcmp0(current_view, "explore") == 0) {
        start_loading_explore();
    } else if (c.g_strcmp0(current_view, "admin") == 0) {
        start_loading_admin_stats();
    } else if (g.g_main_list_box != null) {
        start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
    }
}

export fn on_login_clicked(widget: [*c]c.GtkWidget, window: c.gpointer) void {
    if (g.g_auth_token != null) {
        perform_logout();
        return;
    }

    var parent_window: [*c]c.GtkWindow = null;
    if (window != null) {
        parent_window = @ptrCast(@alignCast(window));
    } else if (widget != null) {
        const toplevel = c.gtk_widget_get_toplevel(widget);
        if (toplevel != null) parent_window = @ptrCast(@alignCast(toplevel));
    }

    const dlg = c.gtk_dialog_new_with_buttons(
        "Login",
        parent_window,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Passkey Login",
        @as(c_int, 4),
        "_Check Username",
        @as(c_int, 3),
        "_Register",
        @as(c_int, 2),
        "_Login",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const content = c.gtk_dialog_get_content_area(asDialog(dlg));
    const form = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(asGrid(form), 5);
    c.gtk_grid_set_column_spacing(asGrid(form), 5);
    c.gtk_container_set_border_width(container(form), 10);

    const user_entry = c.gtk_entry_new();
    const pass_entry = c.gtk_entry_new();
    c.gtk_entry_set_visibility(asEntry(pass_entry), FALSE);
    c.gtk_grid_attach(asGrid(form), c.gtk_label_new("Username:"), 0, 0, 1, 1);
    c.gtk_grid_attach(asGrid(form), user_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(asGrid(form), c.gtk_label_new("Password:"), 0, 1, 1, 1);
    c.gtk_grid_attach(asGrid(form), pass_entry, 1, 1, 1, 1);
    c.gtk_box_pack_start(asBox(content), form, TRUE, TRUE, 0);
    const entries: [*c][*c]c.GtkWidget = @ptrCast(@alignCast(c.g_malloc(@sizeOf([*c]c.GtkWidget) * 2)));
    if (entries == null) {
        c.gtk_widget_destroy(dlg);
        return;
    }
    entries[0] = user_entry;
    entries[1] = pass_entry;
    _ = c.g_signal_connect_data(dlg, "response", cb(on_login_response), @ptrCast(entries), null, c.G_CONNECT_DEFAULT);
    c.gtk_widget_show_all(form);
    c.gtk_widget_show(dlg);
}

export fn on_login_response(dialog: [*c]c.GtkDialog, response_id: c.gint, user_data: c.gpointer) void {
    if (response_id == c.GTK_RESPONSE_ACCEPT or response_id == 2 or response_id == 3 or response_id == 4) {
        const entries: [*c][*c]c.GtkWidget = @ptrCast(@alignCast(user_data));
        const username = c.gtk_entry_get_text(asEntry(entries[0]));
        const password = c.gtk_entry_get_text(asEntry(entries[1]));
        if (response_id == 4) {
            if (api.webauthn_fido2_is_enabled() != FALSE) {
                var passkey_response: [*c]c.gchar = null;
                defer c.g_free(passkey_response);
                var error_message: [*c]c.gchar = null;
                defer c.g_free(error_message);
                if (api.webauthn_fido2_login(&passkey_response, &error_message) != FALSE and applyLoginLikeResponse(passkey_response) != FALSE) {
                    update_login_ui();
                    if (g.g_main_list_box != null) start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
                } else {
                    showModalMessage(c.GTK_MESSAGE_ERROR, "Passkey login failed.", if (error_message != null) error_message else "The passkey response could not be accepted.");
                }
            } else {
                openWebPath(@ptrCast(@alignCast(dialog)), "/login");
            }
        } else if (response_id == 3) {
            const escaped = c.g_uri_escape_string(username, null, TRUE);
            defer c.g_free(escaped);
            const escaped_value: [*c]const c.gchar = if (escaped != null) escaped else lit("");
            const url = c.g_strdup_printf("%s?username=%s", constants.AUTH_USERNAME_AVAILABILITY_URL, escaped_value);
            defer c.g_free(url);
            var result: [*c]c.gchar = null;
            const ok = requestWithResponse(url, null, "GET", &result);
            defer c.g_free(result);
            if (ok != FALSE) {
                var parser: ?*c.JsonParser = null;
                defer if (parser != null) c.g_object_unref(parser);
                const root = parseRootObject(result, &parser);
                const available = jsonBool(root, "available");
                const error_message = extractErrorMessage(result);
                defer c.g_free(error_message);
                showModalMessage(if (available) c.GTK_MESSAGE_INFO else c.GTK_MESSAGE_ERROR, if (available) "Username available." else "Username unavailable.", error_message);
            } else {
                showModalMessage(c.GTK_MESSAGE_ERROR, "Username check failed.", null);
            }
        } else if (response_id == 2) {
            const builder = c.json_builder_new();
            defer c.g_object_unref(builder);
            _ = c.json_builder_begin_object(builder);
            _ = c.json_builder_set_member_name(builder, "username");
            _ = c.json_builder_add_string_value(builder, textOr(username));
            _ = c.json_builder_set_member_name(builder, "password");
            _ = c.json_builder_add_string_value(builder, textOr(password));
            _ = c.json_builder_end_object(builder);
            const payload = builderPayload(builder);
            defer c.g_free(payload);
            var result: [*c]c.gchar = null;
            const ok = requestWithResponse(constants.AUTH_REGISTER_PASSWORD_URL, payload, "POST", &result);
            defer c.g_free(result);
            if (ok != FALSE) {
                const error_message = extractErrorMessage(result);
                defer c.g_free(error_message);
                if (error_message == null) {
                    showModalMessage(c.GTK_MESSAGE_INFO, "Account created.", "You can now log in with this password.");
                } else {
                    showModalMessage(c.GTK_MESSAGE_ERROR, "Registration failed.", error_message);
                }
            } else {
                showModalMessage(c.GTK_MESSAGE_ERROR, "Registration failed.", "The registration request could not be sent.");
            }
        } else if (perform_login(username, password) != FALSE) {
            update_login_ui();
            if (g.g_main_list_box != null) start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Login failed. Check credentials.", null);
        }
    }
    c.g_free(user_data);
    c.gtk_widget_destroy(@ptrCast(@alignCast(dialog)));
}

export fn on_logout_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    perform_logout();
}

export fn on_compose_clicked(widget: [*c]c.GtkWidget, window: c.gpointer) void {
    unused(.{widget});
    const dialog = c.gtk_dialog_new_with_buttons(
        "Compose Tweet",
        @ptrCast(@alignCast(window)),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Tweet",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );

    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    c.gtk_container_set_border_width(container(asWidget(content)), 10);
    const text_view = c.gtk_text_view_new();
    c.gtk_text_view_set_wrap_mode(@ptrCast(@alignCast(text_view)), c.GTK_WRAP_WORD_CHAR);
    c.gtk_widget_set_size_request(text_view, 300, 150);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), text_view, TRUE, TRUE, 0);
    const file_box = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    c.gtk_widget_set_margin_top(file_box, 10);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), file_box, FALSE, FALSE, 0);
    const file_chooser = c.gtk_file_chooser_button_new("Attach File", c.GTK_FILE_CHOOSER_ACTION_OPEN);
    c.gtk_file_chooser_button_set_title(@ptrCast(@alignCast(file_chooser)), "Select Attachment");
    const media_filter = c.gtk_file_filter_new();
    c.gtk_file_filter_set_name(media_filter, "Media Files");
    c.gtk_file_filter_add_mime_type(media_filter, "image/png");
    c.gtk_file_filter_add_mime_type(media_filter, "image/jpeg");
    c.gtk_file_filter_add_mime_type(media_filter, "image/gif");
    c.gtk_file_filter_add_mime_type(media_filter, "image/webp");
    c.gtk_file_filter_add_mime_type(media_filter, "video/mp4");
    c.gtk_file_chooser_add_filter(@ptrCast(@alignCast(file_chooser)), media_filter);
    const all_filter = c.gtk_file_filter_new();
    c.gtk_file_filter_set_name(all_filter, "All Files");
    c.gtk_file_filter_add_pattern(all_filter, "*");
    c.gtk_file_chooser_add_filter(@ptrCast(@alignCast(file_chooser)), all_filter);
    c.gtk_box_pack_start(asBox(file_box), file_chooser, FALSE, FALSE, 0);
    const file_label = c.gtk_label_new("No file selected");
    c.gtk_widget_set_halign(file_label, c.GTK_ALIGN_START);
    c.gtk_widget_set_opacity(file_label, 0.6);
    c.gtk_box_pack_start(asBox(file_box), file_label, TRUE, TRUE, 0);

    const ctx: [*c]types.UploadContext = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.UploadContext))));
    if (ctx == null) {
        c.gtk_widget_destroy(dialog);
        return;
    }
    ctx.*.parent_dialog = dialog;
    ctx.*.file_label = file_label;
    connect(file_chooser, "file-set", onComposeFileSelected, ctx);
    const gif_btn = c.gtk_button_new_with_label("GIF");
    const photo_btn = c.gtk_button_new_with_label("Photo");
    c.g_object_set_data(@ptrCast(@alignCast(gif_btn)), "tenor", @ptrFromInt(1));
    c.g_object_set_data(@ptrCast(@alignCast(photo_btn)), "tenor", null);
    connect(gif_btn, "clicked", onMediaSearchClicked, ctx);
    connect(photo_btn, "clicked", onMediaSearchClicked, ctx);
    c.gtk_box_pack_start(asBox(file_box), gif_btn, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(file_box), photo_btn, FALSE, FALSE, 0);
    c.gtk_widget_show_all(dialog);
    connect(dialog, "response", on_compose_response, ctx);
}

export fn on_compose_response(dialog: [*c]c.GtkDialog, response_id: c.gint, user_data: c.gpointer) void {
    const ctx: [*c]types.UploadContext = @ptrCast(@alignCast(user_data));
    if (response_id == c.GTK_RESPONSE_ACCEPT) {
        const text_view: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(@ptrCast(@alignCast(dialog)), "text_view")));
        const content_text = textViewText(text_view);
        defer c.g_free(content_text);
        var media_url: [*c]c.gchar = null;
        defer c.g_free(media_url);
        var attachments: [*c]c.GList = null;
        defer if (attachments != null) c.g_list_free_full(attachments, api.free_attachment_payload);
        var upload_success = true;
        if (ctx != null and ctx.*.remote_url != null) {
            media_url = c.g_strdup(ctx.*.remote_url);
        } else if (ctx != null and ctx.*.file_path != null) {
            media_url = perform_media_upload(ctx.*.file_path);
            if (media_url == null) upload_success = false;
        }
        if (media_url != null) {
            const file_type: [*c]const c.gchar = if (ctx != null and ctx.*.remote_type != null) ctx.*.remote_type else if (ctx != null and ctx.*.file_type != null) ctx.*.file_type else lit("application/octet-stream");
            attachments = api.build_attachment_list(media_url, file_type);
        }
        var has_text = FALSE;
        if (content_text != null) {
            const trimmed = c.g_strdup(content_text);
            defer c.g_free(trimmed);
            _ = c.g_strstrip(trimmed);
            has_text = if (trimmed[0] != 0) TRUE else FALSE;
        }
        if (upload_success and (has_text != FALSE or attachments != null)) {
            if (perform_post_tweet(if (content_text != null) content_text else "", null, attachments) != FALSE) {
                if (g.g_main_list_box != null) start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
            } else {
                showModalMessage(c.GTK_MESSAGE_ERROR, "Failed to post tweet.", null);
            }
        } else if (!upload_success) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Failed to upload attachment.", null);
        }
    }
    if (ctx != null) {
        c.g_free(ctx.*.file_path);
        c.g_free(ctx.*.file_type);
        c.g_free(ctx.*.remote_url);
        c.g_free(ctx.*.remote_type);
        c.g_free(ctx);
    }
    c.gtk_widget_destroy(@ptrCast(@alignCast(dialog)));
}

export fn on_scroll_edge_reached(scrolled_window: [*c]c.GtkScrolledWindow, pos: c.GtkPositionType, user_data: c.gpointer) void {
    unused(.{ scrolled_window, pos, user_data });
    if (pos != c.GTK_POS_BOTTOM) return;
    var child = c.gtk_bin_get_child(@ptrCast(@alignCast(scrolled_window)));
    if (child != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(child)), c.gtk_viewport_get_type()) != FALSE) {
        child = c.gtk_bin_get_child(@ptrCast(@alignCast(child)));
    }
    if (child == null or c.g_type_check_instance_is_a(@ptrCast(@alignCast(child)), c.gtk_list_box_get_type()) == FALSE) return;
    if (child != g.g_main_list_box and child != g.g_profile_tweets_list and child != g.g_profile_replies_list and child != g.g_notifications_list) return;
    const list_box: [*c]c.GtkListBox = @ptrCast(@alignCast(child));
    if (c.g_object_get_data(@ptrCast(@alignCast(list_box)), "loading_more") != null) return;
    const last_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(list_box)), "last_id"));
    if (last_id == null) return;
    c.g_object_set_data(@ptrCast(@alignCast(list_box)), "loading_more", @ptrFromInt(1));
    if (child == g.g_notifications_list) {
        loadMoreNotifications(list_box, last_id);
    } else {
        load_more_tweets(list_box, last_id);
    }
}

export fn start_loading_notifications(list_box: [*c]c.GtkListBox) void {
    if (g.g_auth_token == null or list_box == null) return;
    c.g_mutex_lock(&load_notifications_mutex);
    active_notifications_request_id += 1;
    const current_request_id = active_notifications_request_id;
    c.g_mutex_unlock(&load_notifications_mutex);

    setListBoxStatus(list_box, "Loading notifications...");
    setObjectStringData(asWidget(list_box), "last_id", null);

    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.list_box = list_box;
    data.*.request_id = current_request_id;
    _ = c.g_thread_new("notification-loader", fetchNotificationsThread, data);
}

fn loadMoreNotifications(list_box: [*c]c.GtkListBox, before_id: [*c]const c.gchar) void {
    c.g_mutex_lock(&load_notifications_mutex);
    const current_request_id = active_notifications_request_id;
    c.g_mutex_unlock(&load_notifications_mutex);

    const loading = c.gtk_label_new("Loading more...");
    c.gtk_widget_show(loading);
    c.gtk_list_box_insert(list_box, loading, -1);

    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.list_box = list_box;
    data.*.request_id = current_request_id;
    data.*.is_append = TRUE;
    data.*.before_id = c.g_strdup(before_id);
    _ = c.g_thread_new("notification-loader", fetchNotificationsThread, data);
}

export fn start_loading_conversations(list_box: [*c]c.GtkListBox) void {
    if (g.g_auth_token == null or list_box == null) return;
    c.g_mutex_lock(&load_conversations_mutex);
    active_conversations_request_id += 1;
    const current_request_id = active_conversations_request_id;
    c.g_mutex_unlock(&load_conversations_mutex);

    setListBoxStatus(list_box, "Loading conversations...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.list_box = list_box;
    data.*.request_id = current_request_id;
    _ = c.g_thread_new("conversation-loader", fetchConversationsThread, data);
}
export fn start_loading_messages(list_box: [*c]c.GtkListBox, conversation_id: [*c]const c.gchar) void {
    if (g.g_auth_token == null or list_box == null or conversation_id == null) return;
    c.g_mutex_lock(&load_messages_mutex);
    active_messages_request_id += 1;
    const current_request_id = active_messages_request_id;
    c.g_mutex_unlock(&load_messages_mutex);

    setListBoxStatus(list_box, "Loading messages...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.list_box = list_box;
    data.*.request_id = current_request_id;
    data.*.conversation_id = c.g_strdup(conversation_id);
    _ = c.g_thread_new("message-loader", fetchMessagesThread, data);
}

fn onAdminJsonLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const kind = async_data.*.before_id;
    const ok = async_data.*.success != FALSE and async_data.*.json_data != null;
    if (c.g_strcmp0(kind, "stats") == 0) {
        if (ok) {
            const stats = api.parse_admin_stats(async_data.*.json_data);
            defer c.g_free(stats);
            labelSet(g.g_admin_stats_label, if (stats != null) stats else "Failed to load admin statistics.");
        } else {
            labelSet(g.g_admin_stats_label, "Failed to load admin statistics.");
        }
    } else if (c.g_strcmp0(kind, "users") == 0) {
        if (g.g_admin_users_list != null and ok) {
            const users = api.parse_admin_users(async_data.*.json_data);
            api.populate_user_list(listBox(g.g_admin_users_list), users);
            api.free_users(users);
        } else {
            labelSet(g.g_user_label, "Failed to load admin users.");
        }
    } else if (c.g_strcmp0(kind, "posts") == 0) {
        if (g.g_admin_posts_list != null and ok) {
            const tweets = api.parse_admin_posts(async_data.*.json_data);
            api.populate_tweet_list(listBox(g.g_admin_posts_list), tweets);
            api.free_tweets(tweets);
        }
    } else if (c.g_strcmp0(kind, "suspensions") == 0) {
        if (ok) populateAdminSuspensionsFromJson(async_data.*.json_data) else if (g.g_admin_suspensions_list != null) setListBoxStatus(listBox(g.g_admin_suspensions_list), "Failed to load suspensions.");
    } else if (c.g_strcmp0(kind, "reports") == 0) {
        if (ok) populateAdminReportsFromJson(async_data.*.json_data) else if (g.g_admin_reports_list != null) setListBoxStatus(listBox(g.g_admin_reports_list), "Failed to load reports.");
    } else if (c.g_strcmp0(kind, "logs") == 0) {
        if (ok) populateAdminLogsFromJson(async_data.*.json_data) else if (g.g_admin_logs_list != null) setListBoxStatus(listBox(g.g_admin_logs_list), "Failed to load moderation logs.");
    } else if (c.g_strcmp0(kind, "blocks") == 0) {
        if (ok) populateAdminBlocksFromJson(async_data.*.json_data) else if (g.g_admin_blocks_list != null) setListBoxStatus(listBox(g.g_admin_blocks_list), "Failed to load blocks.");
    } else if (c.g_strcmp0(kind, "emojis") == 0) {
        if (ok) populateAdminEmojisFromJson(async_data.*.json_data) else if (g.g_admin_emojis_list != null) setListBoxStatus(listBox(g.g_admin_emojis_list), "Failed to load emojis.");
    } else if (c.g_strcmp0(kind, "badges") == 0) {
        if (ok) populateAdminBadgesFromJson(async_data.*.json_data) else if (g.g_admin_badges_list != null) setListBoxStatus(listBox(g.g_admin_badges_list), "Failed to load badges.");
    } else if (c.g_strcmp0(kind, "dms") == 0) {
        if (ok) populateAdminDmsFromJson(async_data.*.json_data) else if (g.g_admin_dms_list != null) setListBoxStatus(listBox(g.g_admin_dms_list), "Failed to load conversations.");
    } else if (c.g_strcmp0(kind, "dm_messages") == 0) {
        if (ok) populateAdminDmMessagesFromJson(async_data.*.json_data) else if (g.g_admin_dm_admin_messages_list != null) setListBoxStatus(listBox(g.g_admin_dm_admin_messages_list), "Failed to load DM messages.");
    } else if (c.g_strcmp0(kind, "shop_products") == 0) {
        if (ok) populateAdminShopProductsFromJson(async_data.*.json_data) else if (g.g_admin_shop_products_list != null) setListBoxStatus(listBox(g.g_admin_shop_products_list), "Failed to load shop products.");
    } else if (c.g_strcmp0(kind, "shop_purchases") == 0) {
        if (ok) populateAdminShopPurchasesFromJson(async_data.*.json_data) else if (g.g_admin_shop_purchases_list != null) setListBoxStatus(listBox(g.g_admin_shop_purchases_list), "Failed to load shop purchases.");
    } else if (c.g_strcmp0(kind, "communities") == 0) {
        if (g.g_admin_communities_list != null and ok) {
            const communities = api.parse_communities(async_data.*.json_data);
            api.populate_community_list(listBox(g.g_admin_communities_list), communities);
            api.free_communities(communities);
        } else if (g.g_admin_communities_list != null) {
            api.populate_community_list(listBox(g.g_admin_communities_list), null);
        }
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchAdminJsonThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var response: [*c]c.gchar = null;
    async_data.*.success = adminRequestWithResponse(async_data.*.query, null, "GET", &response);
    if (async_data.*.success != FALSE) {
        async_data.*.json_data = response;
        response = null;
    }
    c.g_free(response);
    _ = c.g_idle_add(@ptrCast(&onAdminJsonLoaded), async_data);
    return null;
}

fn startAdminJsonLoader(thread_name: [*c]const c.gchar, kind: [*c]const c.gchar, url: [*c]const c.gchar) void {
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.query = c.g_strdup(url);
    data.*.before_id = c.g_strdup(kind);
    _ = c.g_thread_new(thread_name, fetchAdminJsonThread, data);
}

export fn start_loading_admin_stats() void {
    if (!hasAdminSession()) return;
    labelSet(g.g_admin_stats_label, "Loading admin statistics...");
    startAdminJsonLoader("admin-stats-loader", "stats", constants.ADMIN_STATS_URL);
}
export fn start_loading_admin_users(search: [*c]const c.gchar) void {
    const url = queryUrl(constants.ADMIN_USERS_URL, "search", search);
    defer c.g_free(url);
    startAdminJsonLoader("admin-users-loader", "users", url);
}
export fn start_loading_admin_posts(search: [*c]const c.gchar) void {
    const url = queryUrl(constants.ADMIN_POSTS_URL, "search", search);
    defer c.g_free(url);
    startAdminJsonLoader("admin-posts-loader", "posts", url);
}
export fn start_loading_admin_suspensions() void {
    if (g.g_admin_suspensions_list != null) setListBoxStatus(listBox(g.g_admin_suspensions_list), "Loading suspensions...");
    startAdminJsonLoader("admin-suspensions-loader", "suspensions", constants.ADMIN_SUSPENSIONS_URL);
}
export fn start_loading_admin_reports() void {
    if (g.g_admin_reports_list != null) setListBoxStatus(listBox(g.g_admin_reports_list), "Loading reports...");
    startAdminJsonLoader("admin-reports-loader", "reports", constants.ADMIN_REPORTS_URL);
}
export fn start_loading_admin_logs(search: [*c]const c.gchar) void {
    const url = queryUrl(constants.ADMIN_LOGS_URL, "search", search);
    defer c.g_free(url);
    if (g.g_admin_logs_list != null) setListBoxStatus(listBox(g.g_admin_logs_list), "Loading moderation logs...");
    startAdminJsonLoader("admin-logs-loader", "logs", url);
}
export fn start_loading_admin_blocks() void {
    if (g.g_admin_blocks_list != null) setListBoxStatus(listBox(g.g_admin_blocks_list), "Loading blocks...");
    startAdminJsonLoader("admin-blocks-loader", "blocks", constants.ADMIN_BLOCKS_URL);
}
export fn start_loading_admin_emojis() void {
    if (g.g_admin_emojis_list != null) setListBoxStatus(listBox(g.g_admin_emojis_list), "Loading emojis...");
    startAdminJsonLoader("admin-emojis-loader", "emojis", constants.ADMIN_EMOJIS_URL);
}
export fn start_loading_admin_badges() void {
    if (g.g_admin_badges_list != null) setListBoxStatus(listBox(g.g_admin_badges_list), "Loading badges...");
    startAdminJsonLoader("admin-badges-loader", "badges", constants.ADMIN_BADGES_URL);
}
export fn start_loading_admin_dms(search: [*c]const c.gchar) void {
    const base = if (search != null and search[0] != 0) constants.ADMIN_DMS_SEARCH_URL else constants.ADMIN_DMS_URL;
    const url = queryUrl(base, "username", search);
    defer c.g_free(url);
    if (g.g_admin_dms_list != null) setListBoxStatus(listBox(g.g_admin_dms_list), "Loading conversations...");
    startAdminJsonLoader("admin-dms-loader", "dms", url);
}
export fn start_loading_admin_dm_messages(conversation_id: [*c]const c.gchar) void {
    if (conversation_id == null) return;
    if (g.g_admin_dm_admin_messages_list != null) setObjectStringData(@ptrCast(@alignCast(g.g_admin_dm_admin_messages_list)), "conversation_id", conversation_id);
    const escaped = c.g_uri_escape_string(conversation_id, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf("%s/%s/messages", constants.ADMIN_DMS_URL, escaped);
    defer c.g_free(url);
    if (g.g_admin_dm_admin_messages_list != null) setListBoxStatus(listBox(g.g_admin_dm_admin_messages_list), "Loading messages...");
    startAdminJsonLoader("admin-dm-messages-loader", "dm_messages", url);
}
export fn start_loading_admin_shop(search: [*c]const c.gchar) void {
    const products_url = queryUrl(constants.ADMIN_SHOP_PRODUCTS_URL, "q", search);
    defer c.g_free(products_url);
    if (g.g_admin_shop_products_list != null) setListBoxStatus(listBox(g.g_admin_shop_products_list), "Loading shop products...");
    if (g.g_admin_shop_purchases_list != null) setListBoxStatus(listBox(g.g_admin_shop_purchases_list), "Loading purchases...");
    startAdminJsonLoader("admin-shop-products-loader", "shop_products", products_url);
    startAdminJsonLoader("admin-shop-purchases-loader", "shop_purchases", constants.ADMIN_SHOP_PURCHASES_URL);
}
export fn start_loading_admin_communities() void {
    if (g.g_admin_communities_list == null) return;
    api.populate_community_list(listBox(g.g_admin_communities_list), null);
    startAdminJsonLoader("admin-communities-loader", "communities", constants.COMMUNITIES_LIST_URL);
}
fn onFollowersLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.users != null and g.g_followers_list != null) {
        api.populate_user_list(listBox(g.g_followers_list), async_data.*.users);
        api.free_users(async_data.*.users);
        async_data.*.users = null;
    } else if (g.g_followers_list != null) {
        setListBoxStatus(listBox(g.g_followers_list), "Failed to load followers.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchFollowersThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.PROFILE_FOLLOWERS_URL, async_data.*.username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.users = api.parse_users(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onFollowersLoaded), async_data);
    return null;
}

export fn start_loading_followers(username: [*c]const c.gchar) void {
    if (g.g_followers_list == null) return;
    setListBoxStatus(listBox(g.g_followers_list), "Loading followers...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.username = c.g_strdup(username);
    _ = c.g_thread_new("followers-loader", fetchFollowersThread, data);
}

fn onFollowingLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.users != null and g.g_following_list != null) {
        api.populate_user_list(listBox(g.g_following_list), async_data.*.users);
        api.free_users(async_data.*.users);
        async_data.*.users = null;
    } else if (g.g_following_list != null) {
        setListBoxStatus(listBox(g.g_following_list), "Failed to load following.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchFollowingThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.PROFILE_FOLLOWING_URL, async_data.*.username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.users = api.parse_users(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onFollowingLoaded), async_data);
    return null;
}

export fn start_loading_following(username: [*c]const c.gchar) void {
    if (g.g_following_list == null) return;
    setListBoxStatus(listBox(g.g_following_list), "Loading following...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.username = c.g_strdup(username);
    _ = c.g_thread_new("following-loader", fetchFollowingThread, data);
}

fn onProfileMediaLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.tweets != null and g.g_profile_media_list != null) {
        const list = listBox(g.g_profile_media_list);
        api.populate_tweet_list(list, async_data.*.tweets);
        setTweetListLastId(list, async_data.*.tweets, false);
        api.free_tweets(async_data.*.tweets);
        async_data.*.tweets = null;
    } else if (g.g_profile_media_list != null) {
        api.populate_tweet_list(listBox(g.g_profile_media_list), null);
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchProfileMediaThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.PROFILE_MEDIA_URL, async_data.*.username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.tweets = api.parse_tweets(chunk.memory);
        async_data.*.success = if (async_data.*.tweets != null) TRUE else FALSE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onProfileMediaLoaded), async_data);
    return null;
}

export fn start_loading_profile_media(username: [*c]const c.gchar) void {
    if (g.g_profile_media_list == null or username == null) return;
    api.populate_tweet_list(listBox(g.g_profile_media_list), null);
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.username = c.g_strdup(username);
    _ = c.g_thread_new("profile-media-loader", fetchProfileMediaThread, data);
}

fn onProfileHighlightsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and g.g_profile_highlights_list != null) {
        api.populate_tweet_list(listBox(g.g_profile_highlights_list), async_data.*.tweets);
        api.free_tweets(async_data.*.tweets);
        async_data.*.tweets = null;
    } else if (g.g_profile_highlights_list != null) {
        api.populate_tweet_list(listBox(g.g_profile_highlights_list), null);
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchProfileHighlightsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.PROFILE_HIGHLIGHTS_URL, async_data.*.username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.tweets = api.parse_tweets(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onProfileHighlightsLoaded), async_data);
    return null;
}

export fn start_loading_profile_highlights(username: [*c]const c.gchar) void {
    if (g.g_profile_highlights_list == null or username == null) return;
    api.populate_tweet_list(listBox(g.g_profile_highlights_list), null);
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.username = c.g_strdup(username);
    _ = c.g_thread_new("profile-highlights-loader", fetchProfileHighlightsThread, data);
}

fn onProfileMutualsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.users != null and g.g_profile_mutuals_list != null) {
        api.populate_user_list(listBox(g.g_profile_mutuals_list), async_data.*.users);
        api.free_users(async_data.*.users);
        async_data.*.users = null;
    } else if (g.g_profile_mutuals_list != null) {
        api.populate_user_list(listBox(g.g_profile_mutuals_list), null);
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchProfileMutualsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.PROFILE_MUTUALS_URL, async_data.*.username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.users = api.parse_users(chunk.memory);
        async_data.*.success = if (async_data.*.users != null) TRUE else FALSE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onProfileMutualsLoaded), async_data);
    return null;
}

export fn start_loading_profile_mutuals(username: [*c]const c.gchar) void {
    if (g.g_profile_mutuals_list == null or username == null or g.g_auth_token == null) return;
    api.populate_user_list(listBox(g.g_profile_mutuals_list), null);
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.username = c.g_strdup(username);
    _ = c.g_thread_new("profile-mutuals-loader", fetchProfileMutualsThread, data);
}

fn onProfileUserListLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.list_box != null) {
        if (async_data.*.success != FALSE) {
            api.populate_user_list(async_data.*.list_box, async_data.*.users);
        } else {
            api.populate_user_list(async_data.*.list_box, null);
        }
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchProfileFollowersYouKnowThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.PROFILE_FOLLOWERS_YOU_KNOW_URL, async_data.*.username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.users = api.parse_users(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onProfileUserListLoaded), async_data);
    return null;
}

export fn start_loading_profile_followers_you_know(username: [*c]const c.gchar) void {
    if (g.g_profile_followers_you_know_list == null or username == null or g.g_auth_token == null) return;
    api.populate_user_list(listBox(g.g_profile_followers_you_know_list), null);
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.username = c.g_strdup(username);
    data.*.list_box = listBox(g.g_profile_followers_you_know_list);
    _ = c.g_thread_new("profile-followers-you-know-loader", fetchProfileFollowersYouKnowThread, data);
}

fn fetchProfileAffiliatesThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.PROFILE_AFFILIATES_URL, async_data.*.username);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.users = api.parse_users(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onProfileUserListLoaded), async_data);
    return null;
}

export fn start_loading_profile_affiliates(username: [*c]const c.gchar) void {
    if (g.g_profile_affiliates_list == null or username == null) return;
    api.populate_user_list(listBox(g.g_profile_affiliates_list), null);
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.username = c.g_strdup(username);
    data.*.list_box = listBox(g.g_profile_affiliates_list);
    _ = c.g_thread_new("profile-affiliates-loader", fetchProfileAffiliatesThread, data);
}
fn onBookmarksLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.tweets != null) {
        api.populate_tweet_list(async_data.*.list_box, async_data.*.tweets);
        api.free_tweets(async_data.*.tweets);
        async_data.*.tweets = null;
    } else {
        setListBoxStatus(async_data.*.list_box, if (async_data.*.success != FALSE) "No bookmarks yet." else "Failed to load bookmarks.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchBookmarksThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(constants.BOOKMARKS_LIST_URL, &chunk, null, "GET") != FALSE) {
        async_data.*.tweets = api.parse_tweets(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onBookmarksLoaded), async_data);
    return null;
}

export fn start_loading_bookmarks(list_box: [*c]c.GtkListBox) void {
    if (g.g_auth_token == null or list_box == null) return;
    setListBoxStatus(list_box, "Loading bookmarks...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.list_box = list_box;
    _ = c.g_thread_new("bookmarks-loader", fetchBookmarksThread, data);
}

fn onListsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const owned = async_data.*.lists;
    const followed: [*c]c.GList = @ptrCast(@alignCast(async_data.*.users));
    async_data.*.lists = null;
    async_data.*.users = null;
    if (async_data.*.success != FALSE) {
        populateListsBox(g.g_lists_owned_list, owned, false);
        populateListsBox(g.g_lists_followed_list, followed, true);
    } else {
        if (g.g_lists_owned_list != null) setListBoxStatus(listBox(g.g_lists_owned_list), "Failed to load lists.");
        if (g.g_lists_followed_list != null) setListBoxStatus(listBox(g.g_lists_followed_list), "Failed to load lists.");
    }
    api.free_tweeta_lists(owned);
    api.free_tweeta_lists(followed);
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchListsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    var owned: [*c]c.GList = null;
    var followed: [*c]c.GList = null;
    if (api.fetch_url(constants.LISTS_URL, &chunk, null, "GET") != FALSE and api.parse_lists_response(chunk.memory, &owned, &followed) != FALSE) {
        async_data.*.success = TRUE;
        async_data.*.lists = owned;
        async_data.*.users = @ptrCast(followed);
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onListsLoaded), async_data);
    return null;
}

export fn start_loading_lists() void {
    if (g.g_auth_token == null) return;
    if (g.g_lists_owned_list != null) setListBoxStatus(listBox(g.g_lists_owned_list), "Loading lists...");
    if (g.g_lists_followed_list != null) setListBoxStatus(listBox(g.g_lists_followed_list), "Loading lists...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    _ = c.g_thread_new("lists-loader", fetchListsThread, data);
}
fn buildCommunityDetailsText(community: [*c]const types.Community) [*c]c.gchar {
    const details = c.g_string_new(null);
    if (community != null) {
        if (community.*.access_mode != null and community.*.access_mode[0] != 0) {
            _ = c.g_string_append(details, community.*.access_mode);
        }
        if (community.*.member_count > 0) {
            if (details.*.len > 0) _ = c.g_string_append(details, " | ");
            c.g_string_append_printf(details, "%d member%s", community.*.member_count, if (community.*.member_count == 1) lit("") else lit("s"));
        }
        if (community.*.description != null and community.*.description[0] != 0) {
            if (details.*.len > 0) _ = c.g_string_append(details, " | ");
            _ = c.g_string_append(details, community.*.description);
        }
    }
    return c.g_string_free(details, FALSE);
}

fn onCommunitiesLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    clearListBox(async_data.*.list_box);
    if (async_data.*.success != FALSE) {
        api.populate_community_list(async_data.*.list_box, async_data.*.communities);
        api.free_communities(async_data.*.communities);
        async_data.*.communities = null;
    } else {
        _ = appendListLabel(async_data.*.list_box, "Failed to load communities");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchCommunitiesThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = if (async_data.*.query != null and async_data.*.query[0] != 0) c.g_strdup(async_data.*.query) else c.g_strdup(constants.COMMUNITIES_LIST_URL);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.communities = api.parse_communities(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onCommunitiesLoaded), async_data);
    return null;
}

fn startLoadingCommunitiesFromUrl(list_box: [*c]c.GtkListBox, url: [*c]const c.gchar) void {
    if (list_box == null) return;
    setListBoxStatus(list_box, "Loading communities...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.list_box = list_box;
    if (url != null) data.*.query = c.g_strdup(url);
    _ = c.g_thread_new("communities-loader", fetchCommunitiesThread, data);
}

export fn start_loading_communities(list_box: [*c]c.GtkListBox) void {
    startLoadingCommunitiesFromUrl(list_box, null);
}
export fn start_loading_communities_search(list_box: [*c]c.GtkListBox, query: [*c]const c.gchar) void {
    if (query == null or cstr.len(query) < 2) {
        start_loading_communities(list_box);
        return;
    }
    const escaped = c.g_uri_escape_string(query, null, TRUE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf("%s?q=%s", constants.COMMUNITIES_SEARCH_URL, escaped);
    defer c.g_free(url);
    startLoadingCommunitiesFromUrl(list_box, url);
}
export fn start_loading_communities_trending(list_box: [*c]c.GtkListBox) void {
    startLoadingCommunitiesFromUrl(list_box, constants.COMMUNITIES_TRENDING_URL);
}
export fn start_loading_communities_recommended(list_box: [*c]c.GtkListBox) void {
    startLoadingCommunitiesFromUrl(list_box, constants.COMMUNITIES_RECOMMENDED_URL);
}
export fn start_loading_my_communities(list_box: [*c]c.GtkListBox) void {
    startLoadingCommunitiesFromUrl(list_box, constants.COMMUNITIES_MY_URL);
}

fn onCommunityTweetsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.tweets != null) {
        api.populate_tweet_list(async_data.*.list_box, async_data.*.tweets);
        api.free_tweets(async_data.*.tweets);
        async_data.*.tweets = null;
    } else {
        setListBoxStatus(async_data.*.list_box, "Failed to load community tweets.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchCommunityTweetsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.COMMUNITY_TWEETS_URL, async_data.*.community_id);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.tweets = api.parse_tweets(chunk.memory);
        async_data.*.success = TRUE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onCommunityTweetsLoaded), async_data);
    return null;
}

export fn start_loading_community_tweets(list_box: [*c]c.GtkListBox, community_id: [*c]const c.gchar) void {
    if (g.g_auth_token == null or list_box == null or community_id == null) return;
    setListBoxStatus(list_box, "Loading community tweets...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data != null) {
        data.*.list_box = list_box;
        data.*.community_id = c.g_strdup(community_id);
        _ = c.g_thread_new("community-tweets-loader", fetchCommunityTweetsThread, data);
    }
    start_loading_community_details(community_id);
}

fn onCommunityMembersLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.users != null) {
        api.populate_user_list(async_data.*.list_box, async_data.*.users);
        api.free_users(async_data.*.users);
        async_data.*.users = null;
    } else {
        api.populate_user_list(async_data.*.list_box, null);
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchCommunityMembersThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const base_url = c.g_strdup_printf(constants.COMMUNITY_MEMBERS_URL, async_data.*.community_id);
    defer c.g_free(base_url);
    const url = c.g_strdup_printf("%s?limit=100", base_url);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        async_data.*.users = api.parse_users(chunk.memory);
        async_data.*.success = if (async_data.*.users != null) TRUE else FALSE;
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onCommunityMembersLoaded), async_data);
    return null;
}

export fn start_loading_community_members(community_id: [*c]const c.gchar, list_box: [*c]c.GtkListBox) void {
    if (community_id == null or list_box == null) return;
    api.populate_user_list(list_box, null);
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.community_id = c.g_strdup(community_id);
    data.*.list_box = list_box;
    _ = c.g_thread_new("community-members-loader", fetchCommunityMembersThread, data);
}

fn onCommunityDetailsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.communities != null) {
        const community: [*c]types.Community = @ptrCast(@alignCast(async_data.*.communities.*.data));
        const details = buildCommunityDetailsText(community);
        defer c.g_free(details);
        labelSet(g.g_community_title_label, if (community.*.name != null) community.*.name else "Community");
        labelSet(g.g_community_details_label, details);
        if (g.g_community_tweets_list != null) {
            const list_widget: [*c]c.GtkWidget = @ptrCast(@alignCast(g.g_community_tweets_list));
            setObjectStringData(list_widget, "community_id", community.*.id);
            setObjectStringData(list_widget, "community_name", community.*.name);
            setObjectStringData(list_widget, "community_description", community.*.description);
            setObjectStringData(list_widget, "community_rules", community.*.rules);
            setObjectStringData(list_widget, "community_access_mode", community.*.access_mode);
        }
    } else {
        labelSet(g.g_community_title_label, "Community");
        labelSet(g.g_community_details_label, "");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchCommunityDetailsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const url = c.g_strdup_printf(constants.COMMUNITY_DETAILS_URL, async_data.*.community_id);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        const community = api.parse_community_details(chunk.memory);
        if (community != null) {
            async_data.*.communities = c.g_list_append(null, community);
            async_data.*.success = TRUE;
        } else {
            async_data.*.success = FALSE;
        }
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onCommunityDetailsLoaded), async_data);
    return null;
}

export fn start_loading_community_details(community_id: [*c]const c.gchar) void {
    if (community_id == null) return;
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.community_id = c.g_strdup(community_id);
    _ = c.g_thread_new("community-details-loader", fetchCommunityDetailsThread, data);
}

export fn start_loading_account_requests() void {
    if (g.g_auth_token == null or g.g_follow_requests_list == null or g.g_affiliate_requests_list == null) return;
    setListBoxStatus(listBox(g.g_follow_requests_list), "Loading follow requests...");
    setListBoxStatus(listBox(g.g_affiliate_requests_list), "Loading affiliate requests...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    _ = c.g_thread_new("account-requests-loader", fetchAccountRequestsThread, data);
}

fn onAccountRequestsLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE) {
        populateAccountRequestList(g.g_follow_requests_list, async_data.*.json_data, "No follow requests.", "follow");
        populateAccountRequestList(g.g_affiliate_requests_list, async_data.*.query, "No affiliate requests.", "affiliate");
    } else {
        if (g.g_follow_requests_list != null) setListBoxStatus(listBox(g.g_follow_requests_list), "Failed to load follow requests.");
        if (g.g_affiliate_requests_list != null) setListBoxStatus(listBox(g.g_affiliate_requests_list), "Failed to load affiliate requests.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchAccountRequestsThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const follow_chunk = fetchGet(constants.PROFILE_FOLLOW_REQUESTS_URL);
    const affiliate_chunk = fetchGet(constants.PROFILE_AFFILIATE_REQUESTS_URL);
    async_data.*.success = if (follow_chunk.memory != null and affiliate_chunk.memory != null) TRUE else FALSE;
    if (follow_chunk.memory != null) async_data.*.json_data = c.g_strdup(follow_chunk.memory);
    if (affiliate_chunk.memory != null) async_data.*.query = c.g_strdup(affiliate_chunk.memory);
    c.g_free(follow_chunk.memory);
    c.g_free(affiliate_chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onAccountRequestsLoaded), async_data);
    return null;
}

export fn start_loading_my_shop() void {
    if (g.g_auth_token == null or g.g_shop_products_list == null) return;
    const username = getUsernameSafe();
    if (username == null) return;
    setListBoxStatus(listBox(g.g_shop_products_list), "Loading shop products...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) {
        c.g_free(username);
        return;
    }
    data.*.username = username;
    _ = c.g_thread_new("my-shop-loader", fetchMyShopThread, data);
}

fn onMyShopLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (g.g_shop_products_list != null) {
        if (async_data.*.success != FALSE) {
            populateShopProducts(g.g_shop_products_list, async_data.*.json_data, true);
        } else {
            setListBoxStatus(listBox(g.g_shop_products_list), "Failed to load shop products.");
        }
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchMyShopThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const escaped = c.g_uri_escape_string(async_data.*.username, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf(constants.SHOP_USER_URL, escaped);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    async_data.*.success = api.fetch_url(url, &chunk, null, "GET");
    if (async_data.*.success != FALSE) async_data.*.json_data = c.g_strdup(chunk.memory);
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onMyShopLoaded), async_data);
    return null;
}
export fn start_loading_for_you_interests() void {
    if (g.g_auth_token == null or g.g_for_you_interests_list == null) return;
    setListBoxStatus(listBox(g.g_for_you_interests_list), "Loading interests...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    _ = c.g_thread_new("for-you-interests-loader", fetchForYouInterestsThread, data);
}
export fn start_loading_muted_words() void {
    if (g.g_auth_token == null or g.g_muted_words_list == null) return;
    setListBoxStatus(listBox(g.g_muted_words_list), "Loading muted words...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    _ = c.g_thread_new("muted-words-loader", fetchMutedWordsThread, data);
}
export fn start_loading_muted_conversations() void {
    if (g.g_auth_token == null or g.g_muted_conversations_list == null) return;
    setListBoxStatus(listBox(g.g_muted_conversations_list), "Loading muted conversations...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    _ = c.g_thread_new("muted-conversations-loader", fetchMutedConversationsThread, data);
}
export fn start_loading_delegates() void {
    if (g.g_auth_token == null or g.g_delegates_list == null) return;
    if (g.g_delegates_list != null) setListBoxStatus(listBox(g.g_delegates_list), "Loading delegates...");
    if (g.g_delegations_list != null) setListBoxStatus(listBox(g.g_delegations_list), "Loading delegations...");
    if (g.g_delegate_invitations_list != null) setListBoxStatus(listBox(g.g_delegate_invitations_list), "Loading invitations...");
    if (g.g_delegate_sent_list != null) setListBoxStatus(listBox(g.g_delegate_sent_list), "Loading sent invitations...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    _ = c.g_thread_new("delegates-loader", fetchDelegatesThread, data);
}

fn onDelegatesLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE) {
        populateDelegates(async_data.*.json_data);
    } else {
        if (g.g_delegates_list != null) setListBoxStatus(listBox(g.g_delegates_list), "Failed to load delegates.");
        if (g.g_delegations_list != null) setListBoxStatus(listBox(g.g_delegations_list), "Failed to load delegations.");
        if (g.g_delegate_invitations_list != null) setListBoxStatus(listBox(g.g_delegate_invitations_list), "Failed to load invitations.");
        if (g.g_delegate_sent_list != null) setListBoxStatus(listBox(g.g_delegate_sent_list), "Failed to load sent invitations.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchDelegatesThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    async_data.*.success = api.fetch_url(constants.DELEGATES_SUMMARY_URL, &chunk, null, "GET");
    if (async_data.*.success != FALSE) async_data.*.json_data = c.g_strdup(chunk.memory);
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onDelegatesLoaded), async_data);
    return null;
}
export fn start_loading_scheduled_posts() void {
    if (g.g_auth_token == null or g.g_scheduled_posts_list == null) return;
    setListBoxStatus(listBox(g.g_scheduled_posts_list), "Loading scheduled posts...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    _ = c.g_thread_new("scheduled-posts-loader", fetchScheduledPostsThread, data);
}
export fn start_loading_explore() void {
    if (g.g_explore_list == null) return;
    const index = if (g.g_explore_category_combo != null) c.gtk_combo_box_get_active(@ptrCast(@alignCast(g.g_explore_category_combo))) else 0;
    setListBoxStatus(listBox(g.g_explore_list), "Loading Explore...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.query = c.g_strdup(exploreUrlForIndex(index));
    _ = c.g_thread_new("explore-loader", fetchExploreThread, data);
}

fn populateExploreFromJson(json_data: [*c]const c.gchar) void {
    if (g.g_explore_list == null) return;
    clearListBox(listBox(g.g_explore_list));
    if (json_data == null) {
        _ = appendListLabel(listBox(g.g_explore_list), "Failed to load Explore.");
        return;
    }
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser);
    if (root == null) {
        _ = appendListLabel(listBox(g.g_explore_list), "Failed to parse Explore response.");
    } else if (jsonArrayMember(root, "posts") != null) {
        const tweets = api.parse_tweets(json_data);
        api.populate_tweet_list(@ptrCast(@alignCast(g.g_explore_list)), tweets);
        api.free_tweets(tweets);
    } else if (jsonArrayMember(root, "users") != null) {
        const users = api.parse_users(json_data);
        api.populate_user_list(@ptrCast(@alignCast(g.g_explore_list)), users);
        api.free_users(users);
    } else if (jsonArrayMember(root, "hashtags") != null) {
        populateExploreHashtags(jsonArrayMember(root, "hashtags"));
    } else if (jsonObjectMember(root, "stats") != null) {
        populateExploreStats(jsonObjectMember(root, "stats"));
    } else if (jsonObjectMember(root, "digest") != null) {
        populateExploreDigest(jsonObjectMember(root, "digest"));
    } else if (jsonObjectMember(root, "leaderboard") != null) {
        populateExploreLeaderboard(jsonObjectMember(root, "leaderboard"));
    } else {
        _ = appendListLabel(listBox(g.g_explore_list), "No Explore results.");
    }
}

fn onExploreLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE) populateExploreFromJson(async_data.*.json_data) else populateExploreFromJson(null);
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchExploreThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    async_data.*.success = api.fetch_url(async_data.*.query, &chunk, null, "GET");
    if (async_data.*.success != FALSE) async_data.*.json_data = c.g_strdup(chunk.memory);
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onExploreLoaded), async_data);
    return null;
}

fn articleByline(article: [*c]types.Tweet) [*c]c.gchar {
    return c.g_strdup_printf(
        "@%s%s%s",
        if (article.*.author_username != null) article.*.author_username else lit("unknown"),
        if (article.*.created_at != null) lit(" · ") else lit(""),
        if (article.*.created_at != null) article.*.created_at else lit(""),
    );
}

fn createArticleRow(article: [*c]types.Tweet) [*c]c.GtkWidget {
    const event_box = c.gtk_event_box_new();
    const boxw = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    c.gtk_container_set_border_width(container(boxw), 10);

    const escaped_title = c.g_markup_escape_text(if (article.*.article_title != null) article.*.article_title else lit("Untitled article"), -1);
    defer c.g_free(escaped_title);
    const title_markup = c.g_strdup_printf("<b>%s</b>", if (escaped_title != null) escaped_title else lit(""));
    defer c.g_free(title_markup);
    const title_label = c.gtk_label_new(null);
    c.gtk_label_set_markup(@ptrCast(@alignCast(title_label)), title_markup);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(title_label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(title_label)), TRUE);

    const byline_text = articleByline(article);
    defer c.g_free(byline_text);
    const byline_label = c.gtk_label_new(byline_text);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(byline_label)), 0.0);
    c.gtk_widget_set_opacity(byline_label, 0.7);

    const excerpt_label = c.gtk_label_new(textOr(article.*.content));
    c.gtk_label_set_xalign(@ptrCast(@alignCast(excerpt_label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(excerpt_label)), TRUE);

    c.gtk_box_pack_start(asBox(boxw), title_label, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), byline_label, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), excerpt_label, FALSE, FALSE, 0);
    c.gtk_container_add(container(event_box), boxw);
    setObjectStringData(event_box, "article_id", article.*.id);
    connect(event_box, "button-press-event", onArticleRowActivated, null);
    return event_box;
}

fn parseArticleDetail(json_data: [*c]const c.gchar) [*c]types.Tweet {
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(json_data, &parser) orelse return null;
    const article_node = if (c.json_object_has_member(root, "article") != FALSE) c.json_object_get_member(root, "article") else null;
    if (article_node == null or !c.JSON_NODE_HOLDS_OBJECT(article_node)) return null;

    const generator = c.json_generator_new();
    defer c.g_object_unref(generator);
    const node = c.json_node_copy(article_node);
    defer c.json_node_free(node);
    c.json_generator_set_root(generator, node);
    const article_json = c.json_generator_to_data(generator, null);
    defer c.g_free(article_json);
    const wrapped = c.g_strdup_printf("{\"articles\":[%s]}", article_json);
    defer c.g_free(wrapped);
    const articles = api.parse_tweets(wrapped);
    if (articles == null) return null;
    const article: [*c]types.Tweet = @ptrCast(@alignCast(articles.*.data));
    articles.*.data = null;
    c.g_list_free_full(articles, api.free_tweet);
    return article;
}

fn showArticleReader(parent: [*c]c.GtkWidget, article: [*c]types.Tweet) void {
    const toplevel = if (parent != null) c.gtk_widget_get_toplevel(parent) else null;
    const dialog = c.gtk_dialog_new_with_buttons(
        if (article.*.article_title != null) article.*.article_title else lit("Article"),
        if (toplevel != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(toplevel)), c.gtk_window_get_type()) != FALSE) @as([*c]c.GtkWindow, @ptrCast(@alignCast(toplevel))) else null,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        c.GTK_RESPONSE_CLOSE,
        @as(?*anyopaque, null),
    );
    c.gtk_window_set_default_size(@ptrCast(@alignCast(dialog)), 720, 640);
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    const scroll = c.gtk_scrolled_window_new(null, null);
    const boxw = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 8);
    c.gtk_container_set_border_width(container(boxw), 12);

    const escaped_title = c.g_markup_escape_text(if (article.*.article_title != null) article.*.article_title else lit("Untitled article"), -1);
    defer c.g_free(escaped_title);
    const title_markup = c.g_strdup_printf("<span size='large' weight='bold'>%s</span>", if (escaped_title != null) escaped_title else lit(""));
    defer c.g_free(title_markup);
    const title_label = c.gtk_label_new(null);
    c.gtk_label_set_markup(@ptrCast(@alignCast(title_label)), title_markup);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(title_label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(title_label)), TRUE);

    const byline_text = articleByline(article);
    defer c.g_free(byline_text);
    const byline_label = c.gtk_label_new(byline_text);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(byline_label)), 0.0);
    c.gtk_widget_set_opacity(byline_label, 0.7);

    const body_label = c.gtk_label_new(if (article.*.article_body_markdown != null) article.*.article_body_markdown else textOr(article.*.content));
    c.gtk_label_set_xalign(@ptrCast(@alignCast(body_label)), 0.0);
    c.gtk_label_set_yalign(@ptrCast(@alignCast(body_label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(body_label)), TRUE);
    c.gtk_label_set_selectable(@ptrCast(@alignCast(body_label)), TRUE);

    c.gtk_box_pack_start(asBox(boxw), title_label, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), byline_label, FALSE, FALSE, 0);
    c.gtk_box_pack_start(asBox(boxw), body_label, TRUE, TRUE, 0);
    c.gtk_container_add(container(scroll), boxw);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), scroll, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}

fn openArticle(article_id: [*c]const c.gchar, parent: [*c]c.GtkWidget) void {
    if (article_id == null or article_id[0] == 0) return;
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    data.*.query = c.g_strdup(article_id);
    data.*.list_box = if (parent != null) @ptrCast(@alignCast(parent)) else @ptrCast(@alignCast(g.g_articles_list));
    _ = c.g_thread_new("article-detail-loader", fetchArticleDetailThread, data);
}

fn onArticleDetailLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (async_data.*.success != FALSE and async_data.*.tweets != null) {
        const article: [*c]types.Tweet = @ptrCast(@alignCast(async_data.*.tweets.*.data));
        showArticleReader(@ptrCast(@alignCast(async_data.*.list_box)), article);
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Article unavailable.", "The article could not be loaded.");
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchArticleDetailThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    const article_id = async_data.*.query;
    const url = c.g_strdup_printf(constants.ARTICLE_DETAILS_URL, article_id);
    defer c.g_free(url);
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        const article = parseArticleDetail(chunk.memory);
        if (article != null) {
            async_data.*.tweets = c.g_list_append(null, article);
            async_data.*.success = TRUE;
        } else {
            async_data.*.success = FALSE;
        }
    } else {
        async_data.*.success = FALSE;
    }
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onArticleDetailLoaded), async_data);
    return null;
}

fn populateArticlesFromJson(list: [*c]c.GtkListBox, json_data: [*c]const c.gchar) void {
    clearListBox(list);
    if (json_data == null) {
        _ = appendListLabel(list, "Failed to load articles.");
        return;
    }
    const articles = api.parse_tweets(json_data);
    defer api.free_tweets(articles);
    if (articles == null) {
        _ = appendListLabel(list, "No articles yet.");
        return;
    }
    var item = articles;
    while (item != null) : (item = item.*.next) {
        const article: [*c]types.Tweet = @ptrCast(@alignCast(item.*.data));
        if (article == null) continue;
        const row = createArticleRow(article);
        c.gtk_widget_show_all(row);
        c.gtk_list_box_insert(list, row, -1);
    }
}

fn onArticlesLoaded(data: c.gpointer) callconv(.c) c.gboolean {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    if (g.g_articles_list != null) {
        if (async_data.*.success != FALSE) {
            populateArticlesFromJson(listBox(g.g_articles_list), async_data.*.json_data);
        } else {
            populateArticlesFromJson(listBox(g.g_articles_list), null);
        }
    }
    freeAsyncData(async_data);
    return FALSE;
}

fn fetchArticlesThread(data: c.gpointer) callconv(.c) c.gpointer {
    const async_data: [*c]types.AsyncData = @ptrCast(@alignCast(data));
    var chunk = types.MemoryStruct{ .memory = null, .size = 0 };
    async_data.*.success = api.fetch_url(constants.ARTICLES_URL, &chunk, null, "GET");
    if (async_data.*.success != FALSE) async_data.*.json_data = c.g_strdup(chunk.memory);
    c.g_free(chunk.memory);
    _ = c.g_idle_add(@ptrCast(&onArticlesLoaded), async_data);
    return null;
}

fn loadArticlesSync(list: [*c]c.GtkListBox) void {
    const chunk = fetchGet(constants.ARTICLES_URL);
    defer c.g_free(chunk.memory);
    if (chunk.memory != null) {
        populateArticlesFromJson(list, chunk.memory);
    } else {
        populateArticlesFromJson(list, null);
    }
}

fn onArticleRowActivated(widget: [*c]c.GtkWidget, event: [*c]c.GdkEventButton, user_data: c.gpointer) callconv(.c) c.gboolean {
    unused(.{ event, user_data });
    openArticle(widgetStringData(widget, "article_id"), widget);
    return TRUE;
}

export fn start_loading_articles() void {
    if (g.g_articles_list == null) return;
    const list = listBox(g.g_articles_list);
    setListBoxStatus(list, "Loading articles...");
    const data: [*c]types.AsyncData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.AsyncData))));
    if (data == null) return;
    _ = c.g_thread_new("articles-loader", fetchArticlesThread, data);
}

export fn on_admin_dm_conversation_selected(box: [*c]c.GtkListBox, row: [*c]c.GtkListBoxRow, user_data: c.gpointer) void {
    unused(.{ box, user_data });
    if (row == null) return;
    const child = c.gtk_bin_get_child(@ptrCast(@alignCast(row)));
    const conversation_id = objectStringData(child, "conversation_id");
    if (conversation_id != null) start_loading_admin_dm_messages(conversation_id);
}
export fn on_admin_upload_emoji_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (!hasAdminSession()) return;
    const chooser = c.gtk_file_chooser_dialog_new("Select Emoji Image", null, c.GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", c.GTK_RESPONSE_CANCEL, "_Open", c.GTK_RESPONSE_ACCEPT, @as(?*anyopaque, null));
    var filename: [*c]c.gchar = null;
    if (c.gtk_dialog_run(@ptrCast(@alignCast(chooser))) == c.GTK_RESPONSE_ACCEPT) {
        filename = c.gtk_file_chooser_get_filename(@ptrCast(@alignCast(chooser)));
    }
    c.gtk_widget_destroy(chooser);
    defer c.g_free(filename);
    if (filename == null) return;

    const dialog = c.gtk_dialog_new_with_buttons(
        "Emoji Name",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Create",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const name_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(name_entry)), "emoji_name");
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), name_entry, FALSE, FALSE, 10);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT and entryTextOrEmpty(name_entry)[0] != 0) {
        const upload_url = performAdminMediaUpload(filename);
        defer c.g_free(upload_url);
        if (upload_url == null) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Emoji upload failed.", "The image could not be uploaded.");
            c.gtk_widget_destroy(dialog);
            return;
        }
        const escaped_name = c.g_strescape(entryTextOrEmpty(name_entry), null);
        defer c.g_free(escaped_name);
        const payload = c.g_strdup_printf("{\"name\":\"%s\",\"file_url\":\"%s\"}", escaped_name, upload_url);
        defer c.g_free(payload);
        var response: [*c]c.gchar = null;
        const ok = adminRequestWithResponse(constants.ADMIN_EMOJIS_URL, payload, "POST", &response);
        defer c.g_free(response);
        if (ok == FALSE) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Emoji creation failed.", "The admin API request could not be sent.");
        } else if (extractErrorMessage(response)) |error_message| {
            defer c.g_free(error_message);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Emoji creation failed.", error_message);
        } else {
            start_loading_admin_emojis();
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_admin_create_badge_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (!hasAdminSession()) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Create Badge",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Create",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const name_entry = c.gtk_entry_new();
    const image_entry = c.gtk_entry_new();
    const color_entry = c.gtk_entry_new();
    const desc_entry = c.gtk_entry_new();
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Name:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), name_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Image URL:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), image_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Color:"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), color_entry, 1, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Description:"), 0, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), desc_entry, 1, 3, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const escaped_name = c.g_strescape(entryTextOrEmpty(name_entry), null);
        const escaped_image = c.g_strescape(entryTextOrEmpty(image_entry), null);
        const escaped_color = c.g_strescape(entryTextOrEmpty(color_entry), null);
        const escaped_description = c.g_strescape(entryTextOrEmpty(desc_entry), null);
        defer c.g_free(escaped_name);
        defer c.g_free(escaped_image);
        defer c.g_free(escaped_color);
        defer c.g_free(escaped_description);
        const payload = c.g_strdup_printf(
            "{\"name\":\"%s\",\"image_url\":\"%s\",\"color\":\"%s\",\"description\":\"%s\",\"action_type\":\"none\"}",
            escaped_name,
            escaped_image,
            escaped_color,
            escaped_description,
        );
        defer c.g_free(payload);
        var response: [*c]c.gchar = null;
        const ok = adminRequestWithResponse(constants.ADMIN_BADGES_URL, payload, "POST", &response);
        defer c.g_free(response);
        if (ok == FALSE) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Badge creation failed.", "The admin API request could not be sent.");
        } else if (extractErrorMessage(response)) |error_message| {
            defer c.g_free(error_message);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Badge creation failed.", error_message);
        } else {
            start_loading_admin_badges();
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_admin_send_notification_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (!hasAdminSession()) return;
    const target = entryTextOrEmpty(g.g_admin_notifications_target_entry);
    const title = entryTextOrEmpty(g.g_admin_notifications_title_entry);
    const subtitle = entryTextOrEmpty(g.g_admin_notifications_subtitle_entry);
    const notif_type = entryTextOrEmpty(g.g_admin_notifications_type_entry);
    const url_text = entryTextOrEmpty(g.g_admin_notifications_url_entry);
    const message = textViewText(g.g_admin_notifications_message_view);
    defer c.g_free(message);
    if (target[0] == 0) {
        labelSet(g.g_admin_notifications_result_label, "Enter at least one target.");
        return;
    }
    if (title[0] == 0 and subtitle[0] == 0 and message[0] == 0) {
        labelSet(g.g_admin_notifications_result_label, "Provide a title, subtitle, or message.");
        return;
    }
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "target");
    if (jsonBuilderAddTargetValue(builder, target) == FALSE) {
        labelSet(g.g_admin_notifications_result_label, "Enter at least one target.");
        return;
    }
    if (notif_type[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "type");
        _ = c.json_builder_add_string_value(builder, notif_type);
    }
    if (title[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "title");
        _ = c.json_builder_add_string_value(builder, title);
    }
    if (subtitle[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "subtitle");
        _ = c.json_builder_add_string_value(builder, subtitle);
    }
    if (message[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "message");
        _ = c.json_builder_add_string_value(builder, message);
    }
    if (url_text[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "url");
        _ = c.json_builder_add_string_value(builder, url_text);
    }
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var response: [*c]c.gchar = null;
    const ok = adminRequestWithResponse(constants.ADMIN_FAKE_NOTIFICATION_URL, payload, "POST", &response);
    defer c.g_free(response);
    if (ok == FALSE) {
        labelSet(g.g_admin_notifications_result_label, "Notification request failed.");
    } else if (extractErrorMessage(response)) |error_message| {
        defer c.g_free(error_message);
        labelSet(g.g_admin_notifications_result_label, error_message);
    } else {
        labelSet(g.g_admin_notifications_result_label, "Notification sent.");
    }
}
export fn on_admin_clone_user_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (!hasAdminSession()) return;
    const source = entryTextOrEmpty(g.g_admin_clone_source_entry);
    const username = entryTextOrEmpty(g.g_admin_clone_username_entry);
    const name = entryTextOrEmpty(g.g_admin_clone_name_entry);
    if (source[0] == 0 or username[0] == 0) {
        labelSet(g.g_admin_clone_result_label, "Source and new username are required.");
        return;
    }
    const escaped = c.g_uri_escape_string(source, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf("%s/%s/clone", constants.ADMIN_USERS_URL, escaped);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "username");
    _ = c.json_builder_add_string_value(builder, username);
    if (name[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "name");
        _ = c.json_builder_add_string_value(builder, name);
    }
    _ = c.json_builder_set_member_name(builder, "cloneRelations");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(g.g_admin_clone_relations_check))));
    _ = c.json_builder_set_member_name(builder, "cloneGhosts");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(g.g_admin_clone_ghosts_check))));
    _ = c.json_builder_set_member_name(builder, "cloneTweets");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(g.g_admin_clone_tweets_check))));
    _ = c.json_builder_set_member_name(builder, "cloneReplies");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(g.g_admin_clone_replies_check))));
    _ = c.json_builder_set_member_name(builder, "cloneRetweets");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(g.g_admin_clone_retweets_check))));
    _ = c.json_builder_set_member_name(builder, "cloneReactions");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(g.g_admin_clone_reactions_check))));
    _ = c.json_builder_set_member_name(builder, "cloneCommunities");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(g.g_admin_clone_communities_check))));
    _ = c.json_builder_set_member_name(builder, "cloneMedia");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(g.g_admin_clone_media_check))));
    _ = c.json_builder_set_member_name(builder, "cloneAffiliate");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(g.g_admin_clone_affiliate_check))));
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var response: [*c]c.gchar = null;
    const ok = adminRequestWithResponse(url, payload, "POST", &response);
    defer c.g_free(response);
    if (ok == FALSE) {
        labelSet(g.g_admin_clone_result_label, "Clone request failed.");
    } else if (extractErrorMessage(response)) |error_message| {
        defer c.g_free(error_message);
        labelSet(g.g_admin_clone_result_label, error_message);
    } else {
        const result = c.g_strdup_printf("Cloned user created: @%s", username);
        defer c.g_free(result);
        labelSet(g.g_admin_clone_result_label, result);
        start_loading_admin_users(username);
    }
}
export fn on_admin_impersonate_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (!hasAdminSession()) return;
    const identifier = entryTextOrEmpty(g.g_admin_impersonation_entry);
    var user_id: [*c]c.gchar = null;
    defer c.g_free(user_id);
    var user_username: [*c]c.gchar = null;
    defer c.g_free(user_username);
    var lookup_error: [*c]c.gchar = null;
    defer c.g_free(lookup_error);
    if (lookupAdminUserIdentifier(identifier, &user_id, &user_username, &lookup_error) == FALSE) {
        labelSet(g.g_admin_impersonation_status_label, if (lookup_error != null) lookup_error else "User lookup failed.");
        return;
    }
    const url = c.g_strdup_printf("%s/admin/impersonate/%s", constants.API_BASE_URL, user_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    const ok = adminRequestWithResponse(url, "{}", "POST", &response);
    defer c.g_free(response);
    if (ok == FALSE) {
        labelSet(g.g_admin_impersonation_status_label, "Impersonation request failed.");
        return;
    }
    if (extractErrorMessage(response)) |error_message| {
        defer c.g_free(error_message);
        labelSet(g.g_admin_impersonation_status_label, error_message);
        return;
    }
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(response, &parser);
    const token = jsonString(root, "token");
    if (token[0] == 0) {
        labelSet(g.g_admin_impersonation_status_label, "Invalid impersonation response.");
        return;
    }
    const user = jsonObjectMember(root, "user");
    const response_username = if (jsonString(user, "username")[0] != 0) jsonString(user, "username") else user_username;
    c.g_mutex_lock(&g.g_globals_mutex);
    if (g.g_is_impersonating == FALSE) {
        c.g_free(g.g_impersonation_admin_token);
        c.g_free(g.g_impersonation_admin_username);
        g.g_impersonation_admin_token = if (g.g_auth_token != null) c.g_strdup(g.g_auth_token) else null;
        g.g_impersonation_admin_username = if (g.g_current_username != null) c.g_strdup(g.g_current_username) else null;
        g.g_impersonation_admin_is_admin = g.g_is_admin;
    }
    c.g_free(g.g_auth_token);
    g.g_auth_token = c.g_strdup(token);
    c.g_free(g.g_current_username);
    g.g_current_username = c.g_strdup(response_username);
    g.g_is_admin = FALSE;
    g.g_is_impersonating = TRUE;
    c.g_mutex_unlock(&g.g_globals_mutex);
    api.save_session(g.g_auth_token, g.g_current_username, g.g_is_admin);
    update_login_ui();
    updateAdminImpersonationStatusLabel();
    if (g.g_main_list_box != null) start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
}
export fn on_admin_restore_admin_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    var restored_username: [*c]c.gchar = null;
    defer c.g_free(restored_username);
    c.g_mutex_lock(&g.g_globals_mutex);
    if (g.g_is_impersonating == FALSE or g.g_impersonation_admin_token == null) {
        c.g_mutex_unlock(&g.g_globals_mutex);
        labelSet(g.g_admin_impersonation_status_label, "No impersonation session is active.");
        return;
    }
    c.g_free(g.g_auth_token);
    g.g_auth_token = c.g_strdup(g.g_impersonation_admin_token);
    c.g_free(g.g_current_username);
    g.g_current_username = c.g_strdup(g.g_impersonation_admin_username);
    g.g_is_admin = g.g_impersonation_admin_is_admin;
    restored_username = if (g.g_current_username != null) c.g_strdup(g.g_current_username) else null;
    c.g_free(g.g_impersonation_admin_token);
    c.g_free(g.g_impersonation_admin_username);
    g.g_impersonation_admin_token = null;
    g.g_impersonation_admin_username = null;
    g.g_impersonation_admin_is_admin = FALSE;
    g.g_is_impersonating = FALSE;
    c.g_mutex_unlock(&g.g_globals_mutex);
    api.save_session(g.g_auth_token, g.g_current_username, g.g_is_admin);
    update_login_ui();
    if (g.g_main_list_box != null) start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
    if (restored_username != null) {
        const text = c.g_strdup_printf("Restored admin session for @%s.", restored_username);
        defer c.g_free(text);
        labelSet(g.g_admin_impersonation_status_label, text);
    } else {
        updateAdminImpersonationStatusLabel();
    }
}
export fn on_admin_post_as_user_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (!hasAdminSession()) return;
    const target = entryTextOrEmpty(g.g_admin_tools_post_targets_entry);
    const reply_to = entryTextOrEmpty(g.g_admin_tools_post_reply_to_entry);
    const source = entryTextOrEmpty(g.g_admin_tools_post_source_entry);
    const created_at = entryTextOrEmpty(g.g_admin_tools_post_created_at_entry);
    const content = textViewText(g.g_admin_tools_post_content_view);
    defer c.g_free(content);
    if (target[0] == 0) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "No target users.", "Enter one or more user IDs or usernames.");
        return;
    }
    if (content[0] == 0) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Content is required.", "Enter post text before submitting.");
        return;
    }
    const parts = c.g_strsplit_set(target, ",\n", -1);
    defer c.g_strfreev(parts);
    var total: usize = 0;
    var i: usize = 0;
    while (parts[i] != null) : (i += 1) {
        const trimmed = c.g_strstrip(parts[i]);
        if (trimmed != null and trimmed[0] != 0) total += 1;
    }
    if (total == 0) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "No target users.", "Enter one or more user IDs or usernames.");
        return;
    }
    const no_limit = if (g.g_admin_tools_post_no_char_limit_check != null) c.gtk_toggle_button_get_active(@ptrCast(@alignCast(g.g_admin_tools_post_no_char_limit_check))) else FALSE;
    var succeeded: usize = 0;
    var failure_message: [*c]c.gchar = null;
    defer c.g_free(failure_message);
    i = 0;
    while (parts[i] != null and failure_message == null) : (i += 1) {
        const trimmed = c.g_strstrip(parts[i]);
        if (trimmed == null or trimmed[0] == 0) continue;
        var user_id: [*c]c.gchar = null;
        defer c.g_free(user_id);
        var username: [*c]c.gchar = null;
        defer c.g_free(username);
        var lookup_error: [*c]c.gchar = null;
        defer c.g_free(lookup_error);
        if (lookupAdminUserIdentifier(trimmed, &user_id, &username, &lookup_error) == FALSE) {
            failure_message = c.g_strdup(if (lookup_error != null) lookup_error else "User lookup failed.");
            break;
        }
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "userId");
        _ = c.json_builder_add_string_value(builder, user_id);
        _ = c.json_builder_set_member_name(builder, "content");
        _ = c.json_builder_add_string_value(builder, content);
        _ = c.json_builder_set_member_name(builder, "noCharLimit");
        _ = c.json_builder_add_boolean_value(builder, no_limit);
        if (reply_to[0] != 0) {
            _ = c.json_builder_set_member_name(builder, "replyTo");
            _ = c.json_builder_add_string_value(builder, reply_to);
        }
        if (source[0] != 0) {
            _ = c.json_builder_set_member_name(builder, "source");
            _ = c.json_builder_add_string_value(builder, source);
        }
        if (created_at[0] != 0) {
            _ = c.json_builder_set_member_name(builder, "created_at");
            _ = c.json_builder_add_string_value(builder, created_at);
        }
        if (total > 1) {
            _ = c.json_builder_set_member_name(builder, "massTweet");
            _ = c.json_builder_add_boolean_value(builder, TRUE);
        }
        _ = c.json_builder_end_object(builder);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        var response: [*c]c.gchar = null;
        const ok = adminRequestWithResponse(constants.ADMIN_TWEETS_URL, payload, "POST", &response);
        defer c.g_free(response);
        if (ok == FALSE) {
            failure_message = c.g_strdup("The create-post request failed.");
        } else {
            failure_message = extractErrorMessage(response);
        }
        if (failure_message == null) succeeded += 1;
    }
    if (failure_message != null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Failed to create posts.", failure_message);
    } else {
        const summary = c.g_strdup_printf("Created %u admin post%s.", @as(c_uint, @intCast(succeeded)), if (succeeded == 1) lit("") else lit("s"));
        defer c.g_free(summary);
        showModalMessage(c.GTK_MESSAGE_INFO, "Posts created.", summary);
        start_loading_admin_posts(null);
    }
}
export fn on_admin_bulk_edit_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (!hasAdminSession()) return;
    const target = entryTextOrEmpty(g.g_admin_tools_bulk_targets_entry);
    const payload = textViewText(g.g_admin_tools_bulk_payload_view);
    defer c.g_free(payload);
    if (target[0] == 0) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "No target users.", "Enter one or more user IDs or usernames.");
        return;
    }
    var normalized_payload: [*c]c.gchar = null;
    defer c.g_free(normalized_payload);
    var parse_error: [*c]c.gchar = null;
    defer c.g_free(parse_error);
    if (normalizeJsonObjectPayload(payload, &normalized_payload, &parse_error) == FALSE) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Invalid bulk edit payload.", if (parse_error != null) parse_error else null);
        return;
    }
    const parts = c.g_strsplit_set(target, ",\n", -1);
    defer c.g_strfreev(parts);
    var total: usize = 0;
    var i: usize = 0;
    while (parts[i] != null) : (i += 1) {
        const trimmed = c.g_strstrip(parts[i]);
        if (trimmed != null and trimmed[0] != 0) total += 1;
    }
    if (total == 0) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "No target users.", "Enter one or more user IDs or usernames.");
        return;
    }
    var updated_count: usize = 0;
    var failure_message: [*c]c.gchar = null;
    defer c.g_free(failure_message);
    i = 0;
    while (parts[i] != null and failure_message == null) : (i += 1) {
        const trimmed = c.g_strstrip(parts[i]);
        if (trimmed == null or trimmed[0] == 0) continue;
        var user_id: [*c]c.gchar = null;
        defer c.g_free(user_id);
        var username: [*c]c.gchar = null;
        defer c.g_free(username);
        var lookup_error: [*c]c.gchar = null;
        defer c.g_free(lookup_error);
        if (lookupAdminUserIdentifier(trimmed, &user_id, &username, &lookup_error) == FALSE) {
            failure_message = c.g_strdup(if (lookup_error != null) lookup_error else "User lookup failed.");
            break;
        }
        const escaped = c.g_uri_escape_string(user_id, null, FALSE);
        defer c.g_free(escaped);
        const url = c.g_strdup_printf("%s/%s", constants.ADMIN_USERS_URL, escaped);
        defer c.g_free(url);
        var response: [*c]c.gchar = null;
        const ok = adminRequestWithResponse(url, normalized_payload, "PATCH", &response);
        defer c.g_free(response);
        if (ok == FALSE) {
            failure_message = c.g_strdup("A bulk edit request failed.");
        } else {
            failure_message = extractErrorMessage(response);
        }
        if (failure_message == null) updated_count += 1;
    }
    if (failure_message != null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Bulk edit failed.", failure_message);
    } else {
        const summary = c.g_strdup_printf("Updated %u user account%s.", @as(c_uint, @intCast(updated_count)), if (updated_count == 1) lit("") else lit("s"));
        defer c.g_free(summary);
        showModalMessage(c.GTK_MESSAGE_INFO, "Bulk edit complete.", summary);
        start_loading_admin_users(null);
    }
}
export fn on_admin_create_community_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (!hasAdminSession()) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Create Community",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Create",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const name_entry = c.gtk_entry_new();
    const desc_entry = c.gtk_entry_new();
    const rules_entry = c.gtk_entry_new();
    const owner_entry = c.gtk_entry_new();
    const access_combo = c.gtk_combo_box_text_new();
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(access_combo)), "Open");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(access_combo)), "Locked");
    c.gtk_combo_box_set_active(@ptrCast(@alignCast(access_combo)), 0);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Name:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), name_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Description:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), desc_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Rules:"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), rules_entry, 1, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Access:"), 0, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), access_combo, 1, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Owner Username:"), 0, 4, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), owner_entry, 1, 4, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "name");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(name_entry));
        _ = c.json_builder_set_member_name(builder, "description");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(desc_entry));
        _ = c.json_builder_set_member_name(builder, "rules");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(rules_entry));
        _ = c.json_builder_set_member_name(builder, "access_mode");
        _ = c.json_builder_add_string_value(builder, if (c.gtk_combo_box_get_active(@ptrCast(@alignCast(access_combo))) == 1) "locked" else "open");
        if (entryTextOrEmpty(owner_entry)[0] != 0) {
            _ = c.json_builder_set_member_name(builder, "owner_username");
            _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(owner_entry));
        }
        _ = c.json_builder_end_object(builder);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        var response: [*c]c.gchar = null;
        const ok = adminRequestWithResponse(constants.COMMUNITIES_LIST_URL, payload, "POST", &response);
        defer c.g_free(response);
        if (ok == FALSE) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Community creation failed.", "The request could not be sent.");
        } else if (extractErrorMessage(response)) |error_message| {
            defer c.g_free(error_message);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Community creation failed.", error_message);
        } else {
            start_loading_admin_communities();
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_mark_all_read_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) return;
    if (simpleRequest(constants.NOTIFICATIONS_MARK_ALL_READ_URL, "", "PATCH") != FALSE) {
        refresh_notification_badge();
        if (g.g_notifications_list != null) start_loading_notifications(@ptrCast(@alignCast(g.g_notifications_list)));
    }
}
fn openAddNoteDialog(parent: [*c]c.GtkWidget, tweet_id: [*c]const c.gchar, severity_value: [*c]const c.gchar) void {
    const title_text = c.g_strdup_printf("Add %s Note", severity_value);
    defer c.g_free(title_text);
    const dialog = c.gtk_dialog_new_with_buttons(
        title_text,
        widgetWindow(parent),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        @as(c_int, c.GTK_RESPONSE_CANCEL),
        "_Add Note",
        @as(c_int, c.GTK_RESPONSE_ACCEPT),
        @as([*c]const c.gchar, null),
    );
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    c.gtk_container_set_border_width(@ptrCast(@alignCast(content)), 10);
    const label_text = c.g_strdup_printf("Enter %s note/fact check:", severity_value);
    defer c.g_free(label_text);
    const label_widget = c.gtk_label_new(label_text);
    c.gtk_widget_set_halign(label_widget, c.GTK_ALIGN_START);
    const text_view = c.gtk_text_view_new();
    c.gtk_text_view_set_wrap_mode(@ptrCast(@alignCast(text_view)), c.GTK_WRAP_WORD_CHAR);
    c.gtk_widget_set_size_request(text_view, 300, 100);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), label_widget, FALSE, FALSE, 5);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), text_view, TRUE, TRUE, 0);
    const ctx: [*c]types.NoteContext = @ptrCast(@alignCast(c.g_malloc(@sizeOf(types.NoteContext))));
    if (ctx == null) {
        c.gtk_widget_destroy(dialog);
        return;
    }
    ctx.*.text_view = text_view;
    ctx.*.tweet_id = c.g_strdup(tweet_id);
    ctx.*.severity = c.g_strdup(severity_value);
    c.gtk_widget_show_all(dialog);
    _ = c.g_signal_connect_data(dialog, "response", cb(on_note_response), ctx, null, c.G_CONNECT_DEFAULT);
}

export fn on_note_response(dialog: [*c]c.GtkDialog, response_id: c.gint, user_data: c.gpointer) void {
    const ctx: [*c]types.NoteContext = @ptrCast(@alignCast(user_data));
    if (response_id == c.GTK_RESPONSE_ACCEPT and ctx != null) {
        const note = textViewText(ctx.*.text_view);
        defer c.g_free(note);
        if (note != null and note[0] != 0) {
            if (performAddNote(ctx.*.tweet_id, note, ctx.*.severity) != FALSE) {
                if (g.g_main_list_box != null) start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
            } else {
                showModalMessage(c.GTK_MESSAGE_ERROR, "Failed to add note.", null);
            }
        }
    }
    if (ctx != null) {
        c.g_free(ctx.*.tweet_id);
        c.g_free(ctx.*.severity);
        c.g_free(ctx);
    }
    c.gtk_widget_destroy(@ptrCast(@alignCast(dialog)));
}

fn onNoteMenuItemActivated(menuitem: [*c]c.GtkMenuItem, user_data: c.gpointer) callconv(.c) void {
    const severity: [*c]const c.gchar = @ptrCast(user_data);
    const btn: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(@ptrCast(@alignCast(menuitem)), "origin_button")));
    const tweet_id = if (btn != null) widgetStringData(btn, "tweet_id") else null;
    openAddNoteDialog(btn, tweet_id, severity);
}

export fn on_note_button_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const token = getAuthTokenSafe();
    if (token == null) return;
    c.g_free(token);
    const menu = c.gtk_menu_new();
    const labels = [_][*c]const c.gchar{ "Info Note (Blue)", "Warning Note (Orange)", "Danger Note (Red)" };
    const severities = [_][*c]const c.gchar{ "info", "warning", "danger" };
    var i: usize = 0;
    while (i < labels.len) : (i += 1) {
        const item = c.gtk_menu_item_new_with_label(labels[i]);
        c.g_object_set_data(@ptrCast(@alignCast(item)), "origin_button", widget);
        _ = c.g_signal_connect_data(item, "activate", cb(onNoteMenuItemActivated), @ptrCast(@constCast(severities[i])), null, c.G_CONNECT_DEFAULT);
        c.gtk_menu_shell_append(@ptrCast(@alignCast(menu)), item);
    }
    c.gtk_widget_show_all(menu);
    api.zig_gtk_menu_popup_at_widget(menu, widget);
}
export fn on_report_tweet_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    showReportDialog(widget, "post", widgetStringData(widget, "tweet_id"));
}
export fn on_report_profile_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    showReportDialog(widget, "user", widgetStringData(widget, "user_id"));
}
export fn on_translate_tweet_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to translate tweets.");
        return;
    }
    const tweet_content = widgetStringData(widget, "tweet_content");
    if (tweet_content == null or tweet_content[0] == 0) {
        const translate_error = lit("This tweet has no text to translate.");
        unused(.{translate_error});
        showModalMessage(c.GTK_MESSAGE_INFO, "Nothing to translate.", "This tweet has no text content.");
        return;
    }
    const dialog = c.gtk_dialog_new_with_buttons(
        "Translate Tweet",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Translate",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const target_combo = c.gtk_combo_box_text_new();
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(target_combo)), "en", "English");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(target_combo)), "es", "Spanish");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(target_combo)), "fr", "French");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(target_combo)), "de", "German");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(target_combo)), "hi", "Hindi");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(target_combo)), "ja", "Japanese");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(target_combo)), "pt", "Portuguese");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(target_combo)), "ar", "Arabic");
    c.gtk_combo_box_set_active(@ptrCast(@alignCast(target_combo)), 0);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Translate to:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), target_combo, 1, 0, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const target_label = c.gtk_combo_box_text_get_active_text(@ptrCast(@alignCast(target_combo)));
        defer c.g_free(target_label);
        const target_id = c.gtk_combo_box_get_active_id(@ptrCast(@alignCast(target_combo)));
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "text");
        _ = c.json_builder_add_string_value(builder, tweet_content);
        _ = c.json_builder_set_member_name(builder, "source");
        _ = c.json_builder_add_string_value(builder, "auto");
        _ = c.json_builder_set_member_name(builder, "target");
        _ = c.json_builder_add_string_value(builder, if (target_id != null and target_id[0] != 0) target_id else "en");
        _ = c.json_builder_end_object(builder);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        var response: [*c]c.gchar = null;
        const ok = requestWithResponse(constants.TRANSLATE_URL, payload, "POST", &response);
        defer c.g_free(response);
        var parser: ?*c.JsonParser = null;
        defer if (parser != null) c.g_object_unref(parser);
        const root = parseRootObject(response, &parser);
        const translated = jsonString(root, "translatedText");
        if (ok != FALSE and translated[0] != 0) {
            const secondary = c.g_strdup_printf(
                "Detected: %s\nTarget: %s\n\n%s",
                if (jsonString(root, "detectedLanguage")[0] != 0) jsonString(root, "detectedLanguage") else lit("auto"),
                if (target_label != null and target_label[0] != 0) target_label else lit("English"),
                translated,
            );
            defer c.g_free(secondary);
            showModalMessage(c.GTK_MESSAGE_INFO, "Translation", secondary);
        } else {
            const error_message = extractErrorMessage(response);
            defer c.g_free(error_message);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Translation failed.", if (error_message != null) error_message else if (ok != FALSE and root == null) "The translation response could not be read." else "Translation failed.");
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_follow_button_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const username = widgetStringData(widget, "username");
    const state = buttonBoolData(widget, "is_following");
    const currently_following = if (state != null) state.?.* else FALSE;
    const follow = if (currently_following != FALSE) FALSE else TRUE;
    if (username != null and g.g_auth_token != null and perform_follow(username, follow) != FALSE) {
        setButtonBoolData(widget, "is_following", follow);
        buttonSet(widget, if (follow != FALSE) "Unfollow" else "Follow");
    }
}

export fn on_profile_edit_response(dialog: [*c]c.GtkDialog, response_id: c.gint, user_data: c.gpointer) void {
    if (response_id == c.GTK_RESPONSE_ACCEPT) {
        const widgets: *ProfileEditWidgets = @ptrCast(@alignCast(user_data));
        const username = getUsernameSafe();
        defer c.g_free(username);
        if (username != null and perform_update_profile(
            username,
            entryTextOrEmpty(widgets.name_entry),
            entryTextOrEmpty(widgets.bio_entry),
            entryTextOrEmpty(widgets.location_entry),
            entryTextOrEmpty(widgets.website_entry),
            entryTextOrEmpty(widgets.pronouns_entry),
            profileThemeValue(widgets.theme_combo),
            entryTextOrEmpty(widgets.accent_entry),
            profileLabelValue(widgets.label_combo),
            c.gtk_toggle_button_get_active(@ptrCast(@alignCast(widgets.label_automated_check))),
            widgets.include_avatar_radius,
            c.gtk_spin_button_get_value_as_int(@ptrCast(@alignCast(widgets.avatar_radius_spin))),
        ) != FALSE) {
            show_profile(username);
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Failed to update profile.", null);
        }
    }
    c.g_free(user_data);
    c.gtk_widget_destroy(@ptrCast(@alignCast(dialog)));
}

export fn on_edit_profile_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const username = getUsernameSafe();
    defer c.g_free(username);
    if (username == null) return;

    const dialog = c.gtk_dialog_new_with_buttons(
        "Edit Profile",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 5);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 5);
    c.gtk_container_set_border_width(container(grid), 10);

    const name_entry = c.gtk_entry_new();
    const bio_entry = c.gtk_entry_new();
    const location_entry = c.gtk_entry_new();
    const website_entry = c.gtk_entry_new();
    const pronouns_entry = c.gtk_entry_new();
    const theme_combo = c.gtk_combo_box_text_new();
    const accent_entry = c.gtk_entry_new();
    var accent_row: [*c]c.GtkWidget = null;
    const label_combo = c.gtk_combo_box_text_new();
    const label_automated = c.gtk_check_button_new_with_label("Mark label as automated");
    const avatar_radius = c.gtk_spin_button_new_with_range(0, 1000, 1);
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(name_entry)), "Display name...");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(bio_entry)), "Bio...");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(location_entry)), "Location...");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(website_entry)), "Website...");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(pronouns_entry)), "Pronouns...");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(accent_entry)), "Accent color (for example #1d9bf0)");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(theme_combo)), "Auto");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(theme_combo)), "Light");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(theme_combo)), "Dark");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(label_combo)), "None");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(label_combo)), "Parody");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(label_combo)), "Fan");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(label_combo)), "Commentary");

    const active = g.g_active_profile;
    if (active != null) {
        c.gtk_entry_set_text(@ptrCast(@alignCast(name_entry)), textOr(active.*.name));
        c.gtk_entry_set_text(@ptrCast(@alignCast(bio_entry)), textOr(active.*.bio));
        c.gtk_entry_set_text(@ptrCast(@alignCast(location_entry)), textOr(active.*.location));
        c.gtk_entry_set_text(@ptrCast(@alignCast(website_entry)), textOr(active.*.website));
        c.gtk_entry_set_text(@ptrCast(@alignCast(pronouns_entry)), textOr(active.*.pronouns));
        c.gtk_entry_set_text(@ptrCast(@alignCast(accent_entry)), textOr(active.*.accent_color));
        c.gtk_combo_box_set_active(@ptrCast(@alignCast(theme_combo)), profileThemeIndex(active.*.theme));
        c.gtk_combo_box_set_active(@ptrCast(@alignCast(label_combo)), profileLabelIndex(active.*.label_type));
        c.gtk_toggle_button_set_active(@ptrCast(@alignCast(label_automated)), active.*.label_automated);
        c.gtk_spin_button_set_value(@ptrCast(@alignCast(avatar_radius)), @floatFromInt(active.*.avatar_radius));
        c.gtk_widget_set_sensitive(avatar_radius, if (active.*.author_gold != FALSE or active.*.author_gray != FALSE) TRUE else FALSE);
    } else {
        c.gtk_combo_box_set_active(@ptrCast(@alignCast(theme_combo)), 0);
        c.gtk_combo_box_set_active(@ptrCast(@alignCast(label_combo)), 0);
        c.gtk_widget_set_sensitive(avatar_radius, FALSE);
    }
    accent_row = createColorEntryRow(accent_entry, c.gtk_entry_get_text(@ptrCast(@alignCast(accent_entry))));

    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Name:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), name_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Bio:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), bio_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Location:"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), location_entry, 1, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Website:"), 0, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), website_entry, 1, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Pronouns:"), 0, 4, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), pronouns_entry, 1, 4, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Profile theme:"), 0, 5, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), theme_combo, 1, 5, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Accent color:"), 0, 6, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), accent_row, 1, 6, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Profile label:"), 0, 7, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), label_combo, 1, 7, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), label_automated, 1, 8, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Avatar radius:"), 0, 9, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), avatar_radius, 1, 9, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    const widgets: *ProfileEditWidgets = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(ProfileEditWidgets))));
    widgets.*.name_entry = name_entry;
    widgets.*.bio_entry = bio_entry;
    widgets.*.location_entry = location_entry;
    widgets.*.website_entry = website_entry;
    widgets.*.pronouns_entry = pronouns_entry;
    widgets.*.theme_combo = theme_combo;
    widgets.*.accent_entry = accent_entry;
    widgets.*.label_combo = label_combo;
    widgets.*.label_automated_check = label_automated;
    widgets.*.avatar_radius_spin = avatar_radius;
    widgets.*.include_avatar_radius = if (active != null and (active.*.author_gold != FALSE or active.*.author_gray != FALSE)) TRUE else FALSE;
    _ = c.g_signal_connect_data(dialog, "response", cb(on_profile_edit_response), widgets, null, c.G_CONNECT_DEFAULT);
    c.gtk_widget_show_all(grid);
    c.gtk_widget_show(dialog);
}
export fn on_request_affiliate_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const username = widgetStringData(widget, "username");
    if (username == null or g.g_auth_token == null) return;
    const url = c.g_strdup_printf(constants.PROFILE_REQUEST_AFFILIATE_URL, username);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, "{}", "POST", &response) != FALSE) {
        showModalMessage(c.GTK_MESSAGE_INFO, "Affiliate request sent.", null);
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Affiliate request failed.", if (response != null) response else null);
    }
}
export fn on_profile_shop_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const username = widgetStringData(widget, "username");
    if (username == null) return;
    const escaped = c.g_uri_escape_string(username, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf(constants.SHOP_USER_URL, escaped);
    defer c.g_free(url);
    const dialog = c.gtk_dialog_new_with_buttons("Shop", null, c.GTK_DIALOG_MODAL, "_Close", c.GTK_RESPONSE_CLOSE, @as(?*anyopaque, null));
    c.gtk_window_set_default_size(@ptrCast(@alignCast(dialog)), 560, 420);
    const scroll = c.gtk_scrolled_window_new(null, null);
    const list = c.gtk_list_box_new();
    c.gtk_list_box_set_selection_mode(@ptrCast(@alignCast(list)), c.GTK_SELECTION_NONE);
    c.gtk_container_add(container(scroll), list);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), scroll, TRUE, TRUE, 0);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory != null) {
        populateShopProducts(list, chunk.memory, false);
    } else {
        setListBoxStatus(@ptrCast(@alignCast(list)), "Failed to load shop.");
    }
    c.gtk_widget_show_all(dialog);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}
export fn on_profile_donate_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const username = widgetStringData(widget, "username");
    if (username == null or g.g_auth_token == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Donate",
        null,
        c.GTK_DIALOG_MODAL,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Continue",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const amount_entry = c.gtk_entry_new();
    const note_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(amount_entry)), "Amount in INR");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(note_entry)), "Optional note");
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Amount:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), amount_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Note:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), note_entry, 1, 1, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "recipientUsername");
        _ = c.json_builder_add_string_value(builder, username);
        _ = c.json_builder_set_member_name(builder, "amount");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(amount_entry));
        _ = c.json_builder_set_member_name(builder, "note");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(note_entry));
        _ = c.json_builder_set_member_name(builder, "kind");
        _ = c.json_builder_add_string_value(builder, "donate");
        _ = c.json_builder_end_object(builder);
        var response: [*c]c.gchar = null;
        defer c.g_free(response);
        var create_error: [*c]c.gchar = null;
        defer c.g_free(create_error);
        const ok = requestBuilderWithResponse(constants.MPI_SEND_CREATE_URL, "POST", builder, &response);
        if (ok == FALSE) create_error = extractErrorMessage(response);
        var parser: ?*c.JsonParser = null;
        defer if (parser != null) c.g_object_unref(parser);
        const root = parseRootObject(response, &parser);
        if (ok == FALSE or root == null) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Donation could not start.", create_error);
        } else {
            const payment_url = jsonString(root, "paymentUrl");
            const order_id = jsonString(root, "orderId");
            if (payment_url[0] != 0) _ = c.gtk_show_uri_on_window(widgetWindow(widget), payment_url, c.GDK_CURRENT_TIME, null);
            if (order_id[0] != 0) {
                const confirm = c.gtk_dialog_new_with_buttons(
                    "Confirm Donation",
                    null,
                    c.GTK_DIALOG_MODAL,
                    "_Cancel",
                    c.GTK_RESPONSE_CANCEL,
                    "_Confirm",
                    c.GTK_RESPONSE_ACCEPT,
                    @as(?*anyopaque, null),
                );
                const txn_entry = c.gtk_entry_new();
                c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(txn_entry)), "MyPayIndia transaction id");
                const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(confirm)));
                c.gtk_box_pack_start(@ptrCast(@alignCast(content)), c.gtk_label_new("Complete payment in the opened page, then paste the transaction id."), FALSE, FALSE, 8);
                c.gtk_box_pack_start(@ptrCast(@alignCast(content)), txn_entry, FALSE, FALSE, 8);
                c.gtk_widget_show_all(confirm);
                if (c.gtk_dialog_run(@ptrCast(@alignCast(confirm))) == c.GTK_RESPONSE_ACCEPT) {
                    const confirm_builder = c.json_builder_new();
                    defer c.g_object_unref(confirm_builder);
                    _ = c.json_builder_begin_object(confirm_builder);
                    _ = c.json_builder_set_member_name(confirm_builder, "orderId");
                    _ = c.json_builder_add_string_value(confirm_builder, order_id);
                    _ = c.json_builder_set_member_name(confirm_builder, "transactionId");
                    _ = c.json_builder_add_string_value(confirm_builder, entryTextOrEmpty(txn_entry));
                    _ = c.json_builder_end_object(confirm_builder);
                    var confirm_response: [*c]c.gchar = null;
                    defer c.g_free(confirm_response);
                    var confirm_error: [*c]c.gchar = null;
                    defer c.g_free(confirm_error);
                    const confirm_ok = requestBuilderWithResponse(constants.MPI_SEND_CONFIRM_URL, "POST", confirm_builder, &confirm_response);
                    if (confirm_ok == FALSE) confirm_error = extractErrorMessage(confirm_response);
                    if (confirm_ok != FALSE) {
                        showModalMessage(c.GTK_MESSAGE_INFO, "Donation sent.", null);
                    } else {
                        showModalMessage(c.GTK_MESSAGE_ERROR, "Donation confirmation failed.", confirm_error);
                    }
                }
                c.gtk_widget_destroy(confirm);
            }
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_profile_algorithm_stats_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const username = widgetStringData(widget, "username");
    if (username == null) return;
    const url = c.g_strdup_printf(constants.PROFILE_ALGORITHM_STATS_URL, username);
    defer c.g_free(url);
    showProfileJsonSummary("Algorithm Stats", url, formatAlgorithmStats);
}
export fn on_profile_spam_score_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const username = widgetStringData(widget, "username");
    if (username == null) return;
    const url = c.g_strdup_printf(constants.PROFILE_SPAM_SCORE_URL, username);
    defer c.g_free(url);
    showProfileJsonSummary("Spam Score", url, formatSpamScore);
}
export fn on_profile_analytics_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const username = widgetStringData(widget, "username");
    if (username == null) return;
    const url = c.g_strdup_printf(constants.EXPLORE_USER_ANALYTICS_URL, username);
    defer c.g_free(url);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Analytics unavailable.", null);
        return;
    }
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(chunk.memory, &parser);
    if (root == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Analytics unavailable.", "The server response could not be read.");
        return;
    }
    const analytics = jsonObjectMember(root, "analytics");
    if (analytics == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Analytics unavailable.", "The server response did not include analytics.");
        return;
    }
    const summary = c.g_strdup_printf(
        "Posting streak: %ld days\nActive days in the last 30 days: %ld\nLikes received: %ld\nEngagement rate: %.2f",
        jsonInt(analytics, "posting_streak"),
        jsonInt(analytics, "days_active_30d"),
        jsonInt(analytics, "total_likes_received"),
        jsonDouble(analytics, "engagement_rate"),
    );
    defer c.g_free(summary);
    showModalMessage(c.GTK_MESSAGE_INFO, "Profile Analytics", summary);
}
export fn on_profile_common_followers_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const username = widgetStringData(widget, "username");
    if (username == null) return;
    const url = c.g_strdup_printf(constants.EXPLORE_USER_COMMON_FOLLOWERS_URL, username);
    defer c.g_free(url);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Common followers unavailable.", null);
        return;
    }
    const users = api.parse_users(chunk.memory);
    defer api.free_users(users);
    const dialog = showListDialog("Common Follows", 480, 520);
    const list = dialogList(dialog);
    api.populate_user_list(list, users);
    c.gtk_widget_show_all(dialog);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}
export fn on_profile_top_posts_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const username = widgetStringData(widget, "username");
    if (username == null) return;
    const url = c.g_strdup_printf(constants.EXPLORE_USER_TOP_POSTS_URL, username);
    defer c.g_free(url);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Top posts unavailable.", null);
        return;
    }
    const tweets = api.parse_tweets(chunk.memory);
    defer api.free_tweets(tweets);
    const dialog = showListDialog("Top Posts", 620, 620);
    const list = dialogList(dialog);
    api.populate_tweet_list(list, tweets);
    c.gtk_widget_show_all(dialog);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}
export fn on_profile_communities_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const user_id = widgetStringData(widget, "user_id");
    if (user_id == null) return;
    const url = c.g_strdup_printf(constants.USER_COMMUNITIES_URL, user_id);
    defer c.g_free(url);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Communities unavailable.", null);
        return;
    }
    const communities = api.parse_communities(chunk.memory);
    defer api.free_communities(communities);
    const dialog = c.gtk_dialog_new_with_buttons(
        "Communities",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        c.GTK_RESPONSE_CLOSE,
        @as(?*anyopaque, null),
    );
    const scroll = c.gtk_scrolled_window_new(null, null);
    const list_widget = c.gtk_list_box_new();
    c.gtk_widget_set_size_request(scroll, 520, 420);
    c.gtk_list_box_set_selection_mode(@ptrCast(@alignCast(list_widget)), c.GTK_SELECTION_NONE);
    c.gtk_container_add(@ptrCast(@alignCast(scroll)), list_widget);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), scroll, TRUE, TRUE, 8);
    const list: [*c]c.GtkListBox = @ptrCast(@alignCast(list_widget));
    api.populate_community_list(list, communities);
    c.gtk_widget_show_all(dialog);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}
export fn on_lists_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to use lists.");
        return;
    }
    setStack("lists");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
    start_loading_lists();
}

fn listJsonRequest(url: [*c]const c.gchar, payload: [*c]const c.gchar, method: [*c]const c.gchar, error_out: ?*[*c]c.gchar) c.gboolean {
    if (error_out) |out| out.* = null;
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, payload, method, &response) != FALSE) {
        const error_message = extractErrorMessage(response);
        if (error_message == null) return TRUE;
        if (error_out) |out| {
            out.* = error_message;
        } else {
            c.g_free(error_message);
        }
    } else if (error_out) |out| {
        out.* = c.g_strdup("The list request could not be sent.");
    }
    return FALSE;
}

fn buildListPayload(name: [*c]const c.gchar, description: [*c]const c.gchar, is_private: c.gboolean) [*c]c.gchar {
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    const gen = c.json_generator_new();
    defer c.g_object_unref(gen);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "name");
    _ = c.json_builder_add_string_value(builder, textOr(name));
    _ = c.json_builder_set_member_name(builder, "description");
    _ = c.json_builder_add_string_value(builder, textOr(description));
    _ = c.json_builder_set_member_name(builder, "isPrivate");
    _ = c.json_builder_add_boolean_value(builder, is_private);
    _ = c.json_builder_end_object(builder);
    const root = c.json_builder_get_root(builder);
    defer c.json_node_free(root);
    c.json_generator_set_root(gen, root);
    return c.json_generator_to_data(gen, null);
}

export fn on_create_list_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const dialog = c.gtk_dialog_new_with_buttons(
        "Create List",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );

    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);

    const name_entry = c.gtk_entry_new();
    const description_entry = c.gtk_entry_new();
    const private_check = c.gtk_check_button_new_with_label("Private");
    c.gtk_entry_set_max_length(@ptrCast(@alignCast(name_entry)), 25);
    c.gtk_entry_set_max_length(@ptrCast(@alignCast(description_entry)), 100);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Name:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), name_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Description:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), description_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), private_check, 1, 2, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);

    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const payload = buildListPayload(entryTextOrEmpty(name_entry), entryTextOrEmpty(description_entry), c.gtk_toggle_button_get_active(@ptrCast(@alignCast(private_check))));
        defer c.g_free(payload);
        var error_message: [*c]c.gchar = null;
        defer c.g_free(error_message);
        if (listJsonRequest(constants.LISTS_URL, payload, "POST", &error_message) != FALSE) {
            start_loading_lists();
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "List update failed.", error_message);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_list_follow_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    const list_id = widgetStringData(widget, "list_id");
    if (list_id == null) return;
    var mode: usize = @intFromPtr(user_data);
    if (mode == 2) {
        show_list_details(list_id);
        return;
    }
    if (mode == 3 and g.g_active_list != null) {
        mode = if (g.g_active_list.*.is_following != FALSE) 0 else 1;
    }
    const url = c.g_strdup_printf(constants.LIST_FOLLOW_URL, list_id);
    defer c.g_free(url);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (listJsonRequest(url, null, if (mode != 0) "POST" else "DELETE", &error_message) == FALSE) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "List follow failed.", error_message);
    }
    start_loading_lists();
    show_list_details(list_id);
}
export fn on_list_edit_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const list_id = if (g.g_active_list != null and g.g_active_list.*.id != null) g.g_active_list.*.id else null;
    if (list_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Edit List",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const name_entry = c.gtk_entry_new();
    const description_entry = c.gtk_entry_new();
    const private_check = c.gtk_check_button_new_with_label("Private");
    if (g.g_active_list != null) {
        c.gtk_entry_set_text(@ptrCast(@alignCast(name_entry)), textOr(g.g_active_list.*.name));
        c.gtk_entry_set_text(@ptrCast(@alignCast(description_entry)), textOr(g.g_active_list.*.description));
        c.gtk_toggle_button_set_active(@ptrCast(@alignCast(private_check)), g.g_active_list.*.is_private);
    }
    c.gtk_entry_set_max_length(@ptrCast(@alignCast(name_entry)), 25);
    c.gtk_entry_set_max_length(@ptrCast(@alignCast(description_entry)), 100);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Name:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), name_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Description:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), description_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), private_check, 1, 2, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const url = c.g_strdup_printf(constants.LIST_DETAILS_URL, list_id);
        defer c.g_free(url);
        const payload = buildListPayload(entryTextOrEmpty(name_entry), entryTextOrEmpty(description_entry), c.gtk_toggle_button_get_active(@ptrCast(@alignCast(private_check))));
        defer c.g_free(payload);
        var error_message: [*c]c.gchar = null;
        defer c.g_free(error_message);
        if (listJsonRequest(url, payload, "PATCH", &error_message) != FALSE) {
            const saved_id = c.g_strdup(list_id);
            defer c.g_free(saved_id);
            show_list_details(saved_id);
            start_loading_lists();
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "List update failed.", error_message);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_list_delete_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const list_id = if (g.g_active_list != null and g.g_active_list.*.id != null) g.g_active_list.*.id else null;
    if (list_id == null) return;
    const dialog = c.gtk_message_dialog_new(widgetWindow(widget), c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT, c.GTK_MESSAGE_WARNING, c.GTK_BUTTONS_OK_CANCEL, "Delete this list?");
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_OK) {
        const url = c.g_strdup_printf(constants.LIST_DETAILS_URL, list_id);
        defer c.g_free(url);
        var error_message: [*c]c.gchar = null;
        defer c.g_free(error_message);
        if (listJsonRequest(url, null, "DELETE", &error_message) != FALSE) {
            setStack("lists");
            start_loading_lists();
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "List deletion failed.", error_message);
        }
    }
    c.gtk_widget_destroy(dialog);
}

fn lookupProfileId(username: [*c]const c.gchar) [*c]c.gchar {
    if (username == null or username[0] == 0) return null;
    const escaped = c.g_uri_escape_string(username, null, FALSE);
    defer c.g_free(escaped);
    const url = c.g_strdup_printf(constants.PROFILE_URL, escaped);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, null, "GET", &response) == FALSE) return null;
    const profile = api.parse_profile(response);
    defer api.free_user(profile);
    if (profile == null or profile.*.id == null) return null;
    return c.g_strdup(profile.*.id);
}

export fn on_list_add_member_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const list_id = if (g.g_active_list != null and g.g_active_list.*.id != null) g.g_active_list.*.id else null;
    if (list_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Add List Member",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Add",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "username");
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const user_id = lookupProfileId(entryTextOrEmpty(entry));
        defer c.g_free(user_id);
        if (user_id != null) {
            const url = c.g_strdup_printf(constants.LIST_MEMBERS_URL, list_id);
            defer c.g_free(url);
            const builder = c.json_builder_new();
            defer c.g_object_unref(builder);
            _ = c.json_builder_begin_object(builder);
            _ = c.json_builder_set_member_name(builder, "userId");
            _ = c.json_builder_add_string_value(builder, user_id);
            _ = c.json_builder_end_object(builder);
            const payload = builderPayload(builder);
            defer c.g_free(payload);
            var error_message: [*c]c.gchar = null;
            defer c.g_free(error_message);
            if (listJsonRequest(url, payload, "POST", &error_message) != FALSE) {
                show_list_details(g.g_active_list.*.id);
            } else {
                showModalMessage(c.GTK_MESSAGE_ERROR, "Could not add member.", error_message);
            }
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "User not found.", null);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_manage_passkeys_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Passkeys unavailable.", "Log in and try again.");
        return;
    }
    const chunk = fetchGet(constants.AUTH_PASSKEYS_URL);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Passkeys unavailable.", "Log in and try again.");
        return;
    }
    const summary = formatPasskeySummary(chunk.memory);
    defer c.g_free(summary);
    const dlg = c.gtk_dialog_new_with_buttons(
        "Passkeys",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        c.GTK_RESPONSE_CLOSE,
        if (api.webauthn_fido2_is_enabled() != FALSE) lit("_Add Passkey") else lit("_Add in Browser"),
        @as(c_int, 3),
        "_Rename",
        @as(c_int, 1),
        "_Delete",
        @as(c_int, 2),
        @as(?*anyopaque, null),
    );
    const label = c.gtk_label_new(summary);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(label)), 0.0);
    c.gtk_label_set_selectable(@ptrCast(@alignCast(label)), TRUE);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(label)), TRUE);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dlg))))), label, TRUE, TRUE, 8);
    c.gtk_widget_show_all(dlg);
    const response = c.gtk_dialog_run(@ptrCast(@alignCast(dlg)));
    c.gtk_widget_destroy(dlg);

    if (response == 3) {
        if (api.webauthn_fido2_is_enabled() != FALSE) {
            const username = getUsernameSafe();
            defer c.g_free(username);
            var verify_response: [*c]c.gchar = null;
            defer c.g_free(verify_response);
            var error_message: [*c]c.gchar = null;
            defer c.g_free(error_message);
            if (username == null) {
                showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "Log in and try again.");
            } else if (api.webauthn_fido2_register(username, &verify_response, &error_message) != FALSE) {
                showModalMessage(c.GTK_MESSAGE_INFO, "Passkey added.", null);
            } else {
                showModalMessage(c.GTK_MESSAGE_ERROR, "Passkey registration failed.", if (error_message != null) error_message else "The passkey response could not be accepted.");
            }
        } else {
            openWebPath(widgetWindow(widget), "/settings");
        }
    } else if (response == 1 or response == 2) {
        const edit = c.gtk_dialog_new_with_buttons(
            if (response == 1) lit("Rename passkey") else lit("Delete passkey"),
            widgetWindow(widget),
            c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
            "_Cancel",
            c.GTK_RESPONSE_CANCEL,
            if (response == 1) lit("_Rename") else lit("_Delete"),
            c.GTK_RESPONSE_ACCEPT,
            @as(?*anyopaque, null),
        );
        const id_entry = c.gtk_entry_new();
        const name_entry = c.gtk_entry_new();
        c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(id_entry)), "Passkey ID");
        c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(edit))))), id_entry, FALSE, FALSE, 8);
        if (response == 1) {
            c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(name_entry)), "New name");
            c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(edit))))), name_entry, FALSE, FALSE, 8);
        }
        c.gtk_widget_show_all(edit);
        if (c.gtk_dialog_run(@ptrCast(@alignCast(edit))) == c.GTK_RESPONSE_ACCEPT) {
            const cred_id = c.gtk_entry_get_text(@ptrCast(@alignCast(id_entry)));
            if (cred_id != null and cred_id[0] != 0) {
                const url = if (response == 1) c.g_strdup_printf(constants.AUTH_PASSKEY_NAME_URL, cred_id) else c.g_strdup_printf(constants.AUTH_PASSKEY_DELETE_URL, cred_id);
                defer c.g_free(url);
                var ok: c.gboolean = FALSE;
                if (response == 1) {
                    const builder = c.json_builder_new();
                    defer c.g_object_unref(builder);
                    _ = c.json_builder_begin_object(builder);
                    _ = c.json_builder_set_member_name(builder, "name");
                    _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(name_entry));
                    _ = c.json_builder_end_object(builder);
                    ok = requestBuilder(url, "PUT", builder);
                } else {
                    ok = simpleRequest(url, null, "DELETE");
                }
                if (ok != FALSE) {
                    showModalMessage(c.GTK_MESSAGE_INFO, if (response == 1) "Passkey renamed." else "Passkey deleted.", null);
                } else {
                    showModalMessage(c.GTK_MESSAGE_ERROR, if (response == 1) "Rename failed." else "Delete failed.", null);
                }
            }
        }
        c.gtk_widget_destroy(edit);
    }
}
export fn on_push_notifications_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "Log in before managing push notifications.");
        return;
    }
    const status = fetchGet(constants.PUSH_STATUS_URL);
    defer c.g_free(status.memory);
    const vapid = fetchGet(constants.PUSH_VAPID_KEY_URL);
    defer c.g_free(vapid.memory);
    const text = formatPushSummary(status.memory, vapid.memory);
    defer c.g_free(text);
    const dlg = c.gtk_dialog_new_with_buttons(
        "Push Notifications",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        c.GTK_RESPONSE_CLOSE,
        "_Subscribe",
        @as(c_int, 1),
        "_Unsubscribe",
        @as(c_int, 2),
        @as(?*anyopaque, null),
    );
    const label = c.gtk_label_new(text);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(label)), 0.0);
    c.gtk_label_set_selectable(@ptrCast(@alignCast(label)), TRUE);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(label)), TRUE);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dlg))))), label, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dlg);
    const response = c.gtk_dialog_run(@ptrCast(@alignCast(dlg)));
    c.gtk_widget_destroy(dlg);

    if (response == 1 or response == 2) {
        const edit = c.gtk_dialog_new_with_buttons(
            if (response == 1) lit("Subscribe endpoint") else lit("Unsubscribe endpoint"),
            widgetWindow(widget),
            c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
            "_Cancel",
            c.GTK_RESPONSE_CANCEL,
            if (response == 1) lit("_Subscribe") else lit("_Unsubscribe"),
            c.GTK_RESPONSE_ACCEPT,
            @as(?*anyopaque, null),
        );
        const endpoint_entry = c.gtk_entry_new();
        const p256dh_entry = c.gtk_entry_new();
        const auth_entry = c.gtk_entry_new();
        c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(endpoint_entry)), "Endpoint URL");
        c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(edit))))), endpoint_entry, FALSE, FALSE, 8);
        if (response == 1) {
            c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(p256dh_entry)), "p256dh key");
            c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(auth_entry)), "auth key");
            c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(edit))))), p256dh_entry, FALSE, FALSE, 8);
            c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(edit))))), auth_entry, FALSE, FALSE, 8);
        }
        c.gtk_widget_show_all(edit);
        if (c.gtk_dialog_run(@ptrCast(@alignCast(edit))) == c.GTK_RESPONSE_ACCEPT) {
            const builder = c.json_builder_new();
            defer c.g_object_unref(builder);
            _ = c.json_builder_begin_object(builder);
            if (response == 1) {
                _ = c.json_builder_set_member_name(builder, "subscription");
                _ = c.json_builder_begin_object(builder);
                _ = c.json_builder_set_member_name(builder, "endpoint");
                _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(endpoint_entry));
                _ = c.json_builder_set_member_name(builder, "keys");
                _ = c.json_builder_begin_object(builder);
                _ = c.json_builder_set_member_name(builder, "p256dh");
                _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(p256dh_entry));
                _ = c.json_builder_set_member_name(builder, "auth");
                _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(auth_entry));
                _ = c.json_builder_end_object(builder);
                _ = c.json_builder_end_object(builder);
            } else {
                _ = c.json_builder_set_member_name(builder, "endpoint");
                _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(endpoint_entry));
            }
            _ = c.json_builder_end_object(builder);
            const ok = requestBuilder(if (response == 1) constants.PUSH_SUBSCRIBE_URL else constants.PUSH_UNSUBSCRIBE_URL, "POST", builder);
            if (ok != FALSE) {
                showModalMessage(c.GTK_MESSAGE_INFO, if (response == 1) "Push subscription saved." else "Push subscription removed.", null);
            } else {
                showModalMessage(c.GTK_MESSAGE_ERROR, if (response == 1) "Subscribe failed." else "Unsubscribe failed.", null);
            }
        }
        c.gtk_widget_destroy(edit);
    }
}
export fn on_moderation_history_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Moderation history unavailable.", null);
        return;
    }
    const chunk = fetchGet(constants.AUTH_MODERATION_HISTORY_URL);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Moderation history unavailable.", null);
        return;
    }
    const text = formatModerationHistoryResponse(chunk.memory);
    defer c.g_free(text);
    showTextDialog(widgetWindow(widget), "Moderation History", text);
}
export fn on_blocking_causes_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Block causes unavailable.", null);
        return;
    }
    const chunk = fetchGet(constants.BLOCKING_CAUSES_URL);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Block causes unavailable.", null);
        return;
    }
    const text = formatBlockingCausesResponse(chunk.memory);
    defer c.g_free(text);
    showTextDialog(widgetWindow(widget), "Block Causes", text);
}
export fn on_validate_accounts_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const dialog = c.gtk_dialog_new_with_buttons(
        "Validate Accounts",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Validate",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "User IDs, comma-separated");
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "userIds");
        _ = c.json_builder_begin_array(builder);
        const parts = c.g_strsplit(c.gtk_entry_get_text(@ptrCast(@alignCast(entry))), ",", -1);
        defer c.g_strfreev(parts);
        var index: usize = 0;
        while (parts[index] != null) : (index += 1) {
            const id = c.g_strstrip(parts[index]);
            if (id != null and id[0] != 0) _ = c.json_builder_add_string_value(builder, id);
        }
        _ = c.json_builder_end_array(builder);
        _ = c.json_builder_end_object(builder);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        var response: [*c]c.gchar = null;
        const ok = requestWithResponse(constants.AUTH_VALIDATE_ACCOUNTS_URL, payload, "POST", &response);
        defer c.g_free(response);
        if (ok != FALSE) {
            const text = formatValidateAccountsResponse(response);
            defer c.g_free(text);
            showTextDialog(widgetWindow(widget), "Valid Accounts", text);
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Account validation failed.", null);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_add_account_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const dialog = c.gtk_dialog_new_with_buttons(
        "Add Account",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Add",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const username_entry = c.gtk_entry_new();
    const password_entry = c.gtk_entry_new();
    c.gtk_entry_set_visibility(@ptrCast(@alignCast(password_entry)), FALSE);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Username"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), username_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Password"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), password_entry, 1, 1, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "username");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(username_entry));
        _ = c.json_builder_set_member_name(builder, "password");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(password_entry));
        _ = c.json_builder_end_object(builder);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        var response: [*c]c.gchar = null;
        defer c.g_free(response);
        const ok = requestWithResponse(constants.AUTH_ADD_ACCOUNT_URL, payload, "POST", &response);
        if (ok != FALSE) {
            const err = extractErrorMessage(response);
            defer c.g_free(err);
            showModalMessage(if (err != null) c.GTK_MESSAGE_ERROR else c.GTK_MESSAGE_INFO, if (err != null) "Add account failed." else "Account added.", err);
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Add account failed.", null);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_switch_primary_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (authSwitchRequest(constants.AUTH_SWITCH_PRIMARY_URL, "{}", &error_message) != FALSE) {
        showModalMessage(c.GTK_MESSAGE_INFO, "Switched to primary account.", null);
        start_loading_delegates();
        if (g.g_main_list_box != null) start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not switch accounts.", error_message);
    }
}
export fn on_clear_cache_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const cache_dir = getCacheDirectory();
    defer c.g_free(cache_dir);
    clearDirectory(cache_dir);
    refresh_cache_size_display();
    const window = widgetWindow(widget);
    if (window != null) {
        const dialog = c.gtk_message_dialog_new(window, c.GTK_DIALOG_DESTROY_WITH_PARENT, c.GTK_MESSAGE_INFO, c.GTK_BUTTONS_CLOSE, "%s", "Cache cleared successfully.");
        _ = c.gtk_dialog_run(asDialog(dialog));
        c.gtk_widget_destroy(dialog);
    }
}
export fn on_clear_history_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const window = widgetWindow(widget);
    if (window == null) return;
    const dialog = c.gtk_message_dialog_new(window, c.GTK_DIALOG_DESTROY_WITH_PARENT, c.GTK_MESSAGE_QUESTION, c.GTK_BUTTONS_YES_NO, "%s", "Clear all search history?");
    c.gtk_message_dialog_format_secondary_text(@ptrCast(@alignCast(dialog)), "%s", "This action cannot be undone.");
    const response = c.gtk_dialog_run(asDialog(dialog));
    c.gtk_widget_destroy(dialog);
    if (response == c.GTK_RESPONSE_YES) {
        const confirm_dialog = c.gtk_message_dialog_new(window, c.GTK_DIALOG_DESTROY_WITH_PARENT, c.GTK_MESSAGE_INFO, c.GTK_BUTTONS_CLOSE, "%s", "Search history cleared.");
        _ = c.gtk_dialog_run(asDialog(confirm_dialog));
        c.gtk_widget_destroy(confirm_dialog);
    }
}
export fn on_change_password_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const window = widgetWindow(widget);
    if (window == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Change Password",
        window,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Change",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 10);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 10);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 20);
    const current_entry = c.gtk_entry_new();
    const new_entry = c.gtk_entry_new();
    const confirm_entry = c.gtk_entry_new();
    c.gtk_entry_set_visibility(@ptrCast(@alignCast(current_entry)), FALSE);
    c.gtk_entry_set_visibility(@ptrCast(@alignCast(new_entry)), FALSE);
    c.gtk_entry_set_visibility(@ptrCast(@alignCast(confirm_entry)), FALSE);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Current password:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), current_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("New password:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), new_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Confirm password:"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), confirm_entry, 1, 2, 1, 1);
    c.gtk_container_add(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const current = entryTextOrEmpty(current_entry);
        const new_pw = entryTextOrEmpty(new_entry);
        const confirm = entryTextOrEmpty(confirm_entry);
        const username = getUsernameSafe();
        defer c.g_free(username);
        if (username == null) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to change your password.");
        } else if (cstr.len(new_pw) < 8) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Password must be at least 8 characters long.", null);
        } else if (!cstr.eql(new_pw, confirm)) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "New passwords do not match.", null);
        } else {
            const escaped_username = c.g_uri_escape_string(username, null, FALSE);
            defer c.g_free(escaped_username);
            const url = c.g_strdup_printf(constants.PROFILE_PASSWORD_URL, escaped_username);
            defer c.g_free(url);
            const builder = c.json_builder_new();
            defer c.g_object_unref(builder);
            _ = c.json_builder_begin_object(builder);
            if (current[0] != 0) {
                _ = c.json_builder_set_member_name(builder, "currentPassword");
                _ = c.json_builder_add_string_value(builder, current);
            }
            _ = c.json_builder_set_member_name(builder, "newPassword");
            _ = c.json_builder_add_string_value(builder, new_pw);
            _ = c.json_builder_end_object(builder);
            const payload = builderPayload(builder);
            defer c.g_free(payload);
            var error_message: [*c]c.gchar = null;
            defer c.g_free(error_message);
            if (requestErrorFromResponse(url, payload, "PATCH", &error_message) != FALSE) {
                showModalMessage(c.GTK_MESSAGE_INFO, "Password updated.", "Your password was changed successfully.");
            } else {
                if (error_message == null) error_message = c.g_strdup("The password change request could not be sent.");
                showModalMessage(c.GTK_MESSAGE_ERROR, "Password change failed.", error_message);
            }
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_change_username_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null or g.g_settings_new_username_entry == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to change your username.");
        return;
    }
    const username = getUsernameSafe();
    defer c.g_free(username);
    if (username == null) return;
    const new_username = c.gtk_entry_get_text(@ptrCast(@alignCast(g.g_settings_new_username_entry)));
    if (new_username == null or new_username[0] == 0) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Username required.", "Enter a new username.");
        return;
    }
    const url = c.g_strdup_printf(constants.PROFILE_USERNAME_URL, username);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "newUsername");
    _ = c.json_builder_add_string_value(builder, new_username);
    _ = c.json_builder_end_object(builder);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    const username_ok = requestBuilderWithResponse(url, "PATCH", builder, &response);
    if (username_ok == FALSE) {
        error_message = extractErrorMessage(response);
        if (error_message == null) error_message = c.g_strdup("Request failed.");
    }
    if (username_ok != FALSE) {
        var parser: ?*c.JsonParser = null;
        defer if (parser != null) c.g_object_unref(parser);
        const root = parseRootObject(response, &parser);
        const token = jsonString(root, "token");
        const updated_username = jsonString(root, "username");
        if (token[0] != 0 and updated_username[0] != 0) {
            c.g_mutex_lock(&g.g_globals_mutex);
            c.g_free(g.g_auth_token);
            c.g_free(g.g_current_username);
            g.g_auth_token = c.g_strdup(token);
            g.g_current_username = c.g_strdup(updated_username);
            c.g_mutex_unlock(&g.g_globals_mutex);
            api.save_session(g.g_auth_token, g.g_current_username, g.g_is_admin);
            c.gtk_entry_set_text(@ptrCast(@alignCast(g.g_settings_new_username_entry)), "");
            update_login_ui();
            showModalMessage(c.GTK_MESSAGE_INFO, "Username updated.", "Your session has been updated.");
        }
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Username change failed.", error_message);
    }
}
export fn on_account_private_toggled(switch_widget: [*c]c.GtkSwitch, state: c.gboolean, user_data: c.gpointer) void {
    unused(.{ switch_widget, user_data });
    if (g.g_auth_token != null) {
        _ = booleanSetting(constants.PROFILE_SETTINGS_PRIVATE_URL, "enabled", state);
    }
}
export fn on_transparency_location_toggled(switch_widget: [*c]c.GtkSwitch, state: c.gboolean, user_data: c.gpointer) void {
    unused(.{ switch_widget, user_data });
    if (g.g_auth_token != null) {
        _ = booleanSetting(constants.PROFILE_SETTINGS_TRANSPARENCY_LOCATION_URL, "showContinent", state);
    }
}
export fn on_update_community_tag_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_settings_community_tag_entry == null) return;
    updateCommunityTag(c.gtk_entry_get_text(@ptrCast(@alignCast(g.g_settings_community_tag_entry))));
}
export fn on_clear_community_tag_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    updateCommunityTag(null);
    if (g.g_settings_community_tag_entry != null) {
        c.gtk_entry_set_text(@ptrCast(@alignCast(g.g_settings_community_tag_entry)), "");
    }
}
export fn on_update_outlines_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null or g.g_settings_checkmark_outline_entry == null or g.g_settings_avatar_outline_entry == null) return;
    const username = getUsernameSafe();
    defer c.g_free(username);
    if (username == null) return;
    const checkmark = c.gtk_entry_get_text(@ptrCast(@alignCast(g.g_settings_checkmark_outline_entry)));
    const avatar = c.gtk_entry_get_text(@ptrCast(@alignCast(g.g_settings_avatar_outline_entry)));
    if ((checkmark == null or checkmark[0] == 0) and (avatar == null or avatar[0] == 0)) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Outline value required.", "Enter a checkmark or avatar outline color.");
        return;
    }
    const url = c.g_strdup_printf(constants.PROFILE_OUTLINES_URL, username);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    if (checkmark != null and checkmark[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "checkmark_outline");
        _ = c.json_builder_add_string_value(builder, checkmark);
    }
    if (avatar != null and avatar[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "avatar_outline");
        _ = c.json_builder_add_string_value(builder, avatar);
    }
    _ = c.json_builder_end_object(builder);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestBuilderWithResponse(url, "PATCH", builder, &response) != FALSE) {
        showModalMessage(c.GTK_MESSAGE_INFO, "Outlines updated.", null);
    } else {
        const error_message = extractErrorMessage(response);
        defer c.g_free(error_message);
        showModalMessage(c.GTK_MESSAGE_ERROR, "Outline update failed.", if (error_message != null) error_message else "Request failed.");
    }
}
export fn on_delete_account_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to delete your account.");
        return;
    }
    const username = getUsernameSafe();
    defer c.g_free(username);
    if (username == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Delete Account",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Delete",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "Type DELETE MY ACCOUNT");
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), c.gtk_label_new("Type DELETE MY ACCOUNT to permanently delete your account."), FALSE, FALSE, 8);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const url = c.g_strdup_printf(constants.PROFILE_DELETE_ACCOUNT_URL, username);
        defer c.g_free(url);
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "confirmationText");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(entry));
        _ = c.json_builder_end_object(builder);
        var response: [*c]c.gchar = null;
        defer c.g_free(response);
        if (requestBuilderWithResponse(url, "DELETE", builder, &response) != FALSE) {
            perform_logout();
            showModalMessage(c.GTK_MESSAGE_INFO, "Account deleted.", null);
        } else {
            const error_message = extractErrorMessage(response);
            defer c.g_free(error_message);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Account deletion failed.", if (error_message != null) error_message else "Request failed.");
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_bulk_delete_posts_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to bulk delete posts.");
        return;
    }
    const dialog = c.gtk_dialog_new_with_buttons(
        "Bulk Delete Posts",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Preview",
        c.GTK_RESPONSE_APPLY,
        "_Delete",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 12);
    const after_entry = c.gtk_entry_new();
    const before_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(after_entry)), "YYYY-MM-DD or ISO timestamp");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(before_entry)), "YYYY-MM-DD or ISO timestamp");
    const limit_spin = c.gtk_spin_button_new_with_range(1, 500, 1);
    c.gtk_spin_button_set_value(@ptrCast(@alignCast(limit_spin)), 100);
    const include_replies = c.gtk_check_button_new_with_label("Include replies");
    const keep_pinned = c.gtk_check_button_new_with_label("Keep pinned post");
    c.gtk_toggle_button_set_active(@ptrCast(@alignCast(include_replies)), TRUE);
    c.gtk_toggle_button_set_active(@ptrCast(@alignCast(keep_pinned)), TRUE);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("After:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), after_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Before:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), before_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Limit:"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), limit_spin, 1, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), include_replies, 1, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), keep_pinned, 1, 4, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    const result = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    if (result != c.GTK_RESPONSE_APPLY and result != c.GTK_RESPONSE_ACCEPT) {
        c.gtk_widget_destroy(dialog);
        return;
    }
    const after = entryTextOrEmpty(after_entry);
    const before = entryTextOrEmpty(before_entry);
    if (after[0] == 0 and before[0] == 0) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Date bound required.", "Enter an after date, a before date, or both.");
        c.gtk_widget_destroy(dialog);
        return;
    }
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    if (after[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "after");
        _ = c.json_builder_add_string_value(builder, after);
    }
    if (before[0] != 0) {
        _ = c.json_builder_set_member_name(builder, "before");
        _ = c.json_builder_add_string_value(builder, before);
    }
    _ = c.json_builder_set_member_name(builder, "limit");
    _ = c.json_builder_add_int_value(builder, c.gtk_spin_button_get_value_as_int(@ptrCast(@alignCast(limit_spin))));
    _ = c.json_builder_set_member_name(builder, "includeReplies");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(include_replies))));
    _ = c.json_builder_set_member_name(builder, "keepPinned");
    _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(keep_pinned))));
    _ = c.json_builder_set_member_name(builder, "dryRun");
    _ = c.json_builder_add_boolean_value(builder, if (result == c.GTK_RESPONSE_APPLY) TRUE else FALSE);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(constants.TWEET_BULK_DELETE_URL, payload, "POST", &response) != FALSE) {
        var parser: ?*c.JsonParser = null;
        defer if (parser != null) c.g_object_unref(parser);
        const root = parseRootObject(response, &parser);
        if (root == null) {
            showModalMessage(c.GTK_MESSAGE_INFO, "Bulk delete request completed.", null);
        } else if (result == c.GTK_RESPONSE_APPLY) {
            const preview = jsonObjectMember(root, "preview");
            const summary = c.g_strdup_printf("Matching posts: %ld\nBatch limit: %ld", jsonInt(preview, "total"), jsonInt(preview, "limit"));
            defer c.g_free(summary);
            showModalMessage(c.GTK_MESSAGE_INFO, "Bulk Delete Preview", summary);
        } else {
            const summary = c.g_strdup_printf("Deleted: %ld\nRemaining: %ld", jsonInt(root, "deleted"), jsonInt(root, "remaining"));
            defer c.g_free(summary);
            showModalMessage(c.GTK_MESSAGE_INFO, "Bulk Delete Complete", summary);
            if (g.g_main_list_box != null) start_loading_timeline(@ptrCast(@alignCast(g.g_main_list_box)));
        }
    } else {
        var error_message = extractErrorMessage(response);
        defer c.g_free(error_message);
        if (error_message == null) error_message = c.g_strdup("Request failed.");
        showModalMessage(c.GTK_MESSAGE_ERROR, "Bulk delete failed.", error_message);
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_remove_affiliate_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) return;
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(constants.PROFILE_REMOVE_AFFILIATE_URL, null, "DELETE", &response) != FALSE) {
        showModalMessage(c.GTK_MESSAGE_INFO, "Affiliate removed.", null);
    } else {
        const error_message = extractErrorMessage(response);
        defer c.g_free(error_message);
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not remove affiliate.", error_message);
    }
}
export fn on_create_shop_product_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    const editing = @intFromPtr(user_data) == 1;
    const product_id = if (editing) objectStringData(widget, "product_id") else null;
    const dialog = c.gtk_dialog_new_with_buttons(
        if (editing) "Edit Product" else "New Product",
        null,
        c.GTK_DIALOG_MODAL,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        if (editing) lit("_Save") else lit("_Create"),
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    c.gtk_window_set_default_size(@ptrCast(@alignCast(dialog)), 560, 420);
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const title_entry = c.gtk_entry_new();
    const price_entry = c.gtk_entry_new();
    const description_entry = c.gtk_entry_new();
    const image_entry = c.gtk_entry_new();
    const type_combo = c.gtk_combo_box_text_new();
    const content_view = c.gtk_text_view_new();
    const content_scroll = c.gtk_scrolled_window_new(null, null);
    c.gtk_text_view_set_wrap_mode(@ptrCast(@alignCast(content_view)), c.GTK_WRAP_WORD_CHAR);
    c.gtk_widget_set_size_request(content_scroll, -1, 140);
    c.gtk_container_add(@ptrCast(@alignCast(content_scroll)), content_view);
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(title_entry)), "Title");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(price_entry)), "Price in INR");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(description_entry)), "Description");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(image_entry)), "Image URL");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(type_combo)), "text", "Text");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(type_combo)), "link", "Link");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(type_combo)), "image", "Image");
    c.gtk_combo_box_set_active(@ptrCast(@alignCast(type_combo)), 0);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Title:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), title_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Price:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), price_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Description:"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), description_entry, 1, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Image:"), 0, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), image_entry, 1, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Content type:"), 0, 4, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), type_combo, 1, 4, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Content:"), 0, 5, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), content_scroll, 1, 5, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const product_content = textViewText(content_view);
        defer c.g_free(product_content);
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "title");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(title_entry));
        _ = c.json_builder_set_member_name(builder, "price");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(price_entry));
        _ = c.json_builder_set_member_name(builder, "description");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(description_entry));
        _ = c.json_builder_set_member_name(builder, "image_url");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(image_entry));
        _ = c.json_builder_set_member_name(builder, "content_type");
        _ = c.json_builder_add_string_value(builder, c.gtk_combo_box_get_active_id(@ptrCast(@alignCast(type_combo))));
        _ = c.json_builder_set_member_name(builder, "content");
        _ = c.json_builder_add_string_value(builder, product_content);
        _ = c.json_builder_end_object(builder);
        var error_message: [*c]c.gchar = null;
        defer c.g_free(error_message);
        if (editing) {
            const url = c.g_strdup_printf(constants.SHOP_PRODUCT_URL, product_id);
            defer c.g_free(url);
            const payload = builderPayload(builder);
            defer c.g_free(payload);
            if (requestErrorFromResponse(url, payload, "PATCH", &error_message) != FALSE) {
                start_loading_my_shop();
            } else {
                showModalMessage(c.GTK_MESSAGE_ERROR, "Product save failed.", error_message);
            }
        } else {
            const payload = builderPayload(builder);
            defer c.g_free(payload);
            if (requestErrorFromResponse(constants.SHOP_PRODUCTS_URL, payload, "POST", &error_message) != FALSE) {
                start_loading_my_shop();
            } else {
                showModalMessage(c.GTK_MESSAGE_ERROR, "Product save failed.", error_message);
            }
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_clear_for_you_interests_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) return;
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (requestErrorFromResponse(constants.FOR_YOU_INTERESTS_URL, null, "DELETE", &error_message) != FALSE) {
        start_loading_for_you_interests();
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not reset interests.", error_message);
    }
}
export fn on_add_muted_word_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_muted_word_entry == null) return;
    const word = c.gtk_entry_get_text(@ptrCast(@alignCast(g.g_muted_word_entry)));
    if (word == null or word[0] == 0) return;
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "word");
    _ = c.json_builder_add_string_value(builder, word);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (requestErrorFromResponse(constants.MUTED_WORDS_URL, payload, "POST", &error_message) != FALSE) {
        c.gtk_entry_set_text(@ptrCast(@alignCast(g.g_muted_word_entry)), "");
        start_loading_muted_words();
    } else {
        if (error_message == null) error_message = c.g_strdup("The mute request could not be sent.");
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not add muted word.", error_message);
    }
}
export fn on_mute_conversation_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{user_data});
    const tweet_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(widget)), "tweet_id"));
    if (tweet_id == null or g.g_auth_token == null) return;
    const url = c.g_strdup_printf(constants.MUTED_CONVERSATION_URL, tweet_id);
    defer c.g_free(url);
    var response: [*c]c.gchar = null;
    defer c.g_free(response);
    if (requestWithResponse(url, null, "POST", &response) != FALSE) {
        const error_message = extractErrorMessage(response);
        defer c.g_free(error_message);
        if (error_message != null) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Could not update conversation mute.", error_message);
        } else {
            start_loading_muted_conversations();
        }
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not update conversation mute.", null);
    }
}

export fn on_invite_delegate_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_delegate_username_entry == null) return;
    const username = c.gtk_entry_get_text(@ptrCast(@alignCast(g.g_delegate_username_entry)));
    if (username == null or username[0] == 0) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Username required.", "Enter the username you want to invite.");
        return;
    }
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "username");
    _ = c.json_builder_add_string_value(builder, username);
    _ = c.json_builder_end_object(builder);
    const payload = builderPayload(builder);
    defer c.g_free(payload);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (delegateActionWithError("%s", constants.DELEGATES_INVITE_URL, "POST", payload, &error_message) != FALSE) {
        c.gtk_entry_set_text(@ptrCast(@alignCast(g.g_delegate_username_entry)), "");
        start_loading_delegates();
    } else {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Invite failed.", error_message);
    }
}
export fn on_schedule_post_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to schedule posts.");
        return;
    }
    const dialog = c.gtk_dialog_new_with_buttons(
        "Schedule Post",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Schedule",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    c.gtk_window_set_default_size(@ptrCast(@alignCast(dialog)), 560, 360);
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const time_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(time_entry)), "YYYY-MM-DDTHH:MM:SSZ");
    const text_view = c.gtk_text_view_new();
    c.gtk_text_view_set_wrap_mode(@ptrCast(@alignCast(text_view)), c.GTK_WRAP_WORD_CHAR);
    const text_scroll = c.gtk_scrolled_window_new(null, null);
    c.gtk_widget_set_size_request(text_scroll, -1, 180);
    c.gtk_container_add(@ptrCast(@alignCast(text_scroll)), text_view);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("When:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), time_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Post:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), text_scroll, 1, 1, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const content = textViewText(text_view);
        defer c.g_free(content);
        const scheduled_for = entryTextOrEmpty(time_entry);
        if (content == null or content[0] == 0 or scheduled_for[0] == 0) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Schedule failed.", "Content and scheduled time are required.");
        } else {
            const builder = c.json_builder_new();
            defer c.g_object_unref(builder);
            _ = c.json_builder_begin_object(builder);
            _ = c.json_builder_set_member_name(builder, "content");
            _ = c.json_builder_add_string_value(builder, content);
            _ = c.json_builder_set_member_name(builder, "scheduled_for");
            _ = c.json_builder_add_string_value(builder, scheduled_for);
            _ = c.json_builder_set_member_name(builder, "reply_restriction");
            _ = c.json_builder_add_string_value(builder, "everyone");
            _ = c.json_builder_end_object(builder);
            const payload = builderPayload(builder);
            defer c.g_free(payload);
            var response: [*c]c.gchar = null;
            defer c.g_free(response);
            if (requestWithResponse(constants.SCHEDULED_POSTS_URL, payload, "POST", &response) != FALSE) {
                start_loading_scheduled_posts();
            } else {
                const server_error = extractErrorMessage(response);
                defer c.g_free(server_error);
                showModalMessage(c.GTK_MESSAGE_ERROR, "Schedule failed.", if (server_error != null) server_error else "Post could not be scheduled.");
            }
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_explore_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    setStack("explore");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
    start_loading_explore();
}
export fn on_explore_category_changed(combo: [*c]c.GtkComboBox, user_data: c.gpointer) void {
    unused(.{ combo, user_data });
    start_loading_explore();
}
export fn on_articles_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    setStack("articles");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
    start_loading_articles();
}
export fn on_compose_article_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_auth_token == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to publish articles.");
        return;
    }
    const dialog = c.gtk_dialog_new_with_buttons(
        "New Article",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Publish",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    c.gtk_window_set_default_size(@ptrCast(@alignCast(dialog)), 680, 560);
    const box_widget = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(box_widget)), 10);
    const title_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(title_entry)), "Article title");
    const body_view = c.gtk_text_view_new();
    c.gtk_text_view_set_wrap_mode(@ptrCast(@alignCast(body_view)), c.GTK_WRAP_WORD_CHAR);
    const body_scroll = c.gtk_scrolled_window_new(null, null);
    c.gtk_container_add(@ptrCast(@alignCast(body_scroll)), body_view);
    c.gtk_box_pack_start(@ptrCast(@alignCast(box_widget)), title_entry, FALSE, FALSE, 0);
    c.gtk_box_pack_start(@ptrCast(@alignCast(box_widget)), body_scroll, TRUE, TRUE, 0);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), box_widget, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const body = textViewText(body_view);
        defer c.g_free(body);
        const title = entryTextOrEmpty(title_entry);
        if (title[0] == 0 or cstr.len(title) < 5) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Publish failed.", "Title must be at least 5 characters.");
            c.gtk_widget_destroy(dialog);
            return;
        }
        if (body == null or cstr.len(body) < 50) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Publish failed.", "Article body must be at least 50 characters.");
            c.gtk_widget_destroy(dialog);
            return;
        }
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "title");
        _ = c.json_builder_add_string_value(builder, title);
        _ = c.json_builder_set_member_name(builder, "markdown");
        _ = c.json_builder_add_string_value(builder, body);
        _ = c.json_builder_set_member_name(builder, "source");
        _ = c.json_builder_add_string_value(builder, "tweeta-desktop");
        _ = c.json_builder_end_object(builder);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        var error_message: [*c]c.gchar = null;
        defer c.g_free(error_message);
        if (requestErrorFromResponse(constants.ARTICLES_URL, payload, "POST", &error_message) != FALSE) {
            showModalMessage(c.GTK_MESSAGE_INFO, "Article published.", "Your article is now available.");
            start_loading_articles();
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Publish failed.", if (error_message != null) error_message else "Article could not be published.");
        }
    }
    c.gtk_widget_destroy(dialog);
}

export fn on_dm_invite_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const conversation_id = activeDmConversationId();
    if (conversation_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Group Invite",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Create",
        c.GTK_RESPONSE_ACCEPT,
        "_Revoke",
        c.GTK_RESPONSE_REJECT,
        @as(?*anyopaque, null),
    );
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    const max_uses = c.gtk_spin_button_new_with_range(0, 500, 1);
    const expires = c.gtk_spin_button_new_with_range(0, 30, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), c.gtk_label_new("Max uses (0 for unlimited):"), FALSE, FALSE, 6);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), max_uses, FALSE, FALSE, 0);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), c.gtk_label_new("Expires in days (0 for no expiry):"), FALSE, FALSE, 6);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), expires, FALSE, FALSE, 0);
    c.gtk_widget_show_all(dialog);
    const response_id = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    if (response_id == c.GTK_RESPONSE_ACCEPT) {
        const url = c.g_strdup_printf(constants.DM_INVITE_URL, conversation_id);
        defer c.g_free(url);
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        const max_uses_value = c.gtk_spin_button_get_value_as_int(@ptrCast(@alignCast(max_uses)));
        const expires_value = c.gtk_spin_button_get_value_as_int(@ptrCast(@alignCast(expires)));
        if (max_uses_value > 0) {
            _ = c.json_builder_set_member_name(builder, "max_uses");
            _ = c.json_builder_add_int_value(builder, max_uses_value);
        }
        if (expires_value > 0) {
            _ = c.json_builder_set_member_name(builder, "expires_in_days");
            _ = c.json_builder_add_int_value(builder, expires_value);
        }
        _ = c.json_builder_end_object(builder);
        var response: [*c]c.gchar = null;
        defer c.g_free(response);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        if (requestWithResponse(url, payload, "POST", &response) != FALSE) {
            var parser: ?*c.JsonParser = null;
            defer if (parser != null) c.g_object_unref(parser);
            const root = parseRootObject(response, &parser);
            if (root != null) {
                const token = jsonString(root, "token");
                const message = c.g_strdup_printf("Invite token:\n%s", token);
                defer c.g_free(message);
                showModalMessage(c.GTK_MESSAGE_INFO, "Invite created.", message);
            }
        } else {
            const err = extractErrorMessage(response);
            defer c.g_free(err);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Could not create invite.", err);
        }
    } else if (response_id == c.GTK_RESPONSE_REJECT) {
        const url = c.g_strdup_printf(constants.DM_INVITE_REVOKE_URL, conversation_id);
        defer c.g_free(url);
        var response: [*c]c.gchar = null;
        defer c.g_free(response);
        if (requestWithResponse(url, "{}", "POST", &response) == FALSE) {
            const err = extractErrorMessage(response);
            defer c.g_free(err);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Could not revoke invite.", err);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_dm_join_invite_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const dialog = c.gtk_dialog_new_with_buttons(
        "Join Group Invite",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Join",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "Invite token");
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const token = entryTextOrEmpty(entry);
        const escaped = c.g_uri_escape_string(token, null, FALSE);
        defer c.g_free(escaped);
        const url = c.g_strdup_printf(constants.DM_INVITE_JOIN_URL, escaped);
        defer c.g_free(url);
        var response: [*c]c.gchar = null;
        if (requestWithResponse(url, "{}", "POST", &response) != FALSE) {
            setStack("messages");
            if (g.g_conversations_list != null) start_loading_conversations(@ptrCast(@alignCast(g.g_conversations_list)));
        } else {
            const err = extractErrorMessage(response);
            defer c.g_free(err);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Could not join invite.", err);
        }
        c.g_free(response);
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_dm_send_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g.g_dm_messages_list == null or g.g_dm_entry == null) return;
    const conversation_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_dm_messages_list)), "conversation_id"));
    const p2p_recipient: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_dm_messages_list)), "p2p_recipient"));
    const content = c.gtk_entry_get_text(@ptrCast(@alignCast(g.g_dm_entry)));
    const reply_to = dmContextString("reply_to_id");
    const pending_file_path = dmContextString("pending_file_path");
    const pending_file_type = dmContextString("pending_file_type");

    if (p2p_recipient != null and g.g_p2p_session != null) {
        c.g_mutex_lock(&g.g_p2p_session.*.session_mutex);
        const contact: [*c]types.P2PContact = @ptrCast(@alignCast(c.g_hash_table_lookup(g.g_p2p_session.*.contacts, p2p_recipient)));
        const fingerprint = if (contact != null and contact.*.public_key_fingerprint != null) c.g_strdup(contact.*.public_key_fingerprint) else null;
        c.g_mutex_unlock(&g.g_p2p_session.*.session_mutex);
        defer c.g_free(fingerprint);
        if (fingerprint != null and content != null and content[0] != 0) {
            _ = api.p2p_send_message(p2p_recipient, content, fingerprint);
            c.gtk_entry_set_text(@ptrCast(@alignCast(g.g_dm_entry)), "");
            _ = api.on_p2p_contact_clicked(null, null, null);
        }
        return;
    }

    if (conversation_id == null or ((content == null or content[0] == 0) and pending_file_path == null)) return;

    const url = c.g_strdup_printf(constants.DM_SEND_MESSAGE_URL, conversation_id);
    defer c.g_free(url);
    const builder = c.json_builder_new();
    defer c.g_object_unref(builder);
    var uploaded_url: [*c]c.gchar = null;
    defer c.g_free(uploaded_url);
    _ = c.json_builder_begin_object(builder);
    if (content != null) {
        _ = c.json_builder_set_member_name(builder, "content");
        _ = c.json_builder_add_string_value(builder, content);
    }
    if (reply_to != null) {
        _ = c.json_builder_set_member_name(builder, "replyTo");
        _ = c.json_builder_add_string_value(builder, reply_to);
    }
    if (pending_file_path != null) {
        uploaded_url = perform_media_upload(pending_file_path);
        if (uploaded_url != null) {
            const basename = c.g_path_get_basename(pending_file_path);
            defer c.g_free(basename);
            var stat_buf: c.GStatBuf = undefined;
            _ = c.json_builder_set_member_name(builder, "files");
            _ = c.json_builder_begin_array(builder);
            _ = c.json_builder_begin_object(builder);
            _ = c.json_builder_set_member_name(builder, "hash");
            _ = c.json_builder_add_null_value(builder);
            _ = c.json_builder_set_member_name(builder, "name");
            _ = c.json_builder_add_string_value(builder, if (basename != null) basename else "attachment");
            _ = c.json_builder_set_member_name(builder, "type");
            _ = c.json_builder_add_string_value(builder, if (pending_file_type != null) pending_file_type else "application/octet-stream");
            _ = c.json_builder_set_member_name(builder, "size");
            _ = c.json_builder_add_int_value(builder, if (c.g_stat(pending_file_path, &stat_buf) == 0) @intCast(stat_buf.st_size) else 0);
            _ = c.json_builder_set_member_name(builder, "url");
            _ = c.json_builder_add_string_value(builder, uploaded_url);
            _ = c.json_builder_end_object(builder);
            _ = c.json_builder_end_array(builder);
        }
    }
    _ = c.json_builder_end_object(builder);
    const post_data = builderPayload(builder);
    defer c.g_free(post_data);
    var chunk: types.MemoryStruct = .{ .memory = null, .size = 0 };
    if (api.fetch_url(url, &chunk, post_data, "POST") != FALSE) {
        c.gtk_entry_set_text(@ptrCast(@alignCast(g.g_dm_entry)), "");
        c.g_object_set_data(@ptrCast(@alignCast(g.g_dm_entry)), "typing_active", null);
        const typing_url = c.g_strdup_printf(constants.DM_TYPING_STOP_URL, conversation_id);
        defer c.g_free(typing_url);
        _ = simpleRequest(typing_url, "{}", "POST");
        clearDmComposeContext();
        start_loading_messages(@ptrCast(@alignCast(g.g_dm_messages_list)), conversation_id);
        c.g_free(chunk.memory);
    }
}
export fn on_dm_permissions_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const conversation_id = activeDmConversationId();
    if (conversation_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Group Permissions",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    const send_combo = c.gtk_combo_box_text_new();
    const invite_combo = c.gtk_combo_box_text_new();
    const metadata_combo = c.gtk_combo_box_text_new();
    for ([_][*c]c.GtkWidget{ send_combo, invite_combo, metadata_combo }) |combo_widget| {
        c.gtk_combo_box_text_append(@ptrCast(@alignCast(combo_widget)), "all", "Everyone");
        c.gtk_combo_box_text_append(@ptrCast(@alignCast(combo_widget)), "admins", "Admins");
        c.gtk_combo_box_set_active(@ptrCast(@alignCast(combo_widget)), 0);
    }
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Send messages:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), send_combo, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Create invites:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), invite_combo, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Edit metadata:"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), metadata_combo, 1, 2, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const url = c.g_strdup_printf(constants.DM_PERMISSIONS_URL, conversation_id);
        defer c.g_free(url);
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "send_permission");
        _ = c.json_builder_add_string_value(builder, c.gtk_combo_box_get_active_id(@ptrCast(@alignCast(send_combo))));
        _ = c.json_builder_set_member_name(builder, "invite_permission");
        _ = c.json_builder_add_string_value(builder, c.gtk_combo_box_get_active_id(@ptrCast(@alignCast(invite_combo))));
        _ = c.json_builder_set_member_name(builder, "edit_metadata_permission");
        _ = c.json_builder_add_string_value(builder, c.gtk_combo_box_get_active_id(@ptrCast(@alignCast(metadata_combo))));
        _ = c.json_builder_end_object(builder);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        var response: [*c]c.gchar = null;
        defer c.g_free(response);
        if (requestWithResponse(url, payload, "PATCH", &response) != FALSE) {
            if (g.g_dm_messages_list != null) start_loading_messages(@ptrCast(@alignCast(g.g_dm_messages_list)), conversation_id);
        } else {
            const err = extractErrorMessage(response);
            defer c.g_free(err);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Could not update permissions.", err);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_dm_roles_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const conversation_id = activeDmConversationId();
    const conversation = activeDmConversationDetail();
    if (conversation_id == null or conversation == null or conversation.*.participants == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Roles unavailable.", "Open a group conversation before changing roles.");
        return;
    }
    const dialog = c.gtk_dialog_new_with_buttons(
        "Group Roles",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const participant = c.gtk_combo_box_text_new();
    var item = conversation.*.participants;
    while (item != null) : (item = item.*.next) {
        const profile: [*c]types.Profile = @ptrCast(@alignCast(item.*.data));
        if (profile != null and profile.*.id != null and profile.*.username != null) {
            c.gtk_combo_box_text_append(@ptrCast(@alignCast(participant)), profile.*.id, profile.*.username);
        }
    }
    c.gtk_combo_box_set_active(@ptrCast(@alignCast(participant)), 0);
    const role = c.gtk_combo_box_text_new();
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(role)), "member", "Member");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(role)), "admin", "Admin");
    c.gtk_combo_box_set_active(@ptrCast(@alignCast(role)), 0);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Participant:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), participant, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Role:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), role, 1, 1, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const participant_id = c.gtk_combo_box_get_active_id(@ptrCast(@alignCast(participant)));
        const role_id = c.gtk_combo_box_get_active_id(@ptrCast(@alignCast(role)));
        if (participant_id != null and role_id != null) {
            const url = c.g_strdup_printf(constants.DM_PARTICIPANT_ROLE_URL, conversation_id, participant_id);
            defer c.g_free(url);
            const builder = c.json_builder_new();
            defer c.g_object_unref(builder);
            _ = c.json_builder_begin_object(builder);
            _ = c.json_builder_set_member_name(builder, "role");
            _ = c.json_builder_add_string_value(builder, role_id);
            _ = c.json_builder_end_object(builder);
            const payload = builderPayload(builder);
            defer c.g_free(payload);
            var response: [*c]c.gchar = null;
            defer c.g_free(response);
            if (requestWithResponse(url, payload, "PATCH", &response) != FALSE) {
                if (g.g_dm_messages_list != null) start_loading_messages(@ptrCast(@alignCast(g.g_dm_messages_list)), conversation_id);
            } else {
                const err = extractErrorMessage(response);
                defer c.g_free(err);
                showModalMessage(c.GTK_MESSAGE_ERROR, "Could not update role.", err);
            }
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_dm_pinned_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const conversation_id = activeDmConversationId();
    if (conversation_id == null) return;
    const url = c.g_strdup_printf(constants.DM_PINNED_MESSAGES_URL, conversation_id);
    defer c.g_free(url);
    const dialog = c.gtk_dialog_new_with_buttons("Pinned Messages", null, c.GTK_DIALOG_MODAL, "_Close", c.GTK_RESPONSE_CLOSE, @as(?*anyopaque, null));
    c.gtk_window_set_default_size(@ptrCast(@alignCast(dialog)), 560, 420);
    const scroll = c.gtk_scrolled_window_new(null, null);
    const list = c.gtk_list_box_new();
    c.gtk_list_box_set_selection_mode(@ptrCast(@alignCast(list)), c.GTK_SELECTION_NONE);
    c.gtk_container_add(container(scroll), list);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), scroll, TRUE, TRUE, 0);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory != null) {
        const messages = api.parse_messages(chunk.memory);
        api.populate_message_list(@ptrCast(@alignCast(list)), messages);
        api.free_messages(messages);
    } else {
        _ = appendListLabel(@ptrCast(@alignCast(list)), "Failed to load pinned messages.");
    }
    c.gtk_widget_show_all(dialog);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}
export fn on_dm_pin_message_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const message_id = widgetStringData(widget, "message_id");
    const conversation_id = activeDmConversationId();
    if (message_id == null or conversation_id == null) return;
    const url = c.g_strdup_printf(constants.DM_PIN_MESSAGE_URL, conversation_id, message_id);
    defer c.g_free(url);
    var error_message: [*c]c.gchar = null;
    defer c.g_free(error_message);
    if (requestErrorFromResponse(url, "{}", "POST", &error_message) == FALSE) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Could not pin message.", error_message);
    }
}
export fn on_community_create_invite_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const community_id = activeCommunityId();
    if (community_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Create Community Invite",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Create",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const max_uses_spin = c.gtk_spin_button_new_with_range(0, 500, 1);
    const expires_spin = c.gtk_spin_button_new_with_range(0, 30, 1);
    c.gtk_spin_button_set_value(@ptrCast(@alignCast(expires_spin)), 7);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Max uses (0 unlimited):"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), max_uses_spin, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Expires in days (0 never):"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), expires_spin, 1, 1, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        const max_uses = c.gtk_spin_button_get_value_as_int(@ptrCast(@alignCast(max_uses_spin)));
        const expires = c.gtk_spin_button_get_value_as_int(@ptrCast(@alignCast(expires_spin)));
        if (max_uses > 0) {
            _ = c.json_builder_set_member_name(builder, "max_uses");
            _ = c.json_builder_add_int_value(builder, max_uses);
        }
        if (expires > 0) {
            _ = c.json_builder_set_member_name(builder, "expires_in_days");
            _ = c.json_builder_add_int_value(builder, expires);
        }
        _ = c.json_builder_end_object(builder);
        const url = c.g_strdup_printf(constants.COMMUNITY_INVITES_URL, community_id);
        defer c.g_free(url);
        const payload = builderPayload(builder);
        defer c.g_free(payload);
        var response: [*c]c.gchar = null;
        const ok = requestWithResponse(url, payload, "POST", &response);
        defer c.g_free(response);
        if (ok != FALSE) {
            var parser: ?*c.JsonParser = null;
            defer if (parser != null) c.g_object_unref(parser);
            const root = parseRootObject(response, &parser);
            const token = jsonString(root, "token");
            const message = c.g_strdup_printf("Invite token:\n%s", token);
            defer c.g_free(message);
            showModalMessage(c.GTK_MESSAGE_INFO, "Invite created.", if (root != null) message else null);
        } else {
            const err = extractErrorMessage(response);
            defer c.g_free(err);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Could not create invite.", err);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_community_accept_invite_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const dialog = c.gtk_dialog_new_with_buttons(
        "Join Community Invite",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Join",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "Invite token");
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const token = entryTextOrEmpty(entry);
        if (token[0] != 0) {
            const escaped = c.g_uri_escape_string(token, null, TRUE);
            defer c.g_free(escaped);
            const url = c.g_strdup_printf(constants.COMMUNITY_INVITE_ACCEPT_URL, escaped);
            defer c.g_free(url);
            var response: [*c]c.gchar = null;
            defer c.g_free(response);
            if (requestWithResponse(url, "{}", "POST", &response) != FALSE) {
                showModalMessage(c.GTK_MESSAGE_INFO, "Joined community.", null);
                if (g.g_communities_list != null) start_loading_communities(@ptrCast(@alignCast(g.g_communities_list)));
            } else {
                const err = extractErrorMessage(response);
                defer c.g_free(err);
                showModalMessage(c.GTK_MESSAGE_ERROR, "Could not join invite.", err);
            }
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_community_manage_invites_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const community_id = activeCommunityId();
    if (community_id == null) return;
    const url = c.g_strdup_printf(constants.COMMUNITY_INVITES_URL, community_id);
    defer c.g_free(url);
    const chunk = fetchGet(url);
    defer c.g_free(chunk.memory);
    if (chunk.memory == null) {
        showModalMessage(c.GTK_MESSAGE_ERROR, "Invites unavailable.", null);
        return;
    }
    const dialog = c.gtk_dialog_new_with_buttons(
        "Community Invites",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        c.GTK_RESPONSE_CLOSE,
        @as(?*anyopaque, null),
    );
    c.gtk_window_set_default_size(@ptrCast(@alignCast(dialog)), 520, 420);
    const scroll = c.gtk_scrolled_window_new(null, null);
    const list = c.gtk_list_box_new();
    c.gtk_list_box_set_selection_mode(@ptrCast(@alignCast(list)), c.GTK_SELECTION_NONE);
    c.gtk_container_add(container(scroll), list);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), scroll, TRUE, TRUE, 0);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(chunk.memory, &parser);
    const invites = if (root != null and c.json_object_has_member(root, "invites") != FALSE) c.json_object_get_array_member(root, "invites") else null;
    if (root == null) {
        _ = appendListLabel(@ptrCast(@alignCast(list)), "Could not read invites.");
    } else if (invites != null and c.json_array_get_length(invites) > 0) {
        var i: c.guint = 0;
        while (i < c.json_array_get_length(invites)) : (i += 1) {
            const invite = c.json_array_get_object_element(invites, i);
            const use_count = jsonInt(invite, "use_count");
            const max_uses = jsonInt(invite, "max_uses");
            const uses_text = if (max_uses > 0) c.g_strdup_printf("%ld/%ld", use_count, max_uses) else c.g_strdup_printf("%ld", use_count);
            defer c.g_free(uses_text);
            const expires = jsonString(invite, "expires_at");
            const text = c.g_strdup_printf("%s\nUses: %s%s%s", jsonString(invite, "token"), uses_text, if (expires[0] != 0) lit(" · Expires: ") else lit(""), if (expires[0] != 0) expires else lit(""));
            defer c.g_free(text);
            const row = c.gtk_list_box_row_new();
            const boxw = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 8);
            const label = c.gtk_label_new(text);
            const revoke = c.gtk_button_new_with_label("Revoke");
            c.gtk_label_set_xalign(@ptrCast(@alignCast(label)), 0.0);
            c.gtk_container_add(container(row), boxw);
            c.gtk_box_pack_start(asBox(boxw), label, TRUE, TRUE, 8);
            c.gtk_box_pack_end(asBox(boxw), revoke, FALSE, FALSE, 8);
            setObjectStringData(revoke, "community_id", community_id);
            setObjectStringData(revoke, "invite_id", jsonString(invite, "id"));
            connect(revoke, "clicked", onCommunityRevokeInviteClicked, null);
            c.gtk_widget_show_all(row);
            c.gtk_list_box_insert(@ptrCast(@alignCast(list)), row, -1);
        }
    } else {
        _ = appendListLabel(@ptrCast(@alignCast(list)), "No active invites.");
    }
    c.gtk_widget_show_all(dialog);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}
export fn on_community_moderation_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const community_id = activeCommunityId();
    if (community_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Community Moderation",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        c.GTK_RESPONSE_CLOSE,
        "_Approve Request",
        @as(c_int, 10),
        "_Reject Request",
        @as(c_int, 11),
        "_Save Role",
        @as(c_int, 12),
        "_Ban",
        @as(c_int, 13),
        "_Unban",
        @as(c_int, 14),
        "_Log",
        @as(c_int, 15),
        @as(?*anyopaque, null),
    );
    c.gtk_window_set_default_size(@ptrCast(@alignCast(dialog)), 600, 460);

    const notebook = c.gtk_notebook_new();
    const requests_list = c.gtk_list_box_new();
    c.gtk_list_box_set_selection_mode(@ptrCast(@alignCast(requests_list)), c.GTK_SELECTION_SINGLE);
    const requests_url = c.g_strdup_printf(constants.COMMUNITY_JOIN_REQUESTS_URL, community_id);
    defer c.g_free(requests_url);
    const requests_chunk = fetchGet(requests_url);
    defer c.g_free(requests_chunk.memory);
    var parser: ?*c.JsonParser = null;
    defer if (parser != null) c.g_object_unref(parser);
    const root = parseRootObject(requests_chunk.memory, &parser);
    const requests = if (root != null and c.json_object_has_member(root, "requests") != FALSE) c.json_object_get_array_member(root, "requests") else null;
    if (requests_chunk.memory == null) {
        _ = appendListLabel(@ptrCast(@alignCast(requests_list)), "Could not load join requests.");
    } else if (root == null) {
        _ = appendListLabel(@ptrCast(@alignCast(requests_list)), "Could not read join requests.");
    } else if (requests != null and c.json_array_get_length(requests) > 0) {
        var i: c.guint = 0;
        while (i < c.json_array_get_length(requests)) : (i += 1) {
            const req = c.json_array_get_object_element(requests, i);
            const row = c.gtk_list_box_row_new();
            const boxw = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 8);
            const username = jsonString(req, "username");
            const fallback = jsonString(req, "user_id");
            const title = c.g_strdup_printf("@%s", if (username[0] != 0) username else fallback);
            defer c.g_free(title);
            const label = c.gtk_label_new(title);
            c.gtk_label_set_xalign(@ptrCast(@alignCast(label)), 0.0);
            c.gtk_container_add(container(row), boxw);
            c.gtk_box_pack_start(asBox(boxw), label, TRUE, TRUE, 8);
            c.gtk_widget_set_name(row, jsonString(req, "id"));
            c.gtk_widget_show_all(row);
            c.gtk_list_box_insert(@ptrCast(@alignCast(requests_list)), row, -1);
        }
    } else {
        _ = appendListLabel(@ptrCast(@alignCast(requests_list)), "No pending join requests.");
    }

    const member_grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(member_grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(member_grid)), 8);
    c.gtk_container_set_border_width(container(member_grid), 10);
    const user_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(user_entry)), "Member user ID");
    const role_combo = c.gtk_combo_box_text_new();
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(role_combo)), "member", "Member");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(role_combo)), "mod", "Moderator");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(role_combo)), "admin", "Admin");
    c.gtk_combo_box_set_active(@ptrCast(@alignCast(role_combo)), 0);
    const reason_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(reason_entry)), "Ban reason");
    c.gtk_grid_attach(@ptrCast(@alignCast(member_grid)), c.gtk_label_new("Member:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(member_grid)), user_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(member_grid)), c.gtk_label_new("Role:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(member_grid)), role_combo, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(member_grid)), c.gtk_label_new("Reason:"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(member_grid)), reason_entry, 1, 2, 1, 1);

    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), requests_list, c.gtk_label_new("Join Requests"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), member_grid, c.gtk_label_new("Members"));
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), notebook, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);

    const response = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    if (response == 10 or response == 11) {
        const selected = c.gtk_list_box_get_selected_row(@ptrCast(@alignCast(requests_list)));
        const request_id = if (selected != null) c.gtk_widget_get_name(@ptrCast(@alignCast(selected))) else null;
        if (request_id != null and request_id[0] != 0) {
            const url = c.g_strdup_printf(if (response == 10) constants.COMMUNITY_JOIN_REQUEST_APPROVE_URL else constants.COMMUNITY_JOIN_REQUEST_REJECT_URL, community_id, request_id);
            defer c.g_free(url);
            var resp: [*c]c.gchar = null;
            defer c.g_free(resp);
            if (requestWithResponse(url, "{}", "POST", &resp) != FALSE) {
                showModalMessage(c.GTK_MESSAGE_INFO, if (response == 10) "Request approved." else "Request rejected.", null);
            } else {
                const err = extractErrorMessage(resp);
                defer c.g_free(err);
                showModalMessage(c.GTK_MESSAGE_ERROR, "Request update failed.", err);
            }
        }
    } else if (response == 12 or response == 13 or response == 14) {
        const user_id = entryTextOrEmpty(user_entry);
        if (user_id[0] != 0) {
            var url: [*c]c.gchar = null;
            var builder: ?*c.JsonBuilder = null;
            if (response == 12) {
                builder = c.json_builder_new();
                _ = c.json_builder_begin_object(builder);
                _ = c.json_builder_set_member_name(builder, "role");
                _ = c.json_builder_add_string_value(builder, c.gtk_combo_box_get_active_id(@ptrCast(@alignCast(role_combo))));
                _ = c.json_builder_end_object(builder);
                url = c.g_strdup_printf(constants.COMMUNITY_MEMBER_ROLE_URL, community_id, user_id);
            } else if (response == 13) {
                builder = c.json_builder_new();
                _ = c.json_builder_begin_object(builder);
                _ = c.json_builder_set_member_name(builder, "reason");
                _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(reason_entry));
                _ = c.json_builder_end_object(builder);
                url = c.g_strdup_printf(constants.COMMUNITY_MEMBER_BAN_URL, community_id, user_id);
            } else {
                url = c.g_strdup_printf(constants.COMMUNITY_MEMBER_UNBAN_URL, community_id, user_id);
            }
            defer c.g_free(url);
            defer if (builder != null) c.g_object_unref(builder);
            var resp: [*c]c.gchar = null;
            defer c.g_free(resp);
            const ok = if (builder != null) requestBuilderWithResponse(url, "POST", builder.?, &resp) else requestWithResponse(url, "{}", "POST", &resp);
            if (ok != FALSE) {
                showModalMessage(c.GTK_MESSAGE_INFO, "Member updated.", null);
            } else {
                const err = extractErrorMessage(resp);
                defer c.g_free(err);
                showModalMessage(c.GTK_MESSAGE_ERROR, "Member update failed.", err);
            }
        }
    } else if (response == 15) {
        const url = c.g_strdup_printf(constants.COMMUNITY_MOD_LOG_URL, community_id);
        defer c.g_free(url);
        const chunk = fetchGet(url);
        defer c.g_free(chunk.memory);
        if (chunk.memory == null) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Moderation log unavailable.", null);
        } else {
            const text = formatCommunityModLogResponse(chunk.memory);
            defer c.g_free(text);
            showModalMessage(c.GTK_MESSAGE_INFO, "Moderation Log", text);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_community_style_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const community_id = activeCommunityId();
    if (community_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Community Style",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save Tag",
        @as(c_int, 20),
        "_Upload Icon",
        @as(c_int, 21),
        "_Upload Banner",
        @as(c_int, 22),
        @as(?*anyopaque, null),
    );
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(@ptrCast(@alignCast(grid)), 10);
    const enabled = c.gtk_check_button_new_with_label("Enable profile tag");
    const emoji = c.gtk_entry_new();
    const text = c.gtk_entry_new();
    const icon_button = c.gtk_file_chooser_button_new("Icon image", c.GTK_FILE_CHOOSER_ACTION_OPEN);
    const banner_button = c.gtk_file_chooser_button_new("Banner image", c.GTK_FILE_CHOOSER_ACTION_OPEN);
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(emoji)), "Emoji");
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(text)), "Text, up to 4 chars");
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), enabled, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Emoji:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), emoji, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Text:"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), text, 1, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Icon:"), 0, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), icon_button, 1, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new("Banner:"), 0, 4, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), banner_button, 1, 4, 1, 1);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), grid, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    const response = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    if (response == 20) {
        const url = c.g_strdup_printf(constants.COMMUNITY_TAG_URL, community_id);
        defer c.g_free(url);
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "tag_enabled");
        _ = c.json_builder_add_boolean_value(builder, c.gtk_toggle_button_get_active(@ptrCast(@alignCast(enabled))));
        _ = c.json_builder_set_member_name(builder, "tag_emoji");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(emoji));
        _ = c.json_builder_set_member_name(builder, "tag_text");
        _ = c.json_builder_add_string_value(builder, entryTextOrEmpty(text));
        _ = c.json_builder_end_object(builder);
        var resp: [*c]c.gchar = null;
        defer c.g_free(resp);
        if (requestBuilderWithResponse(url, "PATCH", builder, &resp) != FALSE) {
            showModalMessage(c.GTK_MESSAGE_INFO, "Tag updated.", null);
        } else {
            const err = extractErrorMessage(resp);
            defer c.g_free(err);
            showModalMessage(c.GTK_MESSAGE_ERROR, "Tag update failed.", err);
        }
    } else if (response == 21 or response == 22) {
        const chooser = if (response == 21) icon_button else banner_button;
        const file = c.gtk_file_chooser_get_filename(@ptrCast(@alignCast(chooser)));
        defer c.g_free(file);
        const media = if (file != null) perform_media_upload(file) else null;
        defer c.g_free(media);
        if (media != null) {
            const url = c.g_strdup_printf(if (response == 21) constants.COMMUNITY_ICON_URL else constants.COMMUNITY_BANNER_URL, community_id);
            defer c.g_free(url);
            const builder = c.json_builder_new();
            defer c.g_object_unref(builder);
            _ = c.json_builder_begin_object(builder);
            _ = c.json_builder_set_member_name(builder, if (response == 21) "icon" else "banner");
            _ = c.json_builder_add_string_value(builder, media);
            _ = c.json_builder_end_object(builder);
            var resp: [*c]c.gchar = null;
            defer c.g_free(resp);
            if (requestBuilderWithResponse(url, "POST", builder, &resp) != FALSE) {
                showModalMessage(c.GTK_MESSAGE_INFO, if (response == 21) "Icon updated." else "Banner updated.", null);
            } else {
                const err = extractErrorMessage(resp);
                defer c.g_free(err);
                showModalMessage(c.GTK_MESSAGE_ERROR, "Upload update failed.", err);
            }
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_community_pin_post_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const community_id = activeCommunityId();
    if (community_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Community Post Pin",
        widgetWindow(widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Pin",
        c.GTK_RESPONSE_ACCEPT,
        "_Unpin",
        c.GTK_RESPONSE_REJECT,
        @as(?*anyopaque, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "Post ID");
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    const response = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    if (response == c.GTK_RESPONSE_ACCEPT or response == c.GTK_RESPONSE_REJECT) {
        const tweet_id = entryTextOrEmpty(entry);
        if (tweet_id[0] != 0) {
            const url = c.g_strdup_printf(if (response == c.GTK_RESPONSE_ACCEPT) constants.COMMUNITY_TWEET_PIN_URL else constants.COMMUNITY_TWEET_UNPIN_URL, community_id, tweet_id);
            defer c.g_free(url);
            var resp: [*c]c.gchar = null;
            defer c.g_free(resp);
            if (requestWithResponse(url, "{}", "POST", &resp) != FALSE) {
                start_loading_community_tweets(@ptrCast(@alignCast(g.g_community_tweets_list)), community_id);
            } else {
                const err = extractErrorMessage(resp);
                defer c.g_free(err);
                showModalMessage(c.GTK_MESSAGE_ERROR, "Pin update failed.", err);
            }
        }
    }
    c.gtk_widget_destroy(dialog);
}

export fn perform_admin_verify(username: [*c]const c.gchar, verify: c.gboolean) void {
    if (!hasAdminSession() or username == null) return;
    const url = c.g_strdup_printf("%s/%s", constants.ADMIN_USERS_URL, username);
    const payload = c.g_strdup_printf("{\"verified\": %s}", if (verify != FALSE) lit("true") else lit("false"));
    defer c.g_free(url);
    defer c.g_free(payload);
    if (adminSimpleRequest(url, payload, "PATCH") != FALSE) start_loading_admin_users(entryText(g.g_admin_users_search));
}
export fn perform_admin_suspend(username: [*c]const c.gchar, reason: [*c]const c.gchar) void {
    if (!hasAdminSession() or username == null) return;
    const url = c.g_strdup_printf("%s/%s/suspend", constants.ADMIN_USERS_URL, username);
    const payload = c.g_strdup_printf("{\"reason\": \"%s\", \"action\": \"suspend\"}", textOr(reason));
    defer c.g_free(url);
    defer c.g_free(payload);
    if (adminSimpleRequest(url, payload, "POST") != FALSE) start_loading_admin_users(entryText(g.g_admin_users_search));
}
export fn perform_admin_delete_user(username: [*c]const c.gchar) void {
    if (!hasAdminSession() or username == null) return;
    const url = c.g_strdup_printf("%s/%s", constants.ADMIN_USERS_URL, username);
    defer c.g_free(url);
    if (adminSimpleRequest(url, null, "DELETE") != FALSE) start_loading_admin_users(entryText(g.g_admin_users_search));
}
export fn perform_admin_delete_post(post_id: [*c]const c.gchar) void {
    if (!hasAdminSession() or post_id == null) return;
    const url = c.g_strdup_printf("%s/%s", constants.ADMIN_POSTS_URL, post_id);
    defer c.g_free(url);
    if (adminSimpleRequest(url, null, "DELETE") != FALSE) start_loading_admin_posts(entryText(g.g_admin_posts_search));
}

export fn on_p2p_send_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    if (g_p2p_current_contact == null) {
        showModalMessage(c.GTK_MESSAGE_WARNING, "Please select a contact first.", null);
        return;
    }
    if (g.g_p2p_entry == null) return;
    const plaintext = c.gtk_entry_get_text(@ptrCast(@alignCast(g.g_p2p_entry)));
    if (plaintext == null or plaintext[0] == 0) return;
    p2p_send_encrypted_message(g_p2p_current_contact, plaintext);
    c.gtk_entry_set_text(@ptrCast(@alignCast(g.g_p2p_entry)), "");
}
export fn on_p2p_setup_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const dialog = c.gtk_dialog_new_with_buttons(
        "P2P Encryption Setup",
        null,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Generate Keys",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    c.gtk_container_set_border_width(container(asWidget(content)), 20);
    const info = c.gtk_label_new("This will generate a new GPG key pair for P2P encrypted messaging.\nYour private key will be stored locally.\nYour public key can be shared with contacts to enable encrypted communication.");
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(info)), TRUE);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), info, FALSE, FALSE, 10);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), c.gtk_label_new("Passphrase (optional):"), FALSE, FALSE, 5);
    const passphrase_entry = c.gtk_entry_new();
    c.gtk_entry_set_visibility(@ptrCast(@alignCast(passphrase_entry)), FALSE);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), passphrase_entry, FALSE, FALSE, 5);
    c.gtk_widget_show_all(content);

    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const username = getUsernameSafe();
        defer c.g_free(username);
        if (username == null) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "You must be logged in to generate keys.", null);
            c.gtk_widget_destroy(dialog);
            return;
        }
        const email = c.g_strdup_printf("%s@tweetapus.local", username);
        defer c.g_free(email);
        const passphrase = c.gtk_entry_get_text(@ptrCast(@alignCast(passphrase_entry)));
        const fingerprint = api.p2p_generate_keypair(username, email, if (passphrase != null and passphrase[0] != 0) passphrase else null);
        defer c.g_free(fingerprint);
        if (fingerprint != null) {
            const status = c.g_strdup_printf("Key: %s", fingerprint);
            defer c.g_free(status);
            labelSet(g.g_p2p_status_label, status);
            const public_key = api.p2p_export_public_key(fingerprint);
            defer c.g_free(public_key);
            if (public_key != null) {
                const key_dialog = c.gtk_dialog_new_with_buttons("Your Public Key", @ptrCast(@alignCast(dialog)), c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", c.GTK_RESPONSE_CLOSE, @as(?*anyopaque, null));
                const key_content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(key_dialog)));
                const key_view = c.gtk_text_view_new();
                c.gtk_text_view_set_editable(@ptrCast(@alignCast(key_view)), FALSE);
                const buffer = c.gtk_text_view_get_buffer(@ptrCast(@alignCast(key_view)));
                c.gtk_text_buffer_set_text(buffer, public_key, -1);
                const key_scroll = c.gtk_scrolled_window_new(null, null);
                c.gtk_widget_set_size_request(key_scroll, 500, 300);
                c.gtk_container_add(container(key_scroll), key_view);
                c.gtk_box_pack_start(@ptrCast(@alignCast(key_content)), key_scroll, TRUE, TRUE, 0);
                c.gtk_widget_show_all(key_content);
                _ = c.gtk_dialog_run(@ptrCast(@alignCast(key_dialog)));
                c.gtk_widget_destroy(key_dialog);
            }
        } else {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Failed to generate key pair.", null);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn on_p2p_contact_row_selected(box: [*c]c.GtkListBox, row: [*c]c.GtkListBoxRow, user_data: c.gpointer) void {
    unused(.{ box, user_data });
    if (row == null) return;
    const child = c.gtk_bin_get_child(@ptrCast(@alignCast(row)));
    const username: [*c]const c.gchar = if (child != null) @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(child)), "contact_username")) else null;
    c.g_mutex_lock(&g_p2p_mutex);
    c.g_free(g_p2p_current_contact);
    g_p2p_current_contact = if (username != null) c.g_strdup(username) else null;
    c.g_mutex_unlock(&g_p2p_mutex);
    if (username == null) return;
    const display_name: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(child)), "contact_name"));
    const title = c.g_strdup_printf("P2P: %s", if (display_name != null) display_name else username);
    defer c.g_free(title);
    labelSet(g.g_p2p_title_label, title);
    p2p_refresh_messages_list(username);
}
export fn on_p2p_generate_keys_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    on_p2p_setup_clicked(widget, user_data);
}
export fn on_p2p_import_contact_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    unused(.{ widget, user_data });
    const dialog = c.gtk_dialog_new_with_buttons(
        "Import Contact",
        null,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Import",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const username_entry = c.gtk_entry_new();
    const key_view = c.gtk_text_view_new();
    c.gtk_widget_set_size_request(key_view, 400, 200);
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    c.gtk_container_set_border_width(container(asWidget(content)), 20);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), c.gtk_label_new("Contact Username:"), FALSE, FALSE, 5);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), username_entry, FALSE, FALSE, 5);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), c.gtk_label_new("Public Key (armored):"), FALSE, FALSE, 5);
    const key_scroll = c.gtk_scrolled_window_new(null, null);
    c.gtk_container_add(container(key_scroll), key_view);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), key_scroll, TRUE, TRUE, 5);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const contact_username = entryTextOrEmpty(username_entry);
        const key_armor = textViewText(key_view);
        defer c.g_free(key_armor);
        if (contact_username[0] != 0 and key_armor != null and key_armor[0] != 0 and api.p2p_import_public_key(key_armor, null) != FALSE) {
            const contact: [*c]types.P2PContact = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.P2PContact))));
            contact.*.username = c.g_strdup(contact_username);
            contact.*.display_name = c.g_strdup(contact_username);
            contact.*.public_key_armor = c.g_strdup(key_armor);
            if (g.g_p2p_session != null) {
                c.g_mutex_lock(&g.g_p2p_session.*.session_mutex);
                _ = c.g_hash_table_insert(g.g_p2p_session.*.contacts, c.g_strdup(contact_username), contact);
                c.g_mutex_unlock(&g.g_p2p_session.*.session_mutex);
            }
            p2p_refresh_contacts_list();
        } else if (contact_username[0] != 0 and key_armor != null and key_armor[0] != 0) {
            showModalMessage(c.GTK_MESSAGE_ERROR, "Failed to import public key.", null);
        }
    }
    c.gtk_widget_destroy(dialog);
}
export fn p2p_init_session(username: [*c]const c.gchar) c.gboolean {
    if (username == null) return FALSE;
    if (g.g_p2p_session != null) {
        return TRUE;
    }
    if (api.p2p_crypto_init() == FALSE) {
        logWarning("Failed to initialize P2P crypto", .{});
        return FALSE;
    }
    var config = std.mem.zeroes(types.P2PTransportConfig);
    config.mode = types.P2PTransportMode.P2P_TRANSPORT_TWEETAPUS;
    config.local_username = @constCast(username);
    config.local_key_fingerprint = @constCast(api.p2p_get_local_fingerprint());
    if (api.p2p_network_init(&config) == FALSE) {
        logWarning("Failed to initialize P2P network", .{});
        return FALSE;
    }
    const session: [*c]types.P2PSession = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.P2PSession))));
    if (session == null) return FALSE;
    session.*.local_username = c.g_strdup(username);
    session.*.contacts = c.g_hash_table_new_full(c.g_str_hash, c.g_str_equal, c.g_free, p2p_free_contact);
    session.*.conversations = c.g_hash_table_new_full(c.g_str_hash, c.g_str_equal, c.g_free, p2p_free_message_list);
    c.g_mutex_init(&session.*.session_mutex);
    g.g_p2p_session = session;
    return TRUE;
}
export fn p2p_send_encrypted_message(recipient: [*c]const c.gchar, plaintext: [*c]const c.gchar) void {
    if (recipient == null or plaintext == null or g.g_p2p_session == null) return;
    c.g_mutex_lock(&g.g_p2p_session.*.session_mutex);
    const contact: [*c]types.P2PContact = @ptrCast(@alignCast(c.g_hash_table_lookup(g.g_p2p_session.*.contacts, recipient)));
    const fingerprint = if (contact != null) contact.*.public_key_fingerprint else null;
    c.g_mutex_unlock(&g.g_p2p_session.*.session_mutex);
    if (contact == null or fingerprint == null) {
        logWarning("No public key for recipient: %s", .{recipient});
        return;
    }
    const encrypted = api.p2p_encrypt_message(plaintext, fingerprint);
    if (encrypted == null) {
        logWarning("Failed to encrypt message for %s", .{recipient});
        return;
    }
    const msg: [*c]types.P2PMessage = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.P2PMessage))));
    if (msg == null) {
        c.g_free(encrypted);
        return;
    }
    msg.*.id = c.g_strdup_printf("p2p_%ld", std.time.timestamp());
    msg.*.sender_username = c.g_strdup(g.g_p2p_session.*.local_username);
    msg.*.recipient_username = c.g_strdup(recipient);
    msg.*.plaintext_content = c.g_strdup(plaintext);
    msg.*.encrypted_content = encrypted;
    msg.*.timestamp = c.g_strdup("");
    msg.*.is_outgoing = TRUE;
    msg.*.is_verified = TRUE;
    c.g_mutex_lock(&g.g_p2p_session.*.session_mutex);
    const conversation: [*c]c.GList = @ptrCast(@alignCast(c.g_hash_table_lookup(g.g_p2p_session.*.conversations, recipient)));
    const updated = c.g_list_append(conversation, msg);
    _ = c.g_hash_table_insert(g.g_p2p_session.*.conversations, c.g_strdup(recipient), updated);
    c.g_mutex_unlock(&g.g_p2p_session.*.session_mutex);
    p2p_refresh_messages_list(recipient);
    logDebug("P2P message encrypted and stored for %s", .{recipient});
}
export fn p2p_refresh_contacts_list() void {
    if (g.g_p2p_contacts_list == null or g.g_p2p_session == null) return;
    const contacts_list = listBox(g.g_p2p_contacts_list);
    clearListBox(contacts_list);
    c.g_mutex_lock(&g.g_p2p_session.*.session_mutex);
    var iter: c.GHashTableIter = undefined;
    var key: c.gpointer = null;
    var value: c.gpointer = null;
    c.g_hash_table_iter_init(&iter, g.g_p2p_session.*.contacts);
    while (c.g_hash_table_iter_next(&iter, &key, &value) != FALSE) {
        const contact: [*c]types.P2PContact = @ptrCast(@alignCast(value));
        const row_box = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
        c.gtk_container_set_border_width(container(row_box), 10);
        const avatar = c.gtk_image_new_from_icon_name("avatar-default", c.GTK_ICON_SIZE_MENU);
        c.gtk_widget_set_size_request(avatar, 32, 32);
        c.gtk_box_pack_start(asBox(row_box), avatar, FALSE, FALSE, 0);
        const name_label = c.gtk_label_new(if (contact.*.display_name != null) contact.*.display_name else contact.*.username);
        c.gtk_label_set_xalign(@ptrCast(@alignCast(name_label)), 0.0);
        c.gtk_box_pack_start(asBox(row_box), name_label, TRUE, TRUE, 0);
        c.gtk_box_pack_end(asBox(row_box), c.gtk_label_new(if (contact.*.is_online != FALSE) "●" else "○"), FALSE, FALSE, 0);
        setObjectStringData(row_box, "contact_username", contact.*.username);
        setObjectStringData(row_box, "contact_name", if (contact.*.display_name != null) contact.*.display_name else contact.*.username);
        const row = c.gtk_list_box_row_new();
        c.gtk_container_add(container(row), row_box);
        c.gtk_list_box_insert(contacts_list, row, -1);
        c.gtk_widget_show_all(row);
    }
    c.g_mutex_unlock(&g.g_p2p_session.*.session_mutex);
}
export fn p2p_refresh_messages_list(contact_username: [*c]const c.gchar) void {
    if (g.g_p2p_messages_list == null or g.g_p2p_session == null or contact_username == null) return;
    const messages_list = listBox(g.g_p2p_messages_list);
    clearListBox(messages_list);
    c.g_mutex_lock(&g.g_p2p_session.*.session_mutex);
    var messages: [*c]c.GList = @ptrCast(@alignCast(c.g_hash_table_lookup(g.g_p2p_session.*.conversations, contact_username)));
    c.g_mutex_unlock(&g.g_p2p_session.*.session_mutex);
    while (messages != null) : (messages = messages.*.next) {
        const msg: [*c]types.P2PMessage = @ptrCast(@alignCast(messages.*.data));
        if (msg == null) continue;
        const msg_box = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
        c.gtk_container_set_border_width(container(msg_box), 5);
        if (msg.*.is_outgoing != FALSE) {
            c.gtk_widget_set_halign(msg_box, c.GTK_ALIGN_END);
        } else {
            c.gtk_widget_set_halign(msg_box, c.GTK_ALIGN_START);
        }
        const content_label = c.gtk_label_new(textOr(msg.*.plaintext_content));
        c.gtk_label_set_line_wrap(@ptrCast(@alignCast(content_label)), TRUE);
        c.gtk_widget_set_size_request(content_label, 200, -1);
        const context = c.gtk_widget_get_style_context(content_label);
        c.gtk_style_context_add_class(context, if (msg.*.is_outgoing != FALSE) "message-out" else "message-in");
        c.gtk_box_pack_start(asBox(msg_box), content_label, FALSE, FALSE, 0);
        c.gtk_list_box_insert(messages_list, msg_box, -1);
        c.gtk_widget_show_all(msg_box);
    }
}
fn p2p_free_message_list(data: c.gpointer) callconv(.c) void {
    c.g_list_free_full(@ptrCast(@alignCast(data)), p2p_free_message);
}
export fn p2p_free_contact(data: c.gpointer) void {
    const contact: [*c]types.P2PContact = @ptrCast(@alignCast(data));
    if (contact == null) return;
    c.g_free(contact.*.username);
    c.g_free(contact.*.display_name);
    c.g_free(contact.*.public_key_fingerprint);
    c.g_free(contact.*.public_key_armor);
    c.g_free(contact.*.avatar_url);
    c.g_free(contact.*.last_seen);
    c.g_free(contact.*.direct_host);
    c.g_free(contact);
}
export fn p2p_free_message(data: c.gpointer) void {
    const msg: [*c]types.P2PMessage = @ptrCast(@alignCast(data));
    if (msg == null) return;
    c.g_free(msg.*.id);
    c.g_free(msg.*.sender_username);
    c.g_free(msg.*.recipient_username);
    c.g_free(msg.*.encrypted_content);
    c.g_free(msg.*.plaintext_content);
    c.g_free(msg.*.timestamp);
    c.g_free(msg);
}
export fn p2p_free_session(session: [*c]types.P2PSession) void {
    if (session == null) return;
    c.g_mutex_lock(&session.*.session_mutex);
    c.g_free(session.*.local_username);
    c.g_free(session.*.local_key_fingerprint);
    if (session.*.contacts != null) c.g_hash_table_destroy(session.*.contacts);
    if (session.*.conversations != null) c.g_hash_table_destroy(session.*.conversations);
    c.g_mutex_unlock(&session.*.session_mutex);
    c.g_mutex_clear(&session.*.session_mutex);
    c.g_free(session);
}
