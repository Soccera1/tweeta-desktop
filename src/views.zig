const c = @import("c.zig").c;
const api = @import("api.zig");
const g = @import("globals_import.zig");
const types = @import("types.zig");

const TRUE: c.gboolean = 1;
const FALSE: c.gboolean = 0;

fn cb(function: anytype) c.GCallback {
    return @ptrCast(&function);
}

fn widgetPtr(value: anytype) [*c]c.GtkWidget {
    return @ptrCast(@alignCast(value));
}

fn container(widget: [*c]c.GtkWidget) [*c]c.GtkContainer {
    return @ptrCast(@alignCast(widget));
}

fn box(widget: [*c]c.GtkWidget) [*c]c.GtkBox {
    return @ptrCast(@alignCast(widget));
}

fn listBox(widget: [*c]c.GtkWidget) [*c]c.GtkListBox {
    return @ptrCast(@alignCast(widget));
}

fn stack(widget: [*c]c.GtkWidget) [*c]c.GtkStack {
    return @ptrCast(@alignCast(widget));
}

fn headerBar(widget: [*c]c.GtkWidget) [*c]c.GtkHeaderBar {
    return @ptrCast(@alignCast(widget));
}

fn entry(widget: [*c]c.GtkWidget) [*c]c.GtkEntry {
    return @ptrCast(@alignCast(widget));
}

fn combo(widget: [*c]c.GtkWidget) [*c]c.GtkComboBox {
    return @ptrCast(@alignCast(widget));
}

fn switchWidget(widget: [*c]c.GtkWidget) [*c]c.GtkSwitch {
    return @ptrCast(@alignCast(widget));
}

fn packStart(parent: [*c]c.GtkWidget, child: [*c]c.GtkWidget, expand: bool, fill: bool, padding: c.guint) void {
    if (parent == null or child == null or c.gtk_widget_get_parent(child) != null) return;
    c.gtk_box_pack_start(box(parent), child, if (expand) TRUE else FALSE, if (fill) TRUE else FALSE, padding);
}

fn packEnd(parent: [*c]c.GtkWidget, child: [*c]c.GtkWidget, expand: bool, fill: bool, padding: c.guint) void {
    if (parent == null or child == null or c.gtk_widget_get_parent(child) != null) return;
    c.gtk_box_pack_end(box(parent), child, if (expand) TRUE else FALSE, if (fill) TRUE else FALSE, padding);
}

fn addClass(widget: [*c]c.GtkWidget, class_name: [*c]const c.gchar) void {
    const ctx = c.gtk_widget_get_style_context(widget);
    c.gtk_style_context_add_class(ctx, class_name);
}

fn setBoldLabel(label_widget: [*c]c.GtkWidget) void {
    const attrs = c.pango_attr_list_new();
    c.pango_attr_list_insert(attrs, c.pango_attr_weight_new(c.PANGO_WEIGHT_BOLD));
    c.gtk_label_set_attributes(@ptrCast(@alignCast(label_widget)), attrs);
    c.pango_attr_list_unref(attrs);
}

fn setTitleLabelAttrs(label_widget: [*c]c.GtkWidget) void {
    const attrs = c.pango_attr_list_new();
    c.pango_attr_list_insert(attrs, c.pango_attr_weight_new(c.PANGO_WEIGHT_BOLD));
    c.pango_attr_list_insert(attrs, c.pango_attr_scale_new(1.5));
    c.gtk_label_set_attributes(@ptrCast(@alignCast(label_widget)), attrs);
    c.pango_attr_list_unref(attrs);
}

fn connect(widget: [*c]c.GtkWidget, signal: [*c]const c.gchar, function: anytype, data: c.gpointer) void {
    _ = c.g_signal_connect_data(widget, signal, cb(function), data, null, c.G_CONNECT_DEFAULT);
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
    const entry_: [*c]c.GtkEntry = @ptrCast(@alignCast(user_data));
    const hex = colorButtonHexValue(@ptrCast(@alignCast(button)));
    defer c.g_free(hex);
    c.gtk_entry_set_text(entry_, hex);
}

fn onColorEntryChanged(entry_: [*c]c.GtkEntry, user_data: c.gpointer) callconv(.c) void {
    const chooser: ?*c.GtkColorChooser = @ptrCast(@alignCast(user_data));
    var rgba: c.GdkRGBA = undefined;
    const text = c.gtk_entry_get_text(entry_);
    if (text != null and c.gdk_rgba_parse(&rgba, text) != FALSE) {
        c.gtk_color_chooser_set_rgba(chooser, &rgba);
    }
}

fn createColorEntryRow(entry_widget: [*c]c.GtkWidget, initial_value: [*c]const c.gchar) [*c]c.GtkWidget {
    const row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    const button = c.gtk_color_button_new();
    var rgba: c.GdkRGBA = undefined;
    packStart(row, entry_widget, true, true, 0);
    packStart(row, button, false, false, 0);
    if (initial_value != null and c.gdk_rgba_parse(&rgba, initial_value) != FALSE) {
        c.gtk_color_chooser_set_rgba(@ptrCast(@alignCast(button)), &rgba);
    }
    connect(button, "color-set", onColorButtonSet, entry_widget);
    connect(entry_widget, "changed", onColorEntryChanged, button);
    return row;
}

fn makeButton(label: [*c]const c.gchar, function: anytype, data: c.gpointer) [*c]c.GtkWidget {
    const button = c.gtk_button_new_with_label(label);
    connect(button, "clicked", function, data);
    return button;
}

fn makeIconButton(icon: [*c]const c.gchar, function: anytype, data: c.gpointer) [*c]c.GtkWidget {
    const button = c.gtk_button_new_from_icon_name(icon, c.GTK_ICON_SIZE_BUTTON);
    connect(button, "clicked", function, data);
    return button;
}

fn title(text: [*c]const c.gchar) [*c]c.GtkWidget {
    const label = c.gtk_label_new(text);
    setTitleLabelAttrs(label);
    c.gtk_widget_set_halign(label, c.GTK_ALIGN_START);
    return label;
}

fn hideUntilShown(widget: [*c]c.GtkWidget) void {
    if (widget == null) return;
    c.gtk_widget_set_no_show_all(widget, TRUE);
    c.gtk_widget_hide(widget);
}

fn leftAlignLabel(label_: [*c]c.GtkWidget) void {
    if (label_ == null) return;
    c.gtk_widget_set_halign(label_, c.GTK_ALIGN_START);
}

fn dimLabel(label_: [*c]c.GtkWidget) void {
    leftAlignLabel(label_);
    c.gtk_widget_set_opacity(label_, 0.75);
}

fn makeListView(out_list: *[*c]c.GtkWidget) [*c]c.GtkWidget {
    const scrolled = c.gtk_scrolled_window_new(null, null);
    const list = c.gtk_list_box_new();
    c.gtk_list_box_set_selection_mode(listBox(list), c.GTK_SELECTION_NONE);
    c.gtk_container_add(container(scrolled), list);
    out_list.* = list;
    return scrolled;
}

fn makeFeedListView(out_list: *[*c]c.GtkWidget, feed_type: [*c]const c.gchar) [*c]c.GtkWidget {
    const scrolled = makeListView(out_list);
    if (out_list.* != null and feed_type != null) {
        c.g_object_set_data(@ptrCast(@alignCast(out_list.*)), "feed_type", @constCast(feed_type));
    }
    connect(scrolled, "edge-reached", api.on_scroll_edge_reached, null);
    return scrolled;
}

fn makeFeedListViewNoEdge(out_list: *[*c]c.GtkWidget, feed_type: [*c]const c.gchar) [*c]c.GtkWidget {
    const scrolled = makeListView(out_list);
    if (out_list.* != null and feed_type != null) {
        c.g_object_set_data(@ptrCast(@alignCast(out_list.*)), "feed_type", @constCast(feed_type));
    }
    return scrolled;
}

fn makeScrollListView(out_list: *[*c]c.GtkWidget) [*c]c.GtkWidget {
    const scrolled = makeListView(out_list);
    connect(scrolled, "edge-reached", api.on_scroll_edge_reached, null);
    return scrolled;
}

fn makeSection(name: [*c]const c.gchar) [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 10);
    c.gtk_container_set_border_width(container(outer), 12);
    packStart(outer, title(name), false, false, 0);
    return outer;
}

fn framedSettingsSection(label_text: [*c]const c.gchar, child: [*c]c.GtkWidget) [*c]c.GtkWidget {
    const frame = c.gtk_frame_new(label_text);
    c.gtk_container_add(container(frame), child);
    return frame;
}

fn settingsSectionBox() [*c]c.GtkWidget {
    const section = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 10);
    c.gtk_container_set_border_width(container(section), 10);
    return section;
}

fn settingsLabel(text_: [*c]const c.gchar) [*c]c.GtkWidget {
    const label_ = c.gtk_label_new(text_);
    leftAlignLabel(label_);
    return label_;
}

fn settingsSwitchRow(text_: [*c]const c.gchar, switch_: [*c]c.GtkWidget) [*c]c.GtkWidget {
    const row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    packStart(row, c.gtk_label_new(text_), true, true, 0);
    packEnd(row, switch_, false, false, 0);
    return row;
}

fn sizedSettingsList(out_list: *[*c]c.GtkWidget, height: c.gint) [*c]c.GtkWidget {
    const scrolled = makeListView(out_list);
    c.gtk_widget_set_size_request(scrolled, -1, height);
    return scrolled;
}

fn appendNotebookPage(notebook: [*c]c.GtkWidget, child: [*c]c.GtkWidget, label_text: [*c]const c.gchar) void {
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), child, c.gtk_label_new(label_text));
}

fn addStackPage(name: [*c]const c.gchar, page: [*c]c.GtkWidget) void {
    c.gtk_stack_add_named(stack(g.g_stack), page, name);
}

fn showAuthRequired(widget: [*c]c.GtkWidget, message: [*c]const c.gchar) void {
    const toplevel = c.gtk_widget_get_toplevel(widget);
    const window: [*c]c.GtkWindow = if (toplevel != null) @ptrCast(@alignCast(toplevel)) else null;
    const dialog = c.gtk_message_dialog_new(window, c.GTK_DIALOG_DESTROY_WITH_PARENT, c.GTK_MESSAGE_ERROR, c.GTK_BUTTONS_CLOSE, message);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}

