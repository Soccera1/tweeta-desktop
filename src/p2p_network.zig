const std = @import("std");
const c = @import("c.zig").c;
const cstr = @import("cstr.zig");
const constants = @import("constants.zig");
const types = @import("types.zig");

const FALSE: c.gboolean = 0;
const TRUE: c.gboolean = 1;

extern var g_p2p_session: [*c]types.P2PSession;
extern fn fetch_url(url: [*c]const c.gchar, chunk: [*c]types.MemoryStruct, post_data: [*c]const c.gchar, method: [*c]const c.gchar) c.gboolean;
extern fn p2p_encrypt_message(plaintext: [*c]const c.gchar, recipient_fingerprint: [*c]const c.gchar) [*c]c.gchar;
extern fn p2p_decrypt_message(encrypted_armor: [*c]const c.gchar, passphrase: [*c]const c.gchar) [*c]c.gchar;
extern fn p2p_refresh_messages_list(contact_username: [*c]const c.gchar) void;

var g_transport_config: types.P2PTransportConfig = .{
    .mode = .P2P_TRANSPORT_TWEETAPUS,
    .local_username = null,
    .local_key_fingerprint = null,
    .listen_host = null,
    .listen_port = 0,
    .relay_server_url = null,
};
var g_active_connections: ?*c.GHashTable = null;
var g_pending_messages: ?*c.GHashTable = null;
var g_listen_socket: c_int = -1;
var g_listen_port: c.guint16 = 0;
var g_listener_running: c.gboolean = FALSE;
var g_polling_active: c.gboolean = FALSE;
var g_listener_thread: [*c]c.GThread = null;
var g_poll_thread: [*c]c.GThread = null;
var g_network_mutex: c.GMutex = undefined;
var g_connections_mutex: c.GMutex = undefined;
var g_network_initialized: c.gboolean = FALSE;

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn logMsg(level: c.GLogLevelFlags, comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), level, lit(fmt) } ++ args);
}

fn apiBaseUrl() [*c]const c.gchar {
    return @ptrCast(constants.API_BASE_URL);
}

fn spanZ(value: [*c]const c.gchar) []const u8 {
    return if (value == null) "" else std.mem.span(value);
}

fn gFmt(comptime fmt: []const u8, args: anytype) [*c]c.gchar {
    const len = std.fmt.count(fmt, args);
    const out: [*c]c.gchar = @ptrCast(c.g_malloc(len + 1));
    if (out == null) return null;
    _ = std.fmt.bufPrint(out[0..len], fmt, args) catch unreachable;
    out[len] = 0;
    return out;
}

fn p2pFreeContactInfo(data: c.gpointer) callconv(.c) void {
    const info: [*c]types.P2PContactInfo = @ptrCast(@alignCast(data));
    if (info != null) {
        c.g_free(info.*.username);
        c.g_free(info.*.public_key_fingerprint);
        c.g_free(info.*.direct_host);
        c.g_free(info.*.last_seen);
        if (info.*.socket_fd >= 0) std.posix.close(info.*.socket_fd);
        c.g_free(info);
    }
}

fn refreshMessagesIdleCb(user_data: c.gpointer) callconv(.c) c.gboolean {
    const username: [*c]c.gchar = @ptrCast(user_data);
    if (username != null and g_p2p_session != null) {
        p2p_refresh_messages_list(username);
    }
    c.g_free(user_data);
    return FALSE;
}

fn setNonblocking(fd: c_int) c_int {
    const flags = std.posix.fcntl(fd, std.posix.F.GETFL, 0) catch return -1;
    var open_flags: std.posix.O = @bitCast(@as(u32, @intCast(flags)));
    open_flags.NONBLOCK = true;
    _ = std.posix.fcntl(fd, std.posix.F.SETFL, @as(u32, @bitCast(open_flags))) catch return -1;
    return 0;
}

fn generateNonce() c.guint64 {
    const ts = std.posix.clock_gettime(.MONOTONIC) catch return 0;
    return (@as(c.guint64, @intCast(ts.sec)) << 32) | (@as(c.guint64, @intCast(ts.nsec)) & 0xffffffff);
}

fn logPosixError(comptime label: [:0]const u8, err: anyerror) void {
    const name = @errorName(err);
    logMsg(c.G_LOG_LEVEL_WARNING, label ++ ": %.*s", .{ @as(c_int, @intCast(name.len)), name.ptr });
}

