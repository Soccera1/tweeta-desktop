const std = @import("std");

const c = @import("c.zig").c;
const types = @import("types.zig");

extern var g_globals_mutex: c.GMutex;
extern var g_interaction_cache: ?*c.GHashTable;
extern var g_auth_token: [*c]c.gchar;
extern var g_current_username: [*c]c.gchar;
extern var g_community_id: [*c]c.gchar;
extern var g_main_list_box: ?*c.GtkWidget;
extern var g_p2p_session: [*c]types.P2PSession;
extern fn create_window() [*c]c.GtkWidget;
extern fn load_session() void;
extern fn update_login_ui() void;
extern fn start_loading_tweets(list_box: [*c]c.GtkListBox) void;
extern fn p2p_free_session(session: [*c]types.P2PSession) void;
extern fn p2p_network_cleanup() void;
extern fn p2p_crypto_cleanup() void;

const css =
    ".unread-notification { background-color: rgba(0, 100, 255, 0.1); }" ++
    ".dim-label { opacity: 0.7; font-size: 0.9em; }" ++
    ".note-frame { border-radius: 5px; border: 1px solid #ccc; }" ++
    ".note-warning { background-color: rgba(255, 165, 0, 0.1); border-color: orange; }" ++
    ".note-danger { background-color: rgba(255, 0, 0, 0.1); border-color: red; }" ++
    ".note-info { background-color: rgba(0, 191, 255, 0.1); border-color: deepskyblue; }";

pub fn main() u8 {
    var argc: c_int = @intCast(std.os.argv.len);
    var argv: [*c][*:0]u8 = @ptrCast(std.os.argv.ptr);
    c.gtk_init(&argc, @ptrCast(&argv));
    _ = c.curl_global_init(c.CURL_GLOBAL_ALL);

    c.g_mutex_init(&g_globals_mutex);
    g_interaction_cache = c.g_hash_table_new_full(c.g_str_hash, c.g_str_equal, c.g_free, c.g_free);

    const provider = c.gtk_css_provider_new();
    _ = c.gtk_css_provider_load_from_data(provider, css, -1, null);
    c.gtk_style_context_add_provider_for_screen(
        c.gdk_screen_get_default(),
        @ptrCast(provider),
        c.GTK_STYLE_PROVIDER_PRIORITY_APPLICATION,
    );
    c.g_object_unref(provider);

    const window = create_window();

    load_session();
    update_login_ui();

    c.gtk_widget_show_all(window);
    start_loading_tweets(@ptrCast(g_main_list_box));

    c.gtk_main();

    c.curl_global_cleanup();
    if (g_interaction_cache != null) {
        c.g_hash_table_unref(g_interaction_cache);
    }
    c.g_free(g_auth_token);
    c.g_free(g_current_username);
    c.g_free(g_community_id);

    if (g_p2p_session != null) {
        p2p_free_session(g_p2p_session);
    }

    p2p_network_cleanup();
    p2p_crypto_cleanup();
    c.g_mutex_clear(&g_globals_mutex);

    return 0;
}
