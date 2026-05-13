const std = @import("std");
const c = @import("c.zig").c;
const cstr = @import("cstr.zig");
const types = @import("types.zig");

const FALSE: c.gboolean = 0;
const TRUE: c.gboolean = 1;

extern var g_p2p_session: [*c]types.P2PSession;
extern var g_stack: [*c]c.GtkWidget;
extern var g_back_button: [*c]c.GtkWidget;
extern var g_dm_title_label: [*c]c.GtkWidget;
extern var g_dm_messages_list: [*c]c.GtkWidget;
extern fn p2p_set_transport_mode(mode: types.P2PTransportMode) void;
extern fn p2p_get_transport_mode() types.P2PTransportMode;
extern fn p2p_is_listener_running() c.gboolean;
extern fn p2p_stop_listener() void;
extern fn p2p_start_listener(host: [*c]const c.gchar, port: c.guint16) c.gboolean;
extern fn p2p_get_listen_address() [*c]c.gchar;
extern fn p2p_start_message_polling() void;
extern fn p2p_connect_to_peer(host: [*c]const c.gchar, port: c.guint16, username: [*c]const c.gchar) c_int;
extern fn p2p_refresh_contacts_list() void;
extern fn p2p_refresh_messages_list(contact_username: [*c]const c.gchar) void;

fn isEmpty(value: [*c]const c.gchar) bool {
    return value == null or value[0] == 0;
}

fn widgetFromData(data: c.gpointer) [*c]c.GtkWidget {
    return @ptrCast(@alignCast(data));
}

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn logMsg(level: c.GLogLevelFlags, comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), level, lit(fmt) } ++ args);
}

fn obj(widget: [*c]c.GtkWidget) [*c]c.GObject {
    return @ptrCast(@alignCast(widget));
}

fn showMessage(kind: c.GtkMessageType, fmt: [*c]const c.gchar, args: anytype) void {
    const dialog = @call(.auto, c.gtk_message_dialog_new, .{
        @as([*c]c.GtkWindow, null),
        @as(c.GtkDialogFlags, c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT),
        kind,
        c.GTK_BUTTONS_CLOSE,
        fmt,
    } ++ args);
    _ = c.gtk_dialog_run(@ptrCast(dialog));
    c.gtk_widget_destroy(dialog);
}

fn makeText(comptime fmt: []const u8, args: anytype) [*c]c.gchar {
    const len = std.fmt.count(fmt, args);
    const out: [*c]c.gchar = @ptrCast(c.g_malloc(len + 1));
    if (out == null) return null;
    _ = std.fmt.bufPrint(out[0..len], fmt, args) catch unreachable;
    out[len] = 0;
    return out;
}

fn parsePort(value: [*c]const c.gchar) c.guint16 {
    const text = cstr.bytes(value);
    var i: usize = 0;
    while (i < text.len and (text[i] == ' ' or text[i] == '\t' or text[i] == '\n' or text[i] == '\r' or text[i] == 0x0b or text[i] == 0x0c)) : (i += 1) {}
    var negative = false;
    if (i < text.len and (text[i] == '-' or text[i] == '+')) {
        negative = text[i] == '-';
        i += 1;
    }

    var value_i: c_int = 0;
    while (i < text.len and text[i] >= '0' and text[i] <= '9') : (i += 1) {
        value_i = value_i *% 10 +% @as(c_int, @intCast(text[i] - '0'));
    }
    if (negative) value_i = -value_i;
    return @as(c.guint16, @bitCast(@as(i16, @truncate(value_i))));
}

export fn on_p2p_transport_changed(combo: [*c]c.GtkComboBox, user_data: c.gpointer) void {
    _ = user_data;
    if (c.gtk_combo_box_get_active(combo) == 0) {
        p2p_set_transport_mode(types.P2PTransportMode.P2P_TRANSPORT_DIRECT);
        logMsg(c.G_LOG_LEVEL_DEBUG, "P2P transport set to DIRECT mode", .{});
    } else {
        p2p_set_transport_mode(types.P2PTransportMode.P2P_TRANSPORT_TWEETAPUS);
        logMsg(c.G_LOG_LEVEL_DEBUG, "P2P transport set to TWEETAPUS mode", .{});
    }
}

export fn on_p2p_start_listener_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    const main_box = widgetFromData(user_data);
    const host_entry: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(obj(main_box), "listen_host_entry")));
    const port_entry: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(obj(main_box), "listen_port_entry")));
    const status_label: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(obj(main_box), "listener_status_label")));
    if (host_entry == null or port_entry == null or status_label == null) return;

    const host = c.gtk_entry_get_text(@ptrCast(host_entry));
    const port_str = c.gtk_entry_get_text(@ptrCast(port_entry));
    const port = parsePort(port_str);

    if (p2p_is_listener_running() != FALSE) {
        p2p_stop_listener();
        c.gtk_button_set_label(@ptrCast(widget), "Start Listener");
        c.gtk_label_set_text(@ptrCast(status_label), "Listener: Stopped");
    } else if (p2p_start_listener(host, port) != FALSE) {
        const addr = p2p_get_listen_address();
        const status = c.g_strdup_printf("Listener: %s", if (addr != null) addr else lit("running"));
        c.gtk_label_set_text(@ptrCast(status_label), status);
        c.gtk_button_set_label(@ptrCast(widget), "Stop Listener");
        c.g_free(status);
        c.g_free(addr);
        p2p_start_message_polling();
    } else {
        showMessage(c.GTK_MESSAGE_ERROR, "Failed to start P2P listener on %s:%d", .{ host, @as(c_int, port) });
    }
}