fn hasAuth() bool {
    return g.g_auth_token != null;
}

fn showStack(name: [*c]const c.gchar) void {
    c.gtk_stack_set_visible_child_name(stack(g.g_stack), name);
    c.gtk_widget_show(g.g_back_button);
}

fn onBookmarksClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    if (!hasAuth()) return showAuthRequired(widget, "You must be logged in to view bookmarks.");
    showStack("bookmarks");
    api.start_loading_bookmarks(@ptrCast(@alignCast(g.g_bookmarks_list)));
}

fn onCommunitiesClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    if (!hasAuth()) return showAuthRequired(widget, "You must be logged in to view communities.");
    showStack("communities");
    api.start_loading_communities(@ptrCast(@alignCast(g.g_communities_list)));
}

fn onTimelineToggleClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    if (api.get_current_timeline_type() == types.TimelineType.TIMELINE_PUBLIC) {
        api.set_timeline_type(types.TimelineType.TIMELINE_FOLLOWING);
        c.gtk_button_set_label(@ptrCast(@alignCast(widget)), "Following");
    } else {
        api.set_timeline_type(types.TimelineType.TIMELINE_PUBLIC);
        c.gtk_button_set_label(@ptrCast(@alignCast(widget)), "Public");
    }
    api.start_loading_timeline(@ptrCast(@alignCast(g.g_main_list_box)));
}

fn onListsRefreshClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    api.start_loading_lists();
}

fn onFiltersRefreshClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    api.start_loading_muted_words();
    api.start_loading_muted_conversations();
    api.start_loading_for_you_interests();
    api.start_loading_scheduled_posts();
    api.start_loading_my_shop();
    api.start_loading_delegates();
    api.start_loading_account_requests();
}

fn onArticlesRefreshClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    api.start_loading_articles();
}

fn onExploreRefreshClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    api.start_loading_explore();
}

fn onCommunitiesSearchActivated(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    api.start_loading_communities_search(@ptrCast(@alignCast(g.g_communities_list)), c.gtk_entry_get_text(entry(widget)));
}

fn onCommunitiesAllClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    if (g.g_communities_search_entry != null) c.gtk_entry_set_text(entry(g.g_communities_search_entry), "");
    api.start_loading_communities(@ptrCast(@alignCast(g.g_communities_list)));
}

fn onCommunitiesTrendingClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    api.start_loading_communities_trending(@ptrCast(@alignCast(g.g_communities_list)));
}

fn onCommunitiesRecommendedClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    api.start_loading_communities_recommended(@ptrCast(@alignCast(g.g_communities_list)));
}

fn onCommunitiesMineClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    api.start_loading_my_communities(@ptrCast(@alignCast(g.g_communities_list)));
}

fn activeCommunityData(key: [*c]const c.gchar) [*c]const c.gchar {
    if (g.g_community_tweets_list == null) return null;
    return @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(g.g_community_tweets_list)), key));
}

fn communityAccessValue(combo_: [*c]c.GtkWidget) [*c]const c.gchar {
    return if (c.gtk_combo_box_get_active(combo(combo_)) == 1) "locked" else "open";
}

fn communityAccessIndex(access_mode: [*c]const c.gchar) c_int {
    return if (c.g_strcmp0(access_mode, "locked") == 0) 1 else 0;
}

fn attachCommunityFormRow(grid: [*c]c.GtkWidget, row: c_int, label_text: [*c]const c.gchar, field: [*c]c.GtkWidget) void {
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), c.gtk_label_new(label_text), 0, row, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(grid)), field, 1, row, 1, 1);
}

fn onCreateCommunityClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const toplevel = c.gtk_widget_get_toplevel(widget);
    const window: [*c]c.GtkWindow = if (toplevel != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(toplevel)), c.gtk_window_get_type()) != FALSE) @ptrCast(@alignCast(toplevel)) else null;
    const dialog = c.gtk_dialog_new_with_buttons("Create Community", window, c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", @as(c_int, c.GTK_RESPONSE_CANCEL), "_Create", @as(c_int, c.GTK_RESPONSE_ACCEPT), @as([*c]const c.gchar, null));
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(container(grid), 10);
    const name_entry = c.gtk_entry_new();
    const description_entry = c.gtk_entry_new();
    const rules_entry = c.gtk_entry_new();
    const access_combo = c.gtk_combo_box_text_new();
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(access_combo)), "Open");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(access_combo)), "Locked");
    c.gtk_combo_box_set_active(combo(access_combo), 0);
    attachCommunityFormRow(grid, 0, "Name:", name_entry);
    attachCommunityFormRow(grid, 1, "Description:", description_entry);
    attachCommunityFormRow(grid, 2, "Rules:", rules_entry);
    attachCommunityFormRow(grid, 3, "Access:", access_combo);
    packStart(widgetPtr(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)))), grid, true, true, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        if (api.perform_create_community(
            c.gtk_entry_get_text(entry(name_entry)),
            c.gtk_entry_get_text(entry(description_entry)),
            c.gtk_entry_get_text(entry(rules_entry)),
            communityAccessValue(access_combo),
        ) != FALSE) api.start_loading_communities(@ptrCast(@alignCast(g.g_communities_list)));
    }
    c.gtk_widget_destroy(dialog);
}

fn onEditCommunityClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const community_id = activeCommunityData("community_id");
    if (community_id == null) return;
    const toplevel = c.gtk_widget_get_toplevel(widget);
    const window: [*c]c.GtkWindow = if (toplevel != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(toplevel)), c.gtk_window_get_type()) != FALSE) @ptrCast(@alignCast(toplevel)) else null;
    const dialog = c.gtk_dialog_new_with_buttons("Edit Community", window, c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", @as(c_int, c.GTK_RESPONSE_CANCEL), "_Save", @as(c_int, c.GTK_RESPONSE_ACCEPT), @as([*c]const c.gchar, null));
    const grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 8);
    c.gtk_container_set_border_width(container(grid), 10);
    const name_entry = c.gtk_entry_new();
    const description_entry = c.gtk_entry_new();
    const rules_entry = c.gtk_entry_new();
    const access_combo = c.gtk_combo_box_text_new();
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(access_combo)), "Open");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(access_combo)), "Locked");
    c.gtk_entry_set_text(entry(name_entry), if (activeCommunityData("community_name") != null) activeCommunityData("community_name") else "");
    c.gtk_entry_set_text(entry(description_entry), if (activeCommunityData("community_description") != null) activeCommunityData("community_description") else "");
    c.gtk_entry_set_text(entry(rules_entry), if (activeCommunityData("community_rules") != null) activeCommunityData("community_rules") else "");
    c.gtk_combo_box_set_active(combo(access_combo), communityAccessIndex(activeCommunityData("community_access_mode")));
    attachCommunityFormRow(grid, 0, "Name:", name_entry);
    attachCommunityFormRow(grid, 1, "Description:", description_entry);
    attachCommunityFormRow(grid, 2, "Rules:", rules_entry);
    attachCommunityFormRow(grid, 3, "Access:", access_combo);
    packStart(widgetPtr(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)))), grid, true, true, 0);
    c.gtk_widget_show_all(dialog);
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_ACCEPT) {
        if (api.perform_update_community(
            community_id,
            c.gtk_entry_get_text(entry(name_entry)),
            c.gtk_entry_get_text(entry(description_entry)),
            c.gtk_entry_get_text(entry(rules_entry)),
            communityAccessValue(access_combo),
        ) != FALSE) {
            api.start_loading_community_tweets(@ptrCast(@alignCast(g.g_community_tweets_list)), community_id);
            api.start_loading_communities(@ptrCast(@alignCast(g.g_communities_list)));
        }
    }
    c.gtk_widget_destroy(dialog);
}

fn onDeleteCommunityClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const community_id = activeCommunityData("community_id");
    if (community_id == null) return;
    const toplevel = c.gtk_widget_get_toplevel(widget);
    const window: [*c]c.GtkWindow = if (toplevel != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(toplevel)), c.gtk_window_get_type()) != FALSE) @ptrCast(@alignCast(toplevel)) else null;
    const dialog = c.gtk_message_dialog_new(window, c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT, c.GTK_MESSAGE_WARNING, c.GTK_BUTTONS_OK_CANCEL, "Delete this community?");
    if (c.gtk_dialog_run(@ptrCast(@alignCast(dialog))) == c.GTK_RESPONSE_OK and api.perform_delete_community(community_id) != FALSE) {
        c.gtk_stack_set_visible_child_name(stack(g.g_stack), "communities");
        api.start_loading_communities(@ptrCast(@alignCast(g.g_communities_list)));
    }
    c.gtk_widget_destroy(dialog);
}

fn onCommunityMembersClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const community_id = activeCommunityData("community_id");
    if (community_id == null) return;
    const toplevel = c.gtk_widget_get_toplevel(widget);
    const window: [*c]c.GtkWindow = if (toplevel != null and c.g_type_check_instance_is_a(@ptrCast(@alignCast(toplevel)), c.gtk_window_get_type()) != FALSE) @ptrCast(@alignCast(toplevel)) else null;
    const dialog = c.gtk_dialog_new_with_buttons("Community Members", window, c.GTK_DIALOG_MODAL | c.GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", @as(c_int, c.GTK_RESPONSE_CLOSE), @as([*c]const c.gchar, null));
    var members_list: [*c]c.GtkWidget = null;
    const scroll = makeListView(&members_list);
    c.gtk_widget_set_size_request(scroll, 420, 420);
    packStart(widgetPtr(c.gtk_dialog_get_content_area(@ptrCast(@alignCast(dialog)))), scroll, true, true, 0);
    api.start_loading_community_members(community_id, @ptrCast(@alignCast(members_list)));
    c.gtk_widget_show_all(dialog);
    _ = c.gtk_dialog_run(@ptrCast(@alignCast(dialog)));
    c.gtk_widget_destroy(dialog);
}

fn onCommunityAccessClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    const community_id = activeCommunityData("community_id");
    const access_mode = activeCommunityData("community_access_mode");
    if (community_id == null) return;
    const new_mode: [*c]const c.gchar = if (c.g_strcmp0(access_mode, "locked") == 0) "open" else "locked";
    if (api.perform_update_community_access_mode(community_id, new_mode) != FALSE) {
        api.start_loading_community_tweets(@ptrCast(@alignCast(g.g_community_tweets_list)), community_id);
        api.start_loading_communities(@ptrCast(@alignCast(g.g_communities_list)));
    }
}

