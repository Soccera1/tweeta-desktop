const c = @import("c.zig").c;
const cstr = @import("cstr.zig");
const constants = @import("constants.zig");
const types = @import("types.zig");

const FALSE: c.gboolean = 0;
const TRUE: c.gboolean = 1;
const null_gchar: [*c]const c.gchar = null;

extern fn fetch_url_internal(
    url: [*c]const c.gchar,
    chunk: [*c]types.MemoryStruct,
    post_data: [*c]const c.gchar,
    method: [*c]const c.gchar,
    response_code: [*c]c_long,
) c.gboolean;
extern fn show_profile(username: [*c]const c.gchar) void;

fn cStrLen(value: [*c]const c.gchar) usize {
    return cstr.len(value);
}

fn isEmpty(value: [*c]const c.gchar) bool {
    return value == null or value[0] == 0;
}

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn isGtkImage(image: [*c]c.GtkWidget) bool {
    return image != null and c.g_type_check_instance_is_a(@ptrCast(image), c.gtk_image_get_type()) != FALSE;
}

fn isGObject(object: c.gpointer) bool {
    return object != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(object)), c.g_object_get_type()) != FALSE;
}

fn logMsg(level: c.GLogLevelFlags, comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), level, lit(fmt) } ++ args);
}

fn dupConcat(left: [*c]const c.gchar, right: [*c]const c.gchar) [*c]c.gchar {
    const left_len = cStrLen(left);
    const right_len = cStrLen(right);
    const out: [*c]c.gchar = @ptrCast(c.g_malloc(left_len + right_len + 1));
    if (out == null) return null;
    if (left_len > 0) @memcpy(out[0..left_len], left[0..left_len]);
    if (right_len > 0) @memcpy(out[left_len .. left_len + right_len], right[0..right_len]);
    out[left_len + right_len] = 0;
    return out;
}

fn dupReplyBanner(username: [*c]const c.gchar) [*c]c.gchar {
    const display = if (!isEmpty(username)) username else lit("unknown");
    return c.g_strdup_printf("Replying to @%s:", display);
}

fn dupAccountLabel(name: [*c]const c.gchar, username: [*c]const c.gchar) [*c]c.gchar {
    const display_name = if (!isEmpty(name)) name else lit("Unknown");
    const display_username = if (!isEmpty(username)) username else lit("unknown");
    const name_len = cStrLen(display_name);
    const username_len = cStrLen(display_username);
    const total_len = name_len + " (@".len + username_len + ")".len;
    const out: [*c]c.gchar = @ptrCast(c.g_malloc(total_len + 1));
    if (out == null) return null;

    var offset: usize = 0;
    @memcpy(out[offset .. offset + name_len], display_name[0..name_len]);
    offset += name_len;
    @memcpy(out[offset .. offset + " (@".len], " (@");
    offset += " (@".len;
    @memcpy(out[offset .. offset + username_len], display_username[0..username_len]);
    offset += username_len;
    out[offset] = ')';
    out[offset + 1] = 0;
    return out;
}

fn setImagePixbuf(data: c.gpointer) callconv(.c) c.gboolean {
    const params: [*]c.gpointer = @ptrCast(@alignCast(data));
    const pixbuf: ?*c.GdkPixbuf = @ptrCast(params[0]);
    const image: [*c]c.GtkWidget = @ptrCast(@alignCast(params[1]));

    if (isGtkImage(image) and pixbuf != null) {
        c.gtk_image_set_from_pixbuf(@ptrCast(image), pixbuf);
    }

    if (pixbuf != null) {
        c.g_object_unref(@ptrCast(pixbuf));
    }
    if (isGObject(image)) {
        c.g_object_unref(image);
    }
    c.g_free(data);
    return FALSE;
}

