#include "actions.h"
#include "constants.h"
#include "globals.h"
#include "json_utils.h"
#include "network.h"
#include "ui_components.h"
#include "views.h"

static void on_profile_edit_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkWidget **entries = (GtkWidget **)user_data;
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(entries[0]));
        const gchar *bio = gtk_entry_get_text(GTK_ENTRY(entries[1]));

        if (g_current_username && perform_update_profile(g_current_username, name, bio)) {
            show_profile(g_current_username);
        } else {
            GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_CLOSE,
                                     "Failed to update profile.");
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
        }
    }
    g_free(user_data);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void on_edit_profile_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if (!g_current_username) return;

    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Edit Profile",
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Save", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    GtkWidget *name_entry = gtk_entry_new();
    GtkWidget *bio_entry = gtk_entry_new();

    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "Display name...");
    gtk_entry_set_placeholder_text(GTK_ENTRY(bio_entry), "Bio...");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Bio:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), bio_entry, 1, 1, 1, 1);

    gtk_widget_show_all(grid);
    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);

    GtkWidget **entries = g_new(GtkWidget*, 2);
    entries[0] = name_entry;
    entries[1] = bio_entry;

    g_signal_connect(dialog, "response", G_CALLBACK(on_profile_edit_response), entries);
    gtk_widget_show(dialog);
}

static void on_follow_button_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
    gboolean *is_following = g_object_get_data(G_OBJECT(widget), "is_following");

    if (!username || !is_following || !g_auth_token) return;

    if (perform_follow(username, !(*is_following))) {
        *is_following = !(*is_following);
        gtk_button_set_label(GTK_BUTTON(widget), *is_following ? "Unfollow" : "Follow");
    }
}

GtkWidget*
create_profile_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    
    g_profile_avatar_image = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DND);
    gtk_widget_set_size_request(g_profile_avatar_image, 80, 80);
    gtk_box_pack_start(GTK_BOX(hbox), g_profile_avatar_image, FALSE, FALSE, 0);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_profile_name_label = gtk_label_new("");
    gtk_widget_set_halign(g_profile_name_label, GTK_ALIGN_START);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_scale_new(1.5));
    gtk_label_set_attributes(GTK_LABEL(g_profile_name_label), attrs);
    pango_attr_list_unref(attrs);

    g_profile_bio_label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(g_profile_bio_label), TRUE);
    gtk_widget_set_halign(g_profile_bio_label, GTK_ALIGN_START);

    g_profile_stats_label = gtk_label_new("");
    gtk_widget_set_halign(g_profile_stats_label, GTK_ALIGN_START);

    // Follow button
    g_follow_button = gtk_button_new_with_label("Follow");
    gtk_widget_set_no_show_all(g_follow_button, TRUE);
    gtk_widget_hide(g_follow_button);
    g_signal_connect(g_follow_button, "clicked", G_CALLBACK(on_follow_button_clicked), NULL);

    // Edit Profile button (only for own profile)
    GtkWidget *edit_profile_button = gtk_button_new_with_label("Edit Profile");
    gtk_widget_set_no_show_all(edit_profile_button, TRUE);
    gtk_widget_hide(edit_profile_button);
    g_signal_connect(edit_profile_button, "clicked", G_CALLBACK(on_edit_profile_clicked), NULL);
    g_object_set_data(G_OBJECT(box), "edit_profile_button", edit_profile_button);

    gtk_box_pack_start(GTK_BOX(vbox), g_profile_name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_profile_bio_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_profile_stats_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_follow_button, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), edit_profile_button, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);

    GtkWidget *notebook = gtk_notebook_new();
    
    // Tweets tab
    GtkWidget *tweets_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_tweets_list = gtk_list_box_new();
    g_object_set_data(G_OBJECT(g_profile_tweets_list), "feed_type", "profile_posts");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_tweets_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(tweets_scroll), g_profile_tweets_list);
    g_signal_connect(tweets_scroll, "edge-reached", G_CALLBACK(on_scroll_edge_reached), NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tweets_scroll, gtk_label_new("Tweets"));

    // Replies tab
    GtkWidget *replies_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_replies_list = gtk_list_box_new();
    g_object_set_data(G_OBJECT(g_profile_replies_list), "feed_type", "profile_replies");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_replies_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(replies_scroll), g_profile_replies_list);
    g_signal_connect(replies_scroll, "edge-reached", G_CALLBACK(on_scroll_edge_reached), NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), replies_scroll, gtk_label_new("Replies"));

    // Followers tab
    GtkWidget *followers_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_followers_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_followers_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(followers_scroll), g_followers_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), followers_scroll, gtk_label_new("Followers"));

    // Following tab
    GtkWidget *following_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_following_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_following_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(following_scroll), g_following_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), following_scroll, gtk_label_new("Following"));

    gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE, 0);

    return box;
}