fn anyAddress(port: c.guint16) std.net.Address {
    return std.net.Address.initIp4(.{ 0, 0, 0, 0 }, port);
}

fn listenerAddress(host: [*c]const c.gchar, port: c.guint16) std.net.Address {
    if (host == null or host[0] == 0) return anyAddress(port);
    return std.net.Address.parseIp4(cstr.bytes(host), port) catch anyAddress(port);
}

fn sendCString(fd: c_int, value: [*c]const c.gchar) bool {
    if (value == null) return false;
    _ = std.posix.send(fd, cstr.bytes(value), 0) catch return false;
    return true;
}

export fn p2p_serialize_message(msg: [*c]types.P2PNetworkMessage) [*c]c.gchar {
    if (msg == null) return null;

    const obj = c.json_object_new();
    c.json_object_set_int_member(obj, "type", @intFromEnum(msg.*.type));
    c.json_object_set_string_member(obj, "sender_id", if (msg.*.sender_id != null) msg.*.sender_id else "");
    c.json_object_set_string_member(obj, "recipient_id", if (msg.*.recipient_id != null) msg.*.recipient_id else "");
    c.json_object_set_string_member(obj, "payload", if (msg.*.payload != null) msg.*.payload else "");
    c.json_object_set_string_member(obj, "timestamp", if (msg.*.timestamp != null) msg.*.timestamp else "");
    c.json_object_set_int_member(obj, "nonce", @intCast(msg.*.nonce));

    const root = c.json_node_new(c.JSON_NODE_OBJECT);
    c.json_node_set_object(root, obj);
    const json_str = c.json_to_string(root, FALSE);
    c.json_node_free(root);
    c.json_object_unref(obj);
    return json_str;
}

export fn p2p_deserialize_message(data: [*c]const c.gchar) [*c]types.P2PNetworkMessage {
    if (data == null) return null;
    const parser = c.json_parser_new();
    var parse_error: [*c]c.GError = null;
    if (c.json_parser_load_from_data(parser, data, -1, &parse_error) == FALSE) {
        if (parse_error != null) {
            logMsg(c.G_LOG_LEVEL_WARNING, "Failed to parse P2P message: %s", .{parse_error.*.message});
            c.g_error_free(parse_error);
        }
        c.g_object_unref(parser);
        return null;
    }

    const root = c.json_parser_get_root(parser);
    if (root == null or c.JSON_NODE_TYPE(root) != c.JSON_NODE_OBJECT) {
        c.g_object_unref(parser);
        return null;
    }

    const obj = c.json_node_get_object(root);
    const msg: [*c]types.P2PNetworkMessage = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.P2PNetworkMessage))));
    msg.*.type = @enumFromInt(c.json_object_get_int_member(obj, "type"));
    msg.*.sender_id = c.g_strdup(c.json_object_get_string_member(obj, "sender_id"));
    msg.*.recipient_id = c.g_strdup(c.json_object_get_string_member(obj, "recipient_id"));
    msg.*.payload = c.g_strdup(c.json_object_get_string_member(obj, "payload"));
    msg.*.timestamp = c.g_strdup(c.json_object_get_string_member(obj, "timestamp"));
    msg.*.nonce = @intCast(c.json_object_get_int_member(obj, "nonce"));
    c.g_object_unref(parser);
    return msg;
}

export fn p2p_encode_for_tweetapus(encrypted_armor: [*c]const c.gchar) [*c]c.gchar {
    if (encrypted_armor == null) return null;
    return c.g_base64_encode(@ptrCast(encrypted_armor), cstr.len(encrypted_armor));
}

export fn p2p_decode_from_tweetapus(encoded_data: [*c]const c.gchar) [*c]c.gchar {
    if (encoded_data == null) return null;
    var len: c.gsize = 0;
    const decoded = c.g_base64_decode(encoded_data, &len);
    if (decoded == null) return null;
    const result = c.g_strndup(@ptrCast(decoded), len);
    c.g_free(decoded);
    return result;
}

export fn p2p_free_network_message(msg: [*c]types.P2PNetworkMessage) void {
    if (msg == null) return;
    c.g_free(msg.*.sender_id);
    c.g_free(msg.*.recipient_id);
    c.g_free(msg.*.payload);
    c.g_free(msg.*.timestamp);
    c.g_free(msg);
}

