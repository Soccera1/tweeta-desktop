#include "actions.h"
#include "constants.h"
#include "globals.h"
#include "json_utils.h"
#include "network.h"
#include "ui_components.h"
#include "views.h"
#include "p2p_network.h"
#include "p2p_crypto.h"

/* Forward declarations */
extern gboolean on_p2p_contact_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
extern gboolean p2p_send_message(const gchar *recipient_username, const gchar *plaintext, const gchar *recipient_fingerprint);

static inline gchar* get_username_safe(void) {
    g_mutex_lock(&g_globals_mutex);
    gchar *username = g_current_username ? g_strdup(g_current_username) : NULL;
    g_mutex_unlock(&g_globals_mutex);
    return username;
}

static void on_profile_edit_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkWidget **entries = (GtkWidget **)user_data;
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(entries[0]));
        const gchar *bio = gtk_entry_get_text(GTK_ENTRY(entries[1]));

        gchar *username = get_username_safe();
        if (username && perform_update_profile(username, name, bio)) {
            show_profile(username);
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

    gchar *username = get_username_safe();
    if (!username) {
        g_free(username);
        return;
    }
    g_free(username);

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

    g_follow_button = gtk_button_new_with_label("Follow");
    gtk_widget_set_no_show_all(g_follow_button, TRUE);
    gtk_widget_hide(g_follow_button);
    g_signal_connect(g_follow_button, "clicked", G_CALLBACK(on_follow_button_clicked), NULL);

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
    
    GtkWidget *tweets_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_tweets_list = gtk_list_box_new();
    g_object_set_data(G_OBJECT(g_profile_tweets_list), "feed_type", "profile_posts");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_tweets_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(tweets_scroll), g_profile_tweets_list);
    g_signal_connect(tweets_scroll, "edge-reached", G_CALLBACK(on_scroll_edge_reached), NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tweets_scroll, gtk_label_new("Tweets"));

    GtkWidget *replies_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_replies_list = gtk_list_box_new();
    g_object_set_data(G_OBJECT(g_profile_replies_list), "feed_type", "profile_replies");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_replies_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(replies_scroll), g_profile_replies_list);
    g_signal_connect(replies_scroll, "edge-reached", G_CALLBACK(on_scroll_edge_reached), NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), replies_scroll, gtk_label_new("Replies"));

    GtkWidget *followers_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_followers_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_followers_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(followers_scroll), g_followers_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), followers_scroll, gtk_label_new("Followers"));

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

    GtkWidget *users_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_search_users_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_search_users_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(users_scroll), g_search_users_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), users_scroll, gtk_label_new("Users"));

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
    const gchar *p2p_recipient = g_object_get_data(G_OBJECT(g_dm_messages_list), "p2p_recipient");

    /* Check if this is a P2P encrypted conversation */
    if (p2p_recipient && g_p2p_session) {
        /* Send encrypted P2P message */
        g_mutex_lock(&g_p2p_session->session_mutex);
        struct P2PContact *contact = g_hash_table_lookup(g_p2p_session->contacts, p2p_recipient);
        gchar *fingerprint = contact && contact->public_key_fingerprint ? 
            g_strdup(contact->public_key_fingerprint) : NULL;
        g_mutex_unlock(&g_p2p_session->session_mutex);
        
        if (fingerprint && content && strlen(content) > 0) {
            p2p_send_message(p2p_recipient, content, fingerprint);
            gtk_entry_set_text(GTK_ENTRY(g_dm_entry), "");
            
            /* Refresh the messages view */
            on_p2p_contact_clicked(NULL, NULL, NULL);
        }
        g_free(fingerprint);
        return;
    }

    /* Regular tweetapus DM */
    if (content && strlen(content) > 0 && conv_id) {
        gchar *url = g_strdup_printf(DM_SEND_MESSAGE_URL, conv_id);
        gchar *post_data = construct_dm_payload(content);
        struct MemoryStruct chunk = {0};

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
    GtkWidget *notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);

    /* Tab 1: Regular tweetapus conversations */
    GtkWidget *conversations_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_conversations_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_conversations_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(conversations_scroll), g_conversations_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), conversations_scroll,
        gtk_label_new("Tweetapus Conversations"));

    /* Tab 2: Encrypted Messaging */
    GtkWidget *p2p_view = create_p2p_messages_view();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), p2p_view,
        gtk_label_new("Encrypted"));

    return notebook;
}

