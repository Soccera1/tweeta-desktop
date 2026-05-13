const std = @import("std");

const c = @import("c.zig").c;
const cstr = @import("cstr.zig");
const api = @import("api.zig");
const constants = @import("constants.zig");
const g = @import("globals_import.zig");
const types = @import("types.zig");

const TRUE: c.gboolean = 1;
const FALSE: c.gboolean = 0;
const POLL_INPUT_SINGLE: c_int = 0;
const POLL_INPUT_MULTI: c_int = 1;
const POLL_INPUT_TEXT: c_int = 2;
const POLL_INPUT_NUMBER: c_int = 3;
const POLL_INPUT_RANKING: c_int = 4;

const PollStepInput = extern struct {
    type: c_int,
    primary: [*c]c.GtkWidget,
    checks: ?*c.GPtrArray,
    option_count: c.guint,
};

const PollVoteData = extern struct {
    tweet_id: [*c]c.gchar,
    option_id: [*c]c.gchar,
};

const GdkEventButton = extern struct {
    type: c_int,
    window: ?*anyopaque,
    send_event: i8,
    time: c.guint32,
    x: f64,
    y: f64,
    axes: [*c]f64,
    state: c.guint,
    button: c.guint,
    device: ?*anyopaque,
    x_root: f64,
    y_root: f64,
};

extern fn gtk_menu_popup_at_widget(menu: ?*c.GtkMenu, widget: ?*c.GtkWidget, widget_anchor: c.GdkGravity, menu_anchor: c.GdkGravity, trigger_event: c.gpointer) void;
extern fn gtk_menu_popup_at_pointer(menu: ?*c.GtkMenu, trigger_event: c.gpointer) void;

fn widget(value: anytype) [*c]c.GtkWidget {
    return @ptrCast(@alignCast(value));
}

fn container(value: [*c]c.GtkWidget) [*c]c.GtkContainer {
    return @ptrCast(@alignCast(value));
}

fn box(value: [*c]c.GtkWidget) [*c]c.GtkBox {
    return @ptrCast(@alignCast(value));
}

fn label(value: [*c]c.GtkWidget) [*c]c.GtkLabel {
    return @ptrCast(@alignCast(value));
}

fn listBox(value: [*c]c.GtkListBox) [*c]c.GtkWidget {
    return @ptrCast(@alignCast(value));
}

fn pack(parent: [*c]c.GtkWidget, child: [*c]c.GtkWidget, expand: bool) void {
    c.gtk_box_pack_start(box(parent), child, if (expand) TRUE else FALSE, if (expand) TRUE else FALSE, 0);
}

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn debug(comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), c.G_LOG_LEVEL_DEBUG, lit(fmt) } ++ args);
}

fn warning(comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), c.G_LOG_LEVEL_WARNING, lit(fmt) } ++ args);
}

fn textOr(value: [*c]const c.gchar, fallback: [*c]const c.gchar) [*c]const c.gchar {
    return if (value != null and value[0] != 0) value else fallback;
}

fn connect(widget_: [*c]c.GtkWidget, signal: [*c]const c.gchar, callback: c.GCallback, data: c.gpointer) void {
    _ = c.g_signal_connect_data(widget_, signal, callback, data, null, c.G_CONNECT_DEFAULT);
}

export fn zig_gtk_menu_popup_at_widget(menu: [*c]c.GtkWidget, widget_: [*c]c.GtkWidget) void {
    gtk_menu_popup_at_widget(
        @ptrCast(@alignCast(menu)),
        widget_,
        c.GDK_GRAVITY_SOUTH_WEST,
        c.GDK_GRAVITY_NORTH_WEST,
        null,
    );
}

export fn zig_gtk_menu_popup_at_pointer(menu: [*c]c.GtkWidget, event: c.gpointer) void {
    gtk_menu_popup_at_pointer(@ptrCast(@alignCast(menu)), event);
}

fn freeSignalData(data: c.gpointer, closure: ?*c.GClosure) callconv(.c) void {
    _ = closure;
    c.g_free(data);
}

fn freePollVoteData(data: c.gpointer) callconv(.c) void {
    const vote_data: ?*PollVoteData = @ptrCast(@alignCast(data));
    if (vote_data) |ptr| {
        c.g_free(ptr.tweet_id);
        c.g_free(ptr.option_id);
        c.g_free(ptr);
    }
}

fn setStringData(widget_: [*c]c.GtkWidget, key: [*c]const c.gchar, value: [*c]const c.gchar) void {
    c.g_object_set_data_full(@ptrCast(@alignCast(widget_)), key, if (value != null) c.g_strdup(value) else null, c.g_free);
}

fn stringData(widget_: [*c]c.GtkWidget, key: [*c]const c.gchar) [*c]const c.gchar {
    if (widget_ == null) return null;
    return @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(widget_)), key));
}

fn tweetButton(label_text: [*c]const c.gchar, tweet_id: [*c]const c.gchar, callback: c.GCallback) [*c]c.GtkWidget {
    const button = c.gtk_button_new_with_label(label_text);
    c.gtk_button_set_relief(@ptrCast(@alignCast(button)), c.GTK_RELIEF_NONE);
    setStringData(button, lit("tweet_id"), tweet_id);
    connect(button, "clicked", callback, null);
    return button;
}

fn builderPayload(builder: [*c]c.JsonBuilder) [*c]c.gchar {
    const generator = c.json_generator_new();
    defer c.g_object_unref(generator);
    const root = c.json_builder_get_root(builder);
    defer c.json_node_free(root);
    c.json_generator_set_root(generator, root);
    return c.json_generator_to_data(generator, null);
}

fn setBoolData(widget_: [*c]c.GtkWidget, key: [*c]const c.gchar, value: c.gboolean) void {
    const state: [*c]c.gboolean = @ptrCast(@alignCast(c.g_malloc(@sizeOf(c.gboolean))));
    if (state == null) return;
    state.* = value;
    c.g_object_set_data_full(@ptrCast(@alignCast(widget_)), key, state, c.g_free);
}

fn boolData(widget_: [*c]c.GtkWidget, key: [*c]const c.gchar) ?[*c]c.gboolean {
    const data = c.g_object_get_data(@ptrCast(@alignCast(widget_)), key);
    if (data == null) return null;
    return @ptrCast(@alignCast(data));
}

fn messageButton(label_text: [*c]const c.gchar, message_id: [*c]const c.gchar, callback: c.GCallback) [*c]c.GtkWidget {
    const button = c.gtk_button_new_with_label(label_text);
    setStringData(button, lit("message_id"), message_id);
    connect(button, "clicked", callback, null);
    return button;
}

fn refreshCurrentMessages() void {
    if (g.g_dm_messages_list == null) return;
    const conversation_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_dm_messages_list)), lit("conversation_id")));
    if (conversation_id != null) api.start_loading_messages(@ptrCast(@alignCast(g.g_dm_messages_list)), conversation_id);
}

fn showTextDialog(parent_widget: [*c]c.GtkWidget, title: [*c]const c.gchar, text: [*c]const c.gchar) void {
    const dialog = c.gtk_dialog_new_with_buttons(
        title,
        widgetWindow(parent_widget),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        @as(c_int, c.GTK_RESPONSE_CLOSE),
        @as([*c]const c.gchar, null),
    );
    const scroll = c.gtk_scrolled_window_new(null, null);
    c.gtk_widget_set_size_request(scroll, 420, 360);
    const label_ = c.gtk_label_new(if (text != null) text else "");
    c.gtk_label_set_xalign(label(label_), 0.0);
    c.gtk_label_set_line_wrap(label(label_), TRUE);
    c.gtk_label_set_selectable(label(label_), TRUE);
    c.gtk_container_add(container(scroll), label_);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), scroll, TRUE, TRUE, 0);
    c.gtk_widget_show_all(dialog);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}

fn onFileSelected(chooser: [*c]c.GtkFileChooserButton, user_data: c.gpointer) callconv(.c) void {
    const ctx: [*c]types.UploadContext = @ptrCast(@alignCast(user_data));
    const filename = c.gtk_file_chooser_get_filename(@ptrCast(@alignCast(chooser)));
    debug("on_file_selected: filename=%s", .{if (filename != null) filename else lit("(null)")});
    if (filename != null and ctx != null) {
        c.g_free(ctx.*.file_path);
        ctx.*.file_path = filename;

        const basename = c.g_path_get_basename(filename);
        defer c.g_free(basename);
        const label_text = c.g_strdup_printf("Selected: %s", basename);
        defer c.g_free(label_text);
        c.gtk_label_set_text(@ptrCast(@alignCast(ctx.*.file_label)), label_text);

        c.g_free(ctx.*.file_type);
        ctx.*.file_type = api.detect_mime_type(filename);
        debug("on_file_selected: detected mime_type=%s for file=%s", .{
            if (ctx.*.file_type != null) ctx.*.file_type else lit("(null)"),
            filename,
        });
    } else {
        debug("on_file_selected: no file selected", .{});
    }
}

fn performPostTweetWithMedia(content: [*c]const c.gchar, media_url: [*c]const c.gchar, file_type: [*c]const c.gchar) c.gboolean {
    const attachments = api.build_attachment_list(media_url, file_type);
    defer if (attachments != null) c.g_list_free_full(attachments, api.free_attachment_payload);
    return api.perform_post_tweet(if (content != null) content else "", null, attachments);
}

export fn on_compose_with_media_response(dialog: [*c]c.GtkDialog, response_id: c.gint, user_data: c.gpointer) void {
    const ctx: [*c]types.UploadContext = @ptrCast(@alignCast(user_data));
    if (response_id == c.GTK_RESPONSE_ACCEPT) {
        const text_view: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(@ptrCast(@alignCast(dialog)), lit("text_view"))));
        const content_text = textViewText(text_view);
        defer c.g_free(content_text);
        var media_url: [*c]c.gchar = null;
        defer c.g_free(media_url);
        var upload_success = TRUE;

        debug("on_compose_with_media_response: ctx->file_path=%s, ctx->file_type=%s", .{
            if (ctx != null and ctx.*.file_path != null) ctx.*.file_path else lit("(null)"),
            if (ctx != null and ctx.*.file_type != null) ctx.*.file_type else lit("(null)"),
        });
        if (ctx != null and ctx.*.file_path != null) {
            debug("on_compose_with_media_response: uploading media file", .{});
            media_url = api.perform_media_upload(ctx.*.file_path);
            if (media_url == null) {
                debug("on_compose_with_media_response: media upload failed", .{});
                upload_success = FALSE;
            } else {
                debug("on_compose_with_media_response: media upload succeeded, url=%s", .{media_url});
            }
        } else {
            debug("on_compose_with_media_response: no file to upload", .{});
        }

        var has_text = FALSE;
        if (content_text != null) {
            const trimmed = c.g_strdup(content_text);
            defer c.g_free(trimmed);
            _ = c.g_strstrip(trimmed);
            has_text = if (trimmed[0] != 0) TRUE else FALSE;
            debug("on_compose_with_media_response: has_text=%d", .{has_text});
        }
        const has_attachment = if (media_url != null) TRUE else FALSE;
        debug("on_compose_with_media_response: upload_success=%d, has_attachment=%d", .{ upload_success, has_attachment });

        if (upload_success != FALSE and (has_text != FALSE or has_attachment != FALSE)) {
            const file_type: [*c]const c.gchar = if (ctx != null and ctx.*.file_type != null) ctx.*.file_type else lit("application/octet-stream");
            if (performPostTweetWithMedia(if (content_text != null) content_text else "", media_url, file_type) != FALSE) {
                api.start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
            } else {
                showMessage(@ptrCast(@alignCast(dialog)), c.GTK_MESSAGE_ERROR, "Failed to post tweet.", null);
            }
        } else if (upload_success == FALSE) {
            showMessage(@ptrCast(@alignCast(dialog)), c.GTK_MESSAGE_ERROR, "Failed to upload media.", null);
        }
    }
    if (ctx != null) {
        c.g_free(ctx.*.file_path);
        c.g_free(ctx.*.file_type);
        c.g_free(ctx);
    }
    c.gtk_widget_destroy(@ptrCast(@alignCast(dialog)));
}

export fn on_compose_with_media_clicked(widget_: [*c]c.GtkWidget, window: c.gpointer) void {
    _ = widget_;
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
    c.gtk_container_set_border_width(container(@ptrCast(@alignCast(content))), 10);
    const text_view = c.gtk_text_view_new();
    c.gtk_widget_set_size_request(text_view, 300, 150);
    c.gtk_text_view_set_wrap_mode(@ptrCast(@alignCast(text_view)), c.GTK_WRAP_WORD_CHAR);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), text_view, TRUE, TRUE, 0);
    c.g_object_set_data(@ptrCast(@alignCast(dialog)), lit("text_view"), text_view);

    const file_box = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    c.gtk_widget_set_margin_top(file_box, 10);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), file_box, FALSE, FALSE, 0);
    const file_chooser = c.gtk_file_chooser_button_new("Select Media", c.GTK_FILE_CHOOSER_ACTION_OPEN);
    c.gtk_file_chooser_button_set_title(@ptrCast(@alignCast(file_chooser)), "Select Image/Video");
    const filter = c.gtk_file_filter_new();
    c.gtk_file_filter_set_name(filter, "Images and Videos");
    c.gtk_file_filter_add_mime_type(filter, "image/png");
    c.gtk_file_filter_add_mime_type(filter, "image/jpeg");
    c.gtk_file_filter_add_mime_type(filter, "image/gif");
    c.gtk_file_filter_add_mime_type(filter, "image/webp");
    c.gtk_file_filter_add_mime_type(filter, "video/mp4");
    c.gtk_file_filter_add_pattern(filter, "*.png");
    c.gtk_file_filter_add_pattern(filter, "*.jpg");
    c.gtk_file_filter_add_pattern(filter, "*.jpeg");
    c.gtk_file_filter_add_pattern(filter, "*.gif");
    c.gtk_file_filter_add_pattern(filter, "*.webp");
    c.gtk_file_filter_add_pattern(filter, "*.mp4");
    c.gtk_file_chooser_add_filter(@ptrCast(@alignCast(file_chooser)), filter);
    c.gtk_box_pack_start(@ptrCast(@alignCast(file_box)), file_chooser, FALSE, FALSE, 0);

    const file_label = c.gtk_label_new("No file selected");
    c.gtk_widget_set_halign(file_label, c.GTK_ALIGN_START);
    c.gtk_widget_set_opacity(file_label, 0.6);
    c.gtk_box_pack_start(@ptrCast(@alignCast(file_box)), file_label, TRUE, TRUE, 0);

    const ctx: [*c]types.UploadContext = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.UploadContext))));
    if (ctx == null) {
        c.gtk_widget_destroy(dialog);
        return;
    }
    ctx.*.parent_dialog = dialog;
    ctx.*.file_label = file_label;
    connect(file_chooser, "file-set", @ptrCast(&onFileSelected), ctx);
    c.gtk_widget_show_all(dialog);
    connect(dialog, "response", @ptrCast(&on_compose_with_media_response), ctx);
}

fn showPollMessage(parent: [*c]c.GtkWindow, kind: c.GtkMessageType, title: [*c]const c.gchar, message: [*c]const c.gchar) void {
    const dialog = c.gtk_message_dialog_new(parent, c.GTK_DIALOG_MODAL, kind, c.GTK_BUTTONS_OK, "%s", title);
    if (message != null) c.gtk_message_dialog_format_secondary_text(@ptrCast(@alignCast(dialog)), "%s", message);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}

fn widgetWindow(widget_: [*c]c.GtkWidget) [*c]c.GtkWindow {
    const toplevel = c.gtk_widget_get_toplevel(widget_);
    return if (toplevel != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(toplevel)), c.gtk_window_get_type()) != FALSE) @ptrCast(@alignCast(toplevel)) else null;
}

fn showMessage(parent: [*c]c.GtkWindow, kind: c.GtkMessageType, text: [*c]const c.gchar, secondary: [*c]const c.gchar) void {
    const dialog = c.gtk_message_dialog_new(parent, c.GTK_DIALOG_DESTROY_WITH_PARENT, kind, c.GTK_BUTTONS_CLOSE, "%s", text);
    if (secondary != null) c.gtk_message_dialog_format_secondary_text(@ptrCast(@alignCast(dialog)), "%s", secondary);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}