export fn p2p_network_init(config: [*c]types.P2PTransportConfig) c.gboolean {
    if (g_network_initialized != FALSE) return TRUE;

    c.g_mutex_init(&g_network_mutex);
    c.g_mutex_init(&g_connections_mutex);

    if (config != null) {
        g_transport_config = config.*;
        if (config.*.local_username != null) g_transport_config.local_username = c.g_strdup(config.*.local_username);
        if (config.*.local_key_fingerprint != null) g_transport_config.local_key_fingerprint = c.g_strdup(config.*.local_key_fingerprint);
        if (config.*.listen_host != null) g_transport_config.listen_host = c.g_strdup(config.*.listen_host);
        if (config.*.relay_server_url != null) g_transport_config.relay_server_url = c.g_strdup(config.*.relay_server_url);
    }

    g_active_connections = c.g_hash_table_new_full(c.g_str_hash, c.g_str_equal, c.g_free, p2pFreeContactInfo);
    g_pending_messages = c.g_hash_table_new_full(c.g_str_hash, c.g_str_equal, c.g_free, c.g_free);
    g_network_initialized = TRUE;
    return TRUE;
}

export fn p2p_network_cleanup() void {
    if (g_network_initialized == FALSE) return;
    p2p_stop_listener();
    p2p_stop_message_polling();

    c.g_mutex_lock(&g_connections_mutex);
    if (g_active_connections != null) {
        c.g_hash_table_destroy(g_active_connections);
        g_active_connections = null;
    }
    c.g_mutex_unlock(&g_connections_mutex);

    if (g_pending_messages != null) {
        c.g_hash_table_destroy(g_pending_messages);
        g_pending_messages = null;
    }

    c.g_free(g_transport_config.local_username);
    c.g_free(g_transport_config.local_key_fingerprint);
    c.g_free(g_transport_config.listen_host);
    c.g_free(g_transport_config.relay_server_url);
    g_transport_config = std.mem.zeroes(types.P2PTransportConfig);

    c.g_mutex_clear(&g_network_mutex);
    c.g_mutex_clear(&g_connections_mutex);
    g_network_initialized = FALSE;
}

fn listenerThreadFunc(data: c.gpointer) callconv(.c) c.gpointer {
    _ = data;
    var client_addr: std.posix.sockaddr.storage = undefined;
    var client_len: std.posix.socklen_t = @sizeOf(std.posix.sockaddr.storage);

    while (g_listener_running != FALSE and g_listen_socket >= 0) {
        var read_fds = [_]std.posix.pollfd{.{ .fd = g_listen_socket, .events = std.posix.POLL.IN, .revents = 0 }};
        const ret = std.posix.poll(&read_fds, 1000) catch |err| {
            logPosixError("poll() error in P2P listener", err);
            continue;
        };

        if (ret == 0) continue;

        if ((read_fds[0].revents & std.posix.POLL.IN) != 0) {
            const client_fd = std.posix.accept(g_listen_socket, @ptrCast(&client_addr), &client_len, 0) catch |err| {
                logPosixError("accept() failed", err);
                continue;
            };

            _ = setNonblocking(client_fd);
            const accepted = std.net.Address.initPosix(@ptrCast(@alignCast(&client_addr)));
            const ip_bytes: *const [4]u8 = @ptrCast(&accepted.in.sa.addr);
            logMsg(c.G_LOG_LEVEL_DEBUG, "P2P: Incoming connection from %d.%d.%d.%d:%d", .{ ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3], accepted.getPort() });
            std.posix.close(client_fd);
        }
    }
    return null;
}

