/*
 * actions_p2p_network.c - P2P Network Action Handlers
 * 
 * Additional P2P action handlers for transport configuration.
 * 
 * (c)2025 Lily
 * Licensed under the AGPLv3 license
 */

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "actions.h"
#include "p2p_network.h"
#include "globals.h"

void
on_p2p_transport_changed(GtkComboBox *combo, gpointer user_data)
{
    (void)user_data;
    gint active = gtk_combo_box_get_active(combo);
    
    if (active == 0) {
        p2p_set_transport_mode(P2P_TRANSPORT_DIRECT);
        g_debug("P2P transport set to DIRECT mode");
    } else {
        p2p_set_transport_mode(P2P_TRANSPORT_TWEETAPUS);
        g_debug("P2P transport set to TWEETAPUS mode");
    }
}

void
on_p2p_start_listener_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    GtkWidget *main_box = GTK_WIDGET(user_data);
    
    GtkWidget *host_entry = g_object_get_data(G_OBJECT(main_box), "listen_host_entry");
    GtkWidget *port_entry = g_object_get_data(G_OBJECT(main_box), "listen_port_entry");
    GtkWidget *status_label = g_object_get_data(G_OBJECT(main_box), "listener_status_label");
    
    if (!host_entry || !port_entry || !status_label) return;
    
    const gchar *host = gtk_entry_get_text(GTK_ENTRY(host_entry));
    const gchar *port_str = gtk_entry_get_text(GTK_ENTRY(port_entry));
    
    guint16 port = (guint16)atoi(port_str);
    if (port == 0) port = 0;  /* Let system assign */
    
    if (p2p_is_listener_running()) {
        /* Stop listener */
        p2p_stop_listener();
        gtk_button_set_label(GTK_BUTTON(widget), "Start Listener");
        gtk_label_set_text(GTK_LABEL(status_label), "Listener: Stopped");
    } else {
        /* Start listener */
        if (p2p_start_listener(host, port)) {
            gchar *addr = p2p_get_listen_address();
            gchar *status = g_strdup_printf("Listener: %s", addr ? addr : "running");
            gtk_label_set_text(GTK_LABEL(status_label), status);
            gtk_button_set_label(GTK_BUTTON(widget), "Stop Listener");
            g_free(status);
            g_free(addr);
            
            /* Also start message polling for tweetapus transport */
            p2p_start_message_polling();
        } else {
            GtkWidget *dialog = gtk_message_dialog_new(NULL,
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Failed to start P2P listener on %s:%d", host, port);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }
    }
}

void
on_p2p_connect_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    GtkWidget *main_box = GTK_WIDGET(user_data);
    
    GtkWidget *host_entry = g_object_get_data(G_OBJECT(main_box), "peer_host_entry");
    GtkWidget *port_entry = g_object_get_data(G_OBJECT(main_box), "peer_port_entry");
    GtkWidget *user_entry = g_object_get_data(G_OBJECT(main_box), "peer_user_entry");
    
    if (!host_entry || !port_entry || !user_entry) return;
    
    const gchar *host = gtk_entry_get_text(GTK_ENTRY(host_entry));
    const gchar *port_str = gtk_entry_get_text(GTK_ENTRY(port_entry));
    const gchar *username = gtk_entry_get_text(GTK_ENTRY(user_entry));
    
    if (!host || strlen(host) == 0 || !username || strlen(username) == 0) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL,
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_CLOSE,
            "Please enter host and username");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    guint16 port = (guint16)atoi(port_str);
    if (port == 0) port = 9735;  /* Default P2P port */
    
    int sock = p2p_connect_to_peer(host, port, username);
    if (sock < 0) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL,
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            "Failed to connect to %s@%s:%d", username, host, port);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    } else {
        /* Connected successfully - refresh contacts list */
        p2p_refresh_contacts_list();
        
        GtkWidget *dialog = gtk_message_dialog_new(NULL,
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_CLOSE,
            "Connected to %s@%s:%d", username, host, port);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void
on_p2p_add_contact_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    
    P2PTransportMode mode = p2p_get_transport_mode();
    gboolean is_direct = (mode == P2P_TRANSPORT_DIRECT);
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Contact",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Add", GTK_RESPONSE_ACCEPT,
        NULL);
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);
    
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    
    /* Username */
    GtkWidget *user_label = gtk_label_new("Username:");
    gtk_grid_attach(GTK_GRID(grid), user_label, 0, 0, 1, 1);
    
    GtkWidget *user_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), user_entry, 1, 0, 1, 1);
    
    /* Public Key Fingerprint */
    GtkWidget *fp_label = gtk_label_new("Key Fingerprint:");
    gtk_grid_attach(GTK_GRID(grid), fp_label, 0, 1, 1, 1);
    
    GtkWidget *fp_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(fp_entry), "40 character hex fingerprint");
    gtk_grid_attach(GTK_GRID(grid), fp_entry, 1, 1, 1, 1);
    
    /* Info label about key exchange with info icon */
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    GtkWidget *info_label = gtk_label_new(
        "Public keys should be exchanged outside of this application");
    gtk_label_set_line_wrap(GTK_LABEL(info_label), TRUE);
    PangoAttrList *small_attrs = pango_attr_list_new();
    pango_attr_list_insert(small_attrs, pango_attr_scale_new(0.85));
    gtk_label_set_attributes(GTK_LABEL(info_label), small_attrs);
    pango_attr_list_unref(small_attrs);
    gtk_box_pack_start(GTK_BOX(info_box), info_label, FALSE, FALSE, 0);
    
    /* Info icon with tooltip - using event box for better tooltip support */
    GtkWidget *info_event = gtk_event_box_new();
    GtkWidget *info_icon = gtk_image_new_from_icon_name("dialog-information-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(info_event), info_icon);
    
    /* Set tooltip on the event box which will catch hover events */
    gtk_widget_set_has_tooltip(info_event, TRUE);
    gchar *tooltip_text = g_strdup(
        "Why should keys be exchanged outside the app?\n\n"
        "For encrypted messaging to be secure, you need to verify\n"
        "that you are encrypting to the correct person's key.\n\n"
        "If an attacker can replace a public key with their own,\n"
        "they could intercept and read your messages.\n\n"
        "Best practices for key exchange:\n"
        "• Meet in person and verify the key fingerprint\n"
        "• Exchange via another trusted channel you already use\n"
        "• Verify fingerprints through a side channel (phone, etc.)\n"
        "• Never trust a key received through the same channel\n"
        "  you will use for encrypted messages");
    gtk_widget_set_tooltip_text(info_event, tooltip_text);
    g_free(tooltip_text);
    
    gtk_box_pack_start(GTK_BOX(info_box), info_event, FALSE, FALSE, 0);
    
    gtk_grid_attach(GTK_GRID(grid), info_box, 0, 2, 2, 1);
    
    /* Host:Port for Direct P2P only */
    GtkWidget *host_label = NULL;
    GtkWidget *host_entry = NULL;
    GtkWidget *port_label = NULL;
    GtkWidget *port_entry = NULL;
    
    if (is_direct) {
        host_label = gtk_label_new("Host:");
        gtk_grid_attach(GTK_GRID(grid), host_label, 0, 3, 1, 1);
        
        host_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(host_entry), "IP or hostname");
        gtk_grid_attach(GTK_GRID(grid), host_entry, 1, 3, 1, 1);
        
        port_label = gtk_label_new("Port:");
        gtk_grid_attach(GTK_GRID(grid), port_label, 0, 4, 1, 1);
        
        port_entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(port_entry), "9735");
        gtk_grid_attach(GTK_GRID(grid), port_entry, 1, 4, 1, 1);
    }
    
    gtk_widget_show_all(grid);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        const gchar *username = gtk_entry_get_text(GTK_ENTRY(user_entry));
        const gchar *fingerprint = gtk_entry_get_text(GTK_ENTRY(fp_entry));
        
        if (username && strlen(username) > 0 && fingerprint && strlen(fingerprint) > 0) {
            /* Add contact to session */
            if (g_p2p_session) {
                struct P2PContact *contact = g_new0(struct P2PContact, 1);
                contact->username = g_strdup(username);
                contact->public_key_fingerprint = g_strdup(fingerprint);
                contact->display_name = g_strdup(username);
                
                if (is_direct && host_entry && port_entry) {
                    const gchar *host = gtk_entry_get_text(GTK_ENTRY(host_entry));
                    const gchar *port_str = gtk_entry_get_text(GTK_ENTRY(port_entry));
                    
                    if (host && strlen(host) > 0) {
                        contact->direct_host = g_strdup(host);
                        contact->direct_port = (guint16)atoi(port_str);
                    }
                }
                
                g_mutex_lock(&g_p2p_session->session_mutex);
                g_hash_table_insert(g_p2p_session->contacts, g_strdup(username), contact);
                g_mutex_unlock(&g_p2p_session->session_mutex);
                
                p2p_refresh_contacts_list();
            }
        }
    }
    
    gtk_widget_destroy(dialog);
}