fn canReplyWithMessage(tweet_id: [*c]const c.gchar, message_out: *[*c]c.gchar) c.gboolean {
    message_out.* = null;
    const url = c.g_strdup_printf(constants.TWEET_CAN_REPLY_URL, tweet_id);
    defer c.g_free(url);
    var chunk: types.MemoryStruct = std.mem.zeroes(types.MemoryStruct);
    defer c.g_free(chunk.memory);
    var can_reply = FALSE;
    const parser = c.json_parser_new();
    defer c.g_object_unref(parser);
    var parse_error: ?*c.GError = null;
    defer if (parse_error != null) c.g_error_free(parse_error);
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE and chunk.memory != null and c.json_parser_load_from_data(parser, chunk.memory, -1, &parse_error) != FALSE) {
        const root = c.json_parser_get_root(parser);
        if (root != null and c.JSON_NODE_HOLDS_OBJECT(root)) {
            const obj = c.json_node_get_object(root);
            if (obj != null and c.json_object_has_member(obj, "canReply") != FALSE) {
                can_reply = c.json_object_get_boolean_member(obj, "canReply");
            }
            if (can_reply == FALSE and obj != null) {
                if (c.json_object_has_member(obj, "reason") != FALSE and c.json_node_is_null(c.json_object_get_member(obj, "reason")) == FALSE) {
                    const reason = c.json_object_get_string_member(obj, "reason");
                    if (c.g_strcmp0(reason, "blocked") == 0) {
                        message_out.* = c.g_strdup("This account has blocked replies from you.");
                    } else if (c.g_strcmp0(reason, "restriction") == 0) {
                        message_out.* = c.g_strdup("This post has limited who can reply.");
                    }
                } else if (c.json_object_has_member(obj, "error") != FALSE and c.json_node_is_null(c.json_object_get_member(obj, "error")) == FALSE) {
                    message_out.* = c.g_strdup(c.json_object_get_string_member(obj, "error"));
                }
            }
        }
    }
    return can_reply;
}

fn refreshAfterTweetMutation(tweet_id: [*c]const c.gchar) void {
    const current_view = if (g.g_stack != null) c.gtk_stack_get_visible_child_name(@ptrCast(@alignCast(g.g_stack))) else null;
    if (c.g_strcmp0(current_view, "conversation") == 0 and tweet_id != null) {
        api.show_tweet(tweet_id);
    } else if (c.g_strcmp0(current_view, "profile") == 0 and g.g_active_profile != null and g.g_active_profile.*.username != null) {
        api.show_profile(g.g_active_profile.*.username);
    } else if (c.g_strcmp0(current_view, "bookmarks") == 0 and g.g_bookmarks_list != null) {
        api.start_loading_bookmarks(@ptrCast(@alignCast(g.g_bookmarks_list)));
    } else if (c.g_strcmp0(current_view, "community_tweets") == 0 and g.g_community_id != null and g.g_community_tweets_list != null) {
        api.start_loading_community_tweets(@ptrCast(@alignCast(g.g_community_tweets_list)), @ptrCast(g.g_community_id));
    } else if (g.g_main_list_box != null) {
        api.start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
    }
}

fn currentUsernameSafe() [*c]c.gchar {
    c.g_mutex_lock(&g.g_globals_mutex);
    const out = if (g.g_current_username != null) c.g_strdup(g.g_current_username) else null;
    c.g_mutex_unlock(&g.g_globals_mutex);
    return out;
}

fn isLoggedIn() c.gboolean {
    c.g_mutex_lock(&g.g_globals_mutex);
    const logged_in = if (g.g_auth_token != null) TRUE else FALSE;
    c.g_mutex_unlock(&g.g_globals_mutex);
    return logged_in;
}

fn isAdminUser() c.gboolean {
    c.g_mutex_lock(&g.g_globals_mutex);
    const admin = g.g_is_admin;
    c.g_mutex_unlock(&g.g_globals_mutex);
    return admin;
}

fn onLikeClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    debug("on_like_clicked called", .{});
    if (isLoggedIn() == FALSE) {
        debug("on_like_clicked: User not logged in", .{});
        return;
    }
    const tweet_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(button)), lit("tweet_id")));
    const state = boolData(button, lit("liked_state"));
    const current_liked = if (state != null) state.?.* else FALSE;
    debug("on_like_clicked: tweet_id=%s, current_liked=%d", .{ tweet_id, current_liked });
    var chunk: types.MemoryStruct = std.mem.zeroes(types.MemoryStruct);
    const url = c.g_strdup_printf(constants.LIKE_TWEET_URL, tweet_id);
    if (api.fetch_url(url, &chunk, "{}", "POST") != FALSE) {
        debug("on_like_clicked: fetch_url succeeded, response: %s", .{if (chunk.memory != null) chunk.memory else lit("(null)")});
        if (chunk.memory != null and c.strstr(chunk.memory, "\"error\"") == null) {
            const parser = c.json_parser_new();
            var err: ?*c.GError = null;
            if (c.json_parser_load_from_data(parser, chunk.memory, -1, &err) != FALSE) {
                const root = c.json_parser_get_root(parser);
                const obj = c.json_node_get_object(root);
                if (c.json_object_has_member(obj, "liked") != FALSE) {
                    const new_liked = c.json_object_get_boolean_member(obj, "liked");
                    if (state) |s| s.* = new_liked;
                    api.update_interaction_cache(tweet_id, new_liked, -1, -1);
                    debug("on_like_clicked: API returned liked=%d", .{new_liked});
                    c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (new_liked != FALSE) "♥ Liked" else "♡ Like");
                } else if (state) |s| {
                    s.* = if (s.* != FALSE) FALSE else TRUE;
                    api.update_interaction_cache(tweet_id, s.*, -1, -1);
                    c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (s.* != FALSE) "♥ Liked" else "♡ Like");
                }
                c.g_object_unref(parser);
            } else {
                if (err) |e| c.g_error_free(e);
                if (state) |s| {
                    s.* = if (s.* != FALSE) FALSE else TRUE;
                    c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (s.* != FALSE) "♥ Liked" else "♡ Like");
                }
            }
        } else if (chunk.memory != null) {
            warning("on_like_clicked: API returned error: %s", .{chunk.memory});
        }
        c.g_free(chunk.memory);
    } else {
        debug("on_like_clicked: fetch_url failed", .{});
    }
    c.g_free(url);
}

fn onRetweetClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    debug("on_retweet_clicked called", .{});
    if (isLoggedIn() == FALSE) {
        debug("on_retweet_clicked: User not logged in", .{});
        return;
    }
    const tweet_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(button)), lit("tweet_id")));
    const state = boolData(button, lit("retweeted_state"));
    const current_retweeted = if (state != null) state.?.* else FALSE;
    debug("on_retweet_clicked: tweet_id=%s, current_retweeted=%d", .{ tweet_id, current_retweeted });
    var chunk: types.MemoryStruct = std.mem.zeroes(types.MemoryStruct);
    const url = c.g_strdup_printf(constants.RETWEET_URL, tweet_id);
    if (api.fetch_url(url, &chunk, "{}", "POST") != FALSE) {
        debug("on_retweet_clicked: fetch_url succeeded, response: %s", .{if (chunk.memory != null) chunk.memory else lit("(null)")});
        if (chunk.memory != null and c.strstr(chunk.memory, "\"error\"") == null) {
            const parser = c.json_parser_new();
            var err: ?*c.GError = null;
            if (c.json_parser_load_from_data(parser, chunk.memory, -1, &err) != FALSE) {
                const root = c.json_parser_get_root(parser);
                const obj = c.json_node_get_object(root);
                if (c.json_object_has_member(obj, "retweeted") != FALSE) {
                    const new_retweeted = c.json_object_get_boolean_member(obj, "retweeted");
                    if (state) |s| s.* = new_retweeted;
                    debug("on_retweet_clicked: API returned retweeted=%d", .{new_retweeted});
                    api.update_interaction_cache(tweet_id, -1, new_retweeted, -1);
                    c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (new_retweeted != FALSE) "↻ Retweeted" else "↻ Retweet");
                } else if (state) |s| {
                    s.* = if (s.* != FALSE) FALSE else TRUE;
                    api.update_interaction_cache(tweet_id, -1, s.*, -1);
                    c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (s.* != FALSE) "↻ Retweeted" else "↻ Retweet");
                }
                c.g_object_unref(parser);
            } else {
                if (err) |e| c.g_error_free(e);
                if (state) |s| {
                    s.* = if (s.* != FALSE) FALSE else TRUE;
                    c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (s.* != FALSE) "↻ Retweeted" else "↻ Retweet");
                }
            }
        } else if (chunk.memory != null) {
            warning("on_retweet_clicked: API returned error: %s", .{chunk.memory});
        }
        c.g_free(chunk.memory);
    } else {
        debug("on_retweet_clicked: fetch_url failed", .{});
    }
    c.g_free(url);
}

fn onQuoteClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    if (isLoggedIn() == FALSE) return;
    const tweet_id = stringData(button, lit("tweet_id"));
    if (tweet_id == null) return;

    const toplevel = c.gtk_widget_get_toplevel(button);
    const window: [*c]c.GtkWindow = if (toplevel != null) @ptrCast(@alignCast(toplevel)) else null;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Quote Tweet",
        window,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Quote",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const content_area = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    const text_view = c.gtk_text_view_new();
    c.gtk_widget_set_size_request(text_view, 300, 150);
    c.gtk_text_view_set_wrap_mode(@ptrCast(@alignCast(text_view)), c.GTK_WRAP_WORD_CHAR);
    c.gtk_container_set_border_width(container(@ptrCast(@alignCast(content_area))), 10);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content_area)), text_view, TRUE, TRUE, 0);
    const ctx: [*c]types.QuoteContext = @ptrCast(@alignCast(c.g_malloc(@sizeOf(types.QuoteContext))));
    if (ctx == null) {
        c.gtk_widget_destroy(dialog);
        return;
    }
    ctx.*.text_view = text_view;
    ctx.*.quote_id = c.g_strdup(tweet_id);
    c.gtk_widget_show_all(dialog);
    _ = c.g_signal_connect_data(dialog, "response", @ptrCast(&on_quote_response), ctx, null, c.G_CONNECT_DEFAULT);
}

export fn on_quote_response(dialog: [*c]c.GtkDialog, response_id: c.gint, user_data: c.gpointer) void {
    const ctx: [*c]types.QuoteContext = @ptrCast(@alignCast(user_data));
    if (response_id == c.GTK_RESPONSE_ACCEPT and ctx != null) {
        const content = textViewText(ctx.*.text_view);
        defer c.g_free(content);
        if (content != null and content[0] != 0) {
            if (api.perform_quote_tweet(content, ctx.*.quote_id) != FALSE) {
                if (g.g_main_list_box != null) api.start_loading_tweets(@ptrCast(@alignCast(g.g_main_list_box)));
            }
        }
    }
    if (ctx != null) {
        c.g_free(ctx.*.quote_id);
        c.g_free(ctx);
    }
    c.gtk_widget_destroy(@ptrCast(@alignCast(dialog)));
}

fn onRetweetMenuRetweet(menuitem: [*c]c.GtkMenuItem, user_data: c.gpointer) callconv(.c) void {
    _ = menuitem;
    onRetweetClicked(@ptrCast(@alignCast(user_data)), null);
}

fn onRetweetMenuQuote(menuitem: [*c]c.GtkMenuItem, user_data: c.gpointer) callconv(.c) void {
    _ = menuitem;
    onQuoteClicked(@ptrCast(@alignCast(user_data)), null);
}

fn onRetweetButtonClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    if (isLoggedIn() == FALSE) return;
    const menu = c.gtk_menu_new();
    const retweet_item = c.gtk_menu_item_new_with_label("Retweet");
    const quote_item = c.gtk_menu_item_new_with_label("Quote");
    connect(retweet_item, "activate", @ptrCast(&onRetweetMenuRetweet), button);
    connect(quote_item, "activate", @ptrCast(&onRetweetMenuQuote), button);
    c.gtk_menu_shell_append(@ptrCast(@alignCast(menu)), retweet_item);
    c.gtk_menu_shell_append(@ptrCast(@alignCast(menu)), quote_item);
    c.gtk_widget_show_all(menu);
    api.zig_gtk_menu_popup_at_widget(menu, button);
}

fn onBookmarkClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    debug("on_bookmark_clicked called", .{});
    if (isLoggedIn() == FALSE) {
        debug("on_bookmark_clicked: User not logged in", .{});
        return;
    }
    const tweet_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(button)), lit("tweet_id")));
    if (tweet_id != null) {
        const state = boolData(button, lit("bookmarked_state"));
        const current_bookmarked = if (state != null) state.?.* else FALSE;
        debug("on_bookmark_clicked: tweet_id=%s, current_bookmarked=%d", .{ tweet_id, current_bookmarked });
        const add = if (state != null and state.?.* != FALSE) FALSE else TRUE;
        const url: [*c]const c.gchar = if (add != FALSE) @ptrCast(constants.BOOKMARK_ADD_URL.ptr) else @ptrCast(constants.BOOKMARK_REMOVE_URL.ptr);
        const builder = c.json_builder_new();
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "postId");
        _ = c.json_builder_add_string_value(builder, tweet_id);
        _ = c.json_builder_end_object(builder);
        const post_data = builderPayload(builder);
        var chunk: types.MemoryStruct = std.mem.zeroes(types.MemoryStruct);
        if (api.fetch_url(url, &chunk, post_data, "POST") != FALSE) {
            debug("on_bookmark_clicked: fetch_url succeeded, response: %s", .{if (chunk.memory != null) chunk.memory else lit("(null)")});
            if (chunk.memory != null and c.strstr(chunk.memory, "\"error\"") == null) {
                const parser = c.json_parser_new();
                var err: ?*c.GError = null;
                if (c.json_parser_load_from_data(parser, chunk.memory, -1, &err) != FALSE) {
                    const root = c.json_parser_get_root(parser);
                    const obj = c.json_node_get_object(root);
                    if (c.json_object_has_member(obj, "bookmarked") != FALSE) {
                        const new_bookmarked = c.json_object_get_boolean_member(obj, "bookmarked");
                        if (state) |s| s.* = new_bookmarked;
                        debug("on_bookmark_clicked: API returned bookmarked=%d", .{new_bookmarked});
                        api.update_interaction_cache(tweet_id, -1, -1, new_bookmarked);
                        c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (new_bookmarked != FALSE) "★ Saved" else "☆ Bookmark");
                    } else if (state) |s| {
                        s.* = if (s.* != FALSE) FALSE else TRUE;
                        api.update_interaction_cache(tweet_id, -1, -1, s.*);
                        c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (s.* != FALSE) "★ Saved" else "☆ Bookmark");
                    }
                    c.g_object_unref(parser);
                } else {
                    if (err) |e| c.g_error_free(e);
                    if (state) |s| {
                        s.* = if (s.* != FALSE) FALSE else TRUE;
                        c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (s.* != FALSE) "★ Saved" else "☆ Bookmark");
                    }
                }
            } else if (chunk.memory != null) {
                warning("on_bookmark_clicked: API returned error: %s", .{chunk.memory});
            }
            c.g_free(chunk.memory);
        } else {
            debug("on_bookmark_clicked: fetch_url failed", .{});
        }
        c.g_free(post_data);
        c.g_object_unref(builder);
    }
}

fn freeReactionContext(data: c.gpointer) callconv(.c) void {
    const ctx: [*c]types.ReactionContext = @ptrCast(@alignCast(data));
    if (ctx != null) {
        c.g_free(ctx.*.tweet_id);
        c.g_free(ctx);
    }
}

fn onEmojiSelected(child: [*c]c.GtkFlowBoxChild, user_data: c.gpointer) callconv(.c) void {
    const dialog: [*c]c.GtkWidget = @ptrCast(@alignCast(user_data));
    const ctx: [*c]types.ReactionContext = @ptrCast(@alignCast(c.g_object_get_data(@ptrCast(@alignCast(dialog)), lit("reaction_context"))));
    const emoji_name = stringData(@ptrCast(@alignCast(child)), lit("emoji_name"));
    debug("on_emoji_selected: tweet_id=%s, emoji=%s", .{ if (ctx != null and ctx.*.tweet_id != null) ctx.*.tweet_id else lit("NULL"), emoji_name });
    if (emoji_name != null and ctx != null and ctx.*.tweet_id != null) _ = api.perform_reaction(ctx.*.tweet_id, emoji_name);
    c.gtk_widget_destroy(dialog);
}