fn onAdminUsersSearchActivated(entry_: [*c]c.GtkEntry, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    api.start_loading_admin_users(c.gtk_entry_get_text(entry_));
}

fn onAdminPostsSearchActivated(entry_: [*c]c.GtkEntry, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    api.start_loading_admin_posts(c.gtk_entry_get_text(entry_));
}

fn onAdminLogsSearchActivated(entry_: [*c]c.GtkEntry, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    api.start_loading_admin_logs(c.gtk_entry_get_text(entry_));
}

fn onAdminDmsSearchActivated(entry_: [*c]c.GtkEntry, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    api.start_loading_admin_dms(c.gtk_entry_get_text(entry_));
}

fn onAdminShopSearchActivated(entry_: [*c]c.GtkEntry, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    api.start_loading_admin_shop(c.gtk_entry_get_text(entry_));
}

fn widgetStringData(widget: [*c]c.GtkWidget, key: [*c]const c.gchar) [*c]const c.gchar {
    if (widget == null) return null;
    return @ptrCast(c.g_object_get_data(@ptrCast(@alignCast(widget)), key));
}

fn currentUsernameDup() [*c]c.gchar {
    c.g_mutex_lock(&g.g_globals_mutex);
    const username = if (g.g_current_username != null) c.g_strdup(g.g_current_username) else null;
    c.g_mutex_unlock(&g.g_globals_mutex);
    return username;
}

fn onProfileNotifyClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    if (g.g_active_profile == null) return;
    const username = widgetStringData(widget, "username");
    if (username == null) return;
    const new_state = if (g.g_active_profile.*.notify_tweets != FALSE) FALSE else TRUE;
    if (api.perform_profile_notify_tweets(username, new_state) != FALSE) {
        g.g_active_profile.*.notify_tweets = new_state;
        api.show_profile(username);
    }
}

fn onProfileBlockClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    if (g.g_active_profile == null) return;
    const user_id = widgetStringData(widget, "user_id");
    const username = widgetStringData(widget, "username");
    if (user_id == null or username == null) return;
    const new_state = if (g.g_active_profile.*.blocked_profile != FALSE) FALSE else TRUE;
    if (api.perform_block(user_id, new_state) != FALSE) {
        g.g_active_profile.*.blocked_profile = new_state;
        api.show_profile(username);
    }
}

fn onProfileMuteClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = user_data;
    const user_id = widgetStringData(widget, "user_id");
    const username = widgetStringData(widget, "username");
    const muted_state = @as(?[*c]c.gboolean, @ptrCast(@alignCast(c.g_object_get_data(@ptrCast(@alignCast(widget)), "muted_state"))));
    if (user_id == null or username == null or muted_state == null) return;
    const new_state = if (muted_state.?.* != FALSE) FALSE else TRUE;
    if (api.perform_mute(user_id, new_state) != FALSE) {
        muted_state.?.* = new_state;
        c.gtk_button_set_label(@ptrCast(@alignCast(widget)), if (new_state != FALSE) "Unmute" else "Mute");
        api.show_profile(username);
    }
}

fn onProfileDeleteAvatarClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    const username = currentUsernameDup();
    defer c.g_free(username);
    if (username != null and api.perform_delete_profile_avatar(username) != FALSE) api.show_profile(username);
}

fn onProfileDeleteBannerClicked(widget: [*c]c.GtkWidget, user_data: c.gpointer) callconv(.c) void {
    _ = widget;
    _ = user_data;
    const username = currentUsernameDup();
    defer c.g_free(username);
    if (username != null and api.perform_delete_profile_banner(username) != FALSE) api.show_profile(username);
}

export fn create_profile_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 10);
    c.gtk_container_set_border_width(container(outer), 10);
    g.g_profile_banner_image = c.gtk_image_new();
    c.gtk_widget_set_size_request(g.g_profile_banner_image, -1, 160);
    hideUntilShown(g.g_profile_banner_image);
    g.g_profile_avatar_image = c.gtk_image_new_from_icon_name("avatar-default", c.GTK_ICON_SIZE_DND);
    c.gtk_widget_set_size_request(g.g_profile_avatar_image, 80, 80);
    g.g_profile_name_label = c.gtk_label_new("");
    g.g_profile_username_label = c.gtk_label_new("");
    g.g_profile_bio_label = c.gtk_label_new("");
    g.g_profile_status_label = c.gtk_label_new("");
    g.g_profile_details_label = c.gtk_label_new("");
    g.g_profile_stats_label = c.gtk_label_new("");
    g.g_profile_badges_box = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 4);
    leftAlignLabel(g.g_profile_name_label);
    setTitleLabelAttrs(g.g_profile_name_label);
    dimLabel(g.g_profile_username_label);
    leftAlignLabel(g.g_profile_bio_label);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(g.g_profile_bio_label)), TRUE);
    leftAlignLabel(g.g_profile_status_label);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(g.g_profile_status_label)), TRUE);
    c.gtk_widget_set_opacity(g.g_profile_status_label, 0.8);
    leftAlignLabel(g.g_profile_details_label);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(g.g_profile_details_label)), TRUE);
    c.gtk_widget_set_opacity(g.g_profile_details_label, 0.8);
    leftAlignLabel(g.g_profile_stats_label);

    g.g_follow_button = makeButton("Follow", api.on_follow_button_clicked, null);
    g.g_profile_notify_button = makeButton("Alerts Off", onProfileNotifyClicked, null);
    g.g_profile_block_button = makeButton("Block", onProfileBlockClicked, null);
    g.g_profile_mute_button = makeButton("Mute", onProfileMuteClicked, null);
    g.g_profile_report_button = makeButton("Report", api.on_report_profile_clicked, null);
    g.g_profile_affiliate_button = makeButton("Request Affiliate", api.on_request_affiliate_clicked, null);
    g.g_profile_shop_button = makeButton("Shop", api.on_profile_shop_clicked, null);
    g.g_profile_donate_button = makeButton("Donate", api.on_profile_donate_clicked, null);
    g.g_profile_algorithm_button = makeButton("Algorithm", api.on_profile_algorithm_stats_clicked, null);
    g.g_profile_spam_score_button = makeButton("Spam Score", api.on_profile_spam_score_clicked, null);
    g.g_profile_analytics_button = makeButton("Analytics", api.on_profile_analytics_clicked, null);
    g.g_profile_common_followers_button = makeButton("Common", api.on_profile_common_followers_clicked, null);
    g.g_profile_top_posts_button = makeButton("Top Posts", api.on_profile_top_posts_clicked, null);
    g.g_profile_communities_button = makeButton("Communities", api.on_profile_communities_clicked, null);
    g.g_profile_delete_avatar_button = makeButton("Remove Avatar", onProfileDeleteAvatarClicked, null);
    g.g_profile_delete_banner_button = makeButton("Remove Banner", onProfileDeleteBannerClicked, null);
    g.g_profile_edit_button = makeButton("Edit Profile", api.on_edit_profile_clicked, null);

    for ([_][*c]c.GtkWidget{
        g.g_follow_button,
        g.g_profile_notify_button,
        g.g_profile_block_button,
        g.g_profile_mute_button,
        g.g_profile_report_button,
        g.g_profile_affiliate_button,
        g.g_profile_shop_button,
        g.g_profile_donate_button,
        g.g_profile_algorithm_button,
        g.g_profile_spam_score_button,
        g.g_profile_analytics_button,
        g.g_profile_common_followers_button,
        g.g_profile_top_posts_button,
        g.g_profile_communities_button,
        g.g_profile_delete_avatar_button,
        g.g_profile_delete_banner_button,
        g.g_profile_edit_button,
    }) |button| hideUntilShown(button);

    packStart(outer, g.g_profile_banner_image, false, false, 0);
    const hbox = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 15);
    const vbox = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const name_row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 8);
    packStart(name_row, g.g_profile_name_label, false, false, 0);
    packStart(name_row, g.g_profile_badges_box, false, false, 0);
    const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    for ([_][*c]c.GtkWidget{
        g.g_follow_button,
        g.g_profile_notify_button,
        g.g_profile_block_button,
        g.g_profile_mute_button,
        g.g_profile_report_button,
        g.g_profile_affiliate_button,
        g.g_profile_shop_button,
        g.g_profile_donate_button,
        g.g_profile_algorithm_button,
        g.g_profile_spam_score_button,
        g.g_profile_analytics_button,
        g.g_profile_common_followers_button,
        g.g_profile_top_posts_button,
        g.g_profile_communities_button,
        g.g_profile_edit_button,
        g.g_profile_delete_avatar_button,
        g.g_profile_delete_banner_button,
    }) |button| packStart(actions, button, false, false, 0);
    packStart(vbox, name_row, false, false, 0);
    packStart(vbox, g.g_profile_username_label, false, false, 0);
    packStart(vbox, g.g_profile_bio_label, false, false, 0);
    packStart(vbox, g.g_profile_status_label, false, false, 0);
    packStart(vbox, g.g_profile_details_label, false, false, 0);
    packStart(vbox, g.g_profile_stats_label, false, false, 0);
    packStart(vbox, actions, false, false, 5);
    packStart(hbox, g.g_profile_avatar_image, false, false, 0);
    packStart(hbox, vbox, true, true, 0);
    packStart(outer, hbox, false, false, 0);
    const notebook = c.gtk_notebook_new();
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeFeedListView(&g.g_profile_tweets_list, "profile_posts"), c.gtk_label_new("Tweets"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeFeedListView(&g.g_profile_replies_list, "profile_replies"), c.gtk_label_new("Replies"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeFeedListViewNoEdge(&g.g_profile_media_list, "profile_media"), c.gtk_label_new("Media"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_profile_highlights_list), c.gtk_label_new("Highlights"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_followers_list), c.gtk_label_new("Followers"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_following_list), c.gtk_label_new("Following"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_profile_mutuals_list), c.gtk_label_new("Mutuals"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_profile_followers_you_know_list), c.gtk_label_new("You Know"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_profile_affiliates_list), c.gtk_label_new("Affiliates"));
    packStart(outer, notebook, true, true, 0);
    return outer;
}