GtkWidget*
create_p2p_messages_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    /* Action bar with buttons for key management and contacts */
    GtkWidget *action_bar = gtk_action_bar_new();
    
    GtkWidget *setup_keys_btn = gtk_button_new_with_label("Setup Keys");
    g_signal_connect(setup_keys_btn, "clicked", G_CALLBACK(on_p2p_setup_clicked), NULL);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), setup_keys_btn);
    
    GtkWidget *add_contact_btn = gtk_button_new_with_label("Add Contact");
    g_signal_connect(add_contact_btn, "clicked", G_CALLBACK(on_p2p_add_contact_clicked), NULL);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), add_contact_btn);
    
    GtkWidget *import_key_btn = gtk_button_new_with_label("Import Key");
    g_signal_connect(import_key_btn, "clicked", G_CALLBACK(on_p2p_import_contact_clicked), NULL);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), import_key_btn);
    
    /* Transport mode selector */
    GtkWidget *transport_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(transport_combo), "Direct P2P");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(transport_combo), "Relay Mode");
    gtk_combo_box_set_active(GTK_COMBO_BOX(transport_combo), 1);  /* Default to relay for ease of use */
    g_signal_connect(transport_combo, "changed", G_CALLBACK(on_p2p_transport_changed), NULL);
    gtk_action_bar_pack_end(GTK_ACTION_BAR(action_bar), transport_combo);
    
    GtkWidget *transport_label = gtk_label_new("Transport:");
    gtk_action_bar_pack_end(GTK_ACTION_BAR(action_bar), transport_label);
    
    gtk_box_pack_start(GTK_BOX(box), action_bar, FALSE, FALSE, 0);
    
    /* Header - similar to DM interface */
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(header_box), 10);
    g_p2p_title_label = gtk_label_new("Select a contact to start encrypted messaging");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(g_p2p_title_label), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(header_box), g_p2p_title_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), header_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    /* Split view: contacts list and messages */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    
    /* Contacts list on the left */
    GtkWidget *contacts_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(contacts_scroll, 250, -1);
    g_p2p_contacts_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_p2p_contacts_list), GTK_SELECTION_SINGLE);
    g_signal_connect(g_p2p_contacts_list, "row-selected", G_CALLBACK(on_p2p_contact_selected), NULL);
    gtk_container_add(GTK_CONTAINER(contacts_scroll), g_p2p_contacts_list);
    gtk_paned_pack1(GTK_PANED(paned), contacts_scroll, FALSE, TRUE);
    
    /* Messages area on the right */
    GtkWidget *messages_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    g_p2p_messages_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_p2p_messages_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_p2p_messages_list);
    gtk_box_pack_start(GTK_BOX(messages_box), scroll, TRUE, TRUE, 0);
    
    /* Input area - similar to DM interface */
    GtkWidget *input_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(input_hbox), 5);
    g_p2p_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_p2p_entry), "Type encrypted message...");
    g_signal_connect(g_p2p_entry, "activate", G_CALLBACK(on_p2p_send_clicked), NULL);
    
    GtkWidget *send_btn = gtk_button_new_with_label("Send");
    g_signal_connect(send_btn, "clicked", G_CALLBACK(on_p2p_send_clicked), NULL);
    
    gtk_box_pack_start(GTK_BOX(input_hbox), g_p2p_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(input_hbox), send_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(messages_box), input_hbox, FALSE, FALSE, 0);
    
    gtk_paned_pack2(GTK_PANED(paned), messages_box, TRUE, TRUE);
    gtk_box_pack_start(GTK_BOX(box), paned, TRUE, TRUE, 0);

    return box;
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
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(box), 20);

    GtkWidget *title = gtk_label_new("Settings");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_scale_new(1.5));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 10);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(content_box), 10);

    // Appearance Section
    GtkWidget *appearance_frame = gtk_frame_new("Appearance");
    GtkWidget *appearance_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(appearance_box), 10);

    GtkWidget *theme_label = gtk_label_new("Theme:");
    gtk_widget_set_halign(theme_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(appearance_box), theme_label, FALSE, FALSE, 0);

    g_theme_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_theme_combo), "Light");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_theme_combo), "Dark");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_theme_combo), "System Default");
    gtk_combo_box_set_active(GTK_COMBO_BOX(g_theme_combo), 2);
    g_signal_connect(g_theme_combo, "changed", G_CALLBACK(on_theme_changed), NULL);
    gtk_box_pack_start(GTK_BOX(appearance_box), g_theme_combo, FALSE, FALSE, 0);

    g_compact_mode_switch = gtk_switch_new();
    GtkWidget *compact_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *compact_label = gtk_label_new("Compact mode");
    gtk_box_pack_start(GTK_BOX(compact_row), compact_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(compact_row), g_compact_mode_switch, FALSE, FALSE, 0);
    g_signal_connect(g_compact_mode_switch, "state-set", G_CALLBACK(on_compact_mode_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(appearance_box), compact_row, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(appearance_frame), appearance_box);
    gtk_box_pack_start(GTK_BOX(content_box), appearance_frame, FALSE, FALSE, 0);

    // Notifications Section
    GtkWidget *notifications_frame = gtk_frame_new("Notifications");
    GtkWidget *notifications_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(notifications_box), 10);

    g_enable_notifications_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(g_enable_notifications_switch), TRUE);
    GtkWidget *enable_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *enable_label = gtk_label_new("Enable notifications");
    gtk_box_pack_start(GTK_BOX(enable_row), enable_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(enable_row), g_enable_notifications_switch, FALSE, FALSE, 0);
    g_signal_connect(g_enable_notifications_switch, "state-set", G_CALLBACK(on_notifications_enabled_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(notifications_box), enable_row, FALSE, FALSE, 0);

    g_sound_notifications_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(g_sound_notifications_switch), TRUE);
    GtkWidget *sound_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *sound_label = gtk_label_new("Sound effects");
    gtk_box_pack_start(GTK_BOX(sound_row), sound_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(sound_row), g_sound_notifications_switch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(notifications_box), sound_row, FALSE, FALSE, 0);

    g_dm_notifications_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(g_dm_notifications_switch), TRUE);
    GtkWidget *dm_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *dm_label = gtk_label_new("Direct message notifications");
    gtk_box_pack_start(GTK_BOX(dm_row), dm_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(dm_row), g_dm_notifications_switch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(notifications_box), dm_row, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(notifications_frame), notifications_box);
    gtk_box_pack_start(GTK_BOX(content_box), notifications_frame, FALSE, FALSE, 0);

    // Data & Cache Section
    GtkWidget *data_frame = gtk_frame_new("Data & Cache");
    GtkWidget *data_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(data_box), 10);

    GtkWidget *cache_size_label = gtk_label_new("Cache size: Calculating...");
    gtk_widget_set_halign(cache_size_label, GTK_ALIGN_START);
    g_cache_size_label = cache_size_label;
    gtk_box_pack_start(GTK_BOX(data_box), cache_size_label, FALSE, FALSE, 0);

    GtkWidget *clear_cache_btn = gtk_button_new_with_label("Clear Cache");
    g_signal_connect(clear_cache_btn, "clicked", G_CALLBACK(on_clear_cache_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(data_box), clear_cache_btn, FALSE, FALSE, 0);

    GtkWidget *clear_history_btn = gtk_button_new_with_label("Clear Search History");
    g_signal_connect(clear_history_btn, "clicked", G_CALLBACK(on_clear_history_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(data_box), clear_history_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(data_frame), data_box);
    gtk_box_pack_start(GTK_BOX(content_box), data_frame, FALSE, FALSE, 0);

    // Account Section
    GtkWidget *account_frame = gtk_frame_new("Account");
    GtkWidget *account_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(account_box), 10);

    g_settings_username_label = gtk_label_new("Not logged in");
    gtk_widget_set_halign(g_settings_username_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(account_box), g_settings_username_label, FALSE, FALSE, 0);

    GtkWidget *change_pw_btn = gtk_button_new_with_label("Change Password");
    g_signal_connect(change_pw_btn, "clicked", G_CALLBACK(on_change_password_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), change_pw_btn, FALSE, FALSE, 0);

    GtkWidget *logout_btn = gtk_button_new_with_label("Logout");
    gtk_widget_set_name(logout_btn, "logout_button");
    g_signal_connect(logout_btn, "clicked", G_CALLBACK(on_logout_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), logout_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(account_frame), account_box);
    gtk_box_pack_start(GTK_BOX(content_box), account_frame, FALSE, FALSE, 0);

    // About Section
    GtkWidget *about_frame = gtk_frame_new("About");
    GtkWidget *about_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(about_box), 10);

    GtkWidget *app_name = gtk_label_new("Tweeta Desktop");
    PangoAttrList *app_attrs = pango_attr_list_new();
    pango_attr_list_insert(app_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(app_name), app_attrs);
    pango_attr_list_unref(app_attrs);
    gtk_widget_set_halign(app_name, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(about_box), app_name, FALSE, FALSE, 0);

    GtkWidget *version_label = gtk_label_new("Version 1.0.0");
    gtk_widget_set_halign(version_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(about_box), version_label, FALSE, FALSE, 0);

    GtkWidget *agpl_label = gtk_label_new("Licensed under AGPLv3");
    gtk_widget_set_halign(agpl_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(about_box), agpl_label, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(about_frame), about_box);
    gtk_box_pack_start(GTK_BOX(content_box), about_frame, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(scroll), content_box);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

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
    GtkWidget *communities_button = gtk_button_new_from_icon_name("system-users-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(communities_button, "clicked", G_CALLBACK(on_communities_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), communities_button);

    // Admin Button (Left)
    g_admin_button = gtk_button_new_from_icon_name("dialog-password-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_no_show_all(g_admin_button, TRUE);
    gtk_widget_hide(g_admin_button);
    g_signal_connect(g_admin_button, "clicked", G_CALLBACK(on_admin_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), g_admin_button);

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

    GtkWidget *profile_view = create_profile_view();
    gtk_stack_add_named(GTK_STACK(g_stack), profile_view, "profile");

    GtkWidget *search_view = create_search_view();
    gtk_stack_add_named(GTK_STACK(g_stack), search_view, "search");

    GtkWidget *notifications_view = create_notifications_view();
    gtk_stack_add_named(GTK_STACK(g_stack), notifications_view, "notifications");

    GtkWidget *messages_view = create_messages_view();
    gtk_stack_add_named(GTK_STACK(g_stack), messages_view, "messages");

    GtkWidget *dm_messages_view = create_dm_messages_view();
    gtk_stack_add_named(GTK_STACK(g_stack), dm_messages_view, "dm_messages");

    GtkWidget *conversation_view = create_conversation_view();
    gtk_stack_add_named(GTK_STACK(g_stack), conversation_view, "conversation");

    GtkWidget *settings_view = create_settings_view();
    gtk_stack_add_named(GTK_STACK(g_stack), settings_view, "settings");

    GtkWidget *admin_view = create_admin_view();
    gtk_stack_add_named(GTK_STACK(g_stack), admin_view, "admin");

    GtkWidget *bookmarks_view = create_bookmarks_view();
    gtk_stack_add_named(GTK_STACK(g_stack), bookmarks_view, "bookmarks");

    GtkWidget *communities_view = create_communities_view();
    gtk_stack_add_named(GTK_STACK(g_stack), communities_view, "communities");

    GtkWidget *community_tweets_view = create_community_tweets_view();
    gtk_stack_add_named(GTK_STACK(g_stack), community_tweets_view, "community_tweets");

    return window;
}