export fn on_p2p_connect_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    _ = widget;
    const main_box = widgetFromData(user_data);
    const host_entry: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(obj(main_box), "peer_host_entry")));
    const port_entry: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(obj(main_box), "peer_port_entry")));
    const user_entry: [*c]c.GtkWidget = @ptrCast(@alignCast(c.g_object_get_data(obj(main_box), "peer_user_entry")));
    if (host_entry == null or port_entry == null or user_entry == null) return;

    const host = c.gtk_entry_get_text(@ptrCast(host_entry));
    const port_str = c.gtk_entry_get_text(@ptrCast(port_entry));
    const username = c.gtk_entry_get_text(@ptrCast(user_entry));
    if (isEmpty(host) or isEmpty(username)) {
        showMessage(c.GTK_MESSAGE_WARNING, "Please enter host and username", .{});
        return;
    }

    var port = parsePort(port_str);
    if (port == 0) port = 9735;
    const sock = p2p_connect_to_peer(host, port, username);
    if (sock < 0) {
        showMessage(c.GTK_MESSAGE_ERROR, "Failed to connect to %s@%s:%d", .{ username, host, @as(c_int, port) });
    } else {
        p2p_refresh_contacts_list();
        showMessage(c.GTK_MESSAGE_INFO, "Connected to %s@%s:%d", .{ username, host, @as(c_int, port) });
    }
}

fn gridAttach(grid: [*c]c.GtkWidget, child: [*c]c.GtkWidget, left: c_int, top: c_int) void {
    c.gtk_grid_attach(@ptrCast(grid), child, left, top, 1, 1);
}

export fn on_p2p_add_contact_clicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) void {
    _ = widget;
    _ = user_data;

    const is_direct = p2p_get_transport_mode() == types.P2PTransportMode.P2P_TRANSPORT_DIRECT;
    const dialog = c.gtk_dialog_new_with_buttons(
        "Add Contact",
        null,
        c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        @as(c_int, c.GTK_RESPONSE_CANCEL),
        "_Add",
        @as(c_int, c.GTK_RESPONSE_ACCEPT),
        @as([*c]const c.gchar, null),
    );
    const content = c.gtk_dialog_get_content_area(@ptrCast(dialog));
    c.gtk_container_set_border_width(@ptrCast(content), 20);
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(grid), 10);
    c.gtk_grid_set_column_spacing(@ptrCast(grid), 10);

    const user_entry = c.gtk_entry_new();
    const fp_entry = c.gtk_entry_new();
    gridAttach(grid, c.gtk_label_new("Username:"), 0, 0);
    gridAttach(grid, user_entry, 1, 0);
    gridAttach(grid, c.gtk_label_new("Key Fingerprint:"), 0, 1);
    c.gtk_entry_set_placeholder_text(@ptrCast(fp_entry), "40 character hex fingerprint");
    gridAttach(grid, fp_entry, 1, 1);
    const info_box = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    const info_label = c.gtk_label_new("Public keys should be exchanged outside of this application");
    c.gtk_label_set_line_wrap(@ptrCast(info_label), TRUE);
    const small_attrs = c.pango_attr_list_new();
    c.pango_attr_list_insert(small_attrs, c.pango_attr_scale_new(0.85));
    c.gtk_label_set_attributes(@ptrCast(info_label), small_attrs);
    c.pango_attr_list_unref(small_attrs);
    c.gtk_box_pack_start(@ptrCast(info_box), info_label, FALSE, FALSE, 0);

    const info_event = c.gtk_event_box_new();
    const info_icon = c.gtk_image_new_from_icon_name("dialog-information-symbolic", c.GTK_ICON_SIZE_MENU);
    c.gtk_container_add(@ptrCast(info_event), info_icon);
    c.gtk_widget_set_has_tooltip(info_event, TRUE);
    const tooltip_text = c.g_strdup(
        "Why should keys be exchanged outside the app?\n\n" ++
            "For encrypted messaging to be secure, you need to verify\n" ++
            "that you are encrypting to the correct person's key.\n\n" ++
            "If an attacker can replace a public key with their own,\n" ++
            "they could intercept and read your messages.\n\n" ++
            "Best practices for key exchange:\n" ++
            "• Meet in person and verify the key fingerprint\n" ++
            "• Exchange via another trusted channel you already use\n" ++
            "• Verify fingerprints through a side channel (phone, etc.)\n" ++
            "• Never trust a key received through the same channel\n" ++
            "  you will use for encrypted messages",
    );
    c.gtk_widget_set_tooltip_text(info_event, tooltip_text);
    c.g_free(tooltip_text);
    c.gtk_box_pack_start(@ptrCast(info_box), info_event, FALSE, FALSE, 0);
    c.gtk_grid_attach(@ptrCast(grid), info_box, 0, 2, 2, 1);

    var host_entry: [*c]c.GtkWidget = null;
    var port_entry: [*c]c.GtkWidget = null;
    if (is_direct) {
        host_entry = c.gtk_entry_new();
        port_entry = c.gtk_entry_new();
        gridAttach(grid, c.gtk_label_new("Host:"), 0, 3);
        c.gtk_entry_set_placeholder_text(@ptrCast(host_entry), "IP or hostname");
        gridAttach(grid, host_entry, 1, 3);
        gridAttach(grid, c.gtk_label_new("Port:"), 0, 4);
        c.gtk_entry_set_text(@ptrCast(port_entry), "9735");
        gridAttach(grid, port_entry, 1, 4);
    }

    c.gtk_widget_show_all(grid);
    c.gtk_box_pack_start(@ptrCast(content), grid, TRUE, TRUE, 0);

    if (c.gtk_dialog_run(@ptrCast(dialog)) == c.GTK_RESPONSE_ACCEPT) {
        const username = c.gtk_entry_get_text(@ptrCast(user_entry));
        const fingerprint = c.gtk_entry_get_text(@ptrCast(fp_entry));
        if (!isEmpty(username) and !isEmpty(fingerprint) and g_p2p_session != null) {
            const contact: [*c]types.P2PContact = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.P2PContact))));
            contact.*.username = c.g_strdup(username);
            contact.*.public_key_fingerprint = c.g_strdup(fingerprint);
            contact.*.display_name = c.g_strdup(username);
            if (is_direct and host_entry != null and port_entry != null) {
                const host = c.gtk_entry_get_text(@ptrCast(host_entry));
                if (!isEmpty(host)) {
                    contact.*.direct_host = c.g_strdup(host);
                    contact.*.direct_port = parsePort(c.gtk_entry_get_text(@ptrCast(port_entry)));
                }
            }
            c.g_mutex_lock(&g_p2p_session.*.session_mutex);
            _ = c.g_hash_table_insert(g_p2p_session.*.contacts, c.g_strdup(username), contact);
            c.g_mutex_unlock(&g_p2p_session.*.session_mutex);
            p2p_refresh_contacts_list();
        }
    }

    c.gtk_widget_destroy(dialog);
}