fn onReactionClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const tweet_id = stringData(button, lit("tweet_id"));
    if (tweet_id == null or isLoggedIn() == FALSE) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Add Reaction",
        widgetWindow(button),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        @as(c_int, c.GTK_RESPONSE_CANCEL),
        @as([*c]const c.gchar, null),
    );
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    c.gtk_container_set_border_width(container(@ptrCast(@alignCast(content))), 10);
    const scrolled = c.gtk_scrolled_window_new(null, null);
    c.gtk_scrolled_window_set_policy(@ptrCast(@alignCast(scrolled)), c.GTK_POLICY_NEVER, c.GTK_POLICY_AUTOMATIC);
    c.gtk_widget_set_size_request(scrolled, 300, 200);
    const flowbox = c.gtk_flow_box_new();
    c.gtk_flow_box_set_selection_mode(@ptrCast(@alignCast(flowbox)), c.GTK_SELECTION_SINGLE);
    c.gtk_flow_box_set_max_children_per_line(@ptrCast(@alignCast(flowbox)), 8);
    c.gtk_flow_box_set_homogeneous(@ptrCast(@alignCast(flowbox)), TRUE);
    const ctx: [*c]types.ReactionContext = @ptrCast(@alignCast(c.g_malloc(@sizeOf(types.ReactionContext))));
    ctx.*.tweet_id = c.g_strdup(tweet_id);
    ctx.*.parent_window = @ptrCast(widgetWindow(button));
    c.g_object_set_data_full(@ptrCast(@alignCast(dialog)), lit("reaction_context"), ctx, @ptrCast(&freeReactionContext));
    const system_emojis = [_][*c]const c.gchar{
        "👍",   "👎", "❤️", "💔",   "😀", "😃", "😄",    "😁", "😆",   "😅",
        "🤣",   "😂", "🙂",   "🙃",   "😉", "😊", "😇",    "🥰", "😍",   "🤩",
        "😘",   "😗", "😚",   "😙",   "🥲", "😋", "😛",    "😜", "🤪",   "😝",
        "🤑",   "🤗", "🤭",   "🤫",   "🤔", "🤐", "🤨",    "😐", "😑",   "😶",
        "😏",   "😒", "🙄",   "😬",   "🤥", "😌", "😔",    "😪", "🤤",   "😴",
        "😷",   "🤒", "🤕",   "🤢",   "🤮", "🤧", "🥵",    "🥶", "🥴",   "😵",
        "🤯",   "🤠", "🥳",   "🥸",   "😎", "🤓", "🧐",    "😕", "😟",   "🙁",
        "☹️", "😮", "😯",   "😲",   "😳", "🥺", "😦",    "😧", "😨",   "😰",
        "😥",   "😢", "😭",   "😱",   "😖", "😣", "😞",    "😓", "😩",   "😫",
        "🥱",   "😤", "😡",   "😠",   "🤬", "😈", "👿",    "💀", "☠️", "💩",
        "🤡",   "👹", "👺",   "👻",   "👽", "👾", "🤖",    "😺", "😸",   "😹",
        "😻",   "😼", "😽",   "🙀",   "😿", "😾", "🙈",    "🙉", "🙊",   "💋",
        "💯",   "💢", "💥",   "💫",   "💦", "💨", "🕳️", "💣", "💬",   "🔥",
        "✨",    "⭐",  "🌟",   "💫",   "🎉", "🎊", "🎁",    "🏆", "🥇",   "🥈",
        "🥉",   "⚽",  "🏀",   "🎵",   "🎶", "🎤", "🎧",    "👏", "🙌",   "👐",
        "🤲",   "🤝", "🙏",   "✍️", "💪", "🦾", "🦿",    "🦵", "🦶",   "👂",
        "🦻",   "👃", "🧠",   "🦷",   "🦴", "👀", "👁️", "👅", "👄",   "💘",
        "💝",   "💖", "💗",   "💓",   "💞", "💕", "❣️",  "💔", "🧡",   "💛",
        "💚",   "💙", "💜",   "🤎",   "🖤", "🤍", "✅",     "❌",  "❓",    "❗",
    };
    for (system_emojis) |emoji| {
        const child = c.gtk_flow_box_child_new();
        c.gtk_container_add(container(child), c.gtk_label_new(emoji));
        setStringData(child, lit("emoji_name"), emoji);
        c.gtk_container_add(container(flowbox), child);
    }
    const emojis = api.fetch_emojis();
    defer api.free_emojis(emojis);
    var item = emojis;
    while (item != null) : (item = item.*.next) {
        const emoji: [*c]types.Emoji = @ptrCast(@alignCast(item.*.data));
        if (emoji == null or emoji.*.name == null) continue;
        const child = c.gtk_flow_box_child_new();
        if (emoji.*.file_url != null) {
            const image = c.gtk_image_new();
            api.load_avatar(image, emoji.*.file_url, 24);
            c.gtk_container_add(container(child), image);
        } else {
            c.gtk_container_add(container(child), c.gtk_label_new(emoji.*.name));
        }
        setStringData(child, lit("emoji_name"), emoji.*.name);
        c.gtk_widget_set_tooltip_text(child, emoji.*.name);
        c.gtk_container_add(container(flowbox), child);
    }
    connect(flowbox, "child-activated", @ptrCast(&onEmojiSelected), dialog);
    c.gtk_container_add(container(scrolled), flowbox);
    pack(@ptrCast(@alignCast(content)), scrolled, true);
    c.gtk_widget_show_all(dialog);
    _ = c.g_signal_connect_data(dialog, "response", @ptrCast(&c.gtk_widget_destroy), dialog, null, c.G_CONNECT_SWAPPED);
}

fn onHistoryClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const text = api.fetch_tweet_edit_history_text(stringData(button, lit("tweet_id")));
    defer c.g_free(text);
    showTextDialog(button, "Edit History", if (text != null) text else "No edit history available.");
}

export fn on_tweet_history_clicked(widget_: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onHistoryClicked(widget_, user_data);
}

fn onReactionsClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const text = api.fetch_tweet_reactions_text(stringData(button, lit("tweet_id")));
    defer c.g_free(text);
    showTextDialog(button, "Tweet Reactions", if (text != null) text else "No reactions yet.");
}

export fn on_tweet_reactions_clicked(widget_: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onReactionsClicked(widget_, user_data);
}

fn onEditTweetClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const tweet_id = stringData(button, lit("tweet_id"));
    if (tweet_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Edit Tweet",
        widgetWindow(button),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_text(@ptrCast(@alignCast(entry)), textOr(stringData(button, lit("tweet_content")), ""));
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, TRUE, TRUE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        if (api.perform_edit_tweet(tweet_id, c.gtk_entry_get_text(@ptrCast(@alignCast(entry)))) != FALSE) refreshAfterTweetMutation(tweet_id);
    }
    c.gtk_widget_destroy(dialog);
}

export fn on_tweet_edit_clicked(widget_: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onEditTweetClicked(widget_, user_data);
}

fn onDeleteTweetClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const tweet_id = stringData(button, lit("tweet_id"));
    if (tweet_id == null) return;
    const dialog = c.gtk_message_dialog_new(
        @ptrCast(@alignCast(c.gtk_widget_get_toplevel(button))),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        c.GTK_MESSAGE_WARNING,
        c.GTK_BUTTONS_OK_CANCEL,
        "Delete this tweet?",
    );
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_OK) {
        if (api.perform_delete_tweet(tweet_id) != FALSE) refreshAfterTweetMutation(tweet_id);
    }
    c.gtk_widget_destroy(dialog);
}

export fn on_tweet_delete_clicked(widget_: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onDeleteTweetClicked(widget_, user_data);
}

fn onPinTweetClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const tweet_id = stringData(button, lit("tweet_id"));
    const pinned = if (c.g_object_get_data(@ptrCast(@alignCast(button)), lit("tweet_pinned")) != null) TRUE else FALSE;
    const should_pin = if (pinned != FALSE) FALSE else TRUE;
    if (tweet_id != null and api.perform_toggle_pin_tweet(tweet_id, should_pin) != FALSE) {
        c.g_object_set_data(@ptrCast(@alignCast(button)), lit("tweet_pinned"), if (should_pin != FALSE) @as(c.gpointer, @ptrFromInt(1)) else null);
        c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (should_pin != FALSE) "Unpin" else "Pin");
        refreshAfterTweetMutation(tweet_id);
    }
}

export fn on_tweet_pin_clicked(widget_: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onPinTweetClicked(widget_, user_data);
}

fn onHighlightTweetClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const tweet_id = stringData(button, lit("tweet_id"));
    if (tweet_id == null) return;
    const username = currentUsernameSafe();
    defer c.g_free(username);
    if (username == null) return;
    const dialog = c.gtk_message_dialog_new(
        @ptrCast(@alignCast(c.gtk_widget_get_toplevel(button))),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        c.GTK_MESSAGE_QUESTION,
        c.GTK_BUTTONS_NONE,
        "Update highlight",
    );
    c.gtk_message_dialog_format_secondary_text(@ptrCast(@alignCast(dialog)), "%s", "Keep this post in the Highlights section of your profile?");
    _ = c.gtk_dialog_add_button(@ptrCast(@alignCast(dialog)), "_Cancel", c.GTK_RESPONSE_CANCEL);
    _ = c.gtk_dialog_add_button(@ptrCast(@alignCast(dialog)), "_Remove", c.GTK_RESPONSE_REJECT);
    _ = c.gtk_dialog_add_button(@ptrCast(@alignCast(dialog)), "_Highlight", c.GTK_RESPONSE_ACCEPT);
    const response = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
    if (response == c.GTK_RESPONSE_ACCEPT or response == c.GTK_RESPONSE_REJECT) {
        const highlighted = if (response == c.GTK_RESPONSE_ACCEPT) TRUE else FALSE;
        if (api.perform_toggle_highlight_tweet(tweet_id, highlighted) != FALSE) {
            refreshAfterTweetMutation(tweet_id);
            api.start_loading_profile_highlights(username);
        }
    }
}

export fn on_tweet_highlight_clicked(widget_: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onHighlightTweetClicked(widget_, user_data);
}

fn onReplyRestrictionClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const tweet_id = stringData(button, lit("tweet_id"));
    if (tweet_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Reply Permissions",
        widgetWindow(button),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const combo = c.gtk_combo_box_text_new();
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(combo)), "everyone", "Everyone");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(combo)), "followers", "Followers");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(combo)), "following", "Following");
    c.gtk_combo_box_text_append(@ptrCast(@alignCast(combo)), "verified", "Verified users");
    c.gtk_combo_box_set_active(@ptrCast(@alignCast(combo)), 0);
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), combo, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        if (api.perform_tweet_reply_restriction(tweet_id, c.gtk_combo_box_get_active_id(@ptrCast(@alignCast(combo)))) != FALSE) refreshAfterTweetMutation(tweet_id);
    }
    c.gtk_widget_destroy(dialog);
}

export fn on_tweet_reply_restriction_clicked(widget_: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onReplyRestrictionClicked(widget_, user_data);
}

fn onOutlineTweetClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const tweet_id = stringData(button, lit("tweet_id"));
    if (tweet_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Post Outline",
        widgetWindow(button),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Save",
        c.GTK_RESPONSE_ACCEPT,
        "_Clear",
        c.GTK_RESPONSE_REJECT,
        @as(?*anyopaque, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "Outline color");
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    const response = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    if (response == c.GTK_RESPONSE_ACCEPT or response == c.GTK_RESPONSE_REJECT) {
        const value = if (response == c.GTK_RESPONSE_ACCEPT) c.gtk_entry_get_text(@ptrCast(@alignCast(entry))) else null;
        if (api.perform_tweet_outline(tweet_id, if (value != null and value[0] != 0) value else null) != FALSE) refreshAfterTweetMutation(tweet_id);
    }
    c.gtk_widget_destroy(dialog);
}

export fn on_tweet_outline_clicked(widget_: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onOutlineTweetClicked(widget_, user_data);
}

fn onPollOptionClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = button;
    const vote_data: ?*PollVoteData = @ptrCast(@alignCast(user_data));
    if (isLoggedIn() == FALSE or vote_data == null) return;
    const data = vote_data.?;
    if (api.perform_poll_vote(data.tweet_id, data.option_id) != FALSE) {
        api.show_tweet(data.tweet_id);
    }
}

fn jsonObjectString(obj: ?*c.JsonObject, key: [*c]const c.gchar, fallback: [*c]const c.gchar) [*c]const c.gchar {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return fallback;
    const node = c.json_object_get_member(obj, key);
    if (node == null or !c.JSON_NODE_HOLDS_VALUE(node) or c.json_node_is_null(node) != FALSE) return fallback;
    return c.json_object_get_string_member(obj, key);
}

fn jsonObjectInt(obj: ?*c.JsonObject, key: [*c]const c.gchar, fallback: c_int) c_int {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return fallback;
    const node = c.json_object_get_member(obj, key);
    if (node == null or !c.JSON_NODE_HOLDS_VALUE(node) or c.json_node_is_null(node) != FALSE) return fallback;
    return @intCast(c.json_object_get_int_member(obj, key));
}

fn jsonObjectArray(obj: ?*c.JsonObject, key: [*c]const c.gchar) ?*c.JsonArray {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return null;
    const node = c.json_object_get_member(obj, key);
    if (node == null or !c.JSON_NODE_HOLDS_ARRAY(node)) return null;
    return c.json_object_get_array_member(obj, key);
}

fn jsonObjectObject(obj: ?*c.JsonObject, key: [*c]const c.gchar) ?*c.JsonObject {
    if (obj == null or c.json_object_has_member(obj, key) == FALSE) return null;
    const node = c.json_object_get_member(obj, key);
    if (node == null or !c.JSON_NODE_HOLDS_OBJECT(node)) return null;
    return c.json_node_get_object(node);
}

fn jsonArrayObject(array: ?*c.JsonArray, index: c.guint) ?*c.JsonObject {
    if (array == null) return null;
    const node = c.json_array_get_element(array, index);
    if (node == null or !c.JSON_NODE_HOLDS_OBJECT(node)) return null;
    return c.json_node_get_object(node);
}

fn freePollStepInputs(inputs: ?*c.GPtrArray) void {
    const array = inputs orelse return;
    var i: c.guint = 0;
    while (i < array.*.len) : (i += 1) {
        const input: *PollStepInput = @ptrCast(@alignCast(c.g_ptr_array_index(array, i)));
        if (input.checks != null) c.g_ptr_array_unref(input.checks);
        c.g_free(input);
    }
    _ = c.g_ptr_array_free(array, TRUE);
}

fn appendRankingAnswer(builder: [*c]c.JsonBuilder, text: [*c]const c.gchar, option_count: c.guint) bool {
    const seen: [*c]c.gboolean = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(c.gboolean) * option_count)));
    defer c.g_free(seen);
    const parts = c.g_strsplit(if (text != null) text else "", ",", -1);
    defer c.g_strfreev(parts);
    var count: c.guint = 0;
    var valid = true;
    _ = c.json_builder_begin_array(builder);
    var i: usize = 0;
    while (parts[i] != null) : (i += 1) {
        const part = c.g_strstrip(parts[i]);
        if (part[0] == 0) continue;
        var end: [*c]u8 = null;
        const one_based = c.strtol(part, &end, 10);
        if ((end != null and end[0] != 0) or one_based < 1 or one_based > @as(c_long, @intCast(option_count)) or seen[@intCast(one_based - 1)] != FALSE) {
            valid = false;
            break;
        }
        seen[@intCast(one_based - 1)] = TRUE;
        _ = c.json_builder_add_int_value(builder, @intCast(one_based - 1));
        count += 1;
    }
    _ = c.json_builder_end_array(builder);
    return valid and count == option_count;
}