export fn create_search_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 10);
    const notebook = c.gtk_notebook_new();
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_search_users_list), c.gtk_label_new("Users"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_search_tweets_list), c.gtk_label_new("Tweets"));
    packStart(outer, notebook, true, true, 0);
    return outer;
}

export fn create_notifications_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const action_bar = c.gtk_action_bar_new();
    c.gtk_action_bar_pack_end(@ptrCast(@alignCast(action_bar)), makeButton("Mark all as read", api.on_mark_all_read_clicked, null));
    packStart(outer, action_bar, false, false, 0);
    packStart(outer, makeFeedListView(&g.g_notifications_list, "notifications"), true, true, 0);
    return outer;
}

export fn create_conversation_view() [*c]c.GtkWidget {
    g.g_conversation_list = null;
    return makeListView(&g.g_conversation_list);
}

export fn create_messages_view() [*c]c.GtkWidget {
    const notebook = c.gtk_notebook_new();
    c.gtk_notebook_set_tab_pos(@ptrCast(@alignCast(notebook)), c.GTK_POS_TOP);
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_conversations_list), c.gtk_label_new("Tweetapus Conversations"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), create_p2p_messages_view(), c.gtk_label_new("Encrypted"));
    return notebook;
}

export fn create_p2p_messages_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const action_bar = c.gtk_action_bar_new();
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), makeButton("Setup Keys", api.on_p2p_setup_clicked, null));
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), makeButton("Add Contact", api.on_p2p_add_contact_clicked, null));
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), makeButton("Import Key", api.on_p2p_import_contact_clicked, null));
    const transport_combo = c.gtk_combo_box_text_new();
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(transport_combo)), "Direct P2P");
    c.gtk_combo_box_text_append_text(@ptrCast(@alignCast(transport_combo)), "Relay Mode");
    c.gtk_combo_box_set_active(combo(transport_combo), 1);
    connect(transport_combo, "changed", api.on_p2p_transport_changed, null);
    c.gtk_action_bar_pack_end(@ptrCast(@alignCast(action_bar)), transport_combo);
    c.gtk_action_bar_pack_end(@ptrCast(@alignCast(action_bar)), c.gtk_label_new("Transport:"));
    packStart(outer, action_bar, false, false, 0);

    const header_box = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    c.gtk_container_set_border_width(container(header_box), 10);
    g.g_p2p_title_label = c.gtk_label_new("Select a contact to start encrypted messaging");
    setBoldLabel(g.g_p2p_title_label);
    leftAlignLabel(g.g_p2p_title_label);
    packStart(header_box, g.g_p2p_title_label, true, true, 0);
    packStart(outer, header_box, false, false, 0);
    packStart(outer, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false, false, 0);

    g.g_p2p_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_p2p_entry), "Type encrypted message...");
    connect(g.g_p2p_entry, "activate", api.on_p2p_send_clicked, null);

    const paned = c.gtk_paned_new(c.GTK_ORIENTATION_HORIZONTAL);
    const contacts_scroll = makeListView(&g.g_p2p_contacts_list);
    c.gtk_widget_set_size_request(contacts_scroll, 250, -1);
    c.gtk_list_box_set_selection_mode(listBox(g.g_p2p_contacts_list), c.GTK_SELECTION_SINGLE);
    connect(g.g_p2p_contacts_list, "row-selected", api.on_p2p_contact_row_selected, null);
    c.gtk_paned_pack1(@ptrCast(@alignCast(paned)), contacts_scroll, FALSE, TRUE);

    const messages_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    packStart(messages_box, makeListView(&g.g_p2p_messages_list), true, true, 0);
    const input_hbox = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    c.gtk_container_set_border_width(container(input_hbox), 5);
    packStart(input_hbox, g.g_p2p_entry, true, true, 0);
    packStart(input_hbox, makeButton("Send", api.on_p2p_send_clicked, null), false, false, 0);
    packStart(messages_box, input_hbox, false, false, 0);
    c.gtk_paned_pack2(@ptrCast(@alignCast(paned)), messages_box, TRUE, TRUE);
    packStart(outer, paned, true, true, 0);
    return outer;
}

export fn create_dm_messages_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const header_box = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    c.gtk_container_set_border_width(container(header_box), 10);
    const header_text_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 4);
    const header_actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 4);

    g.g_dm_title_label = c.gtk_label_new("Messages");
    const title_attrs = c.pango_attr_list_new();
    c.pango_attr_list_insert(title_attrs, c.pango_attr_weight_new(c.PANGO_WEIGHT_BOLD));
    c.gtk_label_set_attributes(@ptrCast(@alignCast(g.g_dm_title_label)), title_attrs);
    c.pango_attr_list_unref(title_attrs);
    g.g_dm_info_label = c.gtk_label_new("");
    leftAlignLabel(g.g_dm_title_label);
    leftAlignLabel(g.g_dm_info_label);
    c.gtk_widget_set_opacity(g.g_dm_info_label, 0.75);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(g.g_dm_info_label)), TRUE);
    packStart(header_text_box, g.g_dm_title_label, false, false, 0);
    packStart(header_text_box, g.g_dm_info_label, false, false, 0);
    packStart(header_box, header_text_box, true, true, 0);
    for ([_][*c]c.GtkWidget{
        makeButton("Rename", api.on_dm_title_clicked, null),
        makeButton("Add", api.on_dm_add_people_clicked, null),
        makeButton("Disappear", api.on_dm_disappearing_clicked, null),
        makeButton("Invite", api.on_dm_invite_clicked, null),
        makeButton("Join", api.on_dm_join_invite_clicked, null),
        makeButton("Permissions", api.on_dm_permissions_clicked, null),
        makeButton("Roles", api.on_dm_roles_clicked, null),
        makeButton("Pinned", api.on_dm_pinned_clicked, null),
        makeButton("Leave", api.on_dm_leave_clicked, null),
    }) |button| packStart(header_actions, button, false, false, 0);
    packEnd(header_box, header_actions, false, false, 0);
    packStart(outer, header_box, false, false, 0);
    packStart(outer, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false, false, 0);

    g.g_dm_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_dm_entry), "Type a message...");
    connect(g.g_dm_entry, "changed", api.on_dm_entry_changed, null);
    connect(g.g_dm_entry, "activate", api.on_dm_send_clicked, null);
    packStart(outer, makeListView(&g.g_dm_messages_list), true, true, 0);
    const status_label = c.gtk_label_new("");
    c.gtk_widget_set_halign(status_label, c.GTK_ALIGN_START);
    c.gtk_widget_set_opacity(status_label, 0.75);
    c.gtk_widget_set_no_show_all(status_label, TRUE);
    c.g_object_set_data(@ptrCast(@alignCast(g.g_dm_messages_list)), "composer_status_label", status_label);
    packStart(outer, status_label, false, false, 5);
    const input_box = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    c.gtk_container_set_border_width(container(input_box), 5);
    packStart(input_box, makeButton("Attach", api.on_dm_attach_clicked, null), false, false, 0);
    packStart(input_box, makeButton("Request", api.on_dm_request_payment_clicked, null), false, false, 0);
    packStart(input_box, g.g_dm_entry, true, true, 0);
    packStart(input_box, makeButton("Clear", api.on_dm_clear_context_clicked, null), false, false, 0);
    packStart(input_box, makeButton("Send", api.on_dm_send_clicked, null), false, false, 0);
    packStart(outer, input_box, false, false, 0);
    return outer;
}