export fn on_p2p_contact_clicked(widget: [*c]c.GtkWidget, event: c.gpointer, user_data: c.gpointer) c.gboolean {
    _ = event;
    _ = user_data;

    const username: [*c]const c.gchar = if (widget != null) @ptrCast(c.g_object_get_data(obj(widget), "p2p_contact_username")) else null;
    if (username == null) return FALSE;

    c.gtk_stack_set_visible_child_name(@ptrCast(g_stack), "dm_messages");
    c.gtk_widget_show(g_back_button);
    const title = c.g_strdup_printf("@%s (Encrypted)", username);
    c.gtk_label_set_text(@ptrCast(g_dm_title_label), title);
    c.g_free(title);
    c.g_object_set_data_full(@ptrCast(g_dm_messages_list), "p2p_recipient", c.g_strdup(username), c.g_free);

    const children = c.gtk_container_get_children(@ptrCast(g_dm_messages_list));
    var child = children;
    while (child != null) : (child = child.*.next) {
        c.gtk_widget_destroy(@ptrCast(@alignCast(child.*.data)));
    }
    c.g_list_free(children);

    if (g_p2p_session != null) {
        c.g_mutex_lock(&g_p2p_session.*.session_mutex);
        var conversation: [*c]c.GList = @ptrCast(@alignCast(c.g_hash_table_lookup(g_p2p_session.*.conversations, username)));
        while (conversation != null) : (conversation = conversation.*.next) {
            const msg: [*c]types.P2PMessage = @ptrCast(@alignCast(conversation.*.data));
            if (msg == null) continue;
            const msg_widget = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
            c.gtk_container_set_border_width(@ptrCast(msg_widget), 5);
            c.gtk_widget_set_halign(msg_widget, if (msg.*.is_outgoing != FALSE) c.GTK_ALIGN_END else c.GTK_ALIGN_START);
            const label = c.gtk_label_new(msg.*.plaintext_content);
            c.gtk_label_set_line_wrap(@ptrCast(label), TRUE);
            c.gtk_widget_set_size_request(label, 200, -1);
            c.gtk_box_pack_start(@ptrCast(msg_widget), label, FALSE, FALSE, 0);
            c.gtk_list_box_insert(@ptrCast(g_dm_messages_list), msg_widget, -1);
            c.gtk_widget_show_all(msg_widget);
        }
        c.g_mutex_unlock(&g_p2p_session.*.session_mutex);
    }

    return TRUE;
}