export fn p2p_start_listener(host: [*c]const c.gchar, port: c.guint16) c.gboolean {
    if (g_listen_socket >= 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "P2P listener already running", .{});
        return FALSE;
    }

    g_listen_socket = std.posix.socket(std.posix.AF.INET, std.posix.SOCK.STREAM, 0) catch |err| {
        logPosixError("Failed to create P2P socket", err);
        return FALSE;
    };

    var opt: c_int = 1;
    std.posix.setsockopt(g_listen_socket, std.posix.SOL.SOCKET, std.posix.SO.REUSEADDR, std.mem.asBytes(&opt)) catch |err| {
        logPosixError("setsockopt() failed", err);
        std.posix.close(g_listen_socket);
        g_listen_socket = -1;
        return FALSE;
    };

    var addr = listenerAddress(host, port);
    std.posix.bind(g_listen_socket, &addr.any, addr.getOsSockLen()) catch |err| {
        logPosixError("bind() failed", err);
        std.posix.close(g_listen_socket);
        g_listen_socket = -1;
        return FALSE;
    };
    std.posix.listen(g_listen_socket, 10) catch |err| {
        logPosixError("listen() failed", err);
        std.posix.close(g_listen_socket);
        g_listen_socket = -1;
        return FALSE;
    };

    var len: std.posix.socklen_t = addr.getOsSockLen();
    std.posix.getsockname(g_listen_socket, &addr.any, &len) catch {
        len = 0;
    };
    g_listen_port = if (len > 0) addr.getPort() else port;

    _ = setNonblocking(g_listen_socket);
    g_listener_running = TRUE;
    g_listener_thread = c.g_thread_new("p2p-listener", listenerThreadFunc, null);
    logMsg(c.G_LOG_LEVEL_DEBUG, "P2P listener started on %s:%d", .{ if (host != null) host else lit("0.0.0.0"), g_listen_port });
    return TRUE;
}

export fn p2p_stop_listener() void {
    g_listener_running = FALSE;
    if (g_listener_thread != null) {
        _ = c.g_thread_join(g_listener_thread);
        g_listener_thread = null;
    }
    if (g_listen_socket >= 0) {
        std.posix.close(g_listen_socket);
        g_listen_socket = -1;
    }
    g_listen_port = 0;
}

export fn p2p_connect_to_peer(host: [*c]const c.gchar, port: c.guint16, username: [*c]const c.gchar) c_int {
    if (host == null or username == null) return -1;
    const sock = std.posix.socket(std.posix.AF.INET, std.posix.SOCK.STREAM, 0) catch |err| {
        logPosixError("Failed to create socket", err);
        return -1;
    };

    var addr = std.net.Address.parseIp4(cstr.bytes(host), port) catch {
        logMsg(c.G_LOG_LEVEL_WARNING, "Invalid address: %s", .{host});
        std.posix.close(sock);
        return -1;
    };
    std.posix.connect(sock, &addr.any, addr.getOsSockLen()) catch |err| {
        const name = @errorName(err);
        logMsg(c.G_LOG_LEVEL_WARNING, "connect() failed to %s:%d: %.*s", .{ host, port, @as(c_int, @intCast(name.len)), name.ptr });
        std.posix.close(sock);
        return -1;
    };
    _ = setNonblocking(sock);

    const info: [*c]types.P2PContactInfo = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.P2PContactInfo))));
    info.*.username = c.g_strdup(username);
    info.*.direct_host = c.g_strdup(host);
    info.*.direct_port = port;
    info.*.socket_fd = sock;
    info.*.is_online = TRUE;
    info.*.preferred_mode = types.P2PTransportMode.P2P_TRANSPORT_DIRECT;

    c.g_mutex_lock(&g_connections_mutex);
    _ = c.g_hash_table_insert(g_active_connections, c.g_strdup(username), info);
    c.g_mutex_unlock(&g_connections_mutex);

    var hello: types.P2PNetworkMessage = std.mem.zeroes(types.P2PNetworkMessage);
    hello.type = types.P2PMessageType.P2P_MSG_HELLO;
    hello.sender_id = c.g_strdup(g_transport_config.local_username);
    hello.recipient_id = c.g_strdup(username);
    hello.timestamp = c.g_strdup("");
    hello.nonce = generateNonce();
    const hello_json = p2p_serialize_message(&hello);
    _ = sendCString(sock, hello_json);
    _ = std.posix.send(sock, "\n", 0) catch 0;
    c.g_free(hello_json);
    c.g_free(hello.sender_id);
    c.g_free(hello.recipient_id);
    c.g_free(hello.timestamp);
    logMsg(c.G_LOG_LEVEL_DEBUG, "P2P: Connected to %s at %s:%d", .{ username, host, port });
    return sock;
}

fn sendDirectMessage(recipient: [*c]const c.gchar, msg: [*c]types.P2PNetworkMessage) c.gboolean {
    c.g_mutex_lock(&g_connections_mutex);
    const info: [*c]types.P2PContactInfo = @ptrCast(@alignCast(c.g_hash_table_lookup(g_active_connections, recipient)));
    c.g_mutex_unlock(&g_connections_mutex);
    if (info == null or info.*.socket_fd < 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "No active connection to %s", .{recipient});
        return FALSE;
    }
    const json = p2p_serialize_message(msg);
    if (json == null) return FALSE;
    const sent = std.posix.send(info.*.socket_fd, cstr.bytes(json), 0) catch 0;
    _ = std.posix.send(info.*.socket_fd, "\n", 0) catch 0;
    c.g_free(json);
    return if (sent > 0) TRUE else FALSE;
}