export fn create_settings_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    c.gtk_container_set_border_width(container(outer), 20);
    packStart(outer, title("Settings"), false, false, 0);
    packStart(outer, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false, false, 10);

    const scroll = c.gtk_scrolled_window_new(null, null);
    c.gtk_scrolled_window_set_policy(@ptrCast(@alignCast(scroll)), c.GTK_POLICY_NEVER, c.GTK_POLICY_AUTOMATIC);
    const content = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 20);
    c.gtk_container_set_border_width(container(content), 10);

    const appearance_box = settingsSectionBox();
    packStart(appearance_box, settingsLabel("Theme:"), false, false, 0);
    g.g_theme_combo = c.gtk_combo_box_text_new();
    c.gtk_combo_box_text_append_text(@ptrCast(g.g_theme_combo), "Light");
    c.gtk_combo_box_text_append_text(@ptrCast(g.g_theme_combo), "Dark");
    c.gtk_combo_box_text_append_text(@ptrCast(g.g_theme_combo), "System Default");
    c.gtk_combo_box_set_active(combo(g.g_theme_combo), 2);
    connect(g.g_theme_combo, "changed", api.on_theme_changed, null);
    packStart(appearance_box, g.g_theme_combo, false, false, 0);
    g.g_compact_mode_switch = c.gtk_switch_new();
    connect(g.g_compact_mode_switch, "state-set", api.on_compact_mode_toggled, null);
    packStart(appearance_box, settingsSwitchRow("Compact mode", g.g_compact_mode_switch), false, false, 0);
    packStart(content, framedSettingsSection("Appearance", appearance_box), false, false, 0);

    const notifications_box = settingsSectionBox();
    g.g_enable_notifications_switch = c.gtk_switch_new();
    g.g_sound_notifications_switch = c.gtk_switch_new();
    g.g_dm_notifications_switch = c.gtk_switch_new();
    c.gtk_switch_set_active(switchWidget(g.g_enable_notifications_switch), TRUE);
    c.gtk_switch_set_active(switchWidget(g.g_sound_notifications_switch), TRUE);
    c.gtk_switch_set_active(switchWidget(g.g_dm_notifications_switch), TRUE);
    connect(g.g_enable_notifications_switch, "state-set", api.on_notifications_enabled_toggled, null);
    packStart(notifications_box, settingsSwitchRow("Enable notifications", g.g_enable_notifications_switch), false, false, 0);
    packStart(notifications_box, settingsSwitchRow("Sound effects", g.g_sound_notifications_switch), false, false, 0);
    packStart(notifications_box, settingsSwitchRow("Direct message notifications", g.g_dm_notifications_switch), false, false, 0);
    packStart(notifications_box, makeButton("Push Notifications", api.on_push_notifications_clicked, null), false, false, 0);
    packStart(content, framedSettingsSection("Notifications", notifications_box), false, false, 0);

    const filters_box = settingsSectionBox();
    const muted_word_row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    g.g_muted_word_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_muted_word_entry), "Muted word or phrase");
    connect(g.g_muted_word_entry, "activate", api.on_add_muted_word_clicked, null);
    packStart(muted_word_row, g.g_muted_word_entry, true, true, 0);
    packStart(muted_word_row, makeButton("Add", api.on_add_muted_word_clicked, null), false, false, 0);
    packStart(filters_box, muted_word_row, false, false, 0);
    packStart(filters_box, c.gtk_label_new("Muted words"), false, false, 0);
    packStart(filters_box, sizedSettingsList(&g.g_muted_words_list, 150), false, false, 0);
    packStart(filters_box, c.gtk_label_new("Muted conversations"), false, false, 0);
    packStart(filters_box, sizedSettingsList(&g.g_muted_conversations_list, 120), false, false, 0);
    packStart(filters_box, makeButton("Refresh Filters", onFiltersRefreshClicked, null), false, false, 0);
    packStart(content, framedSettingsSection("Content Filters", filters_box), false, false, 0);

    const interests_box = settingsSectionBox();
    const interests_actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    packStart(interests_actions, makeButton("Refresh Interests", onFiltersRefreshClicked, null), false, false, 0);
    packStart(interests_actions, makeButton("Reset Interests", api.on_clear_for_you_interests_clicked, null), false, false, 0);
    packStart(interests_box, interests_actions, false, false, 0);
    packStart(interests_box, sizedSettingsList(&g.g_for_you_interests_list, 160), false, false, 0);
    packStart(content, framedSettingsSection("For You Interests", interests_box), false, false, 0);

    const schedule_box = settingsSectionBox();
    const schedule_actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    packStart(schedule_actions, makeButton("Schedule Post", api.on_schedule_post_clicked, null), false, false, 0);
    packStart(schedule_actions, makeButton("Refresh", onFiltersRefreshClicked, null), false, false, 0);
    packStart(schedule_box, schedule_actions, false, false, 0);
    packStart(schedule_box, sizedSettingsList(&g.g_scheduled_posts_list, 180), false, false, 0);
    packStart(content, framedSettingsSection("Scheduled Posts", schedule_box), false, false, 0);

    const shop_box = settingsSectionBox();
    const shop_actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    packStart(shop_actions, makeButton("New Product", api.on_create_shop_product_clicked, null), false, false, 0);
    packStart(shop_actions, makeButton("Refresh Shop", onFiltersRefreshClicked, null), false, false, 0);
    packStart(shop_box, shop_actions, false, false, 0);
    packStart(shop_box, sizedSettingsList(&g.g_shop_products_list, 220), false, false, 0);
    packStart(content, framedSettingsSection("Shop", shop_box), false, false, 0);

    const delegates_box = settingsSectionBox();
    const delegate_invite_row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    g.g_delegate_username_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_delegate_username_entry), "Username to invite");
    connect(g.g_delegate_username_entry, "activate", api.on_invite_delegate_clicked, null);
    packStart(delegate_invite_row, g.g_delegate_username_entry, true, true, 0);
    packStart(delegate_invite_row, makeButton("Invite", api.on_invite_delegate_clicked, null), false, false, 0);
    packStart(delegates_box, delegate_invite_row, false, false, 0);
    const delegate_actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    packStart(delegate_actions, makeButton("Switch to Primary", api.on_switch_primary_clicked, null), false, false, 0);
    packStart(delegate_actions, makeButton("Add Account", api.on_add_account_clicked, null), false, false, 0);
    packStart(delegate_actions, makeButton("Validate Accounts", api.on_validate_accounts_clicked, null), false, false, 0);
    packStart(delegates_box, delegate_actions, false, false, 0);
    const delegates_notebook = c.gtk_notebook_new();
    c.gtk_widget_set_size_request(delegates_notebook, -1, 260);
    appendNotebookPage(delegates_notebook, sizedSettingsList(&g.g_delegates_list, -1), "Your Delegates");
    appendNotebookPage(delegates_notebook, sizedSettingsList(&g.g_delegations_list, -1), "You Delegate");
    appendNotebookPage(delegates_notebook, sizedSettingsList(&g.g_delegate_invitations_list, -1), "Invites");
    appendNotebookPage(delegates_notebook, sizedSettingsList(&g.g_delegate_sent_list, -1), "Sent");
    packStart(delegates_box, delegates_notebook, false, false, 0);
    packStart(delegates_box, makeButton("Refresh Delegates", onFiltersRefreshClicked, null), false, false, 0);
    packStart(content, framedSettingsSection("Delegates", delegates_box), false, false, 0);

    const data_box = settingsSectionBox();
    g.g_cache_size_label = c.gtk_label_new("Cache size: Calculating...");
    leftAlignLabel(g.g_cache_size_label);
    packStart(data_box, g.g_cache_size_label, false, false, 0);
    packStart(data_box, makeButton("Clear Cache", api.on_clear_cache_clicked, null), false, false, 0);
    packStart(data_box, makeButton("Clear Search History", api.on_clear_history_clicked, null), false, false, 0);
    packStart(content, framedSettingsSection("Data & Cache", data_box), false, false, 0);

    const account_box = settingsSectionBox();
    g.g_settings_username_label = c.gtk_label_new("Not logged in");
    leftAlignLabel(g.g_settings_username_label);
    packStart(account_box, g.g_settings_username_label, false, false, 0);
    const username_row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    g.g_settings_new_username_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_settings_new_username_entry), "New username");
    connect(g.g_settings_new_username_entry, "activate", api.on_change_username_clicked, null);
    packStart(username_row, g.g_settings_new_username_entry, true, true, 0);
    packStart(username_row, makeButton("Change Username", api.on_change_username_clicked, null), false, false, 0);
    packStart(account_box, username_row, false, false, 0);
    g.g_settings_private_switch = c.gtk_switch_new();
    connect(g.g_settings_private_switch, "state-set", api.on_account_private_toggled, null);
    packStart(account_box, settingsSwitchRow("Private account", g.g_settings_private_switch), false, false, 0);
    g.g_settings_transparency_switch = c.gtk_switch_new();
    connect(g.g_settings_transparency_switch, "state-set", api.on_transparency_location_toggled, null);
    packStart(account_box, settingsSwitchRow("Show continent in transparency info", g.g_settings_transparency_switch), false, false, 0);
    const community_tag_row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    g.g_settings_community_tag_entry = c.gtk_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_settings_community_tag_entry), "Community ID for profile tag");
    connect(g.g_settings_community_tag_entry, "activate", api.on_update_community_tag_clicked, null);
    packStart(community_tag_row, g.g_settings_community_tag_entry, true, true, 0);
    packStart(community_tag_row, makeButton("Set Tag", api.on_update_community_tag_clicked, null), false, false, 0);
    packStart(community_tag_row, makeButton("Clear", api.on_clear_community_tag_clicked, null), false, false, 0);
    packStart(account_box, community_tag_row, false, false, 0);
    const outline_grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(outline_grid)), 6);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(outline_grid)), 6);
    g.g_settings_checkmark_outline_entry = c.gtk_entry_new();
    g.g_settings_avatar_outline_entry = c.gtk_entry_new();
    const checkmark_outline_row = createColorEntryRow(g.g_settings_checkmark_outline_entry, null);
    const avatar_outline_row = createColorEntryRow(g.g_settings_avatar_outline_entry, null);
    c.gtk_entry_set_placeholder_text(entry(g.g_settings_checkmark_outline_entry), "Checkmark outline color");
    c.gtk_entry_set_placeholder_text(entry(g.g_settings_avatar_outline_entry), "Avatar outline color");
    c.gtk_grid_attach(@ptrCast(@alignCast(outline_grid)), c.gtk_label_new("Checkmark:"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(outline_grid)), checkmark_outline_row, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(outline_grid)), c.gtk_label_new("Avatar:"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(outline_grid)), avatar_outline_row, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(outline_grid)), makeButton("Update Outlines", api.on_update_outlines_clicked, null), 1, 2, 1, 1);
    packStart(account_box, outline_grid, false, false, 0);
    g.g_change_password_button = makeButton("Change Password", api.on_change_password_clicked, null);
    packStart(account_box, g.g_change_password_button, false, false, 0);
    packStart(account_box, makeButton("Manage Passkeys", api.on_manage_passkeys_clicked, null), false, false, 0);
    packStart(account_box, makeButton("Delete Account", api.on_delete_account_clicked, null), false, false, 0);
    packStart(account_box, makeButton("Bulk Delete Posts", api.on_bulk_delete_posts_clicked, null), false, false, 0);
    packStart(account_box, makeButton("Moderation History", api.on_moderation_history_clicked, null), false, false, 0);
    packStart(account_box, makeButton("Block Causes", api.on_blocking_causes_clicked, null), false, false, 0);
    const requests_notebook = c.gtk_notebook_new();
    c.gtk_widget_set_size_request(requests_notebook, -1, 220);
    appendNotebookPage(requests_notebook, sizedSettingsList(&g.g_follow_requests_list, -1), "Follow Requests");
    appendNotebookPage(requests_notebook, sizedSettingsList(&g.g_affiliate_requests_list, -1), "Affiliate Requests");
    packStart(account_box, requests_notebook, false, false, 0);
    const requests_row = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    packStart(requests_row, makeButton("Refresh Requests", onFiltersRefreshClicked, null), false, false, 0);
    packStart(requests_row, makeButton("Remove Affiliate", api.on_remove_affiliate_clicked, null), false, false, 0);
    packStart(account_box, requests_row, false, false, 0);
    g.g_settings_auth_button = makeButton("Login", api.on_login_clicked, null);
    c.gtk_widget_set_name(g.g_settings_auth_button, "auth_button");
    packStart(account_box, g.g_settings_auth_button, false, false, 0);
    packStart(content, framedSettingsSection("Account", account_box), false, false, 0);

    const about_box = settingsSectionBox();
    const app_name = settingsLabel("Tweeta Desktop");
    setBoldLabel(app_name);
    packStart(about_box, app_name, false, false, 0);
    packStart(about_box, settingsLabel("Version 1.0.0"), false, false, 0);
    packStart(about_box, settingsLabel("Licensed under AGPLv3"), false, false, 0);
    packStart(content, framedSettingsSection("About", about_box), false, false, 0);

    c.gtk_container_add(container(scroll), content);
    packStart(outer, scroll, true, true, 0);
    return outer;
}