GtkWidget*
create_search_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *notebook = gtk_notebook_new();

    // Users tab
    GtkWidget *users_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_search_users_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_search_users_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(users_scroll), g_search_users_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), users_scroll, gtk_label_new("Users"));

    // Tweets tab
    GtkWidget *tweets_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_search_tweets_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_search_tweets_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(tweets_scroll), g_search_tweets_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tweets_scroll, gtk_label_new("Tweets"));

    gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE, 0);

    return box;
}

GtkWidget*
create_notifications_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    GtkWidget *action_bar = gtk_action_bar_new();
    GtkWidget *mark_read_btn = gtk_button_new_with_label("Mark all as read");
    g_signal_connect(mark_read_btn, "clicked", G_CALLBACK(on_mark_all_read_clicked), NULL);
    gtk_action_bar_pack_end(GTK_ACTION_BAR(action_bar), mark_read_btn);
    gtk_box_pack_start(GTK_BOX(box), action_bar, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    g_notifications_list = gtk_list_box_new();
    g_object_set_data(G_OBJECT(g_notifications_list), "feed_type", "notifications");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_notifications_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_notifications_list);
    g_signal_connect(scroll, "edge-reached", G_CALLBACK(on_scroll_edge_reached), NULL);
    
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    
    return box;
}

GtkWidget*
create_conversation_view(void)
{
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    g_conversation_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_conversation_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_conversation_list);
    return scroll;
}

static void
on_dm_send_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    const gchar *content = gtk_entry_get_text(GTK_ENTRY(g_dm_entry));
    const gchar *conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");

    if (content && strlen(content) > 0 && conv_id) {
        gchar *url = g_strdup_printf(DM_SEND_MESSAGE_URL, conv_id);
        gchar *post_data = construct_dm_payload(content);
        struct MemoryStruct chunk;

        if (fetch_url(url, &chunk, post_data, "POST")) {
            gtk_entry_set_text(GTK_ENTRY(g_dm_entry), "");
            start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);
            free(chunk.memory);
        }
        
        g_free(post_data);
        g_free(url);
    }
}

GtkWidget*
create_messages_view(void)
{
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    g_conversations_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_conversations_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_conversations_list);
    return scroll;
}

GtkWidget*
create_dm_messages_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(header_box), 10);
    g_dm_title_label = gtk_label_new("Messages");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(g_dm_title_label), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(header_box), g_dm_title_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), header_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    g_dm_messages_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_dm_messages_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_dm_messages_list);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    
    GtkWidget *input_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(input_hbox), 5);
    g_dm_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_dm_entry), "Type a message...");
    g_signal_connect(g_dm_entry, "activate", G_CALLBACK(on_dm_send_clicked), NULL);
    
    GtkWidget *send_btn = gtk_button_new_with_label("Send");
    g_signal_connect(send_btn, "clicked", G_CALLBACK(on_dm_send_clicked), NULL);
    
    gtk_box_pack_start(GTK_BOX(input_hbox), g_dm_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(input_hbox), send_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), input_hbox, FALSE, FALSE, 0);

    return box;
}

GtkWidget*
create_settings_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(box), 20);

    GtkWidget *title = gtk_label_new("Settings");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_scale_new(1.5));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    GtkWidget *placeholder_label = gtk_label_new("Settings are currently under development.\nCheck back soon for theme, notification, and account options!");
    gtk_label_set_justify(GTK_LABEL(placeholder_label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(box), placeholder_label, TRUE, TRUE, 0);

    return box;
}

static void
on_admin_users_search_activated(GtkEntry *entry, gpointer user_data)
{
    (void)user_data;
    const gchar *query = gtk_entry_get_text(entry);
    start_loading_admin_users(query);
}

static void
on_admin_posts_search_activated(GtkEntry *entry, gpointer user_data)
{
    (void)user_data;
    const gchar *query = gtk_entry_get_text(entry);
    start_loading_admin_posts(query);
}

static void on_communities_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if (!g_auth_token) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
        GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
        GtkWidget *error_dialog = gtk_message_dialog_new(window,
                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                 GTK_MESSAGE_ERROR,
                                 GTK_BUTTONS_CLOSE,
                                 "You must be logged in to view communities.");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }

    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "communities");
    gtk_widget_show(g_back_button);
    start_loading_communities(GTK_LIST_BOX(g_communities_list));
}