fn onMultiPollAnswerClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    const poll: [*c]types.Poll = @ptrCast(@alignCast(user_data));
    const tweet_id = stringData(button, lit("tweet_id"));
    const parent = widgetWindow(button);
    if (isLoggedIn() == FALSE) return;
    if (tweet_id == null or poll == null or poll.*.steps == null or !c.JSON_NODE_HOLDS_ARRAY(poll.*.steps)) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Answer poll",
        parent,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        @as(c_int, c.GTK_RESPONSE_CANCEL),
        "_Submit",
        @as(c_int, c.GTK_RESPONSE_ACCEPT),
        @as([*c]const c.gchar, null),
    );
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    const boxw = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 12);
    c.gtk_container_set_border_width(container(boxw), 12);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), boxw, TRUE, TRUE, 0);
    const inputs = c.g_ptr_array_new();
    const input_array = inputs orelse {
        c.gtk_widget_destroy(dialog);
        return;
    };
    defer freePollStepInputs(input_array);
    const steps = c.json_node_get_array(poll.*.steps);
    var i: c.guint = 0;
    while (i < c.json_array_get_length(steps)) : (i += 1) {
        const step = jsonArrayObject(steps, i) orelse continue;
        const type_text = jsonObjectString(step, "type", "single");
        const question = jsonObjectString(step, "question", "Question");
        const options = jsonObjectArray(step, "options");
        const section = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 6);
        const label_text = c.g_strdup_printf("<b>%u. %s</b>", i + 1, question);
        defer c.g_free(label_text);
        const question_label = c.gtk_label_new(null);
        c.gtk_label_set_markup(label(question_label), label_text);
        c.gtk_label_set_xalign(label(question_label), 0.0);
        c.gtk_label_set_line_wrap(label(question_label), TRUE);
        pack(section, question_label, false);

        const input: *PollStepInput = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(PollStepInput))));
        input.option_count = if (options != null) c.json_array_get_length(options) else 0;
        if (c.g_strcmp0(type_text, "multi") == 0) {
            input.type = POLL_INPUT_MULTI;
            input.checks = c.g_ptr_array_new();
            var j: c.guint = 0;
            while (j < input.option_count) : (j += 1) {
                const option = jsonArrayObject(options, j);
                const check = c.gtk_check_button_new_with_label(jsonObjectString(option, "text", "Option"));
                pack(section, check, false);
                c.g_ptr_array_add(input.checks, check);
            }
        } else if (c.g_strcmp0(type_text, "text") == 0) {
            input.type = POLL_INPUT_TEXT;
            input.primary = c.gtk_entry_new();
            c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(input.primary)), jsonObjectString(step, "text_placeholder", "Type your answer"));
            pack(section, input.primary, false);
        } else if (c.g_strcmp0(type_text, "number") == 0 or c.g_strcmp0(type_text, "scale") == 0) {
            const min = if (c.g_strcmp0(type_text, "scale") == 0) jsonObjectInt(step, "scale_min", 1) else jsonObjectInt(step, "number_min", 0);
            const max = if (c.g_strcmp0(type_text, "scale") == 0) jsonObjectInt(step, "scale_max", 5) else jsonObjectInt(step, "number_max", 1000000);
            input.type = POLL_INPUT_NUMBER;
            input.primary = c.gtk_spin_button_new_with_range(@floatFromInt(min), @floatFromInt(max), 1);
            c.gtk_spin_button_set_value(@ptrCast(@alignCast(input.primary)), @floatFromInt(min));
            pack(section, input.primary, false);
        } else if (c.g_strcmp0(type_text, "ranking") == 0) {
            input.type = POLL_INPUT_RANKING;
            var j: c.guint = 0;
            while (j < input.option_count) : (j += 1) {
                const option = jsonArrayObject(options, j);
                const option_label_text = c.g_strdup_printf("%u. %s", j + 1, jsonObjectString(option, "text", "Option"));
                defer c.g_free(option_label_text);
                const opt = c.gtk_label_new(option_label_text);
                c.gtk_label_set_xalign(label(opt), 0.0);
                pack(section, opt, false);
            }
            input.primary = c.gtk_entry_new();
            c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(input.primary)), "Rank all options, e.g. 2,1,3");
            pack(section, input.primary, false);
        } else {
            input.type = POLL_INPUT_SINGLE;
            input.primary = c.gtk_combo_box_text_new();
            var j: c.guint = 0;
            while (j < input.option_count) : (j += 1) {
                const option = jsonArrayObject(options, j);
                c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(input.primary)), jsonObjectString(option, "text", "Option"));
            }
            c.gtk_combo_box_set_active(@ptrCast(@alignCast(input.primary)), 0);
            pack(section, input.primary, false);
        }
        c.g_ptr_array_add(input_array, input);
        pack(boxw, section, false);
    }
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const builder = c.json_builder_new();
        defer c.g_object_unref(builder);
        var valid = true;
        _ = c.json_builder_begin_array(builder);
        i = 0;
        while (i < input_array.*.len) : (i += 1) {
            const input: *PollStepInput = @ptrCast(@alignCast(c.g_ptr_array_index(input_array, i)));
            switch (input.type) {
                POLL_INPUT_SINGLE => _ = c.json_builder_add_int_value(builder, c.gtk_combo_box_get_active(@ptrCast(@alignCast(input.primary)))),
                POLL_INPUT_MULTI => {
                    var selected: c.guint = 0;
                    _ = c.json_builder_begin_array(builder);
                    var j: c.guint = 0;
                    while (j < input.checks.?.*.len) : (j += 1) {
                        const check: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_ptr_array_index(input.checks.?, j)));
                        if (c.gtk_toggle_button_get_active(@ptrCast(@alignCast(check))) != FALSE) {
                            _ = c.json_builder_add_int_value(builder, @intCast(j));
                            selected += 1;
                        }
                    }
                    _ = c.json_builder_end_array(builder);
                    if (selected == 0) valid = false;
                },
                POLL_INPUT_TEXT => {
                    const answer = c.gtk_entry_get_text(@ptrCast(@alignCast(input.primary)));
                    const trimmed = c.g_strdup(textOr(answer, ""));
                    defer c.g_free(trimmed);
                    if (trimmed == null or c.g_strstrip(trimmed)[0] == 0) valid = false;
                    _ = c.json_builder_add_string_value(builder, textOr(answer, ""));
                },
                POLL_INPUT_NUMBER => _ = c.json_builder_add_int_value(builder, c.gtk_spin_button_get_value_as_int(@ptrCast(@alignCast(input.primary)))),
                POLL_INPUT_RANKING => valid = appendRankingAnswer(builder, c.gtk_entry_get_text(@ptrCast(@alignCast(input.primary))), input.option_count) and valid,
                else => {},
            }
        }
        _ = c.json_builder_end_array(builder);
        const answers = c.json_builder_get_root(builder);
        defer c.json_node_free(answers);
        var message: [*c]c.gchar = null;
        defer c.g_free(message);
        if (!valid) {
            showPollMessage(@ptrCast(@alignCast(dialog)), c.GTK_MESSAGE_ERROR, "Poll answer incomplete.", "Answer every step before submitting.");
        } else if (api.perform_poll_multi_vote(tweet_id, answers, &message) != FALSE) {
            if (message != null) showPollMessage(@ptrCast(@alignCast(dialog)), c.GTK_MESSAGE_INFO, "Poll submitted.", message);
            api.show_tweet(tweet_id);
        } else {
            showPollMessage(@ptrCast(@alignCast(dialog)), c.GTK_MESSAGE_ERROR, "Poll submission failed.", message);
        }
    }
    c.gtk_widget_destroy(dialog);
}

fn textViewText(view: [*c]c.GtkWidget) [*c]c.gchar {
    const buffer = c.gtk_text_view_get_buffer(@ptrCast(@alignCast(view)));
    var start: c.GtkTextIter = undefined;
    var end: c.GtkTextIter = undefined;
    c.gtk_text_buffer_get_bounds(buffer, &start, &end);
    return c.gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

fn onReplyClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const tweet_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(button)), lit("tweet_id")));
    const username = stringData(button, lit("username"));
    const window = widgetWindow(button);
    if (isLoggedIn() == FALSE) {
        showMessage(window, c.GTK_MESSAGE_ERROR, "You must be logged in to reply.", null);
        return;
    }
    if (tweet_id == null or tweet_id[0] == 0) {
        showMessage(window, c.GTK_MESSAGE_ERROR, "This tweet cannot be replied to right now.", null);
        return;
    }
    var reply_message: [*c]c.gchar = null;
    defer c.g_free(reply_message);
    if (canReplyWithMessage(tweet_id, &reply_message) == FALSE) {
        showMessage(window, c.GTK_MESSAGE_INFO, "You cannot reply to this post.", reply_message);
        return;
    }
    const dialog = c.gtk_dialog_new_with_buttons(
        "Reply to Tweet",
        window,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Reply",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );

    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    c.gtk_container_set_border_width(container(@ptrCast(@alignCast(content))), 10);
    const replying_to = api.build_reply_banner_text(username);
    defer c.g_free(replying_to);
    const banner = c.gtk_label_new(replying_to);
    c.gtk_label_set_xalign(label(banner), 0.0);
    pack(@ptrCast(@alignCast(content)), banner, false);
    const text_view = c.gtk_text_view_new();
    c.gtk_text_view_set_wrap_mode(@ptrCast(@alignCast(text_view)), c.GTK_WRAP_WORD_CHAR);
    c.gtk_widget_set_size_request(text_view, 300, 150);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), text_view, TRUE, TRUE, 8);
    const file_box = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    c.gtk_widget_set_margin_top(file_box, 10);
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
    pack(file_box, file_chooser, false);
    const file_label = c.gtk_label_new("No file selected");
    c.gtk_widget_set_halign(file_label, c.GTK_ALIGN_START);
    c.gtk_widget_set_opacity(file_label, 0.6);
    pack(file_box, file_label, true);
    pack(@ptrCast(@alignCast(content)), file_box, false);
    const ctx: [*c]types.ReplyContext = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.ReplyContext))));
    if (ctx == null) {
        c.gtk_widget_destroy(dialog);
        return;
    }
    ctx.*.text_view = text_view;
    ctx.*.reply_to_id = c.g_strdup(tweet_id);
    ctx.*.upload.parent_dialog = dialog;
    ctx.*.upload.file_label = file_label;
    c.gtk_widget_show_all(dialog);
    connect(file_chooser, "file-set", @ptrCast(&onFileSelected), &ctx.*.upload);
    connect(dialog, "response", @ptrCast(&on_reply_response), ctx);
}

export fn on_reply_response(dialog: [*c]c.GtkDialog, response_id: c.gint, user_data: c.gpointer) void {
    const ctx: [*c]types.ReplyContext = @ptrCast(@alignCast(user_data));
    if (response_id == c.GTK_RESPONSE_ACCEPT and ctx != null) {
        const content_text = textViewText(ctx.*.text_view);
        defer c.g_free(content_text);
        const trimmed = c.g_strdup(if (content_text != null) content_text else "");
        defer c.g_free(trimmed);
        _ = c.g_strstrip(trimmed);
        var media_url: [*c]c.gchar = null;
        defer c.g_free(media_url);
        var upload_success = true;
        if (ctx.*.upload.file_path != null) {
            media_url = api.perform_media_upload(ctx.*.upload.file_path);
            if (media_url == null) upload_success = false;
        }
        const attachments = if (media_url != null) api.build_attachment_list(media_url, if (ctx.*.upload.file_type != null) ctx.*.upload.file_type else "application/octet-stream") else null;
        defer if (attachments != null) c.g_list_free_full(attachments, api.free_attachment_payload);
        if (upload_success and (trimmed[0] != 0 or attachments != null)) {
            if (api.perform_post_tweet(if (content_text != null) content_text else "", ctx.*.reply_to_id, attachments) != FALSE) {
                refreshAfterTweetMutation(ctx.*.reply_to_id);
            } else {
                showMessage(@ptrCast(@alignCast(dialog)), c.GTK_MESSAGE_ERROR, "Failed to post reply.", null);
            }
        } else if (!upload_success) {
            showMessage(@ptrCast(@alignCast(dialog)), c.GTK_MESSAGE_ERROR, "Failed to upload attachment.", null);
        }
    }
    if (ctx != null) {
        c.g_free(ctx.*.reply_to_id);
        c.g_free(ctx.*.upload.file_path);
        c.g_free(ctx.*.upload.file_type);
        c.g_free(ctx);
    }
    c.gtk_widget_destroy(@ptrCast(@alignCast(dialog)));
}

fn onOpenConversationClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const conversation_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(button)), lit("conversation_id")));
    const display_name: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(button)), lit("display_name")));
    if (conversation_id == null or g.g_dm_messages_list == null) return;
    if (g.g_stack != null) c.gtk_stack_set_visible_child_name(@ptrCast(@alignCast(g.g_stack)), "dm_messages");
    if (g.g_dm_title_label != null) c.gtk_label_set_text(@ptrCast(@alignCast(g.g_dm_title_label)), textOr(display_name, "Messages"));
    if (g.g_dm_info_label != null) c.gtk_label_set_text(@ptrCast(@alignCast(g.g_dm_info_label)), "");
    setStringData(@ptrCast(@alignCast(g.g_dm_messages_list)), lit("conversation_id"), conversation_id);
    setStringData(@ptrCast(@alignCast(g.g_dm_messages_list)), lit("reply_to_id"), null);
    setStringData(@ptrCast(@alignCast(g.g_dm_messages_list)), lit("reply_preview"), null);
    setStringData(@ptrCast(@alignCast(g.g_dm_messages_list)), lit("pending_file_path"), null);
    setStringData(@ptrCast(@alignCast(g.g_dm_messages_list)), lit("pending_file_type"), null);
    c.g_object_set_data(@ptrCast(@alignCast(g.g_dm_entry)), lit("typing_active"), null);
    const status_label: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_dm_messages_list)), lit("composer_status_label"))));
    if (status_label != null) {
        c.gtk_label_set_text(@ptrCast(@alignCast(status_label)), "");
        c.gtk_widget_hide(status_label);
    }
    api.start_loading_messages(@ptrCast(@alignCast(g.g_dm_messages_list)), conversation_id);
    _ = api.perform_dm_mark_read(conversation_id);
}

export fn on_conversation_clicked(widget_: [*c]c.GtkWidget, user_data: c.gpointer) void {
    onOpenConversationClicked(widget_, user_data);
}

fn onConversationPressed(widget_: [*c]c.GtkWidget, event: [*c]GdkEventButton, user_data: c.gpointer) callconv(.c) c.gboolean {
    _ = event;
    onOpenConversationClicked(widget_, user_data);
    return TRUE;
}

fn onDmMessageReactClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const message_id = stringData(button, lit("message_id"));
    if (message_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "React to Message",
        widgetWindow(button),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        @as(c_int, c.GTK_RESPONSE_CANCEL),
        "_React",
        @as(c_int, c.GTK_RESPONSE_ACCEPT),
        @as([*c]const c.gchar, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "Emoji");
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        if (api.perform_dm_message_reaction(message_id, c.gtk_entry_get_text(@ptrCast(@alignCast(entry)))) != FALSE) refreshCurrentMessages();
    }
    c.gtk_widget_destroy(dialog);
}

fn onDmMessageEditClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const message_id = stringData(button, lit("message_id"));
    if (message_id == null) return;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Edit Message",
        widgetWindow(button),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        @as(c_int, c.GTK_RESPONSE_CANCEL),
        "_Save",
        @as(c_int, c.GTK_RESPONSE_ACCEPT),
        @as([*c]const c.gchar, null),
    );
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_text(@ptrCast(@alignCast(entry)), textOr(stringData(button, lit("message_content")), ""));
    c.gtk_box_pack_start(@ptrCast(@alignCast(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog))))), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        if (api.perform_dm_message_edit(message_id, c.gtk_entry_get_text(@ptrCast(@alignCast(entry)))) != FALSE) refreshCurrentMessages();
    }
    c.gtk_widget_destroy(dialog);
}

fn onDmMessageDeleteClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const message_id = stringData(button, lit("message_id"));
    if (message_id == null) return;
    const dialog = c.gtk_message_dialog_new(
        widgetWindow(button),
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        c.GTK_MESSAGE_WARNING,
        c.GTK_BUTTONS_OK_CANCEL,
        "Delete this message?",
    );
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_OK) {
        if (api.perform_dm_message_delete(message_id) != FALSE) refreshCurrentMessages();
    }
    c.gtk_widget_destroy(dialog);
}