export fn create_admin_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const notebook = c.gtk_notebook_new();

    const stats_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 10);
    c.gtk_container_set_border_width(container(stats_box), 20);
    g.g_admin_stats_label = c.gtk_label_new("Loading admin statistics...");
    c.gtk_label_set_justify(@ptrCast(@alignCast(g.g_admin_stats_label)), c.GTK_JUSTIFY_LEFT);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(g.g_admin_stats_label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(g.g_admin_stats_label)), TRUE);
    packStart(stats_box, g.g_admin_stats_label, false, false, 10);
    packStart(stats_box, makeButton("Refresh Statistics", api.on_refresh_clicked, null), false, false, 0);
    appendNotebookPage(notebook, stats_box, "Stats");

    g.g_admin_users_search = c.gtk_search_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_users_search), "Search users...");
    connect(g.g_admin_users_search, "activate", onAdminUsersSearchActivated, null);
    const users_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    packStart(users_box, g.g_admin_users_search, false, false, 5);
    packStart(users_box, makeListView(&g.g_admin_users_list), true, true, 0);
    appendNotebookPage(notebook, users_box, "Users");

    g.g_admin_posts_search = c.gtk_search_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_posts_search), "Search posts...");
    connect(g.g_admin_posts_search, "activate", onAdminPostsSearchActivated, null);
    const posts_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    packStart(posts_box, g.g_admin_posts_search, false, false, 5);
    packStart(posts_box, makeListView(&g.g_admin_posts_list), true, true, 0);
    appendNotebookPage(notebook, posts_box, "Posts");

    const suspensions_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    packStart(suspensions_box, makeButton("Refresh Suspensions", api.start_loading_admin_suspensions, null), false, false, 5);
    packStart(suspensions_box, makeListView(&g.g_admin_suspensions_list), true, true, 0);
    appendNotebookPage(notebook, suspensions_box, "Suspensions");

    const reports_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    packStart(reports_box, makeButton("Refresh Reports", api.start_loading_admin_reports, null), false, false, 5);
    packStart(reports_box, makeListView(&g.g_admin_reports_list), true, true, 0);
    appendNotebookPage(notebook, reports_box, "Reports");

    g.g_admin_logs_search = c.gtk_search_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_logs_search), "Search moderation logs...");
    connect(g.g_admin_logs_search, "activate", onAdminLogsSearchActivated, null);
    const logs_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    packStart(logs_box, g.g_admin_logs_search, false, false, 5);
    packStart(logs_box, makeListView(&g.g_admin_logs_list), true, true, 0);
    appendNotebookPage(notebook, logs_box, "Logs");

    const dms_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const dms_paned = c.gtk_paned_new(c.GTK_ORIENTATION_HORIZONTAL);
    const dms_left = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const dms_right = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    g.g_admin_dms_search = c.gtk_search_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_dms_search), "Search DM conversations by username...");
    connect(g.g_admin_dms_search, "activate", onAdminDmsSearchActivated, null);
    const dms_left_scroll = makeListView(&g.g_admin_dms_list);
    c.gtk_list_box_set_selection_mode(listBox(g.g_admin_dms_list), c.GTK_SELECTION_SINGLE);
    connect(g.g_admin_dms_list, "row-selected", api.on_admin_dm_conversation_selected, null);
    const dms_right_scroll = makeListView(&g.g_admin_dm_admin_messages_list);
    packStart(dms_box, g.g_admin_dms_search, false, false, 5);
    packStart(dms_left, dms_left_scroll, true, true, 0);
    packStart(dms_right, dms_right_scroll, true, true, 0);
    c.gtk_paned_pack1(@ptrCast(@alignCast(dms_paned)), dms_left, TRUE, FALSE);
    c.gtk_paned_pack2(@ptrCast(@alignCast(dms_paned)), dms_right, TRUE, FALSE);
    packStart(dms_box, dms_paned, true, true, 0);
    appendNotebookPage(notebook, dms_box, "DMs");

    const blocks_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    packStart(blocks_box, makeButton("Refresh Blocks", api.start_loading_admin_blocks, null), false, false, 5);
    packStart(blocks_box, makeListView(&g.g_admin_blocks_list), true, true, 0);
    appendNotebookPage(notebook, blocks_box, "Blocks");

    const emojis_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const emojis_actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    packStart(emojis_actions, makeButton("Refresh Emojis", api.start_loading_admin_emojis, null), false, false, 0);
    packStart(emojis_actions, makeButton("Upload Emoji", api.on_admin_upload_emoji_clicked, null), false, false, 0);
    packStart(emojis_box, emojis_actions, false, false, 5);
    packStart(emojis_box, makeListView(&g.g_admin_emojis_list), true, true, 0);
    appendNotebookPage(notebook, emojis_box, "Emojis");

    const badges_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const badges_actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    packStart(badges_actions, makeButton("Refresh Badges", api.start_loading_admin_badges, null), false, false, 0);
    packStart(badges_actions, makeButton("Create Badge", api.on_admin_create_badge_clicked, null), false, false, 0);
    packStart(badges_box, badges_actions, false, false, 5);
    packStart(badges_box, makeListView(&g.g_admin_badges_list), true, true, 0);
    appendNotebookPage(notebook, badges_box, "Badges");

    const shop_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const shop_paned = c.gtk_paned_new(c.GTK_ORIENTATION_VERTICAL);
    const shop_products_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const shop_purchases_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    g.g_admin_shop_search = c.gtk_search_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_shop_search), "Search shop products...");
    connect(g.g_admin_shop_search, "activate", onAdminShopSearchActivated, null);
    packStart(shop_box, g.g_admin_shop_search, false, false, 5);
    packStart(shop_products_box, c.gtk_label_new("Products"), false, false, 0);
    packStart(shop_products_box, makeListView(&g.g_admin_shop_products_list), true, true, 0);
    packStart(shop_purchases_box, c.gtk_label_new("Purchases"), false, false, 0);
    packStart(shop_purchases_box, makeListView(&g.g_admin_shop_purchases_list), true, true, 0);
    c.gtk_paned_pack1(@ptrCast(@alignCast(shop_paned)), shop_products_box, TRUE, FALSE);
    c.gtk_paned_pack2(@ptrCast(@alignCast(shop_paned)), shop_purchases_box, TRUE, FALSE);
    packStart(shop_box, shop_paned, true, true, 0);
    appendNotebookPage(notebook, shop_box, "Shop");

    const communities_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 5);
    const communities_actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 5);
    packStart(communities_actions, makeButton("Refresh", api.start_loading_admin_communities, null), false, false, 0);
    packStart(communities_actions, makeButton("Create Community", api.on_admin_create_community_clicked, null), false, false, 0);
    packStart(communities_box, communities_actions, false, false, 5);
    packStart(communities_box, makeListView(&g.g_admin_communities_list), true, true, 0);
    appendNotebookPage(notebook, communities_box, "Communities");

    const notifications_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 8);
    c.gtk_container_set_border_width(container(notifications_box), 12);
    const notifications_grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(notifications_grid)), 6);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(notifications_grid)), 6);
    g.g_admin_notifications_target_entry = c.gtk_entry_new();
    g.g_admin_notifications_type_entry = c.gtk_entry_new();
    g.g_admin_notifications_title_entry = c.gtk_entry_new();
    g.g_admin_notifications_subtitle_entry = c.gtk_entry_new();
    g.g_admin_notifications_url_entry = c.gtk_entry_new();
    g.g_admin_notifications_message_view = c.gtk_text_view_new();
    g.g_admin_notifications_result_label = c.gtk_label_new("");
    const notifications_message_scroll = c.gtk_scrolled_window_new(null, null);
    c.gtk_widget_set_size_request(notifications_message_scroll, -1, 180);
    c.gtk_container_add(container(notifications_message_scroll), g.g_admin_notifications_message_view);
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_notifications_target_entry), "username, username2, or all");
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_notifications_type_entry), "default");
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_notifications_title_entry), "Title");
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_notifications_subtitle_entry), "Subtitle");
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_notifications_url_entry), "https://...");
    c.gtk_label_set_xalign(@ptrCast(@alignCast(g.g_admin_notifications_result_label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(g.g_admin_notifications_result_label)), TRUE);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), c.gtk_label_new("Targets"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), g.g_admin_notifications_target_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), c.gtk_label_new("Type"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), g.g_admin_notifications_type_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), c.gtk_label_new("Title"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), g.g_admin_notifications_title_entry, 1, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), c.gtk_label_new("Subtitle"), 0, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), g.g_admin_notifications_subtitle_entry, 1, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), c.gtk_label_new("URL"), 0, 4, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), g.g_admin_notifications_url_entry, 1, 4, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), c.gtk_label_new("Message"), 0, 5, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(notifications_grid)), notifications_message_scroll, 1, 5, 1, 1);
    packStart(notifications_box, notifications_grid, false, false, 0);
    packStart(notifications_box, makeButton("Send Notification", api.on_admin_send_notification_clicked, null), false, false, 0);
    packStart(notifications_box, g.g_admin_notifications_result_label, false, false, 0);
    appendNotebookPage(notebook, notifications_box, "Notifications");

    const clone_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 8);
    c.gtk_container_set_border_width(container(clone_box), 12);
    const clone_grid = c.gtk_grid_new();
    c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(clone_grid)), 6);
    c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(clone_grid)), 6);
    g.g_admin_clone_source_entry = c.gtk_entry_new();
    g.g_admin_clone_username_entry = c.gtk_entry_new();
    g.g_admin_clone_name_entry = c.gtk_entry_new();
    g.g_admin_clone_relations_check = c.gtk_check_button_new_with_label("Relations");
    g.g_admin_clone_ghosts_check = c.gtk_check_button_new_with_label("Ghosts");
    g.g_admin_clone_tweets_check = c.gtk_check_button_new_with_label("Tweets");
    g.g_admin_clone_replies_check = c.gtk_check_button_new_with_label("Replies");
    g.g_admin_clone_retweets_check = c.gtk_check_button_new_with_label("Retweets");
    g.g_admin_clone_reactions_check = c.gtk_check_button_new_with_label("Reactions");
    g.g_admin_clone_communities_check = c.gtk_check_button_new_with_label("Communities");
    g.g_admin_clone_media_check = c.gtk_check_button_new_with_label("Media");
    g.g_admin_clone_affiliate_check = c.gtk_check_button_new_with_label("Affiliate");
    g.g_admin_clone_result_label = c.gtk_label_new("");
    c.gtk_toggle_button_set_active(@ptrCast(@alignCast(g.g_admin_clone_relations_check)), TRUE);
    c.gtk_toggle_button_set_active(@ptrCast(@alignCast(g.g_admin_clone_ghosts_check)), TRUE);
    c.gtk_toggle_button_set_active(@ptrCast(@alignCast(g.g_admin_clone_tweets_check)), TRUE);
    c.gtk_toggle_button_set_active(@ptrCast(@alignCast(g.g_admin_clone_replies_check)), TRUE);
    c.gtk_toggle_button_set_active(@ptrCast(@alignCast(g.g_admin_clone_retweets_check)), TRUE);
    c.gtk_toggle_button_set_active(@ptrCast(@alignCast(g.g_admin_clone_reactions_check)), TRUE);
    c.gtk_toggle_button_set_active(@ptrCast(@alignCast(g.g_admin_clone_communities_check)), TRUE);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(g.g_admin_clone_result_label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(g.g_admin_clone_result_label)), TRUE);
    c.gtk_grid_attach(@ptrCast(@alignCast(clone_grid)), c.gtk_label_new("Source"), 0, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(clone_grid)), g.g_admin_clone_source_entry, 1, 0, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(clone_grid)), c.gtk_label_new("New Username"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(clone_grid)), g.g_admin_clone_username_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(clone_grid)), c.gtk_label_new("Display Name"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(clone_grid)), g.g_admin_clone_name_entry, 1, 2, 1, 1);
    packStart(clone_box, clone_grid, false, false, 0);
    const clone_checks_a = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    const clone_checks_b = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    const clone_checks_c = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    packStart(clone_checks_a, g.g_admin_clone_relations_check, false, false, 0);
    packStart(clone_checks_a, g.g_admin_clone_ghosts_check, false, false, 0);
    packStart(clone_checks_a, g.g_admin_clone_tweets_check, false, false, 0);
    packStart(clone_checks_b, g.g_admin_clone_replies_check, false, false, 0);
    packStart(clone_checks_b, g.g_admin_clone_retweets_check, false, false, 0);
    packStart(clone_checks_b, g.g_admin_clone_reactions_check, false, false, 0);
    packStart(clone_checks_c, g.g_admin_clone_communities_check, false, false, 0);
    packStart(clone_checks_c, g.g_admin_clone_media_check, false, false, 0);
    packStart(clone_checks_c, g.g_admin_clone_affiliate_check, false, false, 0);
    packStart(clone_box, clone_checks_a, false, false, 0);
    packStart(clone_box, clone_checks_b, false, false, 0);
    packStart(clone_box, clone_checks_c, false, false, 0);
    packStart(clone_box, makeButton("Clone User", api.on_admin_clone_user_clicked, null), false, false, 0);
    packStart(clone_box, g.g_admin_clone_result_label, false, false, 0);
    appendNotebookPage(notebook, clone_box, "Clone");

    const tools_scroll = c.gtk_scrolled_window_new(null, null);
    const tools_box = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 12);
    c.gtk_container_set_border_width(container(tools_box), 12);
    const impersonation_grid = c.gtk_grid_new();
    const post_grid = c.gtk_grid_new();
    const bulk_grid = c.gtk_grid_new();
    for ([_][*c]c.GtkWidget{ impersonation_grid, post_grid, bulk_grid }) |grid| {
        c.gtk_grid_set_row_spacing(@ptrCast(@alignCast(grid)), 6);
        c.gtk_grid_set_column_spacing(@ptrCast(@alignCast(grid)), 6);
    }
    g.g_admin_impersonation_entry = c.gtk_entry_new();
    g.g_admin_impersonation_status_label = c.gtk_label_new("Admin session active.");
    g.g_admin_tools_post_targets_entry = c.gtk_entry_new();
    g.g_admin_tools_post_reply_to_entry = c.gtk_entry_new();
    g.g_admin_tools_post_source_entry = c.gtk_entry_new();
    g.g_admin_tools_post_created_at_entry = c.gtk_entry_new();
    g.g_admin_tools_post_no_char_limit_check = c.gtk_check_button_new_with_label("No Character Limit");
    g.g_admin_tools_post_content_view = c.gtk_text_view_new();
    g.g_admin_tools_bulk_targets_entry = c.gtk_entry_new();
    g.g_admin_tools_bulk_payload_view = c.gtk_text_view_new();
    c.gtk_toggle_button_set_active(@ptrCast(@alignCast(g.g_admin_tools_post_no_char_limit_check)), TRUE);
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_tools_post_targets_entry), "username, username2");
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_tools_post_reply_to_entry), "Optional post ID");
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_tools_post_source_entry), "Tweeta Desktop");
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_tools_post_created_at_entry), "2026-04-28T12:00:00.000Z");
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_tools_bulk_targets_entry), "username, username2");
    c.gtk_entry_set_placeholder_text(entry(g.g_admin_impersonation_entry), "username or user id");
    const post_scroll = c.gtk_scrolled_window_new(null, null);
    const bulk_scroll = c.gtk_scrolled_window_new(null, null);
    c.gtk_widget_set_size_request(post_scroll, -1, 180);
    c.gtk_widget_set_size_request(bulk_scroll, -1, 220);
    c.gtk_container_add(container(post_scroll), g.g_admin_tools_post_content_view);
    c.gtk_container_add(container(bulk_scroll), g.g_admin_tools_bulk_payload_view);
    c.gtk_text_buffer_set_text(c.gtk_text_view_get_buffer(@ptrCast(@alignCast(g.g_admin_tools_bulk_payload_view))), "{\n  \"verified\": true\n}", -1);
    c.gtk_label_set_xalign(@ptrCast(@alignCast(g.g_admin_impersonation_status_label)), 0.0);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(g.g_admin_impersonation_status_label)), TRUE);
    c.gtk_grid_attach(@ptrCast(@alignCast(impersonation_grid)), c.gtk_label_new("Impersonation"), 0, 0, 2, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(impersonation_grid)), c.gtk_label_new("Target"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(impersonation_grid)), g.g_admin_impersonation_entry, 1, 1, 1, 1);
    const impersonation_actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 6);
    packStart(impersonation_actions, makeButton("Impersonate", api.on_admin_impersonate_clicked, null), false, false, 0);
    packStart(impersonation_actions, makeButton("Restore Admin", api.on_admin_restore_admin_clicked, null), false, false, 0);
    c.gtk_grid_attach(@ptrCast(@alignCast(impersonation_grid)), impersonation_actions, 1, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(impersonation_grid)), g.g_admin_impersonation_status_label, 0, 3, 2, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), c.gtk_label_new("Posting"), 0, 0, 2, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), c.gtk_label_new("Targets"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), g.g_admin_tools_post_targets_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), c.gtk_label_new("Reply To"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), g.g_admin_tools_post_reply_to_entry, 1, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), c.gtk_label_new("Source"), 0, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), g.g_admin_tools_post_source_entry, 1, 3, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), c.gtk_label_new("Created At"), 0, 4, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), g.g_admin_tools_post_created_at_entry, 1, 4, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), c.gtk_label_new("Content"), 0, 5, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), post_scroll, 1, 5, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), g.g_admin_tools_post_no_char_limit_check, 1, 6, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(post_grid)), makeButton("Create Post", api.on_admin_post_as_user_clicked, null), 1, 7, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(bulk_grid)), c.gtk_label_new("Bulk Edit"), 0, 0, 2, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(bulk_grid)), c.gtk_label_new("Targets"), 0, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(bulk_grid)), g.g_admin_tools_bulk_targets_entry, 1, 1, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(bulk_grid)), c.gtk_label_new("Payload"), 0, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(bulk_grid)), bulk_scroll, 1, 2, 1, 1);
    c.gtk_grid_attach(@ptrCast(@alignCast(bulk_grid)), makeButton("Apply Bulk Edit", api.on_admin_bulk_edit_clicked, null), 1, 3, 1, 1);
    packStart(tools_box, impersonation_grid, false, false, 0);
    packStart(tools_box, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false, false, 0);
    packStart(tools_box, post_grid, false, false, 0);
    packStart(tools_box, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false, false, 0);
    packStart(tools_box, bulk_grid, false, false, 0);
    c.gtk_container_add(container(tools_scroll), tools_box);
    appendNotebookPage(notebook, tools_scroll, "Tools");

    packStart(outer, notebook, true, true, 0);
    return outer;
}