fn fetchAvatarThread(data: c.gpointer) callconv(.c) c.gpointer {
    const avatar_data: [*c]types.AvatarData = @ptrCast(@alignCast(data));
    var chunk: types.MemoryStruct = .{ .memory = null, .size = 0 };

    const full_url = if (c.g_str_has_prefix(avatar_data.*.url, "http") != FALSE)
        c.g_strdup(avatar_data.*.url)
    else
        dupConcat(constants.BASE_DOMAIN, avatar_data.*.url);

    var response_code: c_long = 0;
    if (fetch_url_internal(full_url, &chunk, null_gchar, "GET", &response_code) != FALSE) {
        const stream = c.g_memory_input_stream_new_from_data(chunk.memory, @intCast(chunk.size), null);
        const pixbuf = c.gdk_pixbuf_new_from_stream_at_scale(
            @ptrCast(stream),
            avatar_data.*.size,
            avatar_data.*.size,
            TRUE,
            null,
            null,
        );

        if (pixbuf != null) {
            const params: [*]c.gpointer = @ptrCast(@alignCast(c.g_malloc(2 * @sizeOf(c.gpointer))));
            params[0] = @ptrCast(pixbuf);
            params[1] = if (isGObject(avatar_data.*.image)) c.g_object_ref(avatar_data.*.image) else null;
            _ = c.g_idle_add(setImagePixbuf, @ptrCast(params));
        }
        c.g_object_unref(stream);
        c.g_free(chunk.memory);
    }

    c.g_free(full_url);
    c.g_free(avatar_data.*.url);
    c.g_free(avatar_data);
    return null;
}

export fn load_avatar(image: [*c]c.GtkWidget, url: [*c]const c.gchar, size: c_int) void {
    if (image == null or isEmpty(url)) return;

    const data: [*c]types.AvatarData = @ptrCast(@alignCast(c.g_malloc(@sizeOf(types.AvatarData))));
    if (data == null) return;
    data.*.image = image;
    data.*.url = c.g_strdup(url);
    data.*.size = size;

    _ = c.g_thread_new("avatar-loader", fetchAvatarThread, data);
}

export fn on_author_clicked(button: [*c]c.GtkButton, user_data: c.gpointer) void {
    _ = user_data;
    const username: [*c]const c.gchar = @ptrCast(c.g_object_get_data(@ptrCast(button), "username"));
    if (!isEmpty(username)) {
        show_profile(username);
    }
}

export fn detect_mime_type(file_path: [*c]const c.gchar) [*c]c.gchar {
    if (file_path == null) {
        return c.g_strdup("application/octet-stream");
    }

    var uncertain: c.gboolean = FALSE;
    const content_type = c.g_content_type_guess(file_path, null, 0, &uncertain);
    if (content_type == null) {
        return c.g_strdup("application/octet-stream");
    }

    const mime_type = c.g_content_type_get_mime_type(content_type);
    c.g_free(content_type);

    if (mime_type == null) {
        return c.g_strdup("application/octet-stream");
    }

    return mime_type;
}

export fn build_reply_banner_text(username: [*c]const c.gchar) [*c]c.gchar {
    return dupReplyBanner(username);
}

export fn build_account_label_text(name: [*c]const c.gchar, username: [*c]const c.gchar) [*c]c.gchar {
    return dupAccountLabel(name, username);
}

export fn free_attachment_payload(data: c.gpointer) void {
    const attach: [*c]types.Attachment = @ptrCast(@alignCast(data));
    if (attach != null) {
        c.g_free(attach.*.id);
        c.g_free(attach.*.file_url);
        c.g_free(attach.*.file_type);
        c.g_free(attach);
    }
}

export fn build_attachment_list(file_url: [*c]const c.gchar, file_type: [*c]const c.gchar) [*c]c.GList {
    logMsg(c.G_LOG_LEVEL_DEBUG, "build_attachment_list: file_url=%s, file_type=%s", .{
        if (file_url != null) file_url else lit("(null)"),
        if (file_type != null) file_type else lit("(null)"),
    });

    if (file_url == null) {
        logMsg(c.G_LOG_LEVEL_DEBUG, "build_attachment_list: returning NULL due to NULL file_url", .{});
        return null;
    }

    const attach: [*c]types.Attachment = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.Attachment))));
    if (attach == null) {
        return null;
    }
    attach.*.file_url = c.g_strdup(file_url);
    attach.*.file_type = c.g_strdup(if (file_type != null) file_type else "application/octet-stream");
    logMsg(c.G_LOG_LEVEL_DEBUG, "build_attachment_list: created attachment with file_url=%s, file_type=%s", .{ attach.*.file_url, attach.*.file_type });

    return c.g_list_append(null, attach);
}

export fn free_wrapper(data: c.gpointer, closure: ?*c.GClosure) void {
    _ = closure;
    c.g_free(data);
}
