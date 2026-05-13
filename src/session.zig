const std = @import("std");
const c = @import("c.zig").c;
const cstr = @import("cstr.zig");

const MAX_USERNAME_LEN = 50;
const null_gchar: [*c]const c.gchar = null;
const FALSE: c.gboolean = 0;
const TRUE: c.gboolean = 1;

extern var g_auth_token: [*c]c.gchar;
extern var g_current_username: [*c]c.gchar;
extern var g_is_admin: c.gboolean;
extern var g_is_impersonating: c.gboolean;
extern var g_impersonation_admin_token: [*c]c.gchar;
extern var g_impersonation_admin_username: [*c]c.gchar;
extern var g_impersonation_admin_is_admin: c.gboolean;
extern var g_globals_mutex: c.GMutex;

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn logMsg(level: c.GLogLevelFlags, comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), level, lit(fmt) } ++ args);
}

fn errnoValue() c_int {
    return std.c._errno().*;
}

export fn get_config_path() [*c]c.gchar {
    const config_dir = c.g_get_user_config_dir();
    const app_dir = c.g_build_filename(config_dir, "tweeta-desktop", null_gchar);
    if (app_dir == null) {
        return null;
    }

    if (c.g_mkdir_with_parents(app_dir, 0o700) == -1) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to create config directory: %s (errno: %d)", .{ app_dir, errnoValue() });
    }

    const config_path = c.g_build_filename(app_dir, "session.json", null_gchar);
    c.g_free(app_dir);
    return config_path;
}

export fn save_session(token: [*c]const c.gchar, username: [*c]const c.gchar, is_admin: c.gboolean) void {
    var impersonation_token: [*c]c.gchar = null;
    var impersonation_username: [*c]c.gchar = null;
    var is_impersonating: c.gboolean = FALSE;
    var impersonation_is_admin: c.gboolean = FALSE;

    c.g_mutex_lock(&g_globals_mutex);
    is_impersonating = g_is_impersonating;
    impersonation_is_admin = g_impersonation_admin_is_admin;
    if (g_impersonation_admin_token != null) {
        impersonation_token = c.g_strdup(g_impersonation_admin_token);
    }
    if (g_impersonation_admin_username != null) {
        impersonation_username = c.g_strdup(g_impersonation_admin_username);
    }
    c.g_mutex_unlock(&g_globals_mutex);

    const builder = c.json_builder_new();
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "token");
    _ = c.json_builder_add_string_value(builder, token);
    _ = c.json_builder_set_member_name(builder, "username");
    _ = c.json_builder_add_string_value(builder, username);
    _ = c.json_builder_set_member_name(builder, "is_admin");
    _ = c.json_builder_add_boolean_value(builder, is_admin);
    _ = c.json_builder_set_member_name(builder, "is_impersonating");
    _ = c.json_builder_add_boolean_value(builder, is_impersonating);
    if (impersonation_token != null and impersonation_username != null) {
        _ = c.json_builder_set_member_name(builder, "impersonation_admin_token");
        _ = c.json_builder_add_string_value(builder, impersonation_token);
        _ = c.json_builder_set_member_name(builder, "impersonation_admin_username");
        _ = c.json_builder_add_string_value(builder, impersonation_username);
        _ = c.json_builder_set_member_name(builder, "impersonation_admin_is_admin");
        _ = c.json_builder_add_boolean_value(builder, impersonation_is_admin);
    }
    _ = c.json_builder_end_object(builder);

    const gen = c.json_generator_new();
    c.json_generator_set_root(gen, c.json_builder_get_root(builder));
    const data = c.json_generator_to_data(gen, null);

    const path = get_config_path();
    var err: ?*c.GError = null;
    if (c.g_file_set_contents(path, data, -1, &err) == FALSE) {
        if (err) |e| {
            logMsg(c.G_LOG_LEVEL_WARNING, "Failed to save session: %s", .{e.*.message});
            c.g_error_free(e);
        }
    }

    if (c.g_chmod(path, 0o600) == -1) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to set permissions on session file: %s (errno: %d)", .{ path, errnoValue() });
    }

    c.g_free(path);
    c.g_free(data);
    c.g_free(impersonation_username);
    c.g_free(impersonation_token);
    c.g_object_unref(gen);
    c.g_object_unref(builder);
}

export fn clear_session() void {
    const path = get_config_path();
    _ = c.g_unlink(path);
    c.g_free(path);
}

export fn load_session() void {
    const path = get_config_path();
    var data: [*c]c.gchar = null;
    var err: ?*c.GError = null;

    if (c.g_file_get_contents(path, &data, null, &err) != FALSE) {
        const parser = c.json_parser_new();
        if (c.json_parser_load_from_data(parser, data, -1, null) != FALSE) {
            const root = c.json_parser_get_root(parser);
            const obj = c.json_node_get_object(root);

            if (c.json_object_has_member(obj, "token") != FALSE and c.json_object_has_member(obj, "username") != FALSE) {
                c.g_mutex_lock(&g_globals_mutex);
                c.g_free(g_auth_token);
                c.g_free(g_current_username);
                g_auth_token = c.g_strdup(c.json_object_get_string_member(obj, "token"));
                g_current_username = c.g_strdup(c.json_object_get_string_member(obj, "username"));
                logMsg(c.G_LOG_LEVEL_DEBUG, "load_session: loaded token=%s (len=%d), username=%s", .{ if (g_auth_token != null) g_auth_token else lit("(null)"), if (g_auth_token != null) @as(c_int, @intCast(cstr.len(g_auth_token))) else 0, g_current_username });

                g_is_admin = if (c.json_object_has_member(obj, "is_admin") != FALSE)
                    c.json_object_get_boolean_member(obj, "is_admin")
                else
                    FALSE;
                g_is_impersonating = @intFromBool(c.json_object_has_member(obj, "is_impersonating") != FALSE and c.json_object_get_boolean_member(obj, "is_impersonating") != FALSE);
                c.g_free(g_impersonation_admin_token);
                g_impersonation_admin_token = null;
                c.g_free(g_impersonation_admin_username);
                g_impersonation_admin_username = null;
                g_impersonation_admin_is_admin = FALSE;

                if (c.json_object_has_member(obj, "impersonation_admin_token") != FALSE and c.json_object_has_member(obj, "impersonation_admin_username") != FALSE) {
                    g_impersonation_admin_token = c.g_strdup(c.json_object_get_string_member(obj, "impersonation_admin_token"));
                    g_impersonation_admin_username = c.g_strdup(c.json_object_get_string_member(obj, "impersonation_admin_username"));
                    if (c.json_object_has_member(obj, "impersonation_admin_is_admin") != FALSE) {
                        g_impersonation_admin_is_admin = c.json_object_get_boolean_member(obj, "impersonation_admin_is_admin");
                    }
                }
                c.g_mutex_unlock(&g_globals_mutex);
            }
        }
        c.g_object_unref(parser);
        c.g_free(data);
    } else if (err) |e| {
        c.g_error_free(e);
    }
    c.g_free(path);
}

export fn is_valid_username(username: [*c]const c.gchar) c.gboolean {
    if (username == null) {
        return FALSE;
    }

    const len = cstr.len(username);
    if (len == 0 or len > MAX_USERNAME_LEN) {
        return FALSE;
    }

    var p = username;
    while (p[0] != 0) : (p += 1) {
        if (!c.g_ascii_isalnum(p[0]) and p[0] != '_' and p[0] != '-') {
            return FALSE;
        }
    }

    return TRUE;
}