export fn create_bookmarks_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    packStart(outer, makeFeedListView(&g.g_bookmarks_list, "bookmarks"), true, true, 0);
    return outer;
}

export fn create_lists_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const action_bar = c.gtk_action_bar_new();
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), makeButton("Create List", api.on_create_list_clicked, null));
    c.gtk_action_bar_pack_end(@ptrCast(@alignCast(action_bar)), makeButton("Refresh", onListsRefreshClicked, null));
    const notebook = c.gtk_notebook_new();
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_lists_owned_list), c.gtk_label_new("Owned"));
    _ = c.gtk_notebook_append_page(@ptrCast(@alignCast(notebook)), makeListView(&g.g_lists_followed_list), c.gtk_label_new("Followed"));
    packStart(outer, action_bar, false, false, 0);
    packStart(outer, notebook, true, true, 0);
    return outer;
}

export fn create_list_details_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const header = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    const header_text = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 4);
    const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 4);
    const notebook = c.gtk_notebook_new();
    c.gtk_container_set_border_width(container(header), 10);
    g.g_list_title_label = c.gtk_label_new("List");
    g.g_list_details_label = c.gtk_label_new("");
    leftAlignLabel(g.g_list_title_label);
    dimLabel(g.g_list_details_label);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(g.g_list_details_label)), TRUE);
    packStart(header_text, g.g_list_title_label, false, false, 0);
    packStart(header_text, g.g_list_details_label, false, false, 0);
    packStart(header, header_text, true, true, 0);
    g.g_list_follow_button = makeButton("Follow", api.on_list_follow_clicked, @ptrFromInt(3));
    g.g_list_edit_button = makeButton("Edit", api.on_list_edit_clicked, null);
    g.g_list_delete_button = makeButton("Delete", api.on_list_delete_clicked, null);
    g.g_list_add_member_button = makeButton("Add Member", api.on_list_add_member_clicked, null);
    packStart(actions, g.g_list_follow_button, false, false, 0);
    packStart(actions, g.g_list_edit_button, false, false, 0);
    packStart(actions, g.g_list_delete_button, false, false, 0);
    packStart(actions, g.g_list_add_member_button, false, false, 0);
    packEnd(header, actions, false, false, 0);
    packStart(outer, header, false, false, 0);
    packStart(outer, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false, false, 0);
    appendNotebookPage(notebook, makeListView(&g.g_list_tweets_list), "Tweets");
    appendNotebookPage(notebook, makeListView(&g.g_list_members_list), "Members");
    appendNotebookPage(notebook, makeListView(&g.g_list_followers_list), "Followers");
    packStart(outer, notebook, true, true, 0);
    return outer;
}