static void on_bookmarks_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if (!g_auth_token) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
        GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
        GtkWidget *error_dialog = gtk_message_dialog_new(window,
                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                 GTK_MESSAGE_ERROR,
                                 GTK_BUTTONS_CLOSE,
                                 "You must be logged in to view bookmarks.");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }

    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "bookmarks");
    gtk_widget_show(g_back_button);
    start_loading_bookmarks(GTK_LIST_BOX(g_bookmarks_list));
}

static void on_timeline_toggle_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;

    if (g_current_timeline_type == TIMELINE_PUBLIC) {
        set_timeline_type(TIMELINE_FOLLOWING);
        gtk_button_set_label(GTK_BUTTON(widget), "Following");
    } else {
        set_timeline_type(TIMELINE_PUBLIC);
        gtk_button_set_label(GTK_BUTTON(widget), "Public");
    }

    start_loading_timeline(GTK_LIST_BOX(g_main_list_box));
}

GtkWidget*
create_bookmarks_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    g_bookmarks_list = gtk_list_box_new();
    g_object_set_data(G_OBJECT(g_bookmarks_list), "feed_type", "bookmarks");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_bookmarks_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_bookmarks_list);
    g_signal_connect(scroll, "edge-reached", G_CALLBACK(on_scroll_edge_reached), NULL);

    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    return box;
}

GtkWidget*
create_communities_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    g_communities_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_communities_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_communities_list);
    g_signal_connect(scroll, "edge-reached", G_CALLBACK(on_scroll_edge_reached), NULL);

    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    return box;
}

GtkWidget*
create_community_tweets_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    g_community_tweets_list = gtk_list_box_new();
    g_object_set_data(G_OBJECT(g_community_tweets_list), "feed_type", "community_tweets");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_community_tweets_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_community_tweets_list);
    g_signal_connect(scroll, "edge-reached", G_CALLBACK(on_scroll_edge_reached), NULL);

    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    return box;
}

GtkWidget*
create_admin_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *notebook = gtk_notebook_new();

    // Stats Tab
    GtkWidget *stats_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(stats_box), 20);
    
    g_admin_stats_label = gtk_label_new("Loading admin statistics...");
    gtk_label_set_justify(GTK_LABEL(g_admin_stats_label), GTK_JUSTIFY_LEFT);
    gtk_label_set_xalign(GTK_LABEL(g_admin_stats_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(g_admin_stats_label), TRUE);
    gtk_box_pack_start(GTK_BOX(stats_box), g_admin_stats_label, FALSE, FALSE, 10);

    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh Statistics");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(stats_box), refresh_btn, FALSE, FALSE, 0);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), stats_box, gtk_label_new("Stats"));

    // Users Tab
    GtkWidget *users_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_admin_users_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_users_search), "Search users...");
    g_signal_connect(g_admin_users_search, "activate", G_CALLBACK(on_admin_users_search_activated), NULL);
    gtk_box_pack_start(GTK_BOX(users_vbox), g_admin_users_search, FALSE, FALSE, 5);

    GtkWidget *users_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_users_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_users_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(users_scroll), g_admin_users_list);
    gtk_box_pack_start(GTK_BOX(users_vbox), users_scroll, TRUE, TRUE, 0);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), users_vbox, gtk_label_new("Users"));

    // Posts Tab
    GtkWidget *posts_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_admin_posts_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_posts_search), "Search posts...");
    g_signal_connect(g_admin_posts_search, "activate", G_CALLBACK(on_admin_posts_search_activated), NULL);
    gtk_box_pack_start(GTK_BOX(posts_vbox), g_admin_posts_search, FALSE, FALSE, 5);

    GtkWidget *posts_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_posts_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_posts_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(posts_scroll), g_admin_posts_list);
    gtk_box_pack_start(GTK_BOX(posts_vbox), posts_scroll, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), posts_vbox, gtk_label_new("Posts"));

    gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE, 0);

    return box;
}