fn onDmMessageReplyClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    if (g.g_dm_messages_list == null) return;
    const message_id = stringData(button, lit("message_id"));
    const content = stringData(button, lit("message_content"));
    setStringData(@ptrCast(@alignCast(g.g_dm_messages_list)), lit("reply_to_id"), message_id);
    setStringData(@ptrCast(@alignCast(g.g_dm_messages_list)), lit("reply_preview"), content);
    const status_label: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_dm_messages_list)), "composer_status_label")));
    if (status_label != null) {
        const preview = c.g_strdup(textOr(content, "Replying"));
        defer c.g_free(preview);
        if (cstr.len(preview) > 48) preview[48] = 0;
        c.gtk_label_set_text(@ptrCast(@alignCast(status_label)), preview);
        c.gtk_widget_show(status_label);
    }
    if (g.g_dm_entry != null) c.gtk_widget_grab_focus(g.g_dm_entry);
}

fn onDmPaymentRefreshClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const message_id = stringData(button, lit("message_id"));
    const window = widgetWindow(button);
    if (message_id == null) return;
    const url = c.g_strdup_printf(constants.MPI_PAYMENT_BY_MESSAGE_URL, message_id);
    defer c.g_free(url);
    var chunk: types.MemoryStruct = std.mem.zeroes(types.MemoryStruct);
    if (api.fetch_url(url, &chunk, null, "GET") != FALSE) {
        c.g_free(chunk.memory);
        showPollMessage(window, c.GTK_MESSAGE_INFO, "Payment status refreshed.", null);
        refreshCurrentMessages();
    } else {
        showPollMessage(window, c.GTK_MESSAGE_ERROR, "Payment status unavailable.", null);
    }
}

fn parsePaymentStartResponse(json_data: [*c]const c.gchar, order_id_out: *[*c]c.gchar, payment_url_out: *[*c]c.gchar, error_out: *[*c]c.gchar) c.gboolean {
    order_id_out.* = null;
    payment_url_out.* = null;
    error_out.* = null;
    const parser = c.json_parser_new();
    defer c.g_object_unref(parser);
    if (json_data == null or c.json_parser_load_from_data(parser, json_data, -1, null) == FALSE) return FALSE;
    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) return FALSE;
    const obj = c.json_node_get_object(root);
    if (obj != null and c.json_object_has_member(obj, "paymentUrl") != FALSE and c.json_node_is_null(c.json_object_get_member(obj, "paymentUrl")) == FALSE and c.json_object_has_member(obj, "orderId") != FALSE and c.json_node_is_null(c.json_object_get_member(obj, "orderId")) == FALSE) {
        order_id_out.* = c.g_strdup(c.json_object_get_string_member(obj, "orderId"));
        payment_url_out.* = c.g_strdup(c.json_object_get_string_member(obj, "paymentUrl"));
        return TRUE;
    }
    if (obj != null and c.json_object_has_member(obj, "error") != FALSE) {
        error_out.* = c.g_strdup(c.json_object_get_string_member(obj, "error"));
    }
    return FALSE;
}

fn confirmDmPayment(window: [*c]c.GtkWindow, message_id: [*c]const c.gchar, order_id: [*c]const c.gchar) void {
    const dialog = c.gtk_dialog_new_with_buttons(
        "Confirm payment",
        window,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        c.GTK_RESPONSE_CANCEL,
        "_Confirm",
        c.GTK_RESPONSE_ACCEPT,
        @as(?*anyopaque, null),
    );
    const content = c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)));
    const label_ = c.gtk_label_new("Complete payment in the opened page, then paste the transaction id.");
    const entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(@ptrCast(@alignCast(entry)), "Transaction ID");
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), label_, FALSE, FALSE, 8);
    c.gtk_box_pack_start(@ptrCast(@alignCast(content)), entry, FALSE, FALSE, 8);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        const txn_id = c.gtk_entry_get_text(@ptrCast(@alignCast(entry)));
        if (txn_id != null and txn_id[0] != 0) {
            const url = c.g_strdup_printf(constants.MPI_REQUEST_CONFIRM_URL, message_id);
            defer c.g_free(url);
            const builder = c.json_builder_new();
            defer c.g_object_unref(builder);
            const gen = c.json_generator_new();
            defer c.g_object_unref(gen);
            _ = c.json_builder_begin_object(builder);
            _ = c.json_builder_set_member_name(builder, "orderId");
            _ = c.json_builder_add_string_value(builder, order_id);
            _ = c.json_builder_set_member_name(builder, "transactionId");
            _ = c.json_builder_add_string_value(builder, txn_id);
            _ = c.json_builder_end_object(builder);
            const root = c.json_builder_get_root(builder);
            defer c.json_node_free(root);
            c.json_generator_set_root(gen, root);
            const payload = c.json_generator_to_data(gen, null);
            defer c.g_free(payload);
            var chunk: types.MemoryStruct = std.mem.zeroes(types.MemoryStruct);
            if (api.fetch_url(url, &chunk, payload, "POST") != FALSE) {
                c.g_free(chunk.memory);
                showPollMessage(window, c.GTK_MESSAGE_INFO, "Payment confirmed.", null);
                refreshCurrentMessages();
            } else {
                showPollMessage(window, c.GTK_MESSAGE_ERROR, "Payment confirmation failed.", null);
            }
        }
    }
    c.gtk_widget_destroy(dialog);
}

fn onDmPaymentPayClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const message_id = stringData(button, lit("message_id"));
    const window = widgetWindow(button);
    if (message_id == null) return;
    const url = c.g_strdup_printf(constants.MPI_REQUEST_PAY_URL, message_id);
    defer c.g_free(url);
    var chunk: types.MemoryStruct = std.mem.zeroes(types.MemoryStruct);
    if (api.fetch_url(url, &chunk, "", "POST") != FALSE) {
        defer c.g_free(chunk.memory);
        var order_id: [*c]c.gchar = null;
        var payment_url: [*c]c.gchar = null;
        var error_text: [*c]c.gchar = null;
        defer c.g_free(order_id);
        defer c.g_free(payment_url);
        defer c.g_free(error_text);
        if (parsePaymentStartResponse(chunk.memory, &order_id, &payment_url, &error_text) != FALSE) {
            if (payment_url != null and payment_url[0] != 0) _ = c.gtk_show_uri_on_window(window, payment_url, c.GDK_CURRENT_TIME, null);
            confirmDmPayment(window, message_id, order_id);
        } else {
            showPollMessage(window, c.GTK_MESSAGE_ERROR, "Could not start payment.", error_text);
        }
    } else {
        showPollMessage(window, c.GTK_MESSAGE_ERROR, "Could not start payment.", null);
    }
}

fn onOpenCommunityClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const community_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(button)), lit("community_id")));
    const community_name = stringData(button, lit("community_name"));
    const community_description = stringData(button, lit("community_description"));
    const community_rules = stringData(button, lit("community_rules"));
    const community_access_mode = stringData(button, lit("community_access_mode"));
    if (community_id == null) return;
    c.g_mutex_lock(&g.g_globals_mutex);
    c.g_free(g.g_community_id);
    g.g_community_id = c.g_strdup(community_id);
    c.g_mutex_unlock(&g.g_globals_mutex);
    if (g.g_community_tweets_list != null) {
        setStringData(g.g_community_tweets_list, lit("community_id"), community_id);
        setStringData(g.g_community_tweets_list, lit("community_name"), community_name);
        setStringData(g.g_community_tweets_list, lit("community_description"), community_description);
        setStringData(g.g_community_tweets_list, lit("community_rules"), community_rules);
        setStringData(g.g_community_tweets_list, lit("community_access_mode"), community_access_mode);
        api.start_loading_community_tweets(@ptrCast(@alignCast(g.g_community_tweets_list)), community_id);
    }
    if (g.g_community_title_label != null) c.gtk_label_set_text(@ptrCast(@alignCast(g.g_community_title_label)), textOr(community_name, "Community"));
    if (g.g_community_details_label != null) c.gtk_label_set_text(@ptrCast(@alignCast(g.g_community_details_label)), textOr(community_description, ""));
    if (g.g_stack != null) c.gtk_stack_set_visible_child_name(@ptrCast(@alignCast(g.g_stack)), "community_tweets");
    if (g.g_back_button != null) c.gtk_widget_show(g.g_back_button);
}

export fn on_community_clicked(row: [*c]c.GtkListBoxRow, user_data: c.gpointer) void {
    onOpenCommunityClicked(@ptrCast(@alignCast(row)), user_data);
}

fn onJoinCommunityClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const community_id: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(button)), lit("community_id")));
    const is_member = c.g_object_get_data(@ptrCast(@alignCast(button)), lit("is_member")) != null;
    if (community_id == null) return;
    const ok = if (is_member) api.perform_leave_community(community_id) else api.perform_join_community(community_id);
    if (ok != 0) {
        c.gtk_button_set_label(@ptrCast(@alignCast(button)), if (is_member) "Join" else "Leave");
        if (is_member) {
            c.g_object_set_data(@ptrCast(@alignCast(button)), lit("is_member"), null);
        } else {
            c.g_object_set_data(@ptrCast(@alignCast(button)), lit("is_member"), button);
        }
        if (g.g_communities_list != null) api.start_loading_communities(@ptrCast(@alignCast(g.g_communities_list)));
    } else {
        const text = c.g_strdup_printf("Failed to %s community.", if (is_member) lit("leave") else lit("join"));
        defer c.g_free(text);
        showMessage(widgetWindow(button), c.GTK_MESSAGE_ERROR, text, null);
    }
}

fn setLabelBasics(lbl: [*c]c.GtkWidget) void {
    c.gtk_label_set_xalign(label(lbl), 0.0);
    c.gtk_label_set_line_wrap(label(lbl), TRUE);
    c.gtk_label_set_selectable(label(lbl), TRUE);
}

fn addDimLabel(parent: [*c]c.GtkWidget, text: [*c]const c.gchar) void {
    const lbl = c.gtk_label_new(text);
    c.gtk_label_set_xalign(label(lbl), 0.0);
    const ctx = c.gtk_widget_get_style_context(lbl);
    c.gtk_style_context_add_class(ctx, "dim-label");
    pack(parent, lbl, false);
}

fn clearList(list_box: [*c]c.GtkListBox) void {
    const children = c.gtk_container_get_children(container(listBox(list_box)));
    var iter = children;
    while (iter != null) : (iter = iter.*.next) {
        c.gtk_widget_destroy(widget(iter.*.data));
    }
    c.g_list_free(children);
}

fn appendWidget(list_box: [*c]c.GtkListBox, child: [*c]c.GtkWidget) void {
    c.gtk_widget_show_all(child);
    c.gtk_list_box_insert(list_box, child, -1);
}

fn accountHeader(name_: [*c]const c.gchar, username: [*c]const c.gchar, verified: c.gboolean, gold: c.gboolean, gray: c.gboolean, is_op: bool) [*c]c.GtkWidget {
    const row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    const account = api.build_account_label_text(name_, username);
    defer c.g_free(account);
    const account_button = c.gtk_button_new_with_label(account);
    c.gtk_button_set_relief(@ptrCast(@alignCast(account_button)), c.GTK_RELIEF_NONE);
    c.gtk_widget_set_halign(account_button, c.GTK_ALIGN_START);
    if (username != null) c.g_object_set_data_full(@ptrCast(@alignCast(account_button)), "username", c.g_strdup(username), c.g_free);
    connect(account_button, "clicked", @ptrCast(&api.on_author_clicked), null);
    const child = c.gtk_bin_get_child(@ptrCast(@alignCast(account_button)));
    if (child != null) {
        const attrs = c.pango_attr_list_new();
        c.pango_attr_list_insert(attrs, c.pango_attr_weight_new(c.PANGO_WEIGHT_BOLD));
        c.gtk_label_set_attributes(@ptrCast(@alignCast(child)), attrs);
        c.pango_attr_list_unref(attrs);
    }
    pack(row, account_button, false);
    if (gold != FALSE) {
        pack(row, badgeLabel("Gold", "#c88900"), false);
    } else if (gray != FALSE) {
        pack(row, badgeLabel("Gray", "#6c757d"), false);
    } else if (verified != FALSE) {
        pack(row, badgeLabel("Verified", "#1d9bf0"), false);
    }
    if (is_op) pack(row, createOpBadge(), false);
    return row;
}

fn badgeLabel(text: [*c]const c.gchar, color: [*c]const c.gchar) [*c]c.GtkWidget {
    const badge = c.gtk_label_new(null);
    const markup = c.g_strdup_printf("<span foreground='white' background='%s' size='small' weight='bold'> %s </span>", color, text);
    defer c.g_free(markup);
    c.gtk_label_set_markup(label(badge), markup);
    return badge;
}

fn makeMeta(tweet: [*c]types.Tweet) [*c]c.gchar {
    const meta = c.g_string_new(null);
    _ = c.g_string_append_printf(
        meta,
        "%d replies  %d likes  %d retweets",
        tweet.*.reply_count,
        tweet.*.like_count,
        tweet.*.retweet_count,
    );
    if (tweet.*.quote_count > 0 or tweet.*.view_count > 0 or tweet.*.reaction_count > 0) {
        _ = c.g_string_append_printf(
            meta,
            "  %d quotes  %d views  %d reactions",
            tweet.*.quote_count,
            tweet.*.view_count,
            tweet.*.reaction_count,
        );
    }
    if (tweet.*.edited_at != null) _ = c.g_string_append(meta, "  edited");
    return c.g_string_free(meta, FALSE);
}

fn createOpBadge() [*c]c.GtkWidget {
    const op_label = c.gtk_label_new("OP");
    c.gtk_style_context_add_class(c.gtk_widget_get_style_context(op_label), "op-badge");
    c.gtk_label_set_markup(label(op_label), "<span foreground='white' background='#007bff' size='small' weight='bold'> OP </span>");
    return op_label;
}

fn onVideoClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const url = stringData(button, lit("url"));
    if (url == null) return;
    const toplevel = c.gtk_widget_get_toplevel(button);
    const window: [*c]c.GtkWindow = if (toplevel != null) @ptrCast(@alignCast(toplevel)) else null;
    _ = c.gtk_show_uri_on_window(window, url, c.GDK_CURRENT_TIME, null);
}

fn addAttachments(parent: [*c]c.GtkWidget, attachments: [*c]c.GList) void {
    var item = attachments;
    while (item != null) : (item = item.*.next) {
        const attach: [*c]types.Attachment = @ptrCast(@alignCast(item.*.data));
        if (attach == null or attach.*.file_url == null) continue;
        if (attach.*.file_type != null and c.g_str_has_prefix(attach.*.file_type, "image/") != FALSE) {
            const image = c.gtk_image_new();
            api.load_avatar(image, attach.*.file_url, constants.MEDIA_SIZE);
            c.gtk_box_pack_start(box(parent), image, FALSE, FALSE, 5);
        } else if (attach.*.file_type != null and c.g_str_has_prefix(attach.*.file_type, "video/") != FALSE) {
            const video = c.gtk_button_new_with_label("Play Video ▶");
            setStringData(video, lit("url"), attach.*.file_url);
            connect(video, "clicked", @ptrCast(&onVideoClicked), null);
            c.gtk_box_pack_start(box(parent), video, FALSE, FALSE, 5);
        } else {
            const text = c.g_strdup_printf(
                "Attachment (%s): %s",
                textOr(attach.*.file_type, "unknown"),
                attach.*.file_url,
            );
            defer c.g_free(text);
            const lbl = c.gtk_label_new(text);
            c.gtk_label_set_xalign(label(lbl), 0.0);
            c.gtk_label_set_line_wrap(label(lbl), TRUE);
            c.gtk_box_pack_start(box(parent), lbl, FALSE, FALSE, 5);
        }
    }
}