fn sendTweetapusMessage(recipient: [*c]const c.gchar, msg: [*c]types.P2PNetworkMessage) c.gboolean {
    const json = p2p_serialize_message(msg);
    if (json == null) return FALSE;

    const encoded_payload = p2p_encode_for_tweetapus(msg.*.payload);
    c.g_free(json);

    if (encoded_payload == null) return FALSE;
    const base: [*c]const c.gchar = if (g_transport_config.relay_server_url != null) g_transport_config.relay_server_url else apiBaseUrl();
    const url = gFmt("{s}/api/v1/p2p/encrypted", .{spanZ(base)});

    const obj = c.json_object_new();
    c.json_object_set_string_member(obj, "recipient", recipient);
    c.json_object_set_string_member(obj, "sender_fingerprint", g_transport_config.local_key_fingerprint);
    c.json_object_set_string_member(obj, "encrypted_data", encoded_payload);
    c.json_object_set_int_member(obj, "nonce", @intCast(msg.*.nonce));
    const root = c.json_node_new(c.JSON_NODE_OBJECT);
    c.json_node_set_object(root, obj);
    const post_data = c.json_to_string(root, FALSE);
    c.json_node_free(root);
    c.json_object_unref(obj);
    c.g_free(encoded_payload);

    var chunk: types.MemoryStruct = .{ .memory = null, .size = 0 };
    const success = fetch_url(url, &chunk, post_data, "POST");
    c.g_free(url);
    c.g_free(post_data);
    c.g_free(chunk.memory);
    return success;
}

export fn p2p_broadcast_encrypted(
    encrypted_payload: [*c]const c.gchar,
    recipient_username: [*c]const c.gchar,
    sender_fingerprint: [*c]const c.gchar,
) c.gboolean {
    if (encrypted_payload == null or recipient_username == null) return FALSE;
    const encoded_payload = p2p_encode_for_tweetapus(encrypted_payload);
    if (encoded_payload == null) return FALSE;
    defer c.g_free(encoded_payload);

    const base: [*c]const c.gchar = if (g_transport_config.relay_server_url != null) g_transport_config.relay_server_url else apiBaseUrl();
    const url = gFmt("{s}/api/v1/p2p/encrypted", .{spanZ(base)});
    defer c.g_free(url);

    const obj = c.json_object_new();
    c.json_object_set_string_member(obj, "recipient", recipient_username);
    c.json_object_set_string_member(obj, "sender_fingerprint", if (sender_fingerprint != null) sender_fingerprint else g_transport_config.local_key_fingerprint);
    c.json_object_set_string_member(obj, "encrypted_data", encoded_payload);
    const root = c.json_node_new(c.JSON_NODE_OBJECT);
    c.json_node_set_object(root, obj);
    const post_data = c.json_to_string(root, FALSE);
    c.json_node_free(root);
    c.json_object_unref(obj);
    defer c.g_free(post_data);

    var chunk: types.MemoryStruct = .{ .memory = null, .size = 0 };
    const success = fetch_url(url, &chunk, post_data, "POST");
    c.g_free(chunk.memory);
    return success;
}