GtkWidget*
create_window(void)
{
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Tweeta Desktop");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 800);
    gtk_window_set_icon_name(GTK_WINDOW(window), "tweeta-desktop");
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Header Bar
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Tweeta Desktop");
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    // Search Entry
    g_search_entry = gtk_search_entry_new();
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header), g_search_entry);
    g_signal_connect(g_search_entry, "activate", G_CALLBACK(on_search_activated), NULL);

    // Back Button (Left)
    g_back_button = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_no_show_all(g_back_button, TRUE);
    g_signal_connect(g_back_button, "clicked", G_CALLBACK(on_back_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), g_back_button);

    // Compose Button (Left)
    g_compose_button = gtk_button_new_with_label("Compose");
    gtk_widget_set_sensitive(g_compose_button, FALSE); // Disabled initially
    g_signal_connect(g_compose_button, "clicked", G_CALLBACK(on_compose_clicked), window);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), g_compose_button);

    // Notifications Button (Left)
    GtkWidget *notif_button = gtk_button_new_from_icon_name("preferences-system-notifications-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(notif_button, "clicked", G_CALLBACK(on_notifications_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), notif_button);

    // Messages Button (Left)
    GtkWidget *messages_button = gtk_button_new_from_icon_name("mail-unread-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(messages_button, "clicked", G_CALLBACK(on_messages_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), messages_button);

    // Bookmarks Button (Left)
    GtkWidget *bookmarks_button = gtk_button_new_from_icon_name("bookmark-new-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(bookmarks_button, "clicked", G_CALLBACK(on_bookmarks_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), bookmarks_button);

    // Timeline Toggle Button (Left)
    GtkWidget *timeline_toggle_button = gtk_button_new_with_label("Public");
    g_signal_connect(timeline_toggle_button, "clicked", G_CALLBACK(on_timeline_toggle_clicked), timeline_toggle_button);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), timeline_toggle_button);

    // Settings Button (Left)
    GtkWidget *settings_button = gtk_button_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(settings_button, "clicked", G_CALLBACK(on_settings_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), settings_button);

    // Refresh Button (Left)
    GtkWidget *refresh_button = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), refresh_button);

    // Communities Button (Left)
    GtkWidget *communities_button = gtk_button_new_from_icon_name("users-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(communities_button, "clicked", G_CALLBACK(on_communities_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), communities_button);

    // Admin Button (Left)
    g_admin_button = gtk_button_new_from_icon_name("dialog-password-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_no_show_all(g_admin_button, TRUE);
    gtk_widget_hide(g_admin_button);
    g_signal_connect(g_admin_button, "clicked", G_CALLBACK(on_admin_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), g_admin_button);

    // Login Button (Right)
    g_login_button = gtk_button_new_with_label("Login");
    g_signal_connect(g_login_button, "clicked", G_CALLBACK(on_login_clicked), window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), g_login_button);

    // User Label
    g_user_label = gtk_label_new("Not logged in");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), g_user_label);

    g_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(g_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_container_add(GTK_CONTAINER(window), g_stack);

    // Timeline View
    GtkWidget *timeline_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_main_list_box = gtk_list_box_new();
    g_object_set_data(G_OBJECT(g_main_list_box), "feed_type", "public");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_main_list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(timeline_scroll), g_main_list_box);
    g_signal_connect(timeline_scroll, "edge-reached", G_CALLBACK(on_scroll_edge_reached), NULL);
    gtk_stack_add_named(GTK_STACK(g_stack), timeline_scroll, "timeline");

    // Profile View
    GtkWidget *profile_view = create_profile_view();
    gtk_stack_add_named(GTK_STACK(g_stack), profile_view, "profile");

    // Search View
    GtkWidget *search_view = create_search_view();
    gtk_stack_add_named(GTK_STACK(g_stack), search_view, "search");

    // Notifications View
    GtkWidget *notifications_view = create_notifications_view();
    gtk_stack_add_named(GTK_STACK(g_stack), notifications_view, "notifications");

    // Messages View
    GtkWidget *messages_view = create_messages_view();
    gtk_stack_add_named(GTK_STACK(g_stack), messages_view, "messages");

    // DM Messages View
    GtkWidget *dm_messages_view = create_dm_messages_view();
    gtk_stack_add_named(GTK_STACK(g_stack), dm_messages_view, "dm_messages");

    // Conversation View
    GtkWidget *conversation_view = create_conversation_view();
    gtk_stack_add_named(GTK_STACK(g_stack), conversation_view, "conversation");

    // Settings View
    GtkWidget *settings_view = create_settings_view();
    gtk_stack_add_named(GTK_STACK(g_stack), settings_view, "settings");

    // Admin View
    GtkWidget *admin_view = create_admin_view();
    gtk_stack_add_named(GTK_STACK(g_stack), admin_view, "admin");

    // Bookmarks View
    GtkWidget *bookmarks_view = create_bookmarks_view();
    gtk_stack_add_named(GTK_STACK(g_stack), bookmarks_view, "bookmarks");

    // Communities View
    GtkWidget *communities_view = create_communities_view();
    gtk_stack_add_named(GTK_STACK(g_stack), communities_view, "communities");

    // Community Tweets View
    GtkWidget *community_tweets_view = create_community_tweets_view();
    gtk_stack_add_named(GTK_STACK(g_stack), community_tweets_view, "community_tweets");

    return window;
}