fn createNoteWidget(note: [*c]const c.gchar, severity: [*c]const c.gchar) [*c]c.GtkWidget {
    const frame = c.gtk_frame_new(null);
    const context = c.gtk_widget_get_style_context(frame);
    c.gtk_style_context_add_class(context, "note-frame");
    if (severity != null) {
        if (c.g_strcmp0(severity, "danger") == 0) {
            c.gtk_style_context_add_class(context, "note-danger");
        } else if (c.g_strcmp0(severity, "info") == 0) {
            c.gtk_style_context_add_class(context, "note-info");
        } else {
            c.gtk_style_context_add_class(context, "note-warning");
        }
    } else {
        c.gtk_style_context_add_class(context, "note-warning");
    }

    const note_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    c.gtk_container_set_border_width(container(note_box), 10);
    const note_header = c.gtk_label_new("⚠ Note");
    const attrs = c.pango_attr_list_new();
    c.pango_attr_list_insert(attrs, c.pango_attr_weight_new(c.PANGO_WEIGHT_BOLD));
    c.gtk_label_set_attributes(label(note_header), attrs);
    c.pango_attr_list_unref(attrs);
    c.gtk_widget_set_halign(note_header, c.GTK_ALIGN_START);
    const note_label = c.gtk_label_new(note);
    c.gtk_label_set_xalign(label(note_label), 0.0);
    c.gtk_label_set_line_wrap(label(note_label), TRUE);
    pack(note_box, note_header, false);
    pack(note_box, note_label, false);
    c.gtk_container_add(container(frame), note_box);
    return frame;
}

fn onTweetClicked(widget_: [*c]c.GtkWidget, event: [*c]GdkEventButton, user_data: c.gpointer) callconv(.c) c.gboolean {
    _ = user_data;
    if (event != null and event.*.button != 1) return FALSE;
    const tweet_id = stringData(widget_, lit("tweet_id"));
    if (tweet_id != null) api.show_tweet(tweet_id);
    return TRUE;
}

fn onAdminDeletePostActivated(menuitem: [*c]c.GtkMenuItem, user_data: c.gpointer) callconv(.c) void {
    _ = menuitem;
    const post_id: [*c]const c.gchar = @ptrCast(user_data);
    if (post_id != null) _ = api.perform_admin_delete_post(post_id);
}

fn onAdminPostButtonPress(widget_: [*c]c.GtkWidget, event: [*c]GdkEventButton, user_data: c.gpointer) callconv(.c) c.gboolean {
    _ = user_data;
    if (event == null or event.*.button != 3) return FALSE;
    const post_id = stringData(widget_, lit("tweet_id"));
    if (post_id == null) return FALSE;

    const menu = c.gtk_menu_new();
    const delete_item = c.gtk_menu_item_new_with_label("Admin: Delete Post");
    _ = c.g_signal_connect_data(
        delete_item,
        "activate",
        @ptrCast(&onAdminDeletePostActivated),
        @ptrCast(c.g_strdup(post_id)),
        freeSignalData,
        c.G_CONNECT_DEFAULT,
    );
    c.gtk_menu_shell_append(@ptrCast(@alignCast(menu)), delete_item);
    c.gtk_widget_show_all(menu);
    api.zig_gtk_menu_popup_at_pointer(menu, @ptrCast(event));
    return TRUE;
}

fn onAdminVerifyUserActivated(menuitem: [*c]c.GtkMenuItem, user_data: c.gpointer) callconv(.c) void {
    _ = menuitem;
    const username: [*c]const c.gchar = @ptrCast(user_data);
    if (username != null) _ = api.perform_admin_verify(username, TRUE);
}

fn onAdminDeleteUserActivated(menuitem: [*c]c.GtkMenuItem, user_data: c.gpointer) callconv(.c) void {
    _ = menuitem;
    const username: [*c]const c.gchar = @ptrCast(user_data);
    if (username != null) _ = api.perform_admin_delete_user(username);
}

fn onAdminUserButtonPress(widget_: [*c]c.GtkWidget, event: [*c]GdkEventButton, user_data: c.gpointer) callconv(.c) c.gboolean {
    _ = user_data;
    if (event == null or event.*.button != 3) return FALSE;
    const username = stringData(widget_, lit("username"));
    if (username == null) return FALSE;

    const menu = c.gtk_menu_new();
    const verify_item = c.gtk_menu_item_new_with_label("Admin: Verify User");
    _ = c.g_signal_connect_data(
        verify_item,
        "activate",
        @ptrCast(&onAdminVerifyUserActivated),
        @ptrCast(c.g_strdup(username)),
        freeSignalData,
        c.G_CONNECT_DEFAULT,
    );
    c.gtk_menu_shell_append(@ptrCast(@alignCast(menu)), verify_item);

    const delete_item = c.gtk_menu_item_new_with_label("Admin: Delete User");
    _ = c.g_signal_connect_data(
        delete_item,
        "activate",
        @ptrCast(&onAdminDeleteUserActivated),
        @ptrCast(c.g_strdup(username)),
        freeSignalData,
        c.G_CONNECT_DEFAULT,
    );
    c.gtk_menu_shell_append(@ptrCast(@alignCast(menu)), delete_item);

    c.gtk_widget_show_all(menu);
    api.zig_gtk_menu_popup_at_pointer(menu, @ptrCast(event));
    return TRUE;
}

fn createQuotedTweetWidget(tweet: [*c]types.Tweet) [*c]c.GtkWidget {
    const frame = c.gtk_frame_new(null);
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    c.gtk_container_set_border_width(container(outer), 10);

    const hbox = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    const avatar = c.gtk_image_new();
    c.gtk_widget_set_size_request(avatar, 20, 20);
    api.load_avatar(avatar, tweet.*.author_avatar, 20);
    const author = c.g_strdup_printf(
        "<b>%s</b> <span foreground='gray'>@%s</span>",
        textOr(tweet.*.author_name, "Unknown"),
        textOr(tweet.*.author_username, "unknown"),
    );
    defer c.g_free(author);
    const author_label = c.gtk_label_new(null);
    c.gtk_label_set_markup(label(author_label), author);
    pack(hbox, avatar, false);
    pack(hbox, author_label, false);
    pack(outer, hbox, false);

    if (tweet.*.content != null and tweet.*.content[0] != 0) {
        const content = c.gtk_label_new(tweet.*.content);
        c.gtk_label_set_xalign(label(content), 0.0);
        c.gtk_label_set_line_wrap(label(content), TRUE);
        c.gtk_label_set_ellipsize(label(content), c.PANGO_ELLIPSIZE_END);
        c.gtk_label_set_max_width_chars(label(content), 50);
        pack(outer, content, false);
    }
    addAttachments(outer, tweet.*.attachments);
    c.gtk_container_add(container(frame), outer);

    const event_box = c.gtk_event_box_new();
    c.gtk_container_add(container(event_box), frame);
    setStringData(event_box, lit("tweet_id"), tweet.*.id);
    connect(event_box, "button-press-event", @ptrCast(&onTweetClicked), null);
    return event_box;
}

export fn create_quoted_tweet_widget(tweet: [*c]types.Tweet) [*c]c.GtkWidget {
    return createQuotedTweetWidget(tweet);
}

fn createMultiPollStepResults(step: ?*c.JsonObject, index: c.guint, total_votes: c_int, reveal_quiz: bool) [*c]c.GtkWidget {
    const section = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 4);
    const question = jsonObjectString(step, "question", "Question");
    const type_text = jsonObjectString(step, "type", "single");
    const title_text = c.g_strdup_printf("<b>%u. %s</b>", index + 1, question);
    defer c.g_free(title_text);
    const title_label = c.gtk_label_new(null);
    c.gtk_label_set_markup(label(title_label), title_text);
    c.gtk_label_set_xalign(label(title_label), 0.0);
    c.gtk_label_set_line_wrap(label(title_label), TRUE);
    pack(section, title_label, false);

    const options = jsonObjectArray(step, "options");
    if (options != null and c.json_array_get_length(options) > 0) {
        var j: c.guint = 0;
        while (j < c.json_array_get_length(options)) : (j += 1) {
            const option = jsonArrayObject(options, j);
            const text = jsonObjectString(option, "text", "Option");
            const votes = jsonObjectInt(option, "vote_count", 0);
            const pct = jsonObjectInt(option, "percentage", if (total_votes > 0) @divTrunc(votes * 100, total_votes) else 0);
            const correct = reveal_quiz and option != null and c.json_object_has_member(option, "is_correct") != FALSE and c.json_object_get_boolean_member(option, "is_correct") != FALSE;
            const correct_prefix: [*c]const c.gchar = if (correct) lit("✓ ") else lit("");
            const row = if (c.g_strcmp0(type_text, "ranking") == 0 and option != null and c.json_object_has_member(option, "average_rank") != FALSE)
                c.g_strdup_printf("%s%s · avg rank %.1f", correct_prefix, text, c.json_object_get_double_member(option, "average_rank"))
            else
                c.g_strdup_printf("%s%s · %d%% (%d)", correct_prefix, text, pct, votes);
            defer c.g_free(row);
            const row_label = c.gtk_label_new(row);
            c.gtk_label_set_xalign(label(row_label), 0.0);
            pack(section, row_label, false);
        }
    } else if (c.g_strcmp0(type_text, "number") == 0) {
        const stats = jsonObjectObject(step, "number_stats") orelse return section;
        const summary = c.g_strdup_printf(
            "Average %.1f · min %.1f · max %.1f · %ld responses",
            c.json_object_get_double_member(stats, "average"),
            c.json_object_get_double_member(stats, "min"),
            c.json_object_get_double_member(stats, "max"),
            @as(c_long, @intCast(c.json_object_get_int_member(stats, "count"))),
        );
        defer c.g_free(summary);
        const summary_label = c.gtk_label_new(summary);
        c.gtk_label_set_xalign(label(summary_label), 0.0);
        pack(section, summary_label, false);
    } else if (c.g_strcmp0(type_text, "text") == 0) {
        const summary = c.g_strdup_printf("%d text responses", jsonObjectInt(step, "text_response_count", 0));
        defer c.g_free(summary);
        const summary_label = c.gtk_label_new(summary);
        c.gtk_label_set_xalign(label(summary_label), 0.0);
        pack(section, summary_label, false);
    }

    return section;
}

export fn create_poll_widget(poll: [*c]types.Poll, tweet_id: [*c]const c.gchar) [*c]c.GtkWidget {
    const frame = c.gtk_frame_new(null);
    c.gtk_style_context_add_class(c.gtk_widget_get_style_context(frame), "poll-frame");
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 8);
    c.gtk_container_set_border_width(container(outer), 10);
    c.gtk_container_add(container(frame), outer);
    const question = c.gtk_label_new(null);
    const question_markup = c.g_strdup_printf("<b>%s</b>", textOr(poll.*.question, "Poll"));
    defer c.g_free(question_markup);
    c.gtk_label_set_markup(label(question), question_markup);
    setLabelBasics(question);
    pack(outer, question, false);
    const multi_poll = poll.*.kind != null and c.g_strcmp0(poll.*.kind, "single") != 0;
    if (multi_poll and poll.*.steps != null and c.JSON_NODE_HOLDS_ARRAY(poll.*.steps)) {
        const show_results = poll.*.is_active == FALSE or poll.*.has_user_answers != FALSE;
        if (show_results) {
            const steps = c.json_node_get_array(poll.*.steps);
            const reveal_quiz = c.g_strcmp0(poll.*.kind, "quiz") == 0 and poll.*.has_user_answers != FALSE;
            if (reveal_quiz and poll.*.user_total > 0) {
                const score = c.g_strdup_printf("Score: %d/%d", poll.*.user_score, poll.*.user_total);
                defer c.g_free(score);
                const score_label = c.gtk_label_new(score);
                c.gtk_label_set_xalign(label(score_label), 0.0);
                pack(outer, score_label, false);
            }
            var i: c.guint = 0;
            while (i < c.json_array_get_length(steps)) : (i += 1) {
                const step = jsonArrayObject(steps, i);
                if (step != null) pack(outer, createMultiPollStepResults(step, i, poll.*.total_votes, reveal_quiz), false);
            }
        } else {
            const answer = c.gtk_button_new_with_label("Answer poll");
            c.gtk_button_set_relief(@ptrCast(@alignCast(answer)), c.GTK_RELIEF_NORMAL);
            setStringData(answer, lit("tweet_id"), tweet_id);
            connect(answer, "clicked", @ptrCast(&onMultiPollAnswerClicked), poll);
            pack(outer, answer, false);
        }
        const status_text = c.g_strdup_printf(if (poll.*.is_active != FALSE) "Active · %d submissions" else "Closed · %d submissions", poll.*.total_votes);
        defer c.g_free(status_text);
        addDimLabel(outer, status_text);
        return frame;
    }
    var item = poll.*.options;
    var show_results = poll.*.is_active == FALSE;
    var check = poll.*.options;
    while (check != null and !show_results) : (check = check.*.next) {
        const option: [*c]types.PollOption = @ptrCast(@alignCast(check.*.data));
        if (option != null and option.*.voted != FALSE) show_results = true;
    }
    while (item != null) : (item = item.*.next) {
        const option: [*c]types.PollOption = @ptrCast(@alignCast(item.*.data));
        if (option == null) continue;
        if (!multi_poll and !show_results and poll.*.is_active != FALSE) {
            const vote = c.gtk_button_new_with_label(textOr(option.*.option_text, "Option"));
            c.gtk_button_set_relief(@ptrCast(@alignCast(vote)), c.GTK_RELIEF_NORMAL);
            c.gtk_widget_set_halign(vote, c.GTK_ALIGN_FILL);
            const vote_data: *PollVoteData = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(PollVoteData))));
            vote_data.tweet_id = c.g_strdup(tweet_id);
            vote_data.option_id = c.g_strdup(option.*.id);
            c.g_object_set_data_full(@ptrCast(@alignCast(vote)), "poll_vote_data", vote_data, freePollVoteData);
            connect(vote, "clicked", @ptrCast(&onPollOptionClicked), vote_data);
            pack(outer, vote, false);
        } else if (!multi_poll and show_results) {
            const percentage: f64 = if (poll.*.total_votes > 0)
                @as(f64, @floatFromInt(option.*.vote_count)) / @as(f64, @floatFromInt(poll.*.total_votes)) * 100.0
            else
                0.0;
            const option_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 2);
            const hbox = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
            const option_text = c.g_strdup_printf(if (option.*.voted != FALSE) "✓ %s" else "  %s", textOr(option.*.option_text, "Option"));
            defer c.g_free(option_text);
            const text_label = c.gtk_label_new(option_text);
            c.gtk_label_set_xalign(label(text_label), 0.0);
            const percent_text = c.g_strdup_printf("%.1f%% (%d)", percentage, option.*.vote_count);
            defer c.g_free(percent_text);
            const percent_label = c.gtk_label_new(percent_text);
            c.gtk_label_set_xalign(label(percent_label), 1.0);
            pack(hbox, text_label, true);
            c.gtk_box_pack_end(box(hbox), percent_label, FALSE, FALSE, 0);
            pack(option_box, hbox, false);
            const progress = c.gtk_progress_bar_new();
            c.gtk_progress_bar_set_fraction(@ptrCast(@alignCast(progress)), if (option.*.voted != FALSE) 1.0 else percentage / 100.0);
            c.gtk_progress_bar_set_show_text(@ptrCast(@alignCast(progress)), FALSE);
            pack(option_box, progress, false);
            pack(outer, option_box, false);
        } else {
            const text = c.g_strdup_printf("%s  %d%% (%d)", textOr(option.*.option_text, "Option"), option.*.percentage, option.*.vote_count);
            defer c.g_free(text);
            addDimLabel(outer, text);
        }
    }
    const status_text = if (multi_poll)
        c.g_strdup_printf(if (poll.*.is_active != FALSE) "Active · %d submissions" else "Closed · %d submissions", poll.*.total_votes)
    else
        c.g_strdup_printf(if (poll.*.is_active != FALSE) "Active · %d votes" else "Closed · %d votes", poll.*.total_votes);
    defer c.g_free(status_text);
    addDimLabel(outer, status_text);
    return frame;
}

export fn create_tweet_widget(tweet: [*c]types.Tweet) [*c]c.GtkWidget {
    return create_tweet_widget_full(tweet, null);
}