export fn p2p_send_message(
    recipient_username: [*c]const c.gchar,
    plaintext: [*c]const c.gchar,
    recipient_fingerprint: [*c]const c.gchar,
) c.gboolean {
    if (recipient_username == null or plaintext == null or recipient_fingerprint == null) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Invalid parameters for p2p_send_message", .{});
        return FALSE;
    }
    const encrypted = p2p_encrypt_message(plaintext, recipient_fingerprint);
    if (encrypted == null) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to encrypt message for %s", .{recipient_username});
        return FALSE;
    }

    var msg: types.P2PNetworkMessage = std.mem.zeroes(types.P2PNetworkMessage);
    msg.type = types.P2PMessageType.P2P_MSG_CHAT;
    msg.sender_id = c.g_strdup(g_transport_config.local_username);
    msg.recipient_id = c.g_strdup(recipient_username);
    msg.payload = encrypted;
    msg.timestamp = c.g_strdup("");
    msg.nonce = generateNonce();

    c.g_mutex_lock(&g_connections_mutex);
    const info: [*c]types.P2PContactInfo = @ptrCast(@alignCast(c.g_hash_table_lookup(g_active_connections, recipient_username)));
    const mode = if (info != null) info.*.preferred_mode else g_transport_config.mode;
    c.g_mutex_unlock(&g_connections_mutex);

    const sent = if (mode == types.P2PTransportMode.P2P_TRANSPORT_DIRECT and info != null and info.*.socket_fd >= 0)
        sendDirectMessage(recipient_username, &msg)
    else
        sendTweetapusMessage(recipient_username, &msg);

    if (g_p2p_session != null) {
        const local_msg: [*c]types.P2PMessage = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.P2PMessage))));
        local_msg.*.id = c.g_strdup_printf("p2p_%lu", @as(c_ulong, @intCast(msg.nonce)));
        local_msg.*.sender_username = c.g_strdup(g_transport_config.local_username);
        local_msg.*.recipient_username = c.g_strdup(recipient_username);
        local_msg.*.plaintext_content = c.g_strdup(plaintext);
        local_msg.*.encrypted_content = c.g_strdup(encrypted);
        local_msg.*.timestamp = c.g_strdup("");
        local_msg.*.is_outgoing = TRUE;
        local_msg.*.is_verified = TRUE;
        c.g_mutex_lock(&g_p2p_session.*.session_mutex);
        var conversation = c.g_hash_table_lookup(g_p2p_session.*.conversations, recipient_username);
        conversation = c.g_list_append(@ptrCast(@alignCast(conversation)), local_msg);
        _ = c.g_hash_table_insert(g_p2p_session.*.conversations, c.g_strdup(recipient_username), conversation);
        c.g_mutex_unlock(&g_p2p_session.*.session_mutex);
    }

    c.g_free(msg.sender_id);
    c.g_free(msg.recipient_id);
    c.g_free(msg.timestamp);
    return sent;
}

fn pollThreadFunc(data: c.gpointer) callconv(.c) c.gpointer {
    _ = data;
    var last_poll_time = c.g_strdup("1970-01-01T00:00:00Z");
    while (g_polling_active != FALSE) {
        if (g_transport_config.mode == types.P2PTransportMode.P2P_TRANSPORT_TWEETAPUS or g_transport_config.relay_server_url != null) {
            const messages = p2p_poll_tweetapus_messages(last_poll_time);
            var l = messages;
            while (l != null) : (l = l.*.next) {
                const msg: [*c]types.P2PNetworkMessage = @ptrCast(@alignCast(l.*.data));
                if (msg != null) {
                    _ = p2p_process_received_message(msg);
                    p2p_free_network_message(msg);
                }
            }
            c.g_list_free(messages);
            c.g_free(last_poll_time);
            const dt = c.g_date_time_new_now_utc();
            last_poll_time = c.g_date_time_format_iso8601(dt);
            c.g_date_time_unref(dt);
        }
        var i: c_int = 0;
        while (i < 50 and g_polling_active != FALSE) : (i += 1) c.g_usleep(100_000);
    }
    c.g_free(last_poll_time);
    return null;
}

export fn p2p_start_message_polling() void {
    if (g_polling_active != FALSE) return;
    g_polling_active = TRUE;
    g_poll_thread = c.g_thread_new("p2p-poll", pollThreadFunc, null);
    logMsg(c.G_LOG_LEVEL_DEBUG, "P2P message polling started", .{});
}

export fn p2p_stop_message_polling() void {
    g_polling_active = FALSE;
    if (g_poll_thread != null) {
        _ = c.g_thread_join(g_poll_thread);
        g_poll_thread = null;
    }
}