export fn create_explore_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const action_bar = c.gtk_action_bar_new();
    g.g_explore_category_combo = c.gtk_combo_box_text_new();
    for ([_][*c]const c.gchar{
        "Trends",
        "Best of Week",
        "Most Bookmarked",
        "Most Discussed",
        "Longest Threads",
        "With Media",
        "With Polls",
        "Trending Users",
        "Suggested Users",
        "User Directory",
        "Top Hashtags",
        "Digest",
        "Leaderboard",
        "Stats",
    }) |item| c.gtk_combo_box_text_append_text(@ptrCast(g.g_explore_category_combo), item);
    c.gtk_combo_box_set_active(combo(g.g_explore_category_combo), 0);
    connect(g.g_explore_category_combo, "changed", api.on_explore_category_changed, null);
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), g.g_explore_category_combo);
    c.gtk_action_bar_pack_end(@ptrCast(@alignCast(action_bar)), makeButton("Refresh", onExploreRefreshClicked, null));
    packStart(outer, action_bar, false, false, 0);
    packStart(outer, makeListView(&g.g_explore_list), true, true, 0);
    return outer;
}

fn create_articles_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 6);
    const action_bar = c.gtk_action_bar_new();
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), makeButton("New Article", api.on_compose_article_clicked, null));
    c.gtk_action_bar_pack_end(@ptrCast(@alignCast(action_bar)), makeButton("Refresh", onArticlesRefreshClicked, null));
    packStart(outer, action_bar, false, false, 0);
    packStart(outer, makeListView(&g.g_articles_list), true, true, 0);
    return outer;
}

export fn create_communities_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const action_bar = c.gtk_action_bar_new();
    g.g_communities_search_entry = c.gtk_search_entry_new();
    c.gtk_entry_set_placeholder_text(entry(g.g_communities_search_entry), "Search communities");
    connect(g.g_communities_search_entry, "activate", onCommunitiesSearchActivated, null);
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), makeButton("All", onCommunitiesAllClicked, null));
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), makeButton("Mine", onCommunitiesMineClicked, null));
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), makeButton("Trending", onCommunitiesTrendingClicked, null));
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), makeButton("Recommended", onCommunitiesRecommendedClicked, null));
    c.gtk_action_bar_pack_start(@ptrCast(@alignCast(action_bar)), g.g_communities_search_entry);
    c.gtk_action_bar_pack_end(@ptrCast(@alignCast(action_bar)), makeButton("Join Invite", api.on_community_accept_invite_clicked, null));
    c.gtk_action_bar_pack_end(@ptrCast(@alignCast(action_bar)), makeButton("Create", onCreateCommunityClicked, null));
    packStart(outer, action_bar, false, false, 0);
    packStart(outer, makeScrollListView(&g.g_communities_list), true, true, 0);
    return outer;
}

export fn create_community_tweets_view() [*c]c.GtkWidget {
    const outer = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 0);
    const header = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 10);
    const header_text = c.gtk_box_new(c.GTK_ORIENTATION_VERTICAL, 4);
    const actions = c.gtk_box_new(c.GTK_ORIENTATION_HORIZONTAL, 4);
    c.gtk_container_set_border_width(container(header), 10);
    g.g_community_title_label = c.gtk_label_new("Community");
    g.g_community_details_label = c.gtk_label_new("");
    leftAlignLabel(g.g_community_title_label);
    dimLabel(g.g_community_details_label);
    c.gtk_label_set_line_wrap(@ptrCast(@alignCast(g.g_community_details_label)), TRUE);
    packStart(header_text, g.g_community_title_label, false, false, 0);
    packStart(header_text, g.g_community_details_label, false, false, 0);
    packStart(header, header_text, true, true, 0);
    for ([_][*c]c.GtkWidget{
        makeButton("Members", onCommunityMembersClicked, null),
        makeButton("Invite", api.on_community_create_invite_clicked, null),
        makeButton("Invites", api.on_community_manage_invites_clicked, null),
        makeButton("Moderation", api.on_community_moderation_clicked, null),
        makeButton("Style", api.on_community_style_clicked, null),
        makeButton("Pin Post", api.on_community_pin_post_clicked, null),
        makeButton("Edit", onEditCommunityClicked, null),
        makeButton("Access", onCommunityAccessClicked, null),
        makeButton("Delete", onDeleteCommunityClicked, null),
    }) |button| packStart(actions, button, false, false, 0);
    packEnd(header, actions, false, false, 0);
    packStart(outer, header, false, false, 0);
    packStart(outer, c.gtk_separator_new(c.GTK_ORIENTATION_HORIZONTAL), false, false, 0);
    packStart(outer, makeFeedListView(&g.g_community_tweets_list, "community_tweets"), true, true, 0);
    return outer;
}

export fn create_window() [*c]c.GtkWidget {
    const window = c.gtk_window_new(c.GTK_WINDOW_TOPLEVEL);
    c.gtk_window_set_title(@ptrCast(window), "Tweeta Desktop");
    c.gtk_window_set_default_size(@ptrCast(window), 600, 800);
    c.gtk_window_set_icon_name(@ptrCast(window), "tweeta-desktop");
    connect(window, "destroy", c.gtk_main_quit, null);

    const header = c.gtk_header_bar_new();
    c.gtk_header_bar_set_show_close_button(headerBar(header), TRUE);
    c.gtk_header_bar_set_title(headerBar(header), "Tweeta Desktop");
    c.gtk_window_set_titlebar(@ptrCast(@alignCast(window)), header);

    g.g_search_entry = c.gtk_search_entry_new();
    c.gtk_header_bar_set_custom_title(headerBar(header), g.g_search_entry);
    connect(g.g_search_entry, "activate", api.on_search_activated, null);

    g.g_back_button = makeIconButton("go-previous-symbolic", api.on_back_clicked, null);
    c.gtk_widget_set_no_show_all(g.g_back_button, TRUE);
    c.gtk_header_bar_pack_start(headerBar(header), g.g_back_button);

    g.g_compose_button = makeButton("Compose", api.on_compose_clicked, window);
    c.gtk_widget_set_sensitive(g.g_compose_button, FALSE);
    c.gtk_header_bar_pack_start(headerBar(header), g.g_compose_button);

    g.g_notifications_button = makeButton("Alerts", api.on_notifications_clicked, null);
    c.gtk_header_bar_pack_start(headerBar(header), g.g_notifications_button);

    c.gtk_header_bar_pack_start(headerBar(header), makeIconButton("mail-unread-symbolic", api.on_messages_clicked, null));
    c.gtk_header_bar_pack_start(headerBar(header), makeIconButton("bookmark-new-symbolic", onBookmarksClicked, null));
    c.gtk_header_bar_pack_start(headerBar(header), makeIconButton("view-list-symbolic", api.on_lists_clicked, null));
    c.gtk_header_bar_pack_start(headerBar(header), makeIconButton("edit-find-symbolic", api.on_explore_clicked, null));
    c.gtk_header_bar_pack_start(headerBar(header), makeButton("Articles", api.on_articles_clicked, null));
    c.gtk_header_bar_pack_start(headerBar(header), makeButton("Public", onTimelineToggleClicked, null));
    c.gtk_header_bar_pack_start(headerBar(header), makeIconButton("emblem-system-symbolic", api.on_settings_clicked, null));
    c.gtk_header_bar_pack_start(headerBar(header), makeIconButton("view-refresh-symbolic", api.on_refresh_clicked, null));
    c.gtk_header_bar_pack_start(headerBar(header), makeIconButton("system-users-symbolic", onCommunitiesClicked, null));
    g.g_admin_button = makeIconButton("dialog-password-symbolic", api.on_admin_clicked, null);
    c.gtk_widget_set_no_show_all(g.g_admin_button, TRUE);
    c.gtk_widget_hide(g.g_admin_button);
    c.gtk_header_bar_pack_start(headerBar(header), g.g_admin_button);

    g.g_user_label = c.gtk_label_new("Not logged in");
    c.gtk_header_bar_pack_end(headerBar(header), g.g_user_label);
    g.g_header_auth_button = makeButton("Login", api.on_login_clicked, window);
    c.gtk_header_bar_pack_end(headerBar(header), g.g_header_auth_button);

    g.g_stack = c.gtk_stack_new();
    c.gtk_stack_set_transition_type(stack(g.g_stack), c.GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    c.gtk_container_add(container(window), g.g_stack);

    addStackPage("timeline", makeFeedListView(&g.g_main_list_box, "public"));
    addStackPage("profile", create_profile_view());
    addStackPage("search", create_search_view());
    addStackPage("notifications", create_notifications_view());
    addStackPage("messages", create_messages_view());
    addStackPage("dm_messages", create_dm_messages_view());
    addStackPage("conversation", create_conversation_view());
    addStackPage("settings", create_settings_view());
    addStackPage("admin", create_admin_view());
    addStackPage("bookmarks", create_bookmarks_view());
    addStackPage("lists", create_lists_view());
    addStackPage("list_details", create_list_details_view());
    addStackPage("explore", create_explore_view());
    addStackPage("articles", create_articles_view());
    addStackPage("communities", create_communities_view());
    addStackPage("community_tweets", create_community_tweets_view());
    return window;
}