gboolean
on_p2p_contact_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)event;
    (void)user_data;
    
    const gchar *username = g_object_get_data(G_OBJECT(widget), "p2p_contact_username");
    if (!username) return FALSE;
    
    /* Switch to messages view and show P2P encrypted interface */
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "dm_messages");
    gtk_widget_show(g_back_button);
    
    /* Update the title to show this is an encrypted conversation */
    gchar *title = g_strdup_printf("@%s (Encrypted)", username);
    gtk_label_set_text(GTK_LABEL(g_dm_title_label), title);
    g_free(title);
    
    /* Store that this is a P2P conversation in the messages list */
    g_object_set_data_full(G_OBJECT(g_dm_messages_list), "p2p_recipient", 
        g_strdup(username), g_free);
    
    /* Clear and reload messages for this contact */
    GList *children = gtk_container_get_children(GTK_CONTAINER(g_dm_messages_list));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
    
    /* Load P2P messages */
    if (g_p2p_session) {
        g_mutex_lock(&g_p2p_session->session_mutex);
        GList *conversation = g_hash_table_lookup(g_p2p_session->conversations, username);
        
        for (GList *l = conversation; l; l = l->next) {
            struct P2PMessage *msg = l->data;
            if (!msg) continue;
            
            GtkWidget *msg_widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_container_set_border_width(GTK_CONTAINER(msg_widget), 5);
            
            /* Style based on incoming/outgoing */
            if (msg->is_outgoing) {
                gtk_widget_set_halign(msg_widget, GTK_ALIGN_END);
            } else {
                gtk_widget_set_halign(msg_widget, GTK_ALIGN_START);
            }
            
            GtkWidget *label = gtk_label_new(msg->plaintext_content);
            gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
            gtk_widget_set_size_request(label, 200, -1);
            gtk_box_pack_start(GTK_BOX(msg_widget), label, FALSE, FALSE, 0);
            
            gtk_list_box_insert(GTK_LIST_BOX(g_dm_messages_list), msg_widget, -1);
            gtk_widget_show_all(msg_widget);
        }
        g_mutex_unlock(&g_p2p_session->session_mutex);
    }
    
    return TRUE;
}