export fn create_tweet_widget_full(tweet: [*c]types.Tweet, op_username: [*c]const c.gchar) [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const hbox = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    c.gtk_container_set_border_width(container(hbox), 5);
    const body = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const avatar = c.gtk_image_new_from_icon_name("avatar-default", c.GTK_ICON_SIZE_DIALOG);
    c.gtk_widget_set_size_request(avatar, constants.AVATAR_SIZE, constants.AVATAR_SIZE);
    c.gtk_widget_set_valign(avatar, c.GTK_ALIGN_START);
    api.load_avatar(avatar, tweet.*.author_avatar, constants.AVATAR_SIZE);
    const is_op = op_username != null and tweet.*.author_username != null and c.g_strcmp0(op_username, tweet.*.author_username) == 0;
    pack(body, accountHeader(tweet.*.author_name, tweet.*.author_username, tweet.*.author_verified, tweet.*.author_gold, tweet.*.author_gray, is_op), false);
    const content = c.gtk_label_new(textOr(tweet.*.content, ""));
    setLabelBasics(content);
    pack(body, content, false);
    const meta = makeMeta(tweet);
    defer c.g_free(meta);
    addDimLabel(body, meta);
    if (tweet.*.note != null) {
        c.gtk_box_pack_start(box(body), createNoteWidget(tweet.*.note, tweet.*.note_severity), FALSE, FALSE, 5);
    }
    addAttachments(body, tweet.*.attachments);
    if (tweet.*.quote_tweet != null) {
        const quote = createQuotedTweetWidget(tweet.*.quote_tweet);
        c.gtk_box_pack_start(box(body), quote, FALSE, FALSE, 5);
    }
    if (tweet.*.poll != null) c.gtk_box_pack_start(box(body), create_poll_widget(tweet.*.poll, tweet.*.id), FALSE, FALSE, 5);
    pack(hbox, avatar, false);
    pack(hbox, body, true);
    const event_box = c.gtk_event_box_new();
    c.gtk_container_add(container(event_box), hbox);
    setStringData(event_box, lit("tweet_id"), tweet.*.id);
    if (isAdminUser() != FALSE) {
        connect(event_box, "button-press-event", @ptrCast(&onAdminPostButtonPress), null);
    }
    connect(event_box, "button-press-event", @ptrCast(&onTweetClicked), null);
    pack(outer, event_box, true);
    const buttons = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    c.gtk_widget_set_halign(buttons, c.GTK_ALIGN_START);
    c.gtk_container_set_border_width(container(buttons), 5);
    const like_button = tweetButton(if (tweet.*.liked != FALSE) "♥ Liked" else "♡ Like", tweet.*.id, @ptrCast(&onLikeClicked));
    setBoolData(like_button, lit("liked_state"), tweet.*.liked);
    pack(buttons, like_button, false);
    const retweet_button = tweetButton(if (tweet.*.retweeted != FALSE) "↻ Retweeted" else "↻ Retweet", tweet.*.id, @ptrCast(&onRetweetButtonClicked));
    setBoolData(retweet_button, lit("retweeted_state"), tweet.*.retweeted);
    pack(buttons, retweet_button, false);
    const reply_button = tweetButton("↩ Reply", tweet.*.id, @ptrCast(&onReplyClicked));
    setStringData(reply_button, lit("username"), tweet.*.author_username);
    pack(buttons, reply_button, false);
    const bookmark_button = tweetButton(if (tweet.*.bookmarked != FALSE) "★ Saved" else "☆ Bookmark", tweet.*.id, @ptrCast(&onBookmarkClicked));
    setBoolData(bookmark_button, lit("bookmarked_state"), tweet.*.bookmarked);
    pack(buttons, bookmark_button, false);
    pack(buttons, tweetButton("😀 React", tweet.*.id, @ptrCast(&onReactionClicked)), false);
    pack(buttons, tweetButton("History", tweet.*.id, @ptrCast(&onHistoryClicked)), false);
    pack(buttons, tweetButton("Reactions", tweet.*.id, @ptrCast(&onReactionsClicked)), false);
    const translate = tweetButton("Translate", tweet.*.id, @ptrCast(&api.on_translate_tweet_clicked));
    setStringData(translate, lit("tweet_content"), tweet.*.content);
    pack(buttons, translate, false);
    const current_username = currentUsernameSafe();
    defer c.g_free(current_username);
    if (isLoggedIn() != FALSE and (current_username == null or tweet.*.author_username == null or c.g_strcmp0(current_username, tweet.*.author_username) != 0)) {
        const report = tweetButton("Report", tweet.*.id, @ptrCast(&api.on_report_tweet_clicked));
        pack(buttons, report, false);
        const mute_thread = tweetButton("Mute thread", tweet.*.id, @ptrCast(&api.on_mute_conversation_clicked));
        pack(buttons, mute_thread, false);
    }
    if (current_username != null and tweet.*.author_username != null and c.g_strcmp0(current_username, tweet.*.author_username) == 0) {
        const edit = tweetButton("Edit", tweet.*.id, @ptrCast(&onEditTweetClicked));
        setStringData(edit, lit("tweet_content"), tweet.*.content);
        pack(buttons, edit, false);
        pack(buttons, tweetButton("Delete", tweet.*.id, @ptrCast(&onDeleteTweetClicked)), false);
        const pin_button = tweetButton(if (tweet.*.pinned != FALSE) "Unpin" else "Pin", tweet.*.id, @ptrCast(&onPinTweetClicked));
        c.g_object_set_data(@ptrCast(@alignCast(pin_button)), lit("tweet_pinned"), if (tweet.*.pinned != FALSE) @as(c.gpointer, @ptrFromInt(1)) else null);
        pack(buttons, pin_button, false);
        pack(buttons, tweetButton("Highlight", tweet.*.id, @ptrCast(&onHighlightTweetClicked)), false);
        pack(buttons, tweetButton("Replies", tweet.*.id, @ptrCast(&onReplyRestrictionClicked)), false);
        pack(buttons, tweetButton("Outline", tweet.*.id, @ptrCast(&onOutlineTweetClicked)), false);
    }
    if (isAdminUser() != FALSE) {
        pack(buttons, tweetButton("✎ Note", tweet.*.id, @ptrCast(&api.on_note_button_clicked)), false);
    }
    pack(outer, buttons, false);
    pack(outer, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false);
    return outer;
}

export fn populate_tweet_list(list_box: [*c]c.GtkListBox, tweets: [*c]c.GList) void {
    clearList(list_box);
    var item = tweets;
    while (item != null) : (item = item.*.next) {
        appendWidget(list_box, create_tweet_widget(@ptrCast(@alignCast(item.*.data))));
    }
}

export fn append_tweets_to_list(list_box: [*c]c.GtkListBox, tweets: [*c]c.GList) void {
    var item = tweets;
    while (item != null) : (item = item.*.next) {
        appendWidget(list_box, create_tweet_widget(@ptrCast(@alignCast(item.*.data))));
    }
}

export fn create_user_widget(user: [*c]types.Profile) [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const hbox = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    c.gtk_container_set_border_width(container(hbox), 5);
    const body = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const avatar = c.gtk_image_new_from_icon_name("avatar-default", c.GTK_ICON_SIZE_DIALOG);
    c.gtk_widget_set_size_request(avatar, constants.AVATAR_SIZE, constants.AVATAR_SIZE);
    c.gtk_widget_set_valign(avatar, c.GTK_ALIGN_START);
    api.load_avatar(avatar, user.*.avatar, constants.AVATAR_SIZE);
    const user_text = api.build_account_label_text(user.*.name, user.*.username);
    defer c.g_free(user_text);
    const user_button = c.gtk_button_new_with_label(user_text);
    c.gtk_button_set_relief(@ptrCast(@alignCast(user_button)), c.GTK_RELIEF_NONE);
    c.gtk_widget_set_halign(user_button, c.GTK_ALIGN_START);
    const child = c.gtk_bin_get_child(@ptrCast(@alignCast(user_button)));
    if (child != null) {
        const attrs = c.pango_attr_list_new();
        c.pango_attr_list_insert(attrs, c.pango_attr_weight_new(c.PANGO_WEIGHT_BOLD));
        c.gtk_label_set_attributes(@ptrCast(@alignCast(child)), attrs);
        c.pango_attr_list_unref(attrs);
    }
    setStringData(user_button, lit("username"), user.*.username);
    connect(user_button, "clicked", @ptrCast(&api.on_author_clicked), null);
    pack(body, user_button, false);
    if (user.*.author_gold != FALSE) {
        pack(body, badgeLabel("Gold", "#c88900"), false);
    } else if (user.*.author_gray != FALSE) {
        pack(body, badgeLabel("Gray", "#6c757d"), false);
    } else if (user.*.author_verified != FALSE) {
        pack(body, badgeLabel("Verified", "#1d9bf0"), false);
    }
    if (user.*.bio != null and user.*.bio[0] != 0) {
        const bio = c.gtk_label_new(user.*.bio);
        c.gtk_label_set_xalign(label(bio), 0.0);
        c.gtk_label_set_line_wrap(label(bio), TRUE);
        pack(body, bio, false);
    }
    pack(hbox, avatar, false);
    pack(hbox, body, true);
    const event_box = c.gtk_event_box_new();
    c.gtk_container_add(container(event_box), hbox);
    setStringData(event_box, lit("username"), user.*.username);
    if (isAdminUser() != FALSE) {
        connect(event_box, "button-press-event", @ptrCast(&onAdminUserButtonPress), null);
    }
    pack(outer, event_box, true);
    pack(outer, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false);
    return outer;
}

fn onNotificationAvatarClicked(button: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const username = stringData(button, lit("username"));
    if (username != null and username[0] != 0) api.show_profile(username);
}

fn onNotificationClicked(widget_: [*c]c.GtkWidget, event: [*c]GdkEventButton, user_data: c.gpointer) callconv(.c) c.gboolean {
    _ = user_data;
    if (event != null and event.*.button != 1) return FALSE;
    const notification_id = stringData(widget_, lit("notification_id"));
    const related_id = stringData(widget_, lit("related_id"));
    if (notification_id != null) _ = api.mark_notification_read(notification_id);
    if (related_id != null) api.show_tweet(related_id);
    return TRUE;
}

export fn populate_user_list(list_box: [*c]c.GtkListBox, users: [*c]c.GList) void {
    clearList(list_box);
    var item = users;
    while (item != null) : (item = item.*.next) {
        appendWidget(list_box, create_user_widget(@ptrCast(@alignCast(item.*.data))));
    }
}

export fn create_notification_widget(notif: [*c]types.Notification) [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    if (notif.*.read == FALSE) c.gtk_style_context_add_class(c.gtk_widget_get_style_context(outer), "unread-notification");
    const hbox = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    c.gtk_container_set_border_width(container(hbox), 10);
    const avatar_image = c.gtk_image_new_from_icon_name("avatar-default", c.GTK_ICON_SIZE_DIALOG);
    c.gtk_widget_set_size_request(avatar_image, 32, 32);
    c.gtk_widget_set_valign(avatar_image, c.GTK_ALIGN_START);
    if (notif.*.actor_avatar != null) api.load_avatar(avatar_image, notif.*.actor_avatar, 32);
    const avatar_button = c.gtk_button_new();
    c.gtk_button_set_relief(@ptrCast(@alignCast(avatar_button)), c.GTK_RELIEF_NONE);
    c.gtk_container_add(container(avatar_button), avatar_image);
    setStringData(avatar_button, lit("username"), notif.*.actor_username);
    connect(avatar_button, "clicked", @ptrCast(&onNotificationAvatarClicked), null);
    pack(hbox, avatar_button, false);

    const vbox = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 2);
    const actor_name = textOr(if (notif.*.actor_name != null) notif.*.actor_name else notif.*.actor_username, "");
    const badge_suffix: [*c]const c.gchar = if (notif.*.actor_gold != FALSE) " <span foreground='#c88900'>[Gold]</span>" else if (notif.*.actor_verified != FALSE) " <span foreground='#1d9bf0'>[Verified]</span>" else "";
    var notif_text: [*c]c.gchar = null;
    if (c.g_strcmp0(notif.*.type, "like") == 0) {
        notif_text = c.g_strdup_printf("<b>%s</b>%s liked your tweet", actor_name, badge_suffix);
    } else if (c.g_strcmp0(notif.*.type, "retweet") == 0) {
        notif_text = c.g_strdup_printf("<b>%s</b>%s retweeted your tweet", actor_name, badge_suffix);
    } else if (c.g_strcmp0(notif.*.type, "reply") == 0) {
        notif_text = c.g_strdup_printf("<b>%s</b>%s replied to your tweet", actor_name, badge_suffix);
    } else if (c.g_strcmp0(notif.*.type, "follow") == 0) {
        notif_text = c.g_strdup_printf("<b>%s</b>%s followed you", actor_name, badge_suffix);
    } else if (c.g_strcmp0(notif.*.type, "mention") == 0) {
        notif_text = c.g_strdup_printf("<b>%s</b>%s mentioned you", actor_name, badge_suffix);
    } else if (c.g_strcmp0(notif.*.type, "quote") == 0) {
        notif_text = c.g_strdup_printf("<b>%s</b>%s quoted your tweet", actor_name, badge_suffix);
    } else if (c.g_strcmp0(notif.*.type, "reaction") == 0) {
        notif_text = c.g_strdup_printf("<b>%s</b>%s reacted to your tweet", actor_name, badge_suffix);
    } else {
        notif_text = c.g_strdup_printf("<b>%s</b>%s: %s", actor_name, badge_suffix, textOr(notif.*.content, ""));
    }
    defer c.g_free(notif_text);
    const label_widget = c.gtk_label_new(null);
    c.gtk_label_set_markup(label(label_widget), notif_text);
    c.gtk_label_set_xalign(label(label_widget), 0.0);
    c.gtk_label_set_line_wrap(label(label_widget), TRUE);
    pack(vbox, label_widget, false);
    if (notif.*.content != null and notif.*.content[0] != 0 and c.g_strcmp0(notif.*.type, "dm_message") != 0) {
        const content_label = c.gtk_label_new(notif.*.content);
        c.gtk_label_set_xalign(label(content_label), 0.0);
        c.gtk_label_set_line_wrap(label(content_label), TRUE);
        c.gtk_style_context_add_class(c.gtk_widget_get_style_context(content_label), "dim-label");
        pack(vbox, content_label, false);
    }
    const content_event = c.gtk_event_box_new();
    c.gtk_container_add(container(content_event), vbox);
    setStringData(content_event, lit("notification_id"), notif.*.id);
    setStringData(content_event, lit("related_id"), notif.*.related_id);
    connect(content_event, "button-press-event", @ptrCast(&onNotificationClicked), null);
    pack(hbox, content_event, true);
    pack(outer, hbox, true);
    pack(outer, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false);
    return outer;
}

export fn populate_notification_list(list_box: [*c]c.GtkListBox, notifications: [*c]c.GList) void {
    clearList(list_box);
    append_notifications_to_list(list_box, notifications);
}

export fn append_notifications_to_list(list_box: [*c]c.GtkListBox, notifications: [*c]c.GList) void {
    var item = notifications;
    while (item != null) : (item = item.*.next) {
        appendWidget(list_box, create_notification_widget(@ptrCast(@alignCast(item.*.data))));
    }
}