export fn p2p_poll_tweetapus_messages(since_timestamp: [*c]const c.gchar) [*c]c.GList {
    var messages: [*c]c.GList = null;
    const base: [*c]const c.gchar = if (g_transport_config.relay_server_url != null) g_transport_config.relay_server_url else apiBaseUrl();
    const since = if (since_timestamp != null) since_timestamp else lit("1970-01-01T00:00:00Z");
    const url = gFmt("{s}/api/v1/p2p/encrypted/inbox?since={s}", .{ spanZ(base), spanZ(since) });
    var chunk: types.MemoryStruct = .{ .memory = null, .size = 0 };
    if (fetch_url(url, &chunk, null, "GET") != FALSE) {
        const parser = c.json_parser_new();
        var parse_error: [*c]c.GError = null;
        if (c.json_parser_load_from_data(parser, chunk.memory, -1, &parse_error) != FALSE) {
            const root = c.json_parser_get_root(parser);
            if (root != null and c.JSON_NODE_TYPE(root) == c.JSON_NODE_ARRAY) {
                const array = c.json_node_get_array(root);
                const len = c.json_array_get_length(array);
                var i: c.guint = 0;
                while (i < len) : (i += 1) {
                    const obj = c.json_array_get_object_element(array, i);
                    if (obj == null) continue;
                    const encoded_data = c.json_object_get_string_member(obj, "encrypted_data");
                    const sender_fp = c.json_object_get_string_member(obj, "sender_fingerprint");
                    const nonce = c.json_object_get_int_member(obj, "nonce");
                    if (encoded_data == null) continue;
                    const decoded = p2p_decode_from_tweetapus(encoded_data);
                    if (decoded == null) continue;
                    const msg: [*c]types.P2PNetworkMessage = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.P2PNetworkMessage))));
                    msg.*.type = types.P2PMessageType.P2P_MSG_CHAT;
                    msg.*.sender_id = c.g_strdup(sender_fp);
                    msg.*.recipient_id = c.g_strdup(g_transport_config.local_username);
                    msg.*.payload = decoded;
                    msg.*.nonce = @intCast(nonce);
                    messages = c.g_list_append(messages, msg);
                }
            }
        }
        if (parse_error != null) c.g_error_free(parse_error);
        c.g_object_unref(parser);
    }
    c.g_free(url);
    c.g_free(chunk.memory);
    return messages;
}

export fn p2p_process_received_message(net_msg: [*c]types.P2PNetworkMessage) c.gboolean {
    if (net_msg == null or net_msg.*.payload == null) return FALSE;
    const plaintext = p2p_decrypt_message(net_msg.*.payload, null);
    if (plaintext == null) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to decrypt message from %s", .{if (net_msg.*.sender_id != null) net_msg.*.sender_id else lit("unknown")});
        return FALSE;
    }

    if (g_p2p_session != null) {
        const msg: [*c]types.P2PMessage = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.P2PMessage))));
        msg.*.id = c.g_strdup_printf("p2p_recv_%lu", @as(c_ulong, @intCast(net_msg.*.nonce)));
        msg.*.sender_username = c.g_strdup(net_msg.*.sender_id);
        msg.*.recipient_username = c.g_strdup(g_transport_config.local_username);
        msg.*.plaintext_content = plaintext;
        msg.*.encrypted_content = c.g_strdup(net_msg.*.payload);
        msg.*.timestamp = c.g_strdup(net_msg.*.timestamp);
        msg.*.is_outgoing = FALSE;
        msg.*.is_verified = TRUE;
        c.g_mutex_lock(&g_p2p_session.*.session_mutex);
        var conversation = c.g_hash_table_lookup(g_p2p_session.*.conversations, net_msg.*.sender_id);
        conversation = c.g_list_append(@ptrCast(@alignCast(conversation)), msg);
        _ = c.g_hash_table_insert(g_p2p_session.*.conversations, c.g_strdup(net_msg.*.sender_id), conversation);
        c.g_mutex_unlock(&g_p2p_session.*.session_mutex);
        _ = c.g_idle_add(refreshMessagesIdleCb, c.g_strdup(net_msg.*.sender_id));
    }
    return TRUE;
}

export fn p2p_get_transport_mode() types.P2PTransportMode {
    return g_transport_config.mode;
}

export fn p2p_set_transport_mode(mode: types.P2PTransportMode) void {
    c.g_mutex_lock(&g_network_mutex);
    g_transport_config.mode = mode;
    c.g_mutex_unlock(&g_network_mutex);
    logMsg(c.G_LOG_LEVEL_DEBUG, "P2P transport mode changed to: %s", .{if (mode == types.P2PTransportMode.P2P_TRANSPORT_DIRECT) lit("DIRECT") else lit("TWEETAPUS")});
}

export fn p2p_is_listener_running() c.gboolean {
    return g_listener_running;
}

export fn p2p_get_listen_address() [*c]c.gchar {
    if (g_listener_running == FALSE or g_listen_port == 0) return null;
    const host = if (g_transport_config.listen_host != null) spanZ(g_transport_config.listen_host) else "127.0.0.1";
    return gFmt("{s}:{}", .{ host, g_listen_port });
}