export fn create_conversation_widget(conv: [*c]types.Conversation) [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const event_box = c.gtk_event_box_new();
    const hbox = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    c.gtk_container_set_border_width(container(hbox), 10);
    const avatar = c.gtk_image_new_from_icon_name("avatar-default", c.GTK_ICON_SIZE_DIALOG);
    c.gtk_widget_set_size_request(avatar, 48, 48);
    if (conv.*.display_avatar != null) api.load_avatar(avatar, conv.*.display_avatar, 48);
    const vbox = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 2);
    const name_markup = c.g_strdup_printf("<b>%s</b>", textOr(conv.*.display_name, "Unknown"));
    defer c.g_free(name_markup);
    const name_label = c.gtk_label_new(null);
    c.gtk_label_set_markup(label(name_label), name_markup);
    c.gtk_label_set_xalign(label(name_label), 0.0);
    pack(vbox, name_label, false);
    const last_label = c.gtk_label_new(textOr(conv.*.last_message_content, ""));
    c.gtk_label_set_xalign(label(last_label), 0.0);
    c.gtk_label_set_ellipsize(label(last_label), c.PANGO_ELLIPSIZE_END);
    c.gtk_style_context_add_class(c.gtk_widget_get_style_context(last_label), "dim-label");
    pack(vbox, last_label, false);
    var details: [*c]c.gchar = null;
    if (conv.*.participant_count > 1 and conv.*.last_message_sender != null) {
        details = c.g_strdup_printf("%d participants  •  Last from %s", conv.*.participant_count, conv.*.last_message_sender);
    } else if (conv.*.participant_count > 1) {
        details = c.g_strdup_printf("%d participants", conv.*.participant_count);
    } else if (conv.*.last_message_sender != null) {
        details = c.g_strdup_printf("Last from %s", conv.*.last_message_sender);
    }
    defer c.g_free(details);
    if (details != null) addDimLabel(vbox, details);
    pack(hbox, avatar, false);
    pack(hbox, vbox, true);
    if (conv.*.unread_count > 0) {
        const unread = c.g_strdup_printf("%d", conv.*.unread_count);
        defer c.g_free(unread);
        c.gtk_box_pack_end(box(hbox), c.gtk_label_new(unread), FALSE, FALSE, 0);
    }
    c.gtk_container_add(container(event_box), hbox);
    setStringData(event_box, lit("conversation_id"), conv.*.id);
    setStringData(event_box, lit("display_name"), conv.*.display_name);
    connect(event_box, "button-press-event", @ptrCast(&onConversationPressed), null);
    pack(outer, event_box, true);
    pack(outer, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false);
    return outer;
}

export fn populate_conversation_list(list_box: [*c]c.GtkListBox, conversations: [*c]c.GList) void {
    clearList(list_box);
    var item = conversations;
    while (item != null) : (item = item.*.next) {
        appendWidget(list_box, create_conversation_widget(@ptrCast(@alignCast(item.*.data))));
    }
    if (g.g_p2p_session != null) {
        c.g_mutex_lock(&g.g_p2p_session.*.session_mutex);
        var iter: c.GHashTableIter = undefined;
        var key: c.gpointer = null;
        var value: c.gpointer = null;
        c.g_hash_table_iter_init(&iter, g.g_p2p_session.*.contacts);
        while (c.g_hash_table_iter_next(&iter, &key, &value) != FALSE) {
            const contact: [*c]types.P2PContact = @ptrCast(@alignCast(value));
            if (contact == null) continue;
            const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
            const event_box = c.gtk_event_box_new();
            const hbox = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
            c.gtk_container_set_border_width(container(hbox), 10);
            const avatar = c.gtk_image_new_from_icon_name("security-high", c.GTK_ICON_SIZE_DIALOG);
            c.gtk_widget_set_size_request(avatar, 48, 48);
            const vbox = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 2);
            const name_markup = c.g_strdup_printf("<b>%s</b> <span foreground=\"#2ecc71\" size=\"small\">[Encrypted]</span>", textOr(contact.*.display_name, contact.*.username));
            defer c.g_free(name_markup);
            const name_label = c.gtk_label_new(null);
            c.gtk_label_set_markup(label(name_label), name_markup);
            c.gtk_label_set_xalign(label(name_label), 0.0);
            pack(vbox, name_label, false);
            addDimLabel(vbox, "P2P encrypted messaging");
            pack(hbox, avatar, false);
            pack(hbox, vbox, true);
            c.gtk_container_add(container(event_box), hbox);
            setStringData(event_box, lit("p2p_contact_username"), contact.*.username);
            connect(event_box, "button-press-event", @ptrCast(&api.on_p2p_contact_clicked), null);
            pack(outer, event_box, true);
            pack(outer, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false);
            appendWidget(list_box, outer);
        }
        c.g_mutex_unlock(&g.g_p2p_session.*.session_mutex);
    }
}

fn createDmPaymentCard(msg: [*c]types.DirectMessage, own_message: bool) [*c]c.GtkWidget {
    const frame = c.gtk_frame_new(null);
    const card = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 6);
    c.gtk_container_set_border_width(container(card), 8);
    const kind = textOr(if (msg.*.mpi_kind != null) msg.*.mpi_kind else msg.*.message_type, "");
    const status = textOr(msg.*.mpi_status, "pending");
    const net = textOr(msg.*.mpi_net, "");
    const gross = textOr(msg.*.mpi_gross, "");
    const title: [*c]const c.gchar = if (c.g_strcmp0(kind, "request") == 0) "Payment request" else if (c.g_strcmp0(kind, "donate") == 0) "Donation" else "Payment";
    var summary = c.g_strdup_printf("<b>%s</b> · ₹%s%s%s · %s", title, net, if (gross[0] != 0) lit(" (payer total ₹") else lit(""), if (gross[0] != 0) gross else lit(""), status);
    if (gross[0] != 0) {
        const fixed = c.g_strdup_printf("<b>%s</b> · ₹%s (payer total ₹%s) · %s", title, net, gross, status);
        c.g_free(summary);
        summary = fixed;
    }
    defer c.g_free(summary);
    const summary_label = c.gtk_label_new(null);
    c.gtk_label_set_markup(label(summary_label), summary);
    c.gtk_label_set_xalign(label(summary_label), 0.0);
    pack(card, summary_label, false);

    const note = if (msg.*.mpi_note != null and msg.*.mpi_note[0] != 0) msg.*.mpi_note else msg.*.content;
    if (note != null and note[0] != 0) {
        const note_label = c.gtk_label_new(note);
        c.gtk_label_set_xalign(label(note_label), 0.0);
        c.gtk_label_set_line_wrap(label(note_label), TRUE);
        pack(card, note_label, false);
    }

    const buttons = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 4);
    if (c.g_strcmp0(kind, "request") == 0 and c.g_strcmp0(status, "pending") == 0 and !own_message) {
        const pay = messageButton("Pay", msg.*.id, @ptrCast(&onDmPaymentPayClicked));
        c.gtk_button_set_relief(@ptrCast(@alignCast(pay)), c.GTK_RELIEF_NONE);
        pack(buttons, pay, false);
    }
    if (msg.*.mpi_payment_link_url != null and msg.*.mpi_payment_link_url[0] != 0) {
        const open = c.gtk_button_new_with_label("Open payout");
        c.gtk_button_set_relief(@ptrCast(@alignCast(open)), c.GTK_RELIEF_NONE);
        setStringData(open, lit("url"), msg.*.mpi_payment_link_url);
        connect(open, "clicked", @ptrCast(&onVideoClicked), null);
        pack(buttons, open, false);
    }
    const refresh = messageButton("Refresh", msg.*.id, @ptrCast(&onDmPaymentRefreshClicked));
    c.gtk_button_set_relief(@ptrCast(@alignCast(refresh)), c.GTK_RELIEF_NONE);
    pack(buttons, refresh, false);
    pack(card, buttons, false);
    c.gtk_container_add(container(frame), card);
    return frame;
}

export fn create_message_widget(msg: [*c]types.DirectMessage) [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    c.gtk_container_set_border_width(container(outer), 5);
    const avatar = c.gtk_image_new_from_icon_name("avatar-default", c.GTK_ICON_SIZE_MENU);
    c.gtk_widget_set_size_request(avatar, 32, 32);
    if (msg.*.avatar != null) api.load_avatar(avatar, msg.*.avatar, 32);
    const body = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 2);
    const current_username = currentUsernameSafe();
    defer c.g_free(current_username);
    const own_message = current_username != null and msg.*.username != null and c.g_strcmp0(current_username, msg.*.username) == 0;
    const header_text = if (msg.*.verified != FALSE)
        c.g_strdup_printf(
            "<b>%s</b> <span foreground='#1d9bf0'>[Verified]</span> (@%s) · %s%s",
            textOr(msg.*.name, "Unknown"),
            textOr(msg.*.username, "unknown"),
            textOr(msg.*.created_at, ""),
            if (msg.*.edited_at != null) lit(" · edited") else lit(""),
        )
    else
        c.g_strdup_printf(
            "<b>%s</b> (@%s) · %s%s",
            textOr(msg.*.name, "Unknown"),
            textOr(msg.*.username, "unknown"),
            textOr(msg.*.created_at, ""),
            if (msg.*.edited_at != null) lit(" · edited") else lit(""),
        );
    defer c.g_free(header_text);
    const header = c.gtk_label_new(null);
    c.gtk_label_set_markup(label(header), header_text);
    c.gtk_label_set_xalign(label(header), 0.0);
    pack(body, header, false);
    const is_mpi_message = msg.*.message_type != null and (c.g_strcmp0(msg.*.message_type, "mpi_request") == 0 or c.g_strcmp0(msg.*.message_type, "mpi_send") == 0 or c.g_strcmp0(msg.*.message_type, "mpi_donate") == 0);
    if (msg.*.message_type != null and c.g_strcmp0(msg.*.message_type, "text") != 0 and !is_mpi_message) {
        const type_text = c.g_strdup_printf("Type: %s", msg.*.message_type);
        defer c.g_free(type_text);
        addDimLabel(body, type_text);
    }
    if (msg.*.reply_to != null or msg.*.reply_preview != null) {
        const reply_text = c.g_strdup_printf("Replying to %s%s%s", textOr(msg.*.reply_to, "message"), if (msg.*.reply_preview != null) lit(": ") else lit(""), textOr(msg.*.reply_preview, ""));
        defer c.g_free(reply_text);
        addDimLabel(body, reply_text);
    }
    if (is_mpi_message and msg.*.is_deleted == FALSE) {
        pack(body, createDmPaymentCard(msg, own_message), false);
    } else {
        const content = c.gtk_label_new(if (msg.*.is_deleted != FALSE) "[Deleted message]" else textOr(msg.*.content, ""));
        setLabelBasics(content);
        c.gtk_label_set_selectable(label(content), TRUE);
        pack(body, content, false);
    }
    if (msg.*.is_deleted == FALSE) addAttachments(body, msg.*.attachments);
    if (msg.*.reactions_summary != null and msg.*.reactions_summary[0] != 0) addDimLabel(body, msg.*.reactions_summary);
    const buttons = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 4);
    const reply = messageButton("Reply", msg.*.id, @ptrCast(&onDmMessageReplyClicked));
    setStringData(reply, lit("message_content"), msg.*.content);
    pack(buttons, reply, false);
    pack(buttons, messageButton("React", msg.*.id, @ptrCast(&onDmMessageReactClicked)), false);
    pack(buttons, messageButton("Pin", msg.*.id, @ptrCast(&api.on_dm_pin_message_clicked)), false);
    if (own_message and msg.*.is_deleted == FALSE) {
        const edit = messageButton("Edit", msg.*.id, @ptrCast(&onDmMessageEditClicked));
        setStringData(edit, lit("message_content"), msg.*.content);
        pack(buttons, edit, false);
        pack(buttons, messageButton("Delete", msg.*.id, @ptrCast(&onDmMessageDeleteClicked)), false);
    }
    pack(body, buttons, false);
    pack(outer, avatar, false);
    pack(outer, body, true);
    return outer;
}

export fn populate_message_list(list_box: [*c]c.GtkListBox, messages: [*c]c.GList) void {
    clearList(list_box);
    var item = c.g_list_last(messages);
    while (item != null) : (item = item.*.prev) {
        appendWidget(list_box, create_message_widget(@ptrCast(@alignCast(item.*.data))));
    }
    var scrolled: [*c]c.GtkWidget = c.gtk_widget_get_parent(@ptrCast(@alignCast(list_box)));
    if (scrolled != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(scrolled)), c.gtk_viewport_get_type()) != FALSE) {
        scrolled = c.gtk_widget_get_parent(scrolled);
    }
    if (scrolled != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(scrolled)), c.gtk_scrolled_window_get_type()) != FALSE) {
        const adjustment = c.gtk_scrolled_window_get_vadjustment(@ptrCast(@alignCast(scrolled)));
        c.gtk_adjustment_set_value(adjustment, c.gtk_adjustment_get_upper(adjustment) - c.gtk_adjustment_get_page_size(adjustment));
    }
}

export fn create_community_widget(community: [*c]types.Community) [*c]c.GtkWidget {
    const row = c.gtk_list_box_row_new();
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    c.gtk_widget_set_margin_start(outer, 10);
    c.gtk_widget_set_margin_end(outer, 10);
    c.gtk_widget_set_margin_top(outer, 10);
    c.gtk_widget_set_margin_bottom(outer, 10);
    c.gtk_container_add(container(row), outer);
    const icon = c.gtk_image_new_from_icon_name("avatar-default", c.GTK_ICON_SIZE_DND);
    c.gtk_widget_set_size_request(icon, 48, 48);
    if (community.*.icon_url != null) api.load_avatar(icon, community.*.icon_url, 48);
    pack(outer, icon, false);
    const info = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const name_row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    const name_label = c.gtk_label_new(textOr(community.*.name, "Community"));
    c.gtk_widget_set_halign(name_label, c.GTK_ALIGN_START);
    const attrs = c.pango_attr_list_new();
    c.pango_attr_list_insert(attrs, c.pango_attr_weight_new(c.PANGO_WEIGHT_BOLD));
    c.gtk_label_set_attributes(label(name_label), attrs);
    c.pango_attr_list_unref(attrs);
    pack(name_row, name_label, false);
    var badge_color: [*c]const c.gchar = null;
    if (c.g_strcmp0(community.*.access_mode, "public") == 0) {
        badge_color = "#4CAF50";
    } else if (c.g_strcmp0(community.*.access_mode, "private") == 0) {
        badge_color = "#F44336";
    } else if (c.g_strcmp0(community.*.access_mode, "restricted") == 0) {
        badge_color = "#FF9800";
    }
    if (badge_color != null) {
        const badge = c.gtk_label_new(null);
        const badge_markup = c.g_strdup_printf("<span bgcolor=\"%s\" fgcolor=\"white\" size=\"small\"> %s </span>", badge_color, community.*.access_mode);
        defer c.g_free(badge_markup);
        c.gtk_label_set_markup(label(badge), badge_markup);
        pack(name_row, badge, false);
    }
    pack(info, name_row, false);
    if (community.*.description != null) {
        const desc = c.gtk_label_new(community.*.description);
        c.gtk_widget_set_halign(desc, c.GTK_ALIGN_START);
        c.gtk_label_set_ellipsize(label(desc), c.PANGO_ELLIPSIZE_END);
        c.gtk_label_set_max_width_chars(label(desc), 50);
        c.gtk_label_set_line_wrap(label(desc), FALSE);
        pack(info, desc, false);
    }
    const members = c.g_strdup_printf("%d members", community.*.member_count);
    defer c.g_free(members);
    const members_label = c.gtk_label_new(members);
    c.gtk_widget_set_halign(members_label, c.GTK_ALIGN_START);
    c.gtk_widget_set_opacity(members_label, 0.6);
    pack(info, members_label, false);
    pack(outer, info, true);
    const join = c.gtk_button_new_with_label(if (community.*.is_member != FALSE) "Leave" else "Join");
    c.gtk_widget_set_valign(join, c.GTK_ALIGN_CENTER);
    setStringData(join, lit("community_id"), community.*.id);
    if (community.*.is_member != FALSE) c.g_object_set_data(@ptrCast(@alignCast(join)), lit("is_member"), join);
    connect(join, "clicked", @ptrCast(&onJoinCommunityClicked), null);
    c.gtk_box_pack_end(box(outer), join, FALSE, FALSE, 0);
    setStringData(row, lit("community_id"), community.*.id);
    setStringData(row, lit("community_name"), community.*.name);
    setStringData(row, lit("community_description"), community.*.description);
    setStringData(row, lit("community_rules"), community.*.rules);
    setStringData(row, lit("community_access_mode"), community.*.access_mode);
    connect(row, "activate", @ptrCast(&onOpenCommunityClicked), null);
    return row;
}

export fn populate_community_list(list_box: [*c]c.GtkListBox, communities: [*c]c.GList) void {
    clearList(list_box);
    if (communities == null) {
        appendWidget(list_box, c.gtk_label_new("No communities found."));
        return;
    }
    var item = communities;
    while (item != null) : (item = item.*.next) {
        appendWidget(list_box, create_community_widget(@ptrCast(@alignCast(item.*.data))));
    }
}
