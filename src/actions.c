#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "actions.h"
#include "constants.h"
#include "globals.h"
#include "json_utils.h"
#include "network.h"
#include "session.h"
#include "types.h"
#include "ui_components.h"
#include "ui_utils.h"
#include "views.h"
#include "p2p_crypto.h"
#include "p2p_network.h"

static inline gchar* get_username_safe(void) {
    g_mutex_lock(&g_globals_mutex);
    gchar *username = g_current_username ? g_strdup(g_current_username) : NULL;
    g_mutex_unlock(&g_globals_mutex);
    return username;
}

static GMutex load_tweets_mutex;
static guint active_tweets_request_id = 0;

static GMutex load_notifications_mutex;
static guint active_notifications_request_id = 0;
static const gint notifications_page_size = 20;

static GMutex load_conversations_mutex;
static guint active_conversations_request_id = 0;

static GMutex load_messages_mutex;
static guint active_messages_request_id = 0;

static void remove_loading_more_label(GtkListBox *list_box);
static void append_end_of_list_label(GtkListBox *list_box);
static void clear_box_children(GtkWidget *box);
static void append_profile_badge(GtkWidget *box, const gchar *text, const gchar *color);
static void update_profile_badges(const struct Profile *profile);
static gchar* build_profile_status_text(const struct Profile *profile);
static gchar* build_profile_details_text(const struct Profile *profile);
static gchar* build_community_details_text(const struct Community *community);
static gchar* build_dm_conversation_info(const struct Conversation *conversation);
static void update_notifications_button_label(gint unread_count);
static void load_more_notifications(GtkListBox *list_box, const gchar *before_id);

static inline gchar* get_auth_token_safe(void) {
    g_mutex_lock(&g_globals_mutex);
    gchar *token = g_auth_token ? g_strdup(g_auth_token) : NULL;
    g_mutex_unlock(&g_globals_mutex);
    return token;
}

static void show_modal_message(GtkMessageType type, const gchar *primary, const gchar *secondary);
static gboolean cleanup_launcher_file_cb(gpointer data);
static gboolean write_all_to_fd(int fd, const gchar *data, gsize len, GError **error);
static gchar* create_admin_launcher_file(const gchar *html, GError **error);
static gchar* build_full_admin_launcher_html(const gchar *auth_url, const gchar *admin_url);
static gchar* get_runtime_base_domain_for_browser(void);
static gboolean launch_full_admin_panel(GError **error);

static void
show_modal_message(GtkMessageType type, const gchar *primary, const gchar *secondary)
{
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                               GTK_DIALOG_MODAL,
                                               type,
                                               GTK_BUTTONS_CLOSE,
                                               "%s",
                                               primary ? primary : "");
    if (secondary && *secondary) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", secondary);
    }
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static gboolean
cleanup_launcher_file_cb(gpointer data)
{
    gchar *path = (gchar *)data;

    if (path && *path) {
        gchar *dir = g_path_get_dirname(path);
        g_remove(path);
        g_rmdir(dir);
        g_free(dir);
    }
    g_free(path);
    return G_SOURCE_REMOVE;
}

static gboolean
write_all_to_fd(int fd, const gchar *data, gsize len, GError **error)
{
    gsize offset = 0;

    while (offset < len) {
        ssize_t written = write(fd, data + offset, len - offset);
        if (written < 0) {
            g_set_error(error,
                        G_FILE_ERROR,
                        g_file_error_from_errno(errno),
                        "Failed to write admin launcher: %s",
                        g_strerror(errno));
            return FALSE;
        }
        offset += (gsize)written;
    }

    return TRUE;
}

static gchar *
create_admin_launcher_file(const gchar *html, GError **error)
{
    gchar *dir_path;
    gchar *file_path;
    int fd;

    dir_path = g_dir_make_tmp("tweeta-admin-launcher-XXXXXX", error);
    if (!dir_path) {
        return NULL;
    }

    file_path = g_build_filename(dir_path, "index.html", NULL);
    fd = g_open(file_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fd < 0) {
        g_set_error(error,
                    G_FILE_ERROR,
                    g_file_error_from_errno(errno),
                    "Failed to create admin launcher file: %s",
                    g_strerror(errno));
        g_rmdir(dir_path);
        g_free(file_path);
        g_free(dir_path);
        return NULL;
    }

    if (!write_all_to_fd(fd, html, strlen(html), error)) {
        close(fd);
        g_remove(file_path);
        g_rmdir(dir_path);
        g_free(file_path);
        g_free(dir_path);
        return NULL;
    }

    if (close(fd) != 0) {
        g_set_error(error,
                    G_FILE_ERROR,
                    g_file_error_from_errno(errno),
                    "Failed to close admin launcher file: %s",
                    g_strerror(errno));
        g_remove(file_path);
        g_rmdir(dir_path);
        g_free(file_path);
        g_free(dir_path);
        return NULL;
    }

    g_free(dir_path);
    return file_path;
}

static gchar *
build_full_admin_launcher_html(const gchar *auth_url, const gchar *admin_url)
{
    gchar *auth_url_js = g_strescape(auth_url, NULL);
    gchar *admin_url_js = g_strescape(admin_url, NULL);
    gchar *auth_url_html = g_markup_escape_text(auth_url, -1);
    gchar *admin_url_html = g_markup_escape_text(admin_url, -1);
    gchar *html;

    html = g_strdup_printf(
        "<!doctype html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"utf-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "  <title>Open Tweetapus Admin</title>\n"
        "  <style>\n"
        "    body { font-family: sans-serif; background: #0f172a; color: #e2e8f0; margin: 0; padding: 24px; }\n"
        "    main { max-width: 42rem; margin: 0 auto; background: #111827; border-radius: 14px; padding: 24px; box-shadow: 0 20px 45px rgba(0,0,0,0.35); }\n"
        "    h1 { margin-top: 0; font-size: 1.6rem; }\n"
        "    p { line-height: 1.5; }\n"
        "    button, a.button { display: inline-block; border: 0; border-radius: 999px; padding: 12px 18px; background: #2563eb; color: white; text-decoration: none; cursor: pointer; font: inherit; }\n"
        "    button:hover, a.button:hover { background: #1d4ed8; }\n"
        "    .muted { color: #94a3b8; font-size: 0.95rem; }\n"
        "    .manual { margin-top: 18px; padding-top: 18px; border-top: 1px solid rgba(148,163,184,0.2); }\n"
        "    .links a { display: block; margin-top: 10px; color: #93c5fd; word-break: break-all; }\n"
        "    code { background: rgba(148,163,184,0.14); border-radius: 6px; padding: 0.1rem 0.35rem; }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <main>\n"
        "    <h1>Open the full Tweetapus admin panel</h1>\n"
        "    <p>This helper syncs your browser session to the same account used in Tweeta Desktop, then opens <code>/admin</code>.</p>\n"
        "    <p class=\"muted\">If your browser blocks the automatic handoff, use the manual links below.</p>\n"
        "    <button id=\"launchBtn\" type=\"button\">Continue</button>\n"
        "    <p id=\"status\" class=\"muted\">Preparing browser handoff...</p>\n"
        "    <div class=\"manual\" id=\"manualSection\" hidden>\n"
        "      <strong>Manual fallback</strong>\n"
        "      <div class=\"links\">\n"
        "        <a href=\"%s\" target=\"_blank\">1. Sync browser session with desktop token</a>\n"
        "        <a href=\"%s\" target=\"_blank\">2. Open the admin panel</a>\n"
        "      </div>\n"
        "    </div>\n"
        "  </main>\n"
        "  <script>\n"
        "    (function () {\n"
        "      const authUrl = '%s';\n"
        "      const adminUrl = '%s';\n"
        "      const statusEl = document.getElementById('status');\n"
        "      const manualEl = document.getElementById('manualSection');\n"
        "      const launchBtn = document.getElementById('launchBtn');\n"
        "      let launched = false;\n"
        "      function setStatus(message) { statusEl.textContent = message; }\n"
        "      function showManual() { manualEl.hidden = false; }\n"
        "      function launch() {\n"
        "        let popup;\n"
        "        if (launched) return;\n"
        "        launched = true;\n"
        "        showManual();\n"
        "        setStatus('Opening browser session handoff...');\n"
        "        popup = window.open(authUrl, 'tweetaAdminPanelBridge');\n"
        "        if (!popup) {\n"
        "          setStatus('Popup blocked. Use the manual links below.');\n"
        "          return;\n"
        "        }\n"
        "        window.setTimeout(function () {\n"
        "          try {\n"
        "            popup.location = adminUrl;\n"
        "            setStatus('Admin panel opened in a new tab or window.');\n"
        "          } catch (_err) {\n"
        "            setStatus('Session handoff finished. If the admin panel did not open, use the manual links below.');\n"
        "          }\n"
        "        }, 2200);\n"
        "        window.setTimeout(function () {\n"
        "          try { window.close(); } catch (_err) {}\n"
        "        }, 4000);\n"
        "      }\n"
        "      launchBtn.addEventListener('click', launch);\n"
        "      window.addEventListener('load', function () {\n"
        "        window.setTimeout(showManual, 700);\n"
        "        launch();\n"
        "      });\n"
        "    }());\n"
        "  </script>\n"
        "</body>\n"
        "</html>\n",
        auth_url_html,
        admin_url_html,
        auth_url_js,
        admin_url_js
    );

    g_free(auth_url_js);
    g_free(admin_url_js);
    g_free(auth_url_html);
    g_free(admin_url_html);

    return html;
}

static gchar *
get_runtime_base_domain_for_browser(void)
{
    const gchar *base_domain = g_getenv("TWEETA_BASE_DOMAIN");
    const gchar *api_base = g_getenv("TWEETA_API_BASE_URL");

    if (base_domain && *base_domain) {
        return g_strdup(base_domain);
    }

    if (api_base && *api_base) {
        if (g_str_has_suffix(api_base, "/api")) {
            return g_strndup(api_base, strlen(api_base) - 4);
        }
        if (g_str_has_suffix(api_base, "/api/")) {
            return g_strndup(api_base, strlen(api_base) - 5);
        }
    }

    return g_strdup(BASE_DOMAIN);
}

static gboolean
launch_full_admin_panel(GError **error)
{
    gchar *token = get_auth_token_safe();
    gchar *base_domain = NULL;
    gchar *escaped_token = NULL;
    gchar *auth_url = NULL;
    gchar *admin_url = NULL;
    gchar *html = NULL;
    gchar *path = NULL;
    gchar *uri = NULL;
    gboolean success = FALSE;

    if (!token || !*token) {
        g_set_error_literal(error,
                            G_IO_ERROR,
                            G_IO_ERROR_PERMISSION_DENIED,
                            "You must be logged in before opening the full admin panel.");
        g_free(token);
        return FALSE;
    }

    base_domain = get_runtime_base_domain_for_browser();
    escaped_token = g_uri_escape_string(token, NULL, FALSE);
    auth_url = g_strdup_printf("%s/?impersonate=%s", base_domain, escaped_token);
    admin_url = g_strdup_printf("%s/admin", base_domain);
    html = build_full_admin_launcher_html(auth_url, admin_url);
    path = create_admin_launcher_file(html, error);
    if (!path) {
        goto cleanup;
    }

    uri = g_filename_to_uri(path, NULL, error);
    if (!uri) {
        g_remove(path);
        goto cleanup;
    }

    if (!gtk_show_uri_on_window(NULL, uri, GDK_CURRENT_TIME, error)) {
        g_remove(path);
        goto cleanup;
    }

    g_timeout_add_seconds(120, cleanup_launcher_file_cb, g_strdup(path));
    success = TRUE;

cleanup:
    g_free(uri);
    g_free(path);
    g_free(html);
    g_free(admin_url);
    g_free(auth_url);
    g_free(escaped_token);
    g_free(base_domain);
    g_free(token);
    return success;
}

static gboolean
response_has_success_flag(const gchar *json_data, const gchar *state_key, gboolean *state_value)
{
    JsonParser *parser;
    JsonNode *root;
    JsonObject *obj;
    gboolean success = FALSE;
    GError *error = NULL;

    if (state_value) {
        *state_value = FALSE;
    }

    if (!json_data) {
        return FALSE;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) {
            g_error_free(error);
        }
        g_object_unref(parser);
        return FALSE;
    }

    root = json_parser_get_root(parser);
    if (root && JSON_NODE_HOLDS_OBJECT(root)) {
        obj = json_node_get_object(root);
        if (json_object_has_member(obj, "success")) {
            success = json_object_get_boolean_member(obj, "success");
        }
        if (state_key && state_value && json_object_has_member(obj, state_key)) {
            *state_value = json_object_get_boolean_member(obj, state_key);
        }
    }

    g_object_unref(parser);
    return success;
}

static gboolean
perform_request_with_optional_payload(const gchar *url,
                                      const gchar *payload,
                                      const gchar *method,
                                      gchar **response_out)
{
    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;

    if (response_out) {
        *response_out = NULL;
    }

    if (fetch_url(url, &chunk, payload, method)) {
        success = (chunk.memory && strstr(chunk.memory, "\"error\"") == NULL);
        if (response_out) {
            *response_out = chunk.memory;
            chunk.memory = NULL;
        }
        g_free(chunk.memory);
    }

    return success;
}

static gchar *
perform_simple_json_request(const gchar *url, const gchar *method, const gchar *payload)
{
    gchar *response = NULL;

    if (!perform_request_with_optional_payload(url, payload, method, &response)) {
        g_free(response);
        return NULL;
    }

    return response;
}

/* Memory Management Helpers */

void
p2p_free_contact(gpointer data)
{
    struct P2PContact *contact = (struct P2PContact *)data;
    if (contact) {
        g_free(contact->username);
        g_free(contact->display_name);
        g_free(contact->public_key_fingerprint);
        g_free(contact->public_key_armor);
        g_free(contact->avatar_url);
        g_free(contact->direct_host);
        g_free(contact->last_seen);
        g_free(contact);
    }
}

void
p2p_free_message(gpointer data)
{
    struct P2PMessage *msg = (struct P2PMessage *)data;
    if (msg) {
        g_free(msg->id);
        g_free(msg->sender_username);
        g_free(msg->recipient_username);
        g_free(msg->encrypted_content);
        g_free(msg->plaintext_content);
        g_free(msg->timestamp);
        g_free(msg);
    }
}

static void
p2p_free_message_list(gpointer data)
{
    g_list_free_full((GList *)data, p2p_free_message);
}

void
p2p_free_session(struct P2PSession *session)
{
    if (!session) return;

    g_mutex_lock(&session->session_mutex);

    g_free(session->local_username);
    g_free(session->local_key_fingerprint);

    if (session->contacts) {
        g_hash_table_destroy(session->contacts);
    }

    if (session->conversations) {
        g_hash_table_destroy(session->conversations);
    }

    g_mutex_unlock(&session->session_mutex);
    g_mutex_clear(&session->session_mutex);

    g_free(session);
}

static void
free_async_data(struct AsyncData *data)
{
    if (!data) return;

    if (data->tweets) free_tweets(data->tweets);
    if (data->users) free_users(data->users);
    if (data->notifications) free_notifications(data->notifications);
    if (data->conversations) free_conversations(data->conversations);
    if (data->messages) free_messages(data->messages);
    if (data->communities) free_communities(data->communities);
    if (data->conversation) free_conversation(data->conversation);

    if (data->profile) {
        free_user(data->profile);
    }

    g_free(data->username);
    g_free(data->query);
    g_free(data->conversation_id);
    g_free(data->community_id);
    g_free(data->before_id);

    g_free(data);
}

/* P2P Encrypted Messaging Implementation */

static gchar *g_p2p_current_contact = NULL;
static GMutex g_p2p_mutex;

void
on_p2p_send_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if (!g_p2p_current_contact) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL,
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_CLOSE,
            "Please select a contact first.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    const gchar *plaintext = gtk_entry_get_text(GTK_ENTRY(g_p2p_entry));
    if (!plaintext || strlen(plaintext) == 0) {
        return;
    }

    p2p_send_encrypted_message(g_p2p_current_contact, plaintext);
    gtk_entry_set_text(GTK_ENTRY(g_p2p_entry), "");
}

void
on_p2p_setup_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("P2P Encryption Setup",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Generate Keys", GTK_RESPONSE_ACCEPT,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);

    GtkWidget *info_label = gtk_label_new(
        "This will generate a new GPG key pair for P2P encrypted messaging.\n"
        "Your private key will be stored locally.\n"
        "Your public key can be shared with contacts to enable encrypted communication.");
    gtk_label_set_line_wrap(GTK_LABEL(info_label), TRUE);
    gtk_box_pack_start(GTK_BOX(content), info_label, FALSE, FALSE, 10);

    GtkWidget *passphrase_label = gtk_label_new("Passphrase (optional):");
    gtk_box_pack_start(GTK_BOX(content), passphrase_label, FALSE, FALSE, 5);

    GtkWidget *passphrase_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(passphrase_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(content), passphrase_entry, FALSE, FALSE, 5);

    gtk_widget_show_all(content);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        g_mutex_lock(&g_globals_mutex);
        gchar *username = g_current_username ? g_strdup(g_current_username) : NULL;
        g_mutex_unlock(&g_globals_mutex);

        if (username) {
            gchar *email = g_strdup_printf("%s@tweetapus.local", username);
            const gchar *passphrase = gtk_entry_get_text(GTK_ENTRY(passphrase_entry));

            gchar *fingerprint = p2p_generate_keypair(username, email,
                passphrase && strlen(passphrase) > 0 ? passphrase : NULL);

            if (fingerprint) {
                gchar *status = g_strdup_printf("Key: %s", fingerprint);
                gtk_label_set_text(GTK_LABEL(g_p2p_status_label), status);
                g_free(status);

                /* Export and show public key */
                gchar *public_key = p2p_export_public_key(fingerprint);
                if (public_key) {
                    GtkWidget *key_dialog = gtk_dialog_new_with_buttons("Your Public Key",
                        GTK_WINDOW(dialog),
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        "_Close", GTK_RESPONSE_CLOSE,
                        NULL);
                    GtkWidget *key_content = gtk_dialog_get_content_area(GTK_DIALOG(key_dialog));
                    GtkWidget *key_view = gtk_text_view_new();
                    gtk_text_view_set_editable(GTK_TEXT_VIEW(key_view), FALSE);
                    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(key_view));
                    gtk_text_buffer_set_text(buffer, public_key, -1);
                    GtkWidget *key_scroll = gtk_scrolled_window_new(NULL, NULL);
                    gtk_widget_set_size_request(key_scroll, 500, 300);
                    gtk_container_add(GTK_CONTAINER(key_scroll), key_view);
                    gtk_box_pack_start(GTK_BOX(key_content), key_scroll, TRUE, TRUE, 0);
                    gtk_widget_show_all(key_content);
                    gtk_dialog_run(GTK_DIALOG(key_dialog));
                    gtk_widget_destroy(key_dialog);
                    g_free(public_key);
                }
                g_free(fingerprint);
            } else {
                GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(dialog),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_CLOSE,
                    "Failed to generate key pair.");
                gtk_dialog_run(GTK_DIALOG(error));
                gtk_widget_destroy(error);
            }
            g_free(email);
        } else {
            GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(dialog),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "You must be logged in to generate keys.");
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);
        }
        g_free(username);
    }

    gtk_widget_destroy(dialog);
}

void
on_p2p_contact_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    (void)user_data;

    if (!row) return;

    GtkWidget *child = gtk_bin_get_child(GTK_BIN(row));
    const gchar *username = g_object_get_data(G_OBJECT(child), "contact_username");

    g_mutex_lock(&g_p2p_mutex);
    g_free(g_p2p_current_contact);
    g_p2p_current_contact = username ? g_strdup(username) : NULL;
    g_mutex_unlock(&g_p2p_mutex);

    if (username) {
        const gchar *display_name = g_object_get_data(G_OBJECT(child), "contact_name");
        gchar *title = g_strdup_printf("P2P: %s", display_name ? display_name : username);
        gtk_label_set_text(GTK_LABEL(g_p2p_title_label), title);
        g_free(title);

        p2p_refresh_messages_list(username);
    }
}


void
on_p2p_generate_keys_clicked(GtkWidget *widget, gpointer user_data)
{
    on_p2p_setup_clicked(widget, user_data);
}

void
on_p2p_import_contact_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Import Contact",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Import", GTK_RESPONSE_ACCEPT,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);

    GtkWidget *username_label = gtk_label_new("Contact Username:");
    gtk_box_pack_start(GTK_BOX(content), username_label, FALSE, FALSE, 5);

    GtkWidget *username_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(content), username_entry, FALSE, FALSE, 5);

    GtkWidget *key_label = gtk_label_new("Public Key (armored):");
    gtk_box_pack_start(GTK_BOX(content), key_label, FALSE, FALSE, 5);

    GtkWidget *key_view = gtk_text_view_new();
    gtk_widget_set_size_request(key_view, 400, 200);
    GtkWidget *key_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(key_scroll), key_view);
    gtk_box_pack_start(GTK_BOX(content), key_scroll, TRUE, TRUE, 5);

    gtk_widget_show_all(content);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        const gchar *username = gtk_entry_get_text(GTK_ENTRY(username_entry));
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(key_view));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar *key_armor = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        if (username && strlen(username) > 0 && key_armor && strlen(key_armor) > 0) {
            /* Import the key */
            if (p2p_import_public_key(key_armor, NULL)) {
                /* Add to contacts */
                struct P2PContact *contact = g_new0(struct P2PContact, 1);
                contact->username = g_strdup(username);
                contact->public_key_armor = g_strdup(key_armor);
                contact->display_name = g_strdup(username);

                if (g_p2p_session) {
                    g_mutex_lock(&g_p2p_session->session_mutex);
                    g_hash_table_insert(g_p2p_session->contacts, g_strdup(username), contact);
                    g_mutex_unlock(&g_p2p_session->session_mutex);
                }

                p2p_refresh_contacts_list();
            } else {
                GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(dialog),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_CLOSE,
                    "Failed to import public key.");
                gtk_dialog_run(GTK_DIALOG(error));
                gtk_widget_destroy(error);
            }
        }
        g_free(key_armor);
    }

    gtk_widget_destroy(dialog);
}

gboolean
p2p_init_session(const gchar *username)
{
    if (!username) return FALSE;

    if (g_p2p_session) {
        /* Already initialized */
        return TRUE;
    }

    if (!p2p_crypto_init()) {
        g_warning("Failed to initialize P2P crypto");
        return FALSE;
    }

    struct P2PTransportConfig config = {0};
    config.mode = P2P_TRANSPORT_TWEETAPUS;
    config.local_username = (gchar *)username;
    config.local_key_fingerprint = (gchar *)p2p_get_local_fingerprint();

    if (!p2p_network_init(&config)) {
        g_warning("Failed to initialize P2P network");
        return FALSE;
    }

    g_p2p_session = g_new0(struct P2PSession, 1);
    g_p2p_session->local_username = g_strdup(username);
    g_p2p_session->contacts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, p2p_free_contact);
    g_p2p_session->conversations = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, p2p_free_message_list);
    g_mutex_init(&g_p2p_session->session_mutex);

    return TRUE;
}

void
p2p_send_encrypted_message(const gchar *recipient, const gchar *plaintext)
{
    if (!recipient || !plaintext || !g_p2p_session) return;

    g_mutex_lock(&g_p2p_session->session_mutex);
    struct P2PContact *contact = g_hash_table_lookup(g_p2p_session->contacts, recipient);
    g_mutex_unlock(&g_p2p_session->session_mutex);

    if (!contact || !contact->public_key_fingerprint) {
        g_warning("No public key for recipient: %s", recipient);
        return;
    }

    gchar *encrypted = p2p_encrypt_message(plaintext, contact->public_key_fingerprint);
    if (!encrypted) {
        g_warning("Failed to encrypt message for %s", recipient);
        return;
    }

    /* Store the message locally */
    struct P2PMessage *msg = g_new0(struct P2PMessage, 1);
    msg->id = g_strdup_printf("p2p_%ld", time(NULL));
    msg->sender_username = g_strdup(g_p2p_session->local_username);
    msg->recipient_username = g_strdup(recipient);
    msg->plaintext_content = g_strdup(plaintext);
    msg->encrypted_content = encrypted;
    msg->timestamp = g_strdup("");
    msg->is_outgoing = TRUE;
    msg->is_verified = TRUE;

    /* Add to conversation */
    g_mutex_lock(&g_p2p_session->session_mutex);
    GList *conversation = g_hash_table_lookup(g_p2p_session->conversations, recipient);
    conversation = g_list_append(conversation, msg);
    g_hash_table_insert(g_p2p_session->conversations, g_strdup(recipient), conversation);
    g_mutex_unlock(&g_p2p_session->session_mutex);

    /* Refresh UI */
    p2p_refresh_messages_list(recipient);

    /* TODO: Implement actual P2P transmission via direct connection or out-of-band */
    g_debug("P2P message encrypted and stored for %s", recipient);
}

void
p2p_refresh_contacts_list(void)
{
    if (!g_p2p_contacts_list || !g_p2p_session) return;

    /* Clear existing list */
    GList *children = gtk_container_get_children(GTK_CONTAINER(g_p2p_contacts_list));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    /* Populate with contacts */
    g_mutex_lock(&g_p2p_session->session_mutex);
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, g_p2p_session->contacts);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        struct P2PContact *contact = value;
        if (!contact) continue;

        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(row_box), 10);

        GtkWidget *avatar = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_MENU);
        gtk_widget_set_size_request(avatar, 32, 32);
        gtk_box_pack_start(GTK_BOX(row_box), avatar, FALSE, FALSE, 0);

        GtkWidget *name_label = gtk_label_new(contact->display_name ? contact->display_name : contact->username);
        gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
        gtk_box_pack_start(GTK_BOX(row_box), name_label, TRUE, TRUE, 0);

        GtkWidget *status_dot = gtk_label_new(contact->is_online ? "●" : "○");
        gtk_box_pack_end(GTK_BOX(row_box), status_dot, FALSE, FALSE, 0);

        g_object_set_data_full(G_OBJECT(row_box), "contact_username",
            g_strdup(contact->username), g_free);
        g_object_set_data_full(G_OBJECT(row_box), "contact_name",
            g_strdup(contact->display_name ? contact->display_name : contact->username), g_free);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), row_box);
        gtk_list_box_insert(GTK_LIST_BOX(g_p2p_contacts_list), row, -1);
        gtk_widget_show_all(row);
    }
    g_mutex_unlock(&g_p2p_session->session_mutex);

}

void
p2p_refresh_messages_list(const gchar *contact_username)
{
    if (!g_p2p_messages_list || !g_p2p_session || !contact_username) return;

    /* Clear existing messages */
    GList *children = gtk_container_get_children(GTK_CONTAINER(g_p2p_messages_list));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    /* Get conversation */
    g_mutex_lock(&g_p2p_session->session_mutex);
    GList *conversation = g_hash_table_lookup(g_p2p_session->conversations, contact_username);
    g_mutex_unlock(&g_p2p_session->session_mutex);

    /* Display messages */
    for (GList *l = conversation; l; l = l->next) {
        struct P2PMessage *msg = l->data;
        if (!msg) continue;

        GtkWidget *msg_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(msg_box), 5);

        if (msg->is_outgoing) {
            gtk_widget_set_halign(msg_box, GTK_ALIGN_END);
        } else {
            gtk_widget_set_halign(msg_box, GTK_ALIGN_START);
        }

        GtkWidget *content_label = gtk_label_new(msg->plaintext_content);
        gtk_label_set_line_wrap(GTK_LABEL(content_label), TRUE);
        gtk_widget_set_size_request(content_label, 200, -1);

        GtkStyleContext *ctx = gtk_widget_get_style_context(content_label);
        gtk_style_context_add_class(ctx, msg->is_outgoing ? "message-out" : "message-in");

        gtk_box_pack_start(GTK_BOX(msg_box), content_label, FALSE, FALSE, 0);
        gtk_list_box_insert(GTK_LIST_BOX(g_p2p_messages_list), msg_box, -1);
        gtk_widget_show_all(msg_box);
    }
}

void update_login_ui(void)
{
    gchar *username = get_username_safe();
    gboolean is_admin;
    
    g_mutex_lock(&g_globals_mutex);
    is_admin = g_is_admin;
    g_mutex_unlock(&g_globals_mutex);
    
    if (username) {
        gchar *label_text = g_strdup_printf("Logged in as @%s", username);
        gtk_label_set_text(GTK_LABEL(g_user_label), label_text);
        gtk_widget_set_sensitive(g_compose_button, TRUE);
        if (g_header_auth_button) {
            gtk_button_set_label(GTK_BUTTON(g_header_auth_button), "Logout");
        }
        if (g_settings_auth_button) {
            gtk_button_set_label(GTK_BUTTON(g_settings_auth_button), "Logout");
        }
        if (g_change_password_button) {
            gtk_widget_set_sensitive(g_change_password_button, TRUE);
        }
        if (is_admin) {
            gtk_widget_show(g_admin_button);
        } else {
            gtk_widget_hide(g_admin_button);
        }
        g_free(label_text);
        refresh_notification_badge();
    } else {
        gtk_label_set_text(GTK_LABEL(g_user_label), "Not logged in");
        gtk_widget_set_sensitive(g_compose_button, FALSE);
        if (g_header_auth_button) {
            gtk_button_set_label(GTK_BUTTON(g_header_auth_button), "Login");
        }
        if (g_settings_auth_button) {
            gtk_button_set_label(GTK_BUTTON(g_settings_auth_button), "Login");
        }
        if (g_change_password_button) {
            gtk_widget_set_sensitive(g_change_password_button, FALSE);
        }
        gtk_widget_hide(g_admin_button);
        update_notifications_button_label(0);
    }

    update_settings_username_display();
    g_free(username);
}

void perform_logout(void)
{
    clear_session();
    if (g_active_profile) {
        free_user(g_active_profile);
        g_active_profile = NULL;
    }
    g_mutex_lock(&g_globals_mutex);
    g_free(g_auth_token);
    g_auth_token = NULL;
    g_free(g_current_username);
    g_current_username = NULL;
    g_is_admin = FALSE;
    g_mutex_unlock(&g_globals_mutex);
    update_login_ui();
}

gboolean perform_login(const gchar *username, const gchar *password)
{
    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "username");
    json_builder_add_string_value(builder, username);
    json_builder_set_member_name(builder, "password");
    json_builder_add_string_value(builder, password);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    if (fetch_url(LOGIN_URL, &chunk, post_data, "POST")) {
        gchar *token = NULL;
        gchar *uname = NULL;
        gboolean is_admin = FALSE;
        g_debug("perform_login: fetch_url succeeded, response: %s", chunk.memory ? chunk.memory : "(null)");
        if (parse_login_response(chunk.memory, &token, &uname, &is_admin)) {
            g_debug("perform_login: parsed successfully, token=%s (len=%d)", token ? token : "(null)", token ? (int)strlen(token) : 0);
            g_mutex_lock(&g_globals_mutex);
            g_free(g_auth_token);
            g_auth_token = token;
            g_free(g_current_username);
            g_current_username = uname;
            g_is_admin = is_admin;
            g_mutex_unlock(&g_globals_mutex);
            g_debug("perform_login: token set to g_auth_token");
            
            // The basic-login endpoint doesn't return admin status, so we fetch /auth/me
            struct MemoryStruct me_chunk;
            if (fetch_url(AUTH_ME_URL, &me_chunk, NULL, "GET")) {
                parse_user_me_response(me_chunk.memory, &is_admin);
                g_mutex_lock(&g_globals_mutex);
                g_is_admin = is_admin;
                g_mutex_unlock(&g_globals_mutex);
                g_free(me_chunk.memory);
            }

            save_session(g_auth_token, g_current_username, g_is_admin);
            success = TRUE;
        }
        g_free(chunk.memory);
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);

    return success;
}

void on_login_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkWidget **entries = (GtkWidget **)user_data;
        const gchar *username = gtk_entry_get_text(GTK_ENTRY(entries[0]));
        const gchar *password = gtk_entry_get_text(GTK_ENTRY(entries[1]));

        if (perform_login(username, password)) {
            update_login_ui();
            start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
        } else {
            GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_CLOSE,
                                     "Login failed. Check credentials.");
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
        }
    }
    g_free(user_data);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void on_login_clicked(GtkWidget *widget, gpointer window)
{
    (void)widget;
    if (g_auth_token) {
        perform_logout();
        return;
    }

    if (!window && widget) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
        if (GTK_IS_WINDOW(toplevel)) {
            window = toplevel;
        }
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Login",
                                                    GTK_WINDOW(window),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Login", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    GtkWidget *user_entry = gtk_entry_new();
    GtkWidget *pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Username:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), user_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Password:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), pass_entry, 1, 1, 1, 1);

    gtk_widget_show_all(grid);
    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);

    GtkWidget **entries = g_new(GtkWidget*, 2);
    entries[0] = user_entry;
    entries[1] = pass_entry;

    g_signal_connect(dialog, "response", G_CALLBACK(on_login_response), entries);
    gtk_widget_show(dialog);
}

gboolean perform_post_tweet(const gchar *content, const gchar *reply_to_id, GList *attachments)
{
    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gchar *post_data = construct_tweet_payload(content, reply_to_id, attachments);

    if (fetch_url(POST_TWEET_URL, &chunk, post_data, "POST")) {
        success = TRUE;
        if (chunk.memory) {
            g_free(chunk.memory);
        }
    }
    
    g_free(post_data);
    return success;
}

static void
on_compose_file_selected(GtkFileChooserButton *chooser, gpointer user_data)
{
    struct UploadContext *ctx = (struct UploadContext *)user_data;
    gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));

    if (filename) {
        g_free(ctx->file_path);
        ctx->file_path = filename;

        gchar *basename = g_path_get_basename(filename);
        gchar *label_text = g_strdup_printf("Selected: %s", basename);
        gtk_label_set_text(GTK_LABEL(ctx->file_label), label_text);
        g_free(label_text);
        g_free(basename);

        g_free(ctx->file_type);
        ctx->file_type = detect_mime_type(filename);
    }
}

void on_compose_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    struct UploadContext *ctx = (struct UploadContext *)user_data;
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkWidget *text_view_widget = g_object_get_data(G_OBJECT(dialog), "text_view");
        GtkTextView *text_view = GTK_TEXT_VIEW(text_view_widget);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar *content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        gchar *media_url = NULL;
        gboolean upload_success = TRUE;
        if (ctx->file_path) {
            media_url = perform_media_upload(ctx->file_path);
            if (!media_url) {
                upload_success = FALSE;
            }
        }

        GList *attachments = NULL;
        if (media_url) {
            const gchar *file_type = ctx->file_type ? ctx->file_type : "application/octet-stream";
            attachments = build_attachment_list(media_url, file_type);
        }

        gboolean has_text = FALSE;
        if (content) {
            gchar *trimmed = g_strdup(content);
            g_strstrip(trimmed);
            has_text = (trimmed[0] != '\0');
            g_free(trimmed);
        }
        gboolean has_attachment = (attachments != NULL);

        if (upload_success && (has_text || has_attachment)) {
             if (perform_post_tweet(content ? content : "", NULL, attachments)) {
                start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
             } else {
                GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_CLOSE,
                                         "Failed to post tweet.");
                gtk_dialog_run(GTK_DIALOG(error_dialog));
                gtk_widget_destroy(error_dialog);
             }
        } else if (!upload_success) {
            GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_CLOSE,
                                     "Failed to upload attachment.");
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
        }

        if (attachments) {
            g_list_free_full(attachments, free_attachment_payload);
        }
        g_free(media_url);
        g_free(content);
    }
    g_free(ctx->file_path);
    g_free(ctx->file_type);
    g_free(ctx);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void on_compose_clicked(GtkWidget *widget, gpointer window)
{
    (void)widget;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Compose Tweet",
                                                    GTK_WINDOW(window),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Tweet", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *text_view = gtk_text_view_new();
    gtk_widget_set_size_request(text_view, 300, 150);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);
    gtk_box_pack_start(GTK_BOX(content_area), text_view, TRUE, TRUE, 0);
    g_object_set_data(G_OBJECT(dialog), "text_view", text_view);

    GtkWidget *file_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(file_box, 10);
    gtk_box_pack_start(GTK_BOX(content_area), file_box, FALSE, FALSE, 0);

    GtkWidget *file_chooser = gtk_file_chooser_button_new("Attach File", GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_file_chooser_button_set_title(GTK_FILE_CHOOSER_BUTTON(file_chooser), "Select Attachment");
    gtk_box_pack_start(GTK_BOX(file_box), file_chooser, FALSE, FALSE, 0);

    GtkFileFilter *media_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(media_filter, "Media Files");
    gtk_file_filter_add_mime_type(media_filter, "image/png");
    gtk_file_filter_add_mime_type(media_filter, "image/jpeg");
    gtk_file_filter_add_mime_type(media_filter, "image/gif");
    gtk_file_filter_add_mime_type(media_filter, "image/webp");
    gtk_file_filter_add_mime_type(media_filter, "video/mp4");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_chooser), media_filter);

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All Files");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_chooser), all_filter);

    GtkWidget *file_label = gtk_label_new("No file selected");
    gtk_widget_set_halign(file_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(file_label, 0.6);
    gtk_box_pack_start(GTK_BOX(file_box), file_label, TRUE, TRUE, 0);

    struct UploadContext *ctx = g_new0(struct UploadContext, 1);
    ctx->parent_dialog = dialog;
    ctx->file_label = file_label;

    g_signal_connect(file_chooser, "file-set", G_CALLBACK(on_compose_file_selected), ctx);

    gtk_widget_show_all(dialog);
    g_signal_connect(dialog, "response", G_CALLBACK(on_compose_response), ctx);
}

static gboolean on_tweets_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    g_mutex_lock(&load_tweets_mutex);
    gboolean is_active = (async_data->request_id == active_tweets_request_id);
    g_mutex_unlock(&load_tweets_mutex);
    
    if (!is_active) {
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    
    g_object_set_data(G_OBJECT(async_data->list_box), "loading_more", GINT_TO_POINTER(FALSE));

    if (async_data->success && async_data->tweets) {
        if (async_data->is_append) {
            remove_loading_more_label(async_data->list_box);
            append_tweets_to_list(async_data->list_box, async_data->tweets);
        } else {
            populate_tweet_list(async_data->list_box, async_data->tweets);
        }

        GList *last = g_list_last(async_data->tweets);
        if (last) {
            struct Tweet *last_tweet = (struct Tweet *)last->data;
            g_object_set_data_full(G_OBJECT(async_data->list_box), "last_id", g_strdup(last_tweet->id), g_free);
        } else {
            g_object_set_data(G_OBJECT(async_data->list_box), "last_id", NULL);
        }

        free_tweets(async_data->tweets);
        async_data->tweets = NULL;
    } else if (async_data->success && async_data->is_append) {
        remove_loading_more_label(async_data->list_box);
        g_object_set_data(G_OBJECT(async_data->list_box), "last_id", NULL);
        append_end_of_list_label(async_data->list_box);
    } else {
        if (!async_data->is_append) {
            GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
            for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
                gtk_widget_destroy(GTK_WIDGET(iter->data));
            g_list_free(children);

            GtkWidget *error_label = gtk_label_new("Failed to load tweets.");
            gtk_widget_show(error_label);
            gtk_list_box_insert(async_data->list_box, error_label, -1);
        } else {
            remove_loading_more_label(async_data->list_box);
        }
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static void remove_loading_more_label(GtkListBox *list_box)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    GList *last = g_list_last(children);

    if (last && GTK_IS_LABEL(last->data)) {
        const gchar *text = gtk_label_get_text(GTK_LABEL(last->data));
        if (g_strcmp0(text, "Loading more...") == 0) {
            gtk_widget_destroy(GTK_WIDGET(last->data));
        }
    }

    g_list_free(children);
}

static void append_end_of_list_label(GtkListBox *list_box)
{
    GtkWidget *end_label = gtk_label_new("You've reached the end.");
    gtk_widget_set_halign(end_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(end_label, 12);
    gtk_widget_set_margin_bottom(end_label, 12);
    gtk_widget_set_opacity(end_label, 0.7);
    gtk_widget_show(end_label);
    gtk_list_box_insert(list_box, end_label, -1);
}

static void clear_box_children(GtkWidget *box)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
}

static void append_profile_badge(GtkWidget *box, const gchar *text, const gchar *color)
{
    GtkWidget *badge = gtk_label_new(NULL);
    gchar *markup = g_strdup_printf("<span foreground='white' background='%s' size='small' weight='bold'> %s </span>",
                                    color,
                                    text);
    gtk_label_set_markup(GTK_LABEL(badge), markup);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(box), badge, FALSE, FALSE, 0);
}

static void update_profile_badges(const struct Profile *profile)
{
    if (!g_profile_badges_box) {
        return;
    }

    clear_box_children(g_profile_badges_box);

    if (!profile) {
        return;
    }

    if (profile->author_gold) {
        append_profile_badge(g_profile_badges_box, "Gold", "#c88900");
    } else if (profile->author_gray) {
        append_profile_badge(g_profile_badges_box, "Gray", "#6c757d");
    } else if (profile->author_verified) {
        append_profile_badge(g_profile_badges_box, "Verified", "#1d9bf0");
    }

    if (profile->label_type && profile->label_type[0] != '\0') {
        gchar *label = g_strdup(profile->label_type);
        label[0] = g_ascii_toupper(label[0]);
        append_profile_badge(g_profile_badges_box, label, "#495057");
        g_free(label);
    }

    if (profile->label_automated) {
        append_profile_badge(g_profile_badges_box, "Automated", "#198754");
    }
}

static gchar* build_profile_status_text(const struct Profile *profile)
{
    GString *status = g_string_new(NULL);

    if (!profile) {
        return g_string_free(status, FALSE);
    }

    if (profile->follows_me) {
        g_string_append(status, "Follows you");
    }
    if (profile->blocked_by_profile) {
        if (status->len > 0) {
            g_string_append(status, " | ");
        }
        g_string_append(status, "This account has blocked you");
    }
    if (profile->blocked_profile) {
        if (status->len > 0) {
            g_string_append(status, " | ");
        }
        g_string_append(status, "You have blocked this account");
    }
    if (profile->notify_tweets) {
        if (status->len > 0) {
            g_string_append(status, " | ");
        }
        g_string_append(status, "Tweet notifications on");
    }

    return g_string_free(status, FALSE);
}

static gchar* build_profile_details_text(const struct Profile *profile)
{
    GString *details = g_string_new(NULL);

    if (!profile) {
        return g_string_free(details, FALSE);
    }

    if (profile->pronouns && profile->pronouns[0] != '\0') {
        g_string_append(details, profile->pronouns);
    }
    if (profile->location && profile->location[0] != '\0') {
        if (details->len > 0) {
            g_string_append(details, " | ");
        }
        g_string_append(details, profile->location);
    }
    if (profile->website && profile->website[0] != '\0') {
        if (details->len > 0) {
            g_string_append(details, " | ");
        }
        g_string_append(details, profile->website);
    }

    return g_string_free(details, FALSE);
}

static gchar* build_community_details_text(const struct Community *community)
{
    GString *details = g_string_new(NULL);

    if (!community) {
        return g_string_free(details, FALSE);
    }

    if (community->access_mode && community->access_mode[0] != '\0') {
        g_string_append(details, community->access_mode);
    }
    if (community->member_count > 0) {
        if (details->len > 0) {
            g_string_append(details, " | ");
        }
        g_string_append_printf(details, "%d member%s",
                               community->member_count,
                               community->member_count == 1 ? "" : "s");
    }
    if (community->description && community->description[0] != '\0') {
        if (details->len > 0) {
            g_string_append(details, " | ");
        }
        g_string_append(details, community->description);
    }

    return g_string_free(details, FALSE);
}

static gchar* build_dm_conversation_info(const struct Conversation *conversation)
{
    GString *info = g_string_new(NULL);

    if (!conversation) {
        return g_string_free(info, FALSE);
    }

    if (conversation->participant_count > 0) {
        g_string_append_printf(info, "%d participant%s",
                               conversation->participant_count,
                               conversation->participant_count == 1 ? "" : "s");
    }

    if (conversation->participants) {
        GString *names = g_string_new(NULL);
        for (GList *l = conversation->participants; l != NULL; l = l->next) {
            struct Profile *participant = l->data;
            const gchar *display = participant->name ? participant->name : participant->username;
            if (!display || display[0] == '\0') {
                continue;
            }
            if (names->len > 0) {
                g_string_append(names, ", ");
            }
            g_string_append(names, display);
        }
        if (names->len > 0) {
            if (info->len > 0) {
                g_string_append(info, " | ");
            }
            g_string_append(info, names->str);
        }
        g_string_free(names, TRUE);
    }

    if (conversation->disappearing_enabled) {
        if (info->len > 0) {
            g_string_append(info, " | ");
        }
        if (conversation->disappearing_duration > 0) {
            g_string_append_printf(info, "Disappearing: %ds", conversation->disappearing_duration);
        } else {
            g_string_append(info, "Disappearing enabled");
        }
    }

    return g_string_free(info, FALSE);
}

static void update_notifications_button_label(gint unread_count)
{
    gchar *label;

    if (!g_notifications_button) {
        return;
    }

    if (!g_auth_token) {
        gtk_button_set_label(GTK_BUTTON(g_notifications_button), "Alerts");
        gtk_widget_set_tooltip_text(g_notifications_button, "Notifications");
        return;
    }

    if (unread_count > 0) {
        label = g_strdup_printf("Alerts (%d)", unread_count);
        gtk_widget_set_tooltip_text(g_notifications_button, label);
    } else {
        label = g_strdup("Alerts");
        gtk_widget_set_tooltip_text(g_notifications_button, "No unread notifications");
    }

    gtk_button_set_label(GTK_BUTTON(g_notifications_button), label);
    g_free(label);
}

static gpointer fetch_tweets_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = NULL;
    gboolean empty_response = FALSE;

    const gchar *feed_type = g_object_get_data(G_OBJECT(async_data->list_box), "feed_type");

    if (g_strcmp0(feed_type, "profile_posts") == 0) {
        if (async_data->before_id) {
            gchar *base_url = g_strdup_printf(PROFILE_POSTS_URL, async_data->username);
            url = g_strdup_printf("%s?before=%s", base_url, async_data->before_id);
            g_free(base_url);
        } else {
            url = g_strdup_printf(PROFILE_POSTS_URL, async_data->username);
        }
    } else if (g_strcmp0(feed_type, "profile_replies") == 0) {
        if (async_data->before_id) {
            gchar *base_url = g_strdup_printf(PROFILE_REPLIES_URL, async_data->username);
            url = g_strdup_printf("%s?before=%s", base_url, async_data->before_id);
            g_free(base_url);
        } else {
            url = g_strdup_printf(PROFILE_REPLIES_URL, async_data->username);
        }
    } else if (g_strcmp0(feed_type, "community") == 0) {
        gchar *community_id;
        g_mutex_lock(&g_globals_mutex);
        community_id = g_community_id ? g_strdup(g_community_id) : NULL;
        g_mutex_unlock(&g_globals_mutex);
        
        if (community_id) {
            gchar *community_url = g_strdup_printf(COMMUNITY_TWEETS_URL, community_id);
            if (async_data->before_id) {
                url = g_strdup_printf("%s?before=%s", community_url, async_data->before_id);
                g_free(community_url);
            } else {
                url = community_url;
            }
            g_free(community_id);
        } else {
            url = NULL;
        }
    } else {
        const gchar *timeline_url = (g_current_timeline_type == TIMELINE_FOLLOWING) ? FOLLOWING_TIMELINE_URL : TIMELINE_URL;
        if (async_data->before_id) {
            url = g_strdup_printf("%s?before=%s", timeline_url, async_data->before_id);
        } else {
            url = g_strdup(timeline_url);
        }
    }

    if (fetch_url(url, &chunk, NULL, "GET")) {
        if (g_strcmp0(feed_type, "profile_replies") == 0) {
            async_data->tweets = parse_profile_replies(chunk.memory);
            empty_response = profile_replies_response_is_empty(chunk.memory);
        } else {
            async_data->tweets = parse_tweets(chunk.memory);
            empty_response = tweets_response_is_empty(chunk.memory);
        }
        async_data->success = (async_data->tweets != NULL) || empty_response;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_tweets_loaded, async_data);
    return NULL;
}

void start_loading_tweets(GtkListBox *list_box)
{
    g_mutex_lock(&load_tweets_mutex);
    active_tweets_request_id++;
    guint current_request_id = active_tweets_request_id;
    g_mutex_unlock(&load_tweets_mutex);
    
    // Clear the list and show loading indicator
    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);
    
    // Clear last_id for fresh load
    g_object_set_data(G_OBJECT(list_box), "last_id", NULL);

    GtkWidget *loading_label = gtk_label_new("Loading tweets...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->request_id = current_request_id;
    data->is_append = FALSE;
    
    if (list_box == GTK_LIST_BOX(g_profile_tweets_list) || list_box == GTK_LIST_BOX(g_profile_replies_list)) {
        data->username = g_strdup(g_object_get_data(G_OBJECT(list_box), "current_profile_user"));
    }

    g_thread_new("tweet-loader", fetch_tweets_thread, data);
}

void load_more_tweets(GtkListBox *list_box, const gchar *before_id)
{
    g_mutex_lock(&load_tweets_mutex);
    guint current_request_id = active_tweets_request_id;
    g_mutex_unlock(&load_tweets_mutex);

    GtkWidget *loading_label = gtk_label_new("Loading more...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->request_id = current_request_id;
    data->is_append = TRUE;
    data->before_id = g_strdup(before_id);

    if (list_box == GTK_LIST_BOX(g_profile_tweets_list) || list_box == GTK_LIST_BOX(g_profile_replies_list)) {
        data->username = g_strdup(g_object_get_data(G_OBJECT(list_box), "current_profile_user"));
    }
    
    g_thread_new("tweet-loader", fetch_tweets_thread, data);
}

void on_scroll_edge_reached(GtkScrolledWindow *scrolled_window, GtkPositionType pos, gpointer user_data)
{
    (void)user_data;
    if (pos != GTK_POS_BOTTOM) return;

    GtkWidget *child = gtk_bin_get_child(GTK_BIN(scrolled_window));
    if (child && GTK_IS_VIEWPORT(child)) {
        child = gtk_bin_get_child(GTK_BIN(child));
    }

    if (!child || !GTK_IS_LIST_BOX(child)) {
        return;
    }

    GtkWidget *list_box = child;

    if (list_box != g_main_list_box && 
        list_box != g_profile_tweets_list && 
        list_box != g_profile_replies_list &&
        list_box != g_notifications_list) {
        return;
    }

    gboolean loading = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(list_box), "loading_more"));
    if (loading) {
        return;
    }

    const gchar *last_id = g_object_get_data(G_OBJECT(list_box), "last_id");
    if (!last_id) {
        return;
    }

    g_object_set_data(G_OBJECT(list_box), "loading_more", GINT_TO_POINTER(TRUE));
    if (list_box == g_notifications_list) {
        load_more_notifications(GTK_LIST_BOX(list_box), last_id);
    } else {
        load_more_tweets(GTK_LIST_BOX(list_box), last_id);
    }
}

static gboolean on_profile_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    if (async_data->success && async_data->profile) {
        gchar *stats_str;
        gchar *username_str;
        gchar *status_str;
        gchar *details_str;

        if (g_active_profile) {
            free_user(g_active_profile);
            g_active_profile = NULL;
        }
        g_active_profile = async_data->profile;
        async_data->profile = NULL;

        stats_str = g_strdup_printf("%d Followers · %d Following · %d Posts",
                                    g_active_profile->follower_count,
                                    g_active_profile->following_count,
                                    g_active_profile->post_count);
        username_str = g_active_profile->username
            ? g_strdup_printf("@%s", g_active_profile->username)
            : g_strdup("");
        status_str = build_profile_status_text(g_active_profile);
        details_str = build_profile_details_text(g_active_profile);

        gtk_label_set_text(GTK_LABEL(g_profile_name_label),
                           g_active_profile->name ? g_active_profile->name : "Unknown");
        gtk_label_set_text(GTK_LABEL(g_profile_username_label), username_str);
        gtk_label_set_text(GTK_LABEL(g_profile_bio_label), g_active_profile->bio ? g_active_profile->bio : "");
        gtk_label_set_text(GTK_LABEL(g_profile_status_label), status_str);
        gtk_label_set_text(GTK_LABEL(g_profile_details_label), details_str);
        gtk_label_set_text(GTK_LABEL(g_profile_stats_label), stats_str);
        g_free(stats_str);
        g_free(username_str);
        g_free(status_str);
        g_free(details_str);
        update_profile_badges(g_active_profile);

        if (g_profile_banner_image) {
            gtk_image_clear(GTK_IMAGE(g_profile_banner_image));
            if (g_active_profile->banner && g_active_profile->banner[0] != '\0') {
                gtk_widget_show(g_profile_banner_image);
                load_avatar(g_profile_banner_image, g_active_profile->banner, 640);
            } else {
                gtk_widget_hide(g_profile_banner_image);
            }
        }

        if (g_follow_button) {
            if (!g_active_profile->is_own_profile &&
                g_active_profile->username &&
                g_active_profile->username[0] != '\0' &&
                !g_active_profile->blocked_by_profile &&
                !g_active_profile->blocked_profile &&
                g_auth_token) {
                gboolean *is_following = g_new(gboolean, 1);
                *is_following = g_active_profile->is_following;
                g_object_set_data_full(G_OBJECT(g_follow_button), "username", g_strdup(g_active_profile->username), g_free);
                g_object_set_data_full(G_OBJECT(g_follow_button), "is_following", is_following, g_free);
                gtk_button_set_label(GTK_BUTTON(g_follow_button), g_active_profile->is_following ? "Unfollow" : "Follow");
                gtk_widget_show(g_follow_button);
            } else {
                gtk_widget_hide(g_follow_button);
            }
        }

        if (g_profile_edit_button) {
            if (g_active_profile->is_own_profile && g_auth_token) {
                gtk_widget_show(g_profile_edit_button);
            } else {
                gtk_widget_hide(g_profile_edit_button);
            }
        }

        if (g_profile_notify_button) {
            if (!g_active_profile->is_own_profile &&
                g_auth_token &&
                g_active_profile->username &&
                g_active_profile->username[0] != '\0' &&
                g_active_profile->is_following &&
                !g_active_profile->blocked_by_profile &&
                !g_active_profile->blocked_profile) {
                g_object_set_data_full(G_OBJECT(g_profile_notify_button), "username", g_strdup(g_active_profile->username), g_free);
                gtk_button_set_label(GTK_BUTTON(g_profile_notify_button),
                                     g_active_profile->notify_tweets ? "Alerts On" : "Alerts Off");
                gtk_widget_show(g_profile_notify_button);
            } else {
                gtk_widget_hide(g_profile_notify_button);
            }
        }

        if (g_profile_block_button) {
            if (!g_active_profile->is_own_profile &&
                g_auth_token &&
                g_active_profile->id &&
                g_active_profile->username &&
                g_active_profile->username[0] != '\0') {
                g_object_set_data_full(G_OBJECT(g_profile_block_button), "user_id", g_strdup(g_active_profile->id), g_free);
                g_object_set_data_full(G_OBJECT(g_profile_block_button), "username", g_strdup(g_active_profile->username), g_free);
                gtk_button_set_label(GTK_BUTTON(g_profile_block_button),
                                     g_active_profile->blocked_profile ? "Unblock" : "Block");
                gtk_widget_show(g_profile_block_button);
            } else {
                gtk_widget_hide(g_profile_block_button);
            }
        }

        if (g_profile_mute_button) {
            if (!g_active_profile->is_own_profile &&
                g_auth_token &&
                g_active_profile->id &&
                g_active_profile->username &&
                g_active_profile->username[0] != '\0') {
                gboolean muted = check_user_muted(g_active_profile->username);
                gboolean *muted_state = g_new(gboolean, 1);
                *muted_state = muted;
                g_object_set_data_full(G_OBJECT(g_profile_mute_button), "user_id", g_strdup(g_active_profile->id), g_free);
                g_object_set_data_full(G_OBJECT(g_profile_mute_button), "username", g_strdup(g_active_profile->username), g_free);
                g_object_set_data_full(G_OBJECT(g_profile_mute_button), "muted_state", muted_state, g_free);
                gtk_button_set_label(GTK_BUTTON(g_profile_mute_button), muted ? "Unmute" : "Mute");
                gtk_widget_show(g_profile_mute_button);
            } else {
                gtk_widget_hide(g_profile_mute_button);
            }
        }

        if (g_profile_delete_avatar_button) {
            if (g_active_profile->is_own_profile && g_auth_token) {
                gtk_widget_show(g_profile_delete_avatar_button);
            } else {
                gtk_widget_hide(g_profile_delete_avatar_button);
            }
        }

        if (g_profile_delete_banner_button) {
            if (g_active_profile->is_own_profile && g_auth_token) {
                gtk_widget_show(g_profile_delete_banner_button);
            } else {
                gtk_widget_hide(g_profile_delete_banner_button);
            }
        }

        gtk_image_set_from_icon_name(GTK_IMAGE(g_profile_avatar_image), "avatar-default", GTK_ICON_SIZE_DND);
        if (g_active_profile->avatar) {
            load_avatar(g_profile_avatar_image, g_active_profile->avatar, 80);
        }

        if (async_data->tweets) {
            populate_tweet_list(GTK_LIST_BOX(g_profile_tweets_list), async_data->tweets);

            GList *last = g_list_last(async_data->tweets);
            if (last) {
                struct Tweet *last_tweet = (struct Tweet *)last->data;
                g_object_set_data_full(G_OBJECT(g_profile_tweets_list), "last_id", g_strdup(last_tweet->id), g_free);
            } else {
                g_object_set_data(G_OBJECT(g_profile_tweets_list), "last_id", NULL);
            }

            free_tweets(async_data->tweets);
            async_data->tweets = NULL;
        }

        if (g_active_profile->username && g_active_profile->username[0] != '\0') {
            start_loading_followers(g_active_profile->username);
            start_loading_following(g_active_profile->username);
            start_loading_profile_media(g_active_profile->username);
            start_loading_profile_mutuals(g_active_profile->username);
        }
    } else {
        gtk_label_set_text(GTK_LABEL(g_profile_name_label), "Error loading profile");
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gboolean on_profile_replies_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    if (async_data->success && async_data->tweets) {
        populate_tweet_list(GTK_LIST_BOX(g_profile_replies_list), async_data->tweets);

        GList *last = g_list_last(async_data->tweets);
        if (last) {
            struct Tweet *last_tweet = (struct Tweet *)last->data;
            g_object_set_data_full(G_OBJECT(g_profile_replies_list), "last_id", g_strdup(last_tweet->id), g_free);
        } else {
            g_object_set_data(G_OBJECT(g_profile_replies_list), "last_id", NULL);
        }

        free_tweets(async_data->tweets);
        async_data->tweets = NULL;
    }
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_profile_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(PROFILE_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->profile = parse_profile(chunk.memory);
        async_data->tweets = parse_tweets(chunk.memory);
        async_data->success = (async_data->profile != NULL);
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }
    g_free(url);

    g_idle_add(on_profile_loaded, async_data);
    return NULL;
}

static gpointer fetch_profile_replies_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(PROFILE_REPLIES_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->tweets = parse_profile_replies(chunk.memory);
        async_data->success = (async_data->tweets != NULL);
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }
    g_free(url);
    g_free(async_data->username);

    g_idle_add(on_profile_replies_loaded, async_data);
    return NULL;
}

static gboolean on_tweet_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    if (async_data->success && async_data->tweets) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(g_conversation_list));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        // Find OP username (the author of the very first tweet in the thread)
        const gchar *op_username = NULL;
        if (async_data->tweets) {
            struct Tweet *first_t = (struct Tweet *)async_data->tweets->data;
            op_username = first_t->author_username;
        }

        gboolean main_tweet_reached = FALSE;
        for (GList *l = async_data->tweets; l != NULL; l = l->next) {
            struct Tweet *t = (struct Tweet *)l->data;
            
            if (g_strcmp0(t->id, async_data->query) == 0) {
                if (!main_tweet_reached && l != async_data->tweets) {
                     // Add a separator before the main tweet if there were parents
                     gtk_list_box_insert(GTK_LIST_BOX(g_conversation_list), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), -1);
                }
                main_tweet_reached = TRUE;
            } else if (main_tweet_reached && l != async_data->tweets) {
                GList *prev_l = g_list_previous(l);
                if (prev_l) {
                    struct Tweet *prev_t = (struct Tweet *)prev_l->data;
                    if (g_strcmp0(prev_t->id, async_data->query) == 0) {
                        GtkWidget *header = gtk_label_new("Replies");
                        gtk_widget_set_margin_top(header, 10);
                        gtk_widget_set_margin_bottom(header, 5);
                        gtk_widget_set_halign(header, GTK_ALIGN_START);
                        gtk_widget_set_margin_start(header, 10);
                        PangoAttrList *attrs = pango_attr_list_new();
                        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
                        gtk_label_set_attributes(GTK_LABEL(header), attrs);
                        pango_attr_list_unref(attrs);
                        
                        gtk_widget_show(header);
                        gtk_list_box_insert(GTK_LIST_BOX(g_conversation_list), header, -1);
                    }
                }
            }

            // Don't show OP tag on the root tweet or the main focused tweet
            const gchar *current_op = op_username;
            if (l == async_data->tweets || g_strcmp0(t->id, async_data->query) == 0) {
                current_op = NULL;
            }

            GtkWidget *tweet_widget = create_tweet_widget_full(t, current_op);
            
            if (g_strcmp0(t->id, async_data->query) == 0) {
                GtkStyleContext *context = gtk_widget_get_style_context(tweet_widget);
                gtk_style_context_add_class(context, "main-tweet");
            }

            gtk_widget_show_all(tweet_widget);
            gtk_list_box_insert(GTK_LIST_BOX(g_conversation_list), tweet_widget, -1);
        }

        free_tweets(async_data->tweets);
        async_data->tweets = NULL;
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(g_conversation_list));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new("Tweet not found or error loading.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(GTK_LIST_BOX(g_conversation_list), error_label, -1);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_tweet_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(TWEET_DETAILS_URL, async_data->query);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->tweets = parse_tweet_details(chunk.memory);
        async_data->success = (async_data->tweets != NULL);
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }
    g_free(url);

    g_idle_add(on_tweet_loaded, async_data);
    return NULL;
}

void show_tweet(const gchar *tweet_id)
{
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "conversation");
    gtk_widget_show(g_back_button);

    GList *children = gtk_container_get_children(GTK_CONTAINER(g_conversation_list));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    GtkWidget *loading = gtk_label_new("Loading tweet...");
    gtk_widget_show(loading);
    gtk_list_box_insert(GTK_LIST_BOX(g_conversation_list), loading, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->query = g_strdup(tweet_id); // Reusing query field
    g_thread_new("tweet-detail-loader", fetch_tweet_thread, data);
}

void show_profile(const gchar *username)
{
    if (!username || username[0] == '\0' || !GTK_IS_STACK(g_stack)) {
        return;
    }

    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "profile");
    gtk_widget_show(g_back_button);

    gtk_label_set_text(GTK_LABEL(g_profile_name_label), "Loading...");
    gtk_label_set_text(GTK_LABEL(g_profile_username_label), "");
    gtk_label_set_text(GTK_LABEL(g_profile_bio_label), "");
    gtk_label_set_text(GTK_LABEL(g_profile_status_label), "");
    gtk_label_set_text(GTK_LABEL(g_profile_details_label), "");
    gtk_label_set_text(GTK_LABEL(g_profile_stats_label), "");
    update_profile_badges(NULL);
    if (g_profile_banner_image) {
        gtk_image_clear(GTK_IMAGE(g_profile_banner_image));
        gtk_widget_hide(g_profile_banner_image);
    }
    if (g_follow_button) {
        gtk_widget_hide(g_follow_button);
    }
    if (g_profile_notify_button) {
        gtk_widget_hide(g_profile_notify_button);
    }
    if (g_profile_edit_button) {
        gtk_widget_hide(g_profile_edit_button);
    }
    if (g_profile_block_button) {
        gtk_widget_hide(g_profile_block_button);
    }
    if (g_profile_mute_button) {
        gtk_widget_hide(g_profile_mute_button);
    }
    if (g_profile_delete_avatar_button) {
        gtk_widget_hide(g_profile_delete_avatar_button);
    }
    if (g_profile_delete_banner_button) {
        gtk_widget_hide(g_profile_delete_banner_button);
    }
    
    g_object_set_data_full(G_OBJECT(g_profile_tweets_list), "current_profile_user", g_strdup(username), g_free);
    g_object_set_data_full(G_OBJECT(g_profile_replies_list), "current_profile_user", g_strdup(username), g_free);
    if (g_profile_media_list) {
        g_object_set_data_full(G_OBJECT(g_profile_media_list), "current_profile_user", g_strdup(username), g_free);
    }
    if (g_profile_mutuals_list) {
        g_object_set_data_full(G_OBJECT(g_profile_mutuals_list), "current_profile_user", g_strdup(username), g_free);
    }

    populate_tweet_list(GTK_LIST_BOX(g_profile_tweets_list), NULL);
    populate_tweet_list(GTK_LIST_BOX(g_profile_replies_list), NULL);
    if (g_profile_media_list) {
        populate_tweet_list(GTK_LIST_BOX(g_profile_media_list), NULL);
    }
    if (g_profile_mutuals_list) {
        populate_user_list(GTK_LIST_BOX(g_profile_mutuals_list), NULL);
    }

    g_object_set_data(G_OBJECT(g_profile_tweets_list), "last_id", NULL);
    g_object_set_data(G_OBJECT(g_profile_replies_list), "last_id", NULL);
    if (g_profile_media_list) {
        g_object_set_data(G_OBJECT(g_profile_media_list), "last_id", NULL);
    }

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->username = g_strdup(username);
    g_thread_new("profile-loader", fetch_profile_thread, data);

    struct AsyncData *reply_data = g_new0(struct AsyncData, 1);
    reply_data->username = g_strdup(username);
    g_thread_new("profile-reply-loader", fetch_profile_replies_thread, reply_data);
}

void on_back_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    const gchar *current_view = gtk_stack_get_visible_child_name(GTK_STACK(g_stack));
    if (g_strcmp0(current_view, "dm_messages") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "messages");
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "timeline");
        gtk_widget_hide(g_back_button);
    }
}

void on_refresh_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    const gchar *current_view = gtk_stack_get_visible_child_name(GTK_STACK(g_stack));
    if (g_strcmp0(current_view, "notifications") == 0) {
        start_loading_notifications(GTK_LIST_BOX(g_notifications_list));
    } else if (g_strcmp0(current_view, "messages") == 0) {
        start_loading_conversations(GTK_LIST_BOX(g_conversations_list));
    } else if (g_strcmp0(current_view, "dm_messages") == 0) {
        const gchar *conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
        if (conv_id) {
            start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);
        }
    } else if (g_strcmp0(current_view, "admin") == 0) {
        start_loading_admin_stats();
    } else {
        start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
    }
}

static gboolean on_notifications_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    g_mutex_lock(&load_notifications_mutex);
    gboolean is_active = (async_data->request_id == active_notifications_request_id);
    g_mutex_unlock(&load_notifications_mutex);
    
    if (!is_active) {
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }

    g_object_set_data(G_OBJECT(async_data->list_box), "loading_more", GINT_TO_POINTER(FALSE));

    if (async_data->success && async_data->notifications) {
        if (async_data->is_append) {
            remove_loading_more_label(async_data->list_box);
            append_notifications_to_list(async_data->list_box, async_data->notifications);
        } else {
            populate_notification_list(async_data->list_box, async_data->notifications);
        }

        GList *last = g_list_last(async_data->notifications);
        if (last) {
            struct Notification *last_notification = last->data;
            g_object_set_data_full(G_OBJECT(async_data->list_box), "last_id", g_strdup(last_notification->id), g_free);
        } else {
            g_object_set_data(G_OBJECT(async_data->list_box), "last_id", NULL);
        }

        if (!async_data->has_more) {
            g_object_set_data(G_OBJECT(async_data->list_box), "last_id", NULL);
            if (async_data->is_append) {
                append_end_of_list_label(async_data->list_box);
            }
        }

        free_notifications(async_data->notifications);
        async_data->notifications = NULL;
    } else if (async_data->success && async_data->is_append) {
        remove_loading_more_label(async_data->list_box);
        g_object_set_data(G_OBJECT(async_data->list_box), "last_id", NULL);
        append_end_of_list_label(async_data->list_box);
    } else {
        if (!async_data->is_append) {
            GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
            for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
                gtk_widget_destroy(GTK_WIDGET(iter->data));
            g_list_free(children);

            GtkWidget *error_label = gtk_label_new(async_data->success ? "No notifications." : "Failed to load notifications.");
            gtk_widget_show(error_label);
            gtk_list_box_insert(async_data->list_box, error_label, -1);
        } else {
            remove_loading_more_label(async_data->list_box);
        }
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_notifications_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = NULL;

    if (async_data->before_id) {
        url = g_strdup_printf("%s?limit=%d&before=%s",
                              NOTIFICATIONS_URL,
                              notifications_page_size,
                              async_data->before_id);
    } else {
        url = g_strdup_printf("%s?limit=%d", NOTIFICATIONS_URL, notifications_page_size);
    }

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->notifications = parse_notifications(chunk.memory);
        async_data->has_more = (async_data->notifications != NULL &&
                                g_list_length(async_data->notifications) >= notifications_page_size);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_notifications_loaded, async_data);
    return NULL;
}

void start_loading_notifications(GtkListBox *list_box)
{
    if (!g_auth_token) return;

    g_mutex_lock(&load_notifications_mutex);
    active_notifications_request_id++;
    guint current_request_id = active_notifications_request_id;
    g_mutex_unlock(&load_notifications_mutex);
    
    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    g_object_set_data(G_OBJECT(list_box), "last_id", NULL);
    
    GtkWidget *loading_label = gtk_label_new("Loading notifications...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->request_id = current_request_id;
    
    g_thread_new("notification-loader", fetch_notifications_thread, data);
}

static void load_more_notifications(GtkListBox *list_box, const gchar *before_id)
{
    struct AsyncData *data;
    guint current_request_id;
    GtkWidget *loading_label;

    g_mutex_lock(&load_notifications_mutex);
    current_request_id = active_notifications_request_id;
    g_mutex_unlock(&load_notifications_mutex);

    loading_label = gtk_label_new("Loading more...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->request_id = current_request_id;
    data->is_append = TRUE;
    data->before_id = g_strdup(before_id);

    g_thread_new("notification-loader", fetch_notifications_thread, data);
}

void on_notifications_clicked(GtkWidget *widget, gpointer user_data)
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
                                 "You must be logged in to view notifications.");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }

    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "notifications");
    gtk_widget_show(g_back_button);
    start_loading_notifications(GTK_LIST_BOX(g_notifications_list));
}

void refresh_notification_badge(void)
{
    struct MemoryStruct chunk = {0};
    gint unread_count = 0;

    if (!g_auth_token) {
        update_notifications_button_label(0);
        return;
    }

    if (fetch_url(NOTIFICATIONS_UNREAD_COUNT_URL, &chunk, NULL, "GET")) {
        JsonParser *parser = json_parser_new();
        GError *error = NULL;
        if (json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
            JsonNode *root = json_parser_get_root(parser);
            if (root && JSON_NODE_HOLDS_OBJECT(root)) {
                JsonObject *obj = json_node_get_object(root);
                if (json_object_has_member(obj, "count")) {
                    unread_count = json_object_get_int_member(obj, "count");
                }
            }
        }
        if (error) {
            g_error_free(error);
        }
        g_object_unref(parser);
        g_free(chunk.memory);
    }

    update_notifications_button_label(unread_count);
}

gboolean mark_notification_read(const gchar *notification_id)
{
    struct MemoryStruct chunk = {0};
    gchar *url;
    gboolean success = FALSE;

    if (!g_auth_token || !notification_id) {
        return FALSE;
    }

    url = g_strdup_printf(NOTIFICATION_READ_URL, notification_id);
    if (fetch_url(url, &chunk, "", "PATCH")) {
        success = TRUE;
        g_free(chunk.memory);
        refresh_notification_badge();
    }
    g_free(url);
    return success;
}

void on_mark_all_read_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    
    if (!g_auth_token) return;

    struct MemoryStruct chunk = {0};
    if (fetch_url(NOTIFICATIONS_MARK_ALL_READ_URL, &chunk, "", "PATCH")) {
        g_free(chunk.memory);
        refresh_notification_badge();
        start_loading_notifications(GTK_LIST_BOX(g_notifications_list));
    }
}

static gboolean on_conversations_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    g_mutex_lock(&load_conversations_mutex);
    gboolean is_active = (async_data->request_id == active_conversations_request_id);
    g_mutex_unlock(&load_conversations_mutex);
    
    if (!is_active) {
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }

    if (async_data->success && async_data->conversations) {
        populate_conversation_list(async_data->list_box, async_data->conversations);
        free_conversations(async_data->conversations);
        async_data->conversations = NULL;
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new(async_data->success ? "No conversations." : "Failed to load conversations.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_conversations_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};

    if (fetch_url(DM_CONVERSATIONS_URL, &chunk, NULL, "GET")) {
        async_data->conversations = parse_conversations(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_idle_add(on_conversations_loaded, async_data);
    return NULL;
}

void start_loading_conversations(GtkListBox *list_box)
{
    if (!g_auth_token) return;

    g_mutex_lock(&load_conversations_mutex);
    active_conversations_request_id++;
    guint current_request_id = active_conversations_request_id;
    g_mutex_unlock(&load_conversations_mutex);
    
    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);
    
    GtkWidget *loading_label = gtk_label_new("Loading conversations...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->request_id = current_request_id;
    
    g_thread_new("conversation-loader", fetch_conversations_thread, data);
}

static gboolean on_messages_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    gchar *info_text = NULL;
    
    g_mutex_lock(&load_messages_mutex);
    gboolean is_active = (async_data->request_id == active_messages_request_id);
    g_mutex_unlock(&load_messages_mutex);
    
    if (!is_active) {
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }

    if (async_data->conversation) {
        if (g_dm_title_label) {
            gtk_label_set_text(GTK_LABEL(g_dm_title_label),
                               async_data->conversation->display_name ?
                               async_data->conversation->display_name :
                               (async_data->conversation->title ? async_data->conversation->title : "Messages"));
        }
        if (g_dm_info_label) {
            info_text = build_dm_conversation_info(async_data->conversation);
            gtk_label_set_text(GTK_LABEL(g_dm_info_label), info_text);
            g_free(info_text);
        }
        g_object_set_data_full(G_OBJECT(g_dm_messages_list),
                               "conversation_detail",
                               async_data->conversation,
                               (GDestroyNotify)free_conversation);
        async_data->conversation = NULL;
    }

    if (async_data->success && async_data->messages) {
        populate_message_list(async_data->list_box, async_data->messages);
        free_messages(async_data->messages);
        async_data->messages = NULL;
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new(async_data->success ? "No messages." : "Failed to load messages.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_messages_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(DM_MESSAGES_URL, async_data->conversation_id);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->conversation = parse_conversation_details(chunk.memory);
        async_data->messages = parse_messages(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_messages_loaded, async_data);
    return NULL;
}

void start_loading_messages(GtkListBox *list_box, const gchar *conversation_id)
{
    if (!g_auth_token) return;

    g_mutex_lock(&load_messages_mutex);
    active_messages_request_id++;
    guint current_request_id = active_messages_request_id;
    g_mutex_unlock(&load_messages_mutex);
    
    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);
    
    GtkWidget *loading_label = gtk_label_new("Loading messages...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->request_id = current_request_id;
    data->conversation_id = g_strdup(conversation_id);
    
    g_thread_new("message-loader", fetch_messages_thread, data);
}

void on_messages_clicked(GtkWidget *widget, gpointer user_data)
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
                                 "You must be logged in to view messages.");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }

    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "messages");
    gtk_widget_show(g_back_button);
    start_loading_conversations(GTK_LIST_BOX(g_conversations_list));
}

void on_settings_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "settings");
    gtk_widget_show(g_back_button);
    
    update_settings_username_display();
    refresh_cache_size_display();
}

static gboolean
on_admin_stats_loaded(gpointer data)
{
    gchar *stats_text = (gchar *)data;
    if (stats_text) {
        gtk_label_set_text(GTK_LABEL(g_admin_stats_label), stats_text);
        g_free(stats_text);
    } else {
        gtk_label_set_text(GTK_LABEL(g_admin_stats_label), "Failed to load admin statistics.");
    }
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_stats_thread(gpointer data)
{
    (void)data;
    struct MemoryStruct chunk = {0};
    gchar *stats_text = NULL;

    if (fetch_url(ADMIN_STATS_URL, &chunk, NULL, "GET")) {
        stats_text = parse_admin_stats(chunk.memory);
        g_free(chunk.memory);
    }

    g_idle_add(on_admin_stats_loaded, stats_text);
    return NULL;
}

void start_loading_admin_stats(void)
{
    if (!g_auth_token || !g_is_admin) return;
    gtk_label_set_text(GTK_LABEL(g_admin_stats_label), "Loading admin statistics...");
    g_thread_new("admin-stats-loader", fetch_admin_stats_thread, NULL);
}

void on_admin_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    if (!g_is_admin) return;

    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "admin");
    gtk_widget_show(g_back_button);
    start_loading_admin_stats();
    start_loading_admin_users(NULL);
    start_loading_admin_posts(NULL);
}

void on_open_full_admin_panel_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GError *error = NULL;
    gint response;

    (void)widget;
    (void)user_data;

    if (!g_is_admin) {
        show_modal_message(GTK_MESSAGE_ERROR,
                           "Admin access is required.",
                           "Log into an administrator account before opening the full Tweetapus admin panel.");
        return;
    }

    dialog = gtk_message_dialog_new(NULL,
                                    GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_INFO,
                                    GTK_BUTTONS_OK_CANCEL,
                                    "Open the full Tweetapus admin panel in your browser?");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "Tweeta Desktop will sync the browser session for the current instance to the same "
        "account used in the desktop client, then hand off to /admin."
    );
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_OK) {
        return;
    }

    if (!launch_full_admin_panel(&error)) {
        show_modal_message(GTK_MESSAGE_ERROR,
                           "Failed to open the full admin panel.",
                           error ? error->message : "The browser handoff could not be created.");
        g_clear_error(&error);
    }
}

static gboolean
on_admin_users_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    if (async_data->success && async_data->users) {
        populate_user_list(GTK_LIST_BOX(g_admin_users_list), async_data->users);
        free_users(async_data->users);
        async_data->users = NULL;
    } else {
        gtk_label_set_text(GTK_LABEL(g_user_label), "Failed to load admin users.");
    }
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_users_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url;
    if (async_data->query && strlen(async_data->query) > 0) {
        gchar *escaped = g_uri_escape_string(async_data->query, NULL, FALSE);
        url = g_strdup_printf("%s?search=%s", ADMIN_USERS_URL, escaped);
        g_free(escaped);
    } else {
        url = g_strdup(ADMIN_USERS_URL);
    }

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->users = parse_admin_users(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }
    g_free(url);
    g_idle_add(on_admin_users_loaded, async_data);
    return NULL;
}

void start_loading_admin_users(const gchar *search)
{
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->query = g_strdup(search);
    g_thread_new("admin-users-loader", fetch_admin_users_thread, data);
}

static gboolean
on_admin_posts_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    if (async_data->success && async_data->tweets) {
        populate_tweet_list(GTK_LIST_BOX(g_admin_posts_list), async_data->tweets);
        free_tweets(async_data->tweets);
        async_data->tweets = NULL;
    }
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_posts_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url;
    if (async_data->query && strlen(async_data->query) > 0) {
        gchar *escaped = g_uri_escape_string(async_data->query, NULL, FALSE);
        url = g_strdup_printf("%s?search=%s", ADMIN_POSTS_URL, escaped);
        g_free(escaped);
    } else {
        url = g_strdup(ADMIN_POSTS_URL);
    }

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->tweets = parse_admin_posts(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }
    g_free(url);
    g_idle_add(on_admin_posts_loaded, async_data);
    return NULL;
}

void start_loading_admin_posts(const gchar *search)
{
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->query = g_strdup(search);
    g_thread_new("admin-posts-loader", fetch_admin_posts_thread, data);
}

void perform_admin_verify(const gchar *username, gboolean verify)
{
    if (!g_is_admin) return;
    gchar *url = g_strdup_printf("%s/%s", ADMIN_USERS_URL, username);
    gchar *post_data = g_strdup_printf("{\"verified\": %s}", verify ? "true" : "false");
    struct MemoryStruct chunk = {0};
    if (fetch_url(url, &chunk, post_data, "PATCH")) {
        g_free(chunk.memory);
        start_loading_admin_users(gtk_entry_get_text(GTK_ENTRY(g_admin_users_search)));
    }
    g_free(post_data);
    g_free(url);
}

void perform_admin_suspend(const gchar *username, const gchar *reason)
{
    if (!g_is_admin) return;
    gchar *url = g_strdup_printf("%s/%s/suspend", ADMIN_USERS_URL, username);
    gchar *post_data = g_strdup_printf("{\"reason\": \"%s\", \"action\": \"suspend\"}", reason);
    struct MemoryStruct chunk = {0};
    if (fetch_url(url, &chunk, post_data, "POST")) {
        g_free(chunk.memory);
        start_loading_admin_users(gtk_entry_get_text(GTK_ENTRY(g_admin_users_search)));
    }
    g_free(post_data);
    g_free(url);
}

void perform_admin_delete_user(const gchar *username)
{
    if (!g_is_admin) return;
    gchar *url = g_strdup_printf("%s/%s", ADMIN_USERS_URL, username);
    struct MemoryStruct chunk = {0};
    if (fetch_url(url, &chunk, NULL, "DELETE")) {
        g_free(chunk.memory);
        start_loading_admin_users(gtk_entry_get_text(GTK_ENTRY(g_admin_users_search)));
    }
    g_free(url);
}

void perform_admin_delete_post(const gchar *post_id)
{
    if (!g_is_admin) return;
    gchar *url = g_strdup_printf("%s/%s", ADMIN_POSTS_URL, post_id);
    struct MemoryStruct chunk = {0};
    if (fetch_url(url, &chunk, NULL, "DELETE")) {
        g_free(chunk.memory);
        start_loading_admin_posts(gtk_entry_get_text(GTK_ENTRY(g_admin_posts_search)));
    }
    g_free(url);
}

static gboolean on_users_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    if (async_data->success && async_data->users) {
        populate_user_list(async_data->list_box, async_data->users);
        free_users(async_data->users);
        async_data->users = NULL;
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new("No users found.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gboolean on_search_tweets_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    if (async_data->success && async_data->tweets) {
        populate_tweet_list(async_data->list_box, async_data->tweets);
        free_tweets(async_data->tweets);
        async_data->tweets = NULL;
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new("No tweets found.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_search_users_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *escaped_query = g_uri_escape_string(async_data->query, NULL, FALSE);
    gchar *url = g_strdup_printf("%s?q=%s", SEARCH_USERS_URL, escaped_query);
    g_free(escaped_query);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->users = parse_users(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }
    g_free(url);

    g_idle_add(on_users_loaded, async_data);
    return NULL;
}

static gpointer fetch_search_tweets_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *escaped_query = g_uri_escape_string(async_data->query, NULL, FALSE);
    gchar *url = g_strdup_printf("%s?q=%s", SEARCH_POSTS_URL, escaped_query);
    g_free(escaped_query);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->tweets = parse_tweets(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }
    g_free(url);

    g_idle_add(on_search_tweets_loaded, async_data);
    return NULL;
}

void perform_search(const gchar *query)
{
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "search");
    gtk_widget_show(g_back_button);

    populate_user_list(GTK_LIST_BOX(g_search_users_list), NULL);
    populate_tweet_list(GTK_LIST_BOX(g_search_tweets_list), NULL);

    GtkWidget *loading1 = gtk_label_new("Searching users...");
    gtk_widget_show(loading1);
    gtk_list_box_insert(GTK_LIST_BOX(g_search_users_list), loading1, -1);

    GtkWidget *loading2 = gtk_label_new("Searching tweets...");
    gtk_widget_show(loading2);
    gtk_list_box_insert(GTK_LIST_BOX(g_search_tweets_list), loading2, -1);

    struct AsyncData *data_users = g_new0(struct AsyncData, 1);
    data_users->list_box = GTK_LIST_BOX(g_search_users_list);
    data_users->query = g_strdup(query);
    g_thread_new("search-users-loader", fetch_search_users_thread, data_users);

    struct AsyncData *data_tweets = g_new0(struct AsyncData, 1);
    data_tweets->list_box = GTK_LIST_BOX(g_search_tweets_list);
    data_tweets->query = g_strdup(query);
    g_thread_new("search-tweets-loader", fetch_search_tweets_thread, data_tweets);
}

void on_search_activated(GtkEntry *entry, gpointer user_data)
{
    (void)user_data;
    const gchar *query = gtk_entry_get_text(entry);
    if (query && strlen(query) > 0) {
        perform_search(query);
    }
}

gboolean perform_like(const gchar *tweet_id)
{
    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gboolean liked = FALSE;
    gchar *url = g_strdup_printf(LIKE_TWEET_URL, tweet_id);

    g_debug("perform_like: tweet_id=%s, url=%s", tweet_id, url);
    if (fetch_url(url, &chunk, "{}", "POST")) {
        g_debug("perform_like: fetch_url succeeded, response: %s", chunk.memory ? chunk.memory : "(null)");
        if (chunk.memory && strstr(chunk.memory, "\"error\"") == NULL) {
            if (response_has_success_flag(chunk.memory, "liked", &liked)) {
                update_interaction_cache(tweet_id, liked, -1, -1);
            }
            success = TRUE;
        } else if (chunk.memory) {
            g_warning("perform_like: API returned error: %s", chunk.memory);
        }
        g_free(chunk.memory);
    } else {
        g_debug("perform_like: fetch_url failed");
    }

    g_free(url);
    return success;
}

gboolean perform_retweet(const gchar *tweet_id)
{
    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gboolean retweeted = FALSE;
    gchar *url = g_strdup_printf(RETWEET_URL, tweet_id);

    g_debug("perform_retweet: tweet_id=%s, url=%s", tweet_id, url);
    if (fetch_url(url, &chunk, "{}", "POST")) {
        g_debug("perform_retweet: fetch_url succeeded, response: %s", chunk.memory ? chunk.memory : "(null)");
        if (chunk.memory && strstr(chunk.memory, "\"error\"") == NULL) {
            if (response_has_success_flag(chunk.memory, "retweeted", &retweeted)) {
                update_interaction_cache(tweet_id, -1, retweeted, -1);
            }
            success = TRUE;
        } else if (chunk.memory) {
            g_warning("perform_retweet: API returned error: %s", chunk.memory);
        }
        g_free(chunk.memory);
    } else {
        g_debug("perform_retweet: fetch_url failed");
    }

    g_free(url);
    return success;
}

gboolean perform_bookmark(const gchar *tweet_id, gboolean add)
{
    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gboolean bookmarked = FALSE;
    const gchar *url = add ? BOOKMARK_ADD_URL : BOOKMARK_REMOVE_URL;

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "postId");
    json_builder_add_string_value(builder, tweet_id);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    g_debug("perform_bookmark: tweet_id=%s, add=%d, url=%s", tweet_id, add, url);
    if (fetch_url(url, &chunk, post_data, "POST")) {
        g_debug("perform_bookmark: fetch_url succeeded, response: %s", chunk.memory ? chunk.memory : "(null)");
        if (chunk.memory && strstr(chunk.memory, "\"error\"") == NULL) {
            if (response_has_success_flag(chunk.memory, "bookmarked", &bookmarked)) {
                update_interaction_cache(tweet_id, -1, -1, bookmarked);
            }
            success = TRUE;
        } else if (chunk.memory) {
            g_warning("perform_bookmark: API returned error: %s", chunk.memory);
        }
        g_free(chunk.memory);
    } else {
        g_debug("perform_bookmark: fetch_url failed");
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

gboolean perform_reaction(const gchar *tweet_id, const gchar *emoji)
{
    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gchar *url = g_strdup_printf(REACTION_URL, tweet_id);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "emoji");
    json_builder_add_string_value(builder, emoji);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    g_debug("perform_reaction: tweet_id=%s, emoji=%s, url=%s", tweet_id, emoji, url);
    if (fetch_url(url, &chunk, post_data, "POST")) {
        g_debug("perform_reaction: fetch_url succeeded, response: %s", chunk.memory ? chunk.memory : "(null)");
        if (chunk.memory && strstr(chunk.memory, "\"error\"") == NULL) {
            success = TRUE;
        } else if (chunk.memory) {
            g_warning("perform_reaction: API returned error: %s", chunk.memory);
        }
        g_free(chunk.memory);
    } else {
        g_debug("perform_reaction: fetch_url failed");
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);
    g_free(url);
    return success;
}

gboolean
perform_edit_tweet(const gchar *tweet_id, const gchar *new_content)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *url;
    gchar *response = NULL;
    gboolean success;

    if (!g_auth_token || !tweet_id || !new_content) {
        return FALSE;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, new_content);
    json_builder_end_object(builder);

    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);

    url = g_strdup_printf(TWEET_EDIT_URL, tweet_id);
    success = perform_request_with_optional_payload(url, payload, "PUT", &response);

    g_free(response);
    g_free(url);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

gboolean
perform_delete_tweet(const gchar *tweet_id)
{
    gchar *url;
    gchar *response = NULL;
    gboolean success;

    if (!g_auth_token || !tweet_id) {
        return FALSE;
    }

    url = g_strdup_printf(TWEET_DELETE_URL, tweet_id);
    success = perform_request_with_optional_payload(url, NULL, "DELETE", &response);
    g_free(response);
    g_free(url);
    return success;
}

gchar*
fetch_tweet_edit_history_text(const gchar *tweet_id)
{
    JsonParser *parser;
    JsonNode *root;
    JsonObject *obj;
    JsonArray *history;
    GError *error = NULL;
    gchar *url;
    gchar *response;
    GString *text;

    if (!g_auth_token || !tweet_id) {
        return NULL;
    }

    url = g_strdup_printf(TWEET_EDIT_HISTORY_URL, tweet_id);
    response = perform_simple_json_request(url, "GET", NULL);
    g_free(url);
    if (!response) {
        return NULL;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, response, -1, &error)) {
        if (error) {
            g_error_free(error);
        }
        g_free(response);
        g_object_unref(parser);
        return NULL;
    }

    text = g_string_new(NULL);
    root = json_parser_get_root(parser);
    obj = json_node_get_object(root);
    if (json_object_has_member(obj, "history")) {
        history = json_object_get_array_member(obj, "history");
        for (guint i = 0; i < json_array_get_length(history); i++) {
            JsonObject *item = json_array_get_object_element(history, i);
            const gchar *content = json_object_has_member(item, "content")
                ? json_object_get_string_member(item, "content") : "";
            const gchar *edited_at = json_object_has_member(item, "edited_at")
                ? json_object_get_string_member(item, "edited_at") : "";
            gboolean is_current = json_object_has_member(item, "is_current")
                ? json_object_get_boolean_member(item, "is_current") : FALSE;
            g_string_append_printf(text,
                                   "%s%s\n%s\n\n",
                                   edited_at && edited_at[0] != '\0' ? edited_at : "Unknown time",
                                   is_current ? " (current)" : "",
                                   content ? content : "");
        }
    }

    g_free(response);
    g_object_unref(parser);
    return g_string_free(text, FALSE);
}

gchar*
fetch_tweet_reactions_text(const gchar *tweet_id)
{
    JsonParser *parser;
    JsonNode *root;
    JsonObject *obj;
    JsonArray *reactions;
    GError *error = NULL;
    gchar *url;
    gchar *response;
    GString *text;

    if (!tweet_id) {
        return NULL;
    }

    url = g_strdup_printf(TWEET_REACTIONS_URL, tweet_id);
    response = perform_simple_json_request(url, "GET", NULL);
    g_free(url);
    if (!response) {
        return NULL;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, response, -1, &error)) {
        if (error) {
            g_error_free(error);
        }
        g_free(response);
        g_object_unref(parser);
        return NULL;
    }

    text = g_string_new(NULL);
    root = json_parser_get_root(parser);
    obj = json_node_get_object(root);
    if (json_object_has_member(obj, "reactions")) {
        reactions = json_object_get_array_member(obj, "reactions");
        for (guint i = 0; i < json_array_get_length(reactions); i++) {
            JsonObject *item = json_array_get_object_element(reactions, i);
            const gchar *emoji = json_object_has_member(item, "emoji")
                ? json_object_get_string_member(item, "emoji") : "?";
            const gchar *name = json_object_has_member(item, "name")
                ? json_object_get_string_member(item, "name") : NULL;
            const gchar *username = json_object_has_member(item, "username")
                ? json_object_get_string_member(item, "username") : NULL;
            g_string_append_printf(text, "%s  %s",
                                   emoji ? emoji : "?",
                                   name && name[0] != '\0' ? name : "Unknown");
            if (username && username[0] != '\0') {
                g_string_append_printf(text, " (@%s)", username);
            }
            g_string_append_c(text, '\n');
        }
    }

    if (text->len == 0) {
        g_string_append(text, "No reactions yet.");
    } else if (text->len > 0 && text->str[text->len - 1] == '\n') {
        text->str[text->len - 1] = '\0';
        text->len--;
    }

    g_free(response);
    g_object_unref(parser);
    return g_string_free(text, FALSE);
}

static void free_emoji(gpointer data)
{
    struct Emoji *emoji = data;
    if (emoji) {
        g_free(emoji->id);
        g_free(emoji->name);
        g_free(emoji->file_url);
        g_free(emoji->file_hash);
        g_free(emoji->created_by);
        g_free(emoji);
    }
}

void free_emojis(GList *emojis)
{
    g_list_free_full(emojis, free_emoji);
}

GList* fetch_emojis(void)
{
    struct MemoryStruct chunk = {0};
    GList *emojis = NULL;

    if (fetch_url(EMOJIS_URL, &chunk, NULL, "GET")) {
        JsonParser *parser = json_parser_new();
        GError *error = NULL;
        json_parser_load_from_data(parser, chunk.memory, -1, &error);
        if (!error) {
            JsonNode *root = json_parser_get_root(parser);
            JsonObject *obj = json_node_get_object(root);
            if (json_object_has_member(obj, "emojis")) {
                JsonArray *arr = json_object_get_array_member(obj, "emojis");
                for (guint i = 0; i < json_array_get_length(arr); i++) {
                    JsonObject *e_obj = json_array_get_object_element(arr, i);
                    struct Emoji *emoji = g_new0(struct Emoji, 1);
                    emoji->id = g_strdup(json_object_get_string_member(e_obj, "id"));
                    emoji->name = g_strdup(json_object_get_string_member(e_obj, "name"));
                    emoji->file_url = g_strdup(json_object_get_string_member(e_obj, "file_url"));
                    if (json_object_has_member(e_obj, "file_hash") && !json_node_is_null(json_object_get_member(e_obj, "file_hash")))
                        emoji->file_hash = g_strdup(json_object_get_string_member(e_obj, "file_hash"));
                    if (json_object_has_member(e_obj, "created_by") && !json_node_is_null(json_object_get_member(e_obj, "created_by")))
                        emoji->created_by = g_strdup(json_object_get_string_member(e_obj, "created_by"));
                    emojis = g_list_append(emojis, emoji);
                }
            }
        } else {
            g_error_free(error);
        }
        g_object_unref(parser);
        g_free(chunk.memory);
    }

    return emojis;
}

static gboolean perform_add_note(const gchar *tweet_id, const gchar *note, const gchar *severity)
{
    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gchar *url = g_strdup_printf("%s/admin/fact-check/%s", API_BASE_URL, tweet_id);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "note");
    json_builder_add_string_value(builder, note);
    json_builder_set_member_name(builder, "severity");
    json_builder_add_string_value(builder, severity ? severity : "warning");
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    if (fetch_url(url, &chunk, post_data, "POST")) {
        success = TRUE;
        g_free(chunk.memory);
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);
    g_free(url);
    return success;
}

static void on_note_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    struct NoteContext *ctx = (struct NoteContext *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->text_view));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar *note = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        if (note && strlen(note) > 0) {
            if (perform_add_note(ctx->tweet_id, note, ctx->severity)) {
                start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
            } else {
                GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_CLOSE,
                                         "Failed to add note.");
                gtk_dialog_run(GTK_DIALOG(error_dialog));
                gtk_widget_destroy(error_dialog);
            }
        }
        g_free(note);
    }

    g_free(ctx->tweet_id);
    g_free(ctx->severity);
    g_free(ctx);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void open_add_note_dialog(GtkWidget *parent_widget, const gchar *tweet_id, const gchar *severity)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel(parent_widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    gchar *title = g_strdup_printf("Add %s Note", severity);
    // Capitalize first letter
    if (title[4] >= 'a' && title[4] <= 'z') title[4] -= 32;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Add Note", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    g_free(title);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    gchar *label_text = g_strdup_printf("Enter %s note/fact check:", severity);
    GtkWidget *label = gtk_label_new(label_text);
    g_free(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content_area), label, FALSE, FALSE, 5);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_widget_set_size_request(text_view, 300, 100);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_box_pack_start(GTK_BOX(content_area), text_view, TRUE, TRUE, 0);

    struct NoteContext *ctx = g_new(struct NoteContext, 1);
    ctx->text_view = text_view;
    ctx->tweet_id = g_strdup(tweet_id);
    ctx->severity = g_strdup(severity);

    gtk_widget_show_all(dialog);
    g_signal_connect(dialog, "response", G_CALLBACK(on_note_response), ctx);
}

static void on_note_menu_item_activated(GtkMenuItem *menuitem, gpointer user_data)
{
    const gchar *severity = (const gchar *)user_data;
    GtkWidget *btn = g_object_get_data(G_OBJECT(menuitem), "origin_button");
    const gchar *tweet_id = g_object_get_data(G_OBJECT(btn), "tweet_id");
    
    open_add_note_dialog(btn, tweet_id, severity);
}

void on_note_button_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    gchar *token = get_auth_token_safe();
    if (!token) {
        g_free(token);
        return;
    }
    g_free(token);

    GtkWidget *menu = gtk_menu_new();

    const struct {
        const gchar *label;
        const gchar *severity;
    } options[] = {
        {"Info Note (Blue)", "info"},
        {"Warning Note (Orange)", "warning"},
        {"Danger Note (Red)", "danger"},
        {NULL, NULL}
    };

    for (int i = 0; options[i].label != NULL; i++) {
        GtkWidget *item = gtk_menu_item_new_with_label(options[i].label);
        g_object_set_data(G_OBJECT(item), "origin_button", widget);
        g_signal_connect(item, "activate", G_CALLBACK(on_note_menu_item_activated), (gpointer)options[i].severity);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), widget, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

gboolean perform_follow(const gchar *username, gboolean follow)
{
    if (!g_auth_token || !username) return FALSE;
    
    if (!is_valid_username(username)) {
        g_warning("Invalid username format: %s", username);
        return FALSE;
    }

    gchar *url = g_strdup_printf(PROFILE_FOLLOW_URL, username);
    const gchar *method = follow ? "POST" : "DELETE";

    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;

    if (fetch_url(url, &chunk, "{}", method)) {
        success = TRUE;
        g_free(chunk.memory);
    }

    g_free(url);
    return success;
}

gboolean
perform_profile_notify_tweets(const gchar *username, gboolean notify)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *url;
    gchar *response = NULL;
    gboolean success;

    if (!g_auth_token || !username) {
        return FALSE;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "notify");
    json_builder_add_boolean_value(builder, notify);
    json_builder_end_object(builder);

    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);

    url = g_strdup_printf(PROFILE_NOTIFY_TWEETS_URL, username);
    success = perform_request_with_optional_payload(url, payload, "POST", &response);

    g_free(response);
    g_free(url);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

gboolean
perform_delete_profile_avatar(const gchar *username)
{
    gchar *url;
    gchar *response = NULL;
    gboolean success;

    if (!g_auth_token || !username) {
        return FALSE;
    }

    url = g_strdup_printf(PROFILE_DELETE_AVATAR_URL, username);
    success = perform_request_with_optional_payload(url, NULL, "DELETE", &response);
    g_free(response);
    g_free(url);
    return success;
}

gboolean
perform_delete_profile_banner(const gchar *username)
{
    gchar *url;
    gchar *response = NULL;
    gboolean success;

    if (!g_auth_token || !username) {
        return FALSE;
    }

    url = g_strdup_printf(PROFILE_DELETE_BANNER_URL, username);
    success = perform_request_with_optional_payload(url, NULL, "DELETE", &response);
    g_free(response);
    g_free(url);
    return success;
}

gboolean
perform_toggle_pin_tweet(const gchar *tweet_id, gboolean pin)
{
    gchar *url;
    gchar *response = NULL;
    gboolean success;

    if (!g_auth_token || !tweet_id) {
        return FALSE;
    }

    url = g_strdup_printf(PROFILE_PIN_GLOBAL_URL, tweet_id);
    success = perform_request_with_optional_payload(url, "{}", pin ? "POST" : "DELETE", &response);
    g_free(response);
    g_free(url);
    return success;
}

static gboolean on_followers_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (async_data->success && async_data->users) {
        populate_user_list(GTK_LIST_BOX(g_followers_list), async_data->users);
        free_users(async_data->users);
        async_data->users = NULL;
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(g_followers_list));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new("Failed to load followers.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(GTK_LIST_BOX(g_followers_list), error_label, -1);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_followers_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(PROFILE_FOLLOWERS_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->users = parse_users(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_followers_loaded, async_data);
    return NULL;
}

void start_loading_followers(const gchar *username)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(g_followers_list));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    GtkWidget *loading = gtk_label_new("Loading followers...");
    gtk_widget_show(loading);
    gtk_list_box_insert(GTK_LIST_BOX(g_followers_list), loading, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->username = g_strdup(username);
    g_thread_new("followers-loader", fetch_followers_thread, data);
}

static gboolean on_following_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (async_data->success && async_data->users) {
        populate_user_list(GTK_LIST_BOX(g_following_list), async_data->users);
        free_users(async_data->users);
        async_data->users = NULL;
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(g_following_list));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new("Failed to load following.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(GTK_LIST_BOX(g_following_list), error_label, -1);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_following_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(PROFILE_FOLLOWING_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->users = parse_users(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_following_loaded, async_data);
    return NULL;
}

void start_loading_following(const gchar *username)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(g_following_list));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    GtkWidget *loading = gtk_label_new("Loading following...");
    gtk_widget_show(loading);
    gtk_list_box_insert(GTK_LIST_BOX(g_following_list), loading, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->username = g_strdup(username);
    g_thread_new("following-loader", fetch_following_thread, data);
}

static gboolean
on_profile_media_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (async_data->success && async_data->tweets) {
        populate_tweet_list(GTK_LIST_BOX(g_profile_media_list), async_data->tweets);

        GList *last = g_list_last(async_data->tweets);
        if (last) {
            struct Tweet *last_tweet = (struct Tweet *)last->data;
            g_object_set_data_full(G_OBJECT(g_profile_media_list), "last_id", g_strdup(last_tweet->id), g_free);
        } else {
            g_object_set_data(G_OBJECT(g_profile_media_list), "last_id", NULL);
        }

        free_tweets(async_data->tweets);
        async_data->tweets = NULL;
    } else if (g_profile_media_list) {
        populate_tweet_list(GTK_LIST_BOX(g_profile_media_list), NULL);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_profile_media_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(PROFILE_MEDIA_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->tweets = parse_tweets(chunk.memory);
        async_data->success = (async_data->tweets != NULL);
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_profile_media_loaded, async_data);
    return NULL;
}

void
start_loading_profile_media(const gchar *username)
{
    if (!g_profile_media_list || !username) {
        return;
    }

    populate_tweet_list(GTK_LIST_BOX(g_profile_media_list), NULL);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->username = g_strdup(username);
    g_thread_new("profile-media-loader", fetch_profile_media_thread, data);
}

static gboolean
on_profile_mutuals_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (async_data->success && async_data->users) {
        populate_user_list(GTK_LIST_BOX(g_profile_mutuals_list), async_data->users);
        free_users(async_data->users);
        async_data->users = NULL;
    } else if (g_profile_mutuals_list) {
        populate_user_list(GTK_LIST_BOX(g_profile_mutuals_list), NULL);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_profile_mutuals_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(PROFILE_MUTUALS_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->users = parse_users(chunk.memory);
        async_data->success = (async_data->users != NULL);
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_profile_mutuals_loaded, async_data);
    return NULL;
}

void
start_loading_profile_mutuals(const gchar *username)
{
    if (!g_profile_mutuals_list || !username || !g_auth_token) {
        return;
    }

    populate_user_list(GTK_LIST_BOX(g_profile_mutuals_list), NULL);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->username = g_strdup(username);
    g_thread_new("profile-mutuals-loader", fetch_profile_mutuals_thread, data);
}

static gboolean on_bookmarks_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (async_data->success && async_data->tweets) {
        populate_tweet_list(async_data->list_box, async_data->tweets);
        free_tweets(async_data->tweets);
        async_data->tweets = NULL;
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new(async_data->success ? "No bookmarks yet." : "Failed to load bookmarks.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_bookmarks_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};

    if (fetch_url(BOOKMARKS_LIST_URL, &chunk, NULL, "GET")) {
        // Bookmarks API returns posts similar to regular tweets
        async_data->tweets = parse_tweets(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_idle_add(on_bookmarks_loaded, async_data);
    return NULL;
}

void start_loading_bookmarks(GtkListBox *list_box)
{
    if (!g_auth_token) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    GtkWidget *loading = gtk_label_new("Loading bookmarks...");
    gtk_widget_show(loading);
    gtk_list_box_insert(list_box, loading, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    g_thread_new("bookmarks-loader", fetch_bookmarks_thread, data);
}

gboolean perform_block(const gchar *user_id, gboolean block)
{
    if (!g_auth_token || !user_id) return FALSE;

    const gchar *url = block ? BLOCK_USER_URL : UNBLOCK_USER_URL;

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "userId");
    json_builder_add_string_value(builder, user_id);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;

    if (fetch_url(url, &chunk, post_data, "POST")) {
        success = TRUE;
        g_free(chunk.memory);
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

gboolean perform_mute(const gchar *user_id, gboolean mute)
{
    if (!g_auth_token || !user_id) return FALSE;

    const gchar *url = mute ? MUTE_USER_URL : UNMUTE_USER_URL;

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "userId");
    json_builder_add_string_value(builder, user_id);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;

    if (fetch_url(url, &chunk, post_data, "POST")) {
        success = TRUE;
        g_free(chunk.memory);
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

gboolean check_user_blocked(const gchar *username)
{
    if (!g_auth_token || !username) return FALSE;

    gchar *url = g_strdup_printf(CHECK_BLOCK_URL, username);
    struct MemoryStruct chunk = {0};
    gboolean blocked = FALSE;

    if (fetch_url(url, &chunk, NULL, "GET")) {
        JsonParser *parser = json_parser_new();
        GError *error = NULL;
        json_parser_load_from_data(parser, chunk.memory, -1, &error);
        if (!error) {
            JsonNode *root = json_parser_get_root(parser);
            JsonObject *obj = json_node_get_object(root);
            if (json_object_has_member(obj, "blocked")) {
                blocked = json_object_get_boolean_member(obj, "blocked");
            }
        } else {
            g_error_free(error);
        }
        g_object_unref(parser);
        g_free(chunk.memory);
    }

    g_free(url);
    return blocked;
}

gboolean check_user_muted(const gchar *username)
{
    if (!g_auth_token || !username) return FALSE;

    gchar *url = g_strdup_printf(CHECK_MUTE_URL, username);
    struct MemoryStruct chunk = {0};
    gboolean muted = FALSE;

    if (fetch_url(url, &chunk, NULL, "GET")) {
        JsonParser *parser = json_parser_new();
        GError *error = NULL;
        json_parser_load_from_data(parser, chunk.memory, -1, &error);
        if (!error) {
            JsonNode *root = json_parser_get_root(parser);
            JsonObject *obj = json_node_get_object(root);
            if (json_object_has_member(obj, "muted")) {
                muted = json_object_get_boolean_member(obj, "muted");
            }
        } else {
            g_error_free(error);
        }
        g_object_unref(parser);
        g_free(chunk.memory);
    }

    g_free(url);
    return muted;
}

void set_timeline_type(TimelineType type)
{
    g_current_timeline_type = type;
}

TimelineType get_current_timeline_type(void)
{
    return g_current_timeline_type;
}

void start_loading_timeline(GtkListBox *list_box)
{
    g_mutex_lock(&load_tweets_mutex);
    active_tweets_request_id++;
    g_mutex_unlock(&load_tweets_mutex);

    // Clear the list and show loading indicator
    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    g_object_set_data(G_OBJECT(list_box), "last_id", NULL);

    GtkWidget *loading_label = gtk_label_new("Loading timeline...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->request_id = active_tweets_request_id;
    data->is_append = FALSE;

    g_thread_new("timeline-loader", fetch_tweets_thread, data);
}

gboolean perform_poll_vote(const gchar *tweet_id, const gchar *option_id)
{
    if (!g_auth_token || !tweet_id || !option_id) return FALSE;

    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gchar *url = g_strdup_printf(POLL_VOTE_URL, tweet_id);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "optionId");
    json_builder_add_string_value(builder, option_id);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    if (fetch_url(url, &chunk, post_data, "POST")) {
        success = TRUE;
        g_free(chunk.memory);
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);
    g_free(url);

    return success;
}

void free_poll(struct Poll *poll)
{
    if (!poll) return;

    g_free(poll->id);
    g_free(poll->question);
    g_free(poll->expires_at);

    if (poll->options) {
        g_list_free_full(poll->options, free_poll_option);
    }

    g_free(poll);
}

void free_poll_option(gpointer data)
{
    struct PollOption *option = (struct PollOption *)data;
    if (!option) return;

    g_free(option->id);
    g_free(option->option_text);
    g_free(option->user_vote);
    g_free(option);
}

gboolean perform_update_profile(const gchar *username,
                                const gchar *name,
                                const gchar *bio,
                                const gchar *location,
                                const gchar *website,
                                const gchar *pronouns,
                                const gchar *theme,
                                const gchar *accent_color,
                                const gchar *label_type,
                                gboolean label_automated,
                                gboolean include_avatar_radius,
                                gint avatar_radius)
{
    if (!g_auth_token || !username) return FALSE;

    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gchar *url = g_strdup_printf(UPDATE_PROFILE_URL, username);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, name ? name : "");

    json_builder_set_member_name(builder, "bio");
    json_builder_add_string_value(builder, bio ? bio : "");

    json_builder_set_member_name(builder, "location");
    json_builder_add_string_value(builder, location ? location : "");

    json_builder_set_member_name(builder, "website");
    json_builder_add_string_value(builder, website ? website : "");

    json_builder_set_member_name(builder, "pronouns");
    json_builder_add_string_value(builder, pronouns ? pronouns : "");

    json_builder_set_member_name(builder, "theme");
    json_builder_add_string_value(builder, theme ? theme : "auto");

    json_builder_set_member_name(builder, "accent_color");
    json_builder_add_string_value(builder, accent_color ? accent_color : "");

    json_builder_set_member_name(builder, "label_type");
    if (label_type) {
        json_builder_add_string_value(builder, label_type);
    } else {
        json_builder_add_null_value(builder);
    }

    json_builder_set_member_name(builder, "label_automated");
    json_builder_add_boolean_value(builder, label_automated);

    if (include_avatar_radius) {
        json_builder_set_member_name(builder, "avatar_radius");
        json_builder_add_int_value(builder, avatar_radius);
    }

    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    if (fetch_url(url, &chunk, post_data, "PUT")) {
        success = TRUE;
        g_free(chunk.memory);
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);
    g_free(url);

    return success;
}

gboolean perform_upload_avatar(const gchar *username, const gchar *file_path)
{
    if (!g_auth_token || !username || !file_path) return FALSE;

    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gchar *url = g_strdup_printf(UPDATE_AVATAR_URL, username);

    if (fetch_url_with_file(url, &chunk, file_path, "image")) {
        success = TRUE;
        g_free(chunk.memory);
    }

    g_free(url);
    return success;
}

gboolean perform_upload_banner(const gchar *username, const gchar *file_path)
{
    if (!g_auth_token || !username || !file_path) return FALSE;

    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gchar *url = g_strdup_printf(UPDATE_BANNER_URL, username);

    if (fetch_url_with_file(url, &chunk, file_path, "image")) {
        success = TRUE;
        g_free(chunk.memory);
    }

    g_free(url);
    return success;
}

gchar* perform_media_upload(const gchar *file_path)
{
    g_debug("perform_media_upload: starting upload for file_path=%s", file_path ? file_path : "(null)");
    
    if (!g_auth_token || !file_path) {
        g_debug("perform_media_upload: failed - auth_token=%s, file_path=%s", 
                g_auth_token ? "set" : "(null)", file_path ? file_path : "(null)");
        return NULL;
    }

    struct MemoryStruct chunk = {0};
    gchar *file_url = NULL;

    g_debug("perform_media_upload: calling fetch_url_with_file for UPLOAD_URL");
    if (fetch_url_with_file(UPLOAD_URL, &chunk, file_path, "file")) {
        if (!chunk.memory) {
            g_critical("fetch_url_with_file succeeded but chunk.memory is NULL");
            return NULL;
        }
        
        g_debug("perform_media_upload: upload succeeded, response=%s", chunk.memory);
        
        file_url = parse_upload_response(chunk.memory);
        g_debug("perform_media_upload: extracted file_url=%s", file_url ? file_url : "(null)");
        
        g_free(chunk.memory);
    } else {
        g_debug("perform_media_upload: fetch_url_with_file failed");
    }

    g_debug("perform_media_upload: returning file_url=%s", file_url ? file_url : "(null)");
    return file_url;
}

gboolean perform_join_community(const gchar *community_id)
{
    if (!g_auth_token || !community_id) return FALSE;

    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gchar *url = g_strdup_printf(COMMUNITY_JOIN_URL, community_id);

    if (fetch_url(url, &chunk, "{}", "POST")) {
        success = TRUE;
        g_free(chunk.memory);
    }

    g_free(url);
    return success;
}

gboolean perform_leave_community(const gchar *community_id)
{
    if (!g_auth_token || !community_id) return FALSE;

    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gchar *url = g_strdup_printf(COMMUNITY_LEAVE_URL, community_id);

    if (fetch_url(url, &chunk, "{}", "POST")) {
        success = TRUE;
        g_free(chunk.memory);
    }

    g_free(url);
    return success;
}

gboolean
perform_create_community(const gchar *name,
                         const gchar *description,
                         const gchar *rules,
                         const gchar *access_mode)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *response = NULL;
    gboolean success;

    if (!g_auth_token || !name || name[0] == '\0') {
        return FALSE;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, name);
    json_builder_set_member_name(builder, "description");
    json_builder_add_string_value(builder, description ? description : "");
    json_builder_set_member_name(builder, "rules");
    json_builder_add_string_value(builder, rules ? rules : "");
    json_builder_set_member_name(builder, "access_mode");
    json_builder_add_string_value(builder, access_mode ? access_mode : "open");
    json_builder_end_object(builder);

    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);

    success = perform_request_with_optional_payload(COMMUNITIES_LIST_URL, payload, "POST", &response);

    g_free(response);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

gboolean
perform_update_community(const gchar *community_id,
                         const gchar *name,
                         const gchar *description,
                         const gchar *rules,
                         const gchar *access_mode)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *url;
    gchar *response = NULL;
    gboolean success;

    if (!g_auth_token || !community_id || !name || name[0] == '\0') {
        return FALSE;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, name);
    json_builder_set_member_name(builder, "description");
    json_builder_add_string_value(builder, description ? description : "");
    json_builder_set_member_name(builder, "rules");
    json_builder_add_string_value(builder, rules ? rules : "");
    json_builder_set_member_name(builder, "access_mode");
    json_builder_add_string_value(builder, access_mode ? access_mode : "open");
    json_builder_end_object(builder);

    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);

    url = g_strdup_printf(COMMUNITY_DETAILS_URL, community_id);
    success = perform_request_with_optional_payload(url, payload, "PATCH", &response);

    g_free(response);
    g_free(url);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

gboolean
perform_delete_community(const gchar *community_id)
{
    gchar *url;
    gchar *response = NULL;
    gboolean success;

    if (!g_auth_token || !community_id) {
        return FALSE;
    }

    url = g_strdup_printf(COMMUNITY_DETAILS_URL, community_id);
    success = perform_request_with_optional_payload(url, NULL, "DELETE", &response);
    g_free(response);
    g_free(url);
    return success;
}

gboolean
perform_update_community_access_mode(const gchar *community_id, const gchar *access_mode)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *url;
    gchar *response = NULL;
    gboolean success;

    if (!g_auth_token || !community_id || !access_mode) {
        return FALSE;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "access_mode");
    json_builder_add_string_value(builder, access_mode);
    json_builder_end_object(builder);

    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);

    url = g_strdup_printf(COMMUNITY_ACCESS_MODE_URL, community_id);
    success = perform_request_with_optional_payload(url, payload, "PATCH", &response);

    g_free(response);
    g_free(url);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

static gboolean
on_community_details_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (async_data->success && async_data->communities) {
        struct Community *community = async_data->communities->data;
        gchar *details = build_community_details_text(community);

        if (g_community_title_label) {
            gtk_label_set_text(GTK_LABEL(g_community_title_label),
                               community->name ? community->name : "Community");
        }
        if (g_community_details_label) {
            gtk_label_set_text(GTK_LABEL(g_community_details_label), details);
        }
        if (g_community_tweets_list) {
            g_object_set_data_full(G_OBJECT(g_community_tweets_list), "community_name", g_strdup(community->name), g_free);
            g_object_set_data_full(G_OBJECT(g_community_tweets_list), "community_description", g_strdup(community->description), g_free);
            g_object_set_data_full(G_OBJECT(g_community_tweets_list), "community_rules", g_strdup(community->rules), g_free);
            g_object_set_data_full(G_OBJECT(g_community_tweets_list), "community_access_mode", g_strdup(community->access_mode), g_free);
        }

        g_free(details);
    } else {
        if (g_community_title_label) {
            gtk_label_set_text(GTK_LABEL(g_community_title_label), "Community");
        }
        if (g_community_details_label) {
            gtk_label_set_text(GTK_LABEL(g_community_details_label), "");
        }
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_community_details_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(COMMUNITY_DETAILS_URL, async_data->community_id);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        struct Community *community = parse_community_details(chunk.memory);
        if (community) {
            async_data->communities = g_list_append(NULL, community);
            async_data->success = TRUE;
        } else {
            async_data->success = FALSE;
        }
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_community_details_loaded, async_data);
    return NULL;
}

void
start_loading_community_details(const gchar *community_id)
{
    struct AsyncData *data;

    if (!community_id) {
        return;
    }

    data = g_new0(struct AsyncData, 1);
    data->community_id = g_strdup(community_id);
    g_thread_new("community-details-loader", fetch_community_details_thread, data);
}

static gboolean
on_community_members_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (async_data->success && async_data->users) {
        populate_user_list(GTK_LIST_BOX(async_data->list_box), async_data->users);
        free_users(async_data->users);
        async_data->users = NULL;
    } else {
        populate_user_list(GTK_LIST_BOX(async_data->list_box), NULL);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_community_members_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *base_url = g_strdup_printf(COMMUNITY_MEMBERS_URL, async_data->community_id);
    gchar *url = g_strdup_printf("%s?limit=100", base_url);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->users = parse_users(chunk.memory);
        async_data->success = (async_data->users != NULL);
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(base_url);
    g_free(url);
    g_idle_add(on_community_members_loaded, async_data);
    return NULL;
}

void
start_loading_community_members(const gchar *community_id, GtkListBox *list_box)
{
    struct AsyncData *data;

    if (!community_id || !list_box) {
        return;
    }

    populate_user_list(list_box, NULL);

    data = g_new0(struct AsyncData, 1);
    data->community_id = g_strdup(community_id);
    data->list_box = list_box;
    g_thread_new("community-members-loader", fetch_community_members_thread, data);
}

static gboolean on_communities_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    if (async_data->success) {
        populate_community_list(async_data->list_box, async_data->communities);
        free_communities(async_data->communities);
        async_data->communities = NULL;
    } else {
        GtkWidget *error_label = gtk_label_new("Failed to load communities");
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_communities_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};

    if (fetch_url(COMMUNITIES_LIST_URL, &chunk, NULL, "GET")) {
        async_data->communities = parse_communities(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_idle_add(on_communities_loaded, async_data);
    return NULL;
}

void start_loading_communities(GtkListBox *list_box)
{
    if (!g_auth_token) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    GtkWidget *loading = gtk_label_new("Loading communities...");
    gtk_widget_show(loading);
    gtk_list_box_insert(list_box, loading, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    g_thread_new("communities-loader", fetch_communities_thread, data);
}

static gboolean on_community_tweets_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    if (async_data->success && async_data->tweets) {
        populate_tweet_list(async_data->list_box, async_data->tweets);
        free_tweets(async_data->tweets);
        async_data->tweets = NULL;
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);
        
        GtkWidget *error_label = gtk_label_new("Failed to load community tweets.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }
    
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_community_tweets_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    
    gchar *url = g_strdup_printf(COMMUNITY_TWEETS_URL, async_data->community_id);
    
    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->tweets = parse_tweets(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }
    
    g_free(url);
    g_idle_add(on_community_tweets_loaded, async_data);
    return NULL;
}

void start_loading_community_tweets(GtkListBox *list_box, const gchar *community_id)
{
    if (!g_auth_token || !community_id) return;
    
    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);
    
    GtkWidget *loading = gtk_label_new("Loading community tweets...");
    gtk_widget_show(loading);
    gtk_list_box_insert(list_box, loading, -1);
    
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->community_id = g_strdup(community_id);
    g_thread_new("community-tweets-loader", fetch_community_tweets_thread, data);

    start_loading_community_details(community_id);
}

void update_interaction_cache(const gchar *tweet_id, gboolean liked, gboolean retweeted, gboolean bookmarked)
{
    if (!tweet_id) return;
    
    g_mutex_lock(&g_globals_mutex);
    
    struct InteractionState *cached = NULL;
    
    if (g_interaction_cache) {
        cached = g_hash_table_lookup(g_interaction_cache, tweet_id);
    }
    
    if (cached == NULL) {
        cached = g_malloc(sizeof(struct InteractionState));
        cached->liked = FALSE;
        cached->retweeted = FALSE;
        cached->bookmarked = FALSE;
        g_hash_table_insert(g_interaction_cache, g_strdup(tweet_id), cached);
    } else {
        if (liked >= 0) cached->liked = liked;
        if (retweeted >= 0) cached->retweeted = retweeted;
        if (bookmarked >= 0) cached->bookmarked = bookmarked;
    }
    
    g_mutex_unlock(&g_globals_mutex);
}

gboolean get_cached_liked(const gchar *tweet_id)
{
    if (!tweet_id || !g_interaction_cache) return FALSE;
    
    g_mutex_lock(&g_globals_mutex);
    struct InteractionState *cached = g_hash_table_lookup(g_interaction_cache, tweet_id);
    gboolean result = cached ? cached->liked : FALSE;
    g_mutex_unlock(&g_globals_mutex);
    
    return result;
}

gboolean get_cached_retweeted(const gchar *tweet_id)
{
    if (!tweet_id || !g_interaction_cache) return FALSE;
    
    g_mutex_lock(&g_globals_mutex);
    struct InteractionState *cached = g_hash_table_lookup(g_interaction_cache, tweet_id);
    gboolean result = cached ? cached->retweeted : FALSE;
    g_mutex_unlock(&g_globals_mutex);
    
    return result;
}

gboolean get_cached_bookmarked(const gchar *tweet_id)
{
    if (!tweet_id || !g_interaction_cache) return FALSE;
    
    g_mutex_lock(&g_globals_mutex);
    struct InteractionState *cached = g_hash_table_lookup(g_interaction_cache, tweet_id);
    gboolean result = cached ? cached->bookmarked : FALSE;
    g_mutex_unlock(&g_globals_mutex);
    
    return result;
}

void on_theme_changed(GtkComboBox *combo, gpointer user_data)
{
    (void)user_data;
    gint active = gtk_combo_box_get_active(combo);
    g_theme_preference = active;
    
    GtkSettings *settings = gtk_settings_get_default();
    
    switch (active) {
        case 0:
            g_object_set(settings, "gtk-application-prefer-dark-theme", FALSE, NULL);
            break;
        case 1:
            g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);
            break;
        case 2:
        default:
            g_object_set(settings, "gtk-application-prefer-dark-theme", FALSE, NULL);
            break;
    }
    
    g_debug("Theme changed to: %d", active);
}

void on_compact_mode_toggled(GtkSwitch *switch_widget, gboolean state, gpointer user_data)
{
    (void)switch_widget;
    (void)user_data;
    g_compact_mode_enabled = state;
    g_debug("Compact mode: %s", state ? "enabled" : "disabled");
}

void on_notifications_enabled_toggled(GtkSwitch *switch_widget, gboolean state, gpointer user_data)
{
    (void)switch_widget;
    (void)user_data;
    g_notifications_enabled = state;
    
    if (g_sound_notifications_switch) {
        gtk_widget_set_sensitive(g_sound_notifications_switch, state);
    }
    if (g_dm_notifications_switch) {
        gtk_widget_set_sensitive(g_dm_notifications_switch, state);
    }
    
    g_debug("Notifications: %s", state ? "enabled" : "disabled");
}

static gchar* get_cache_directory(void)
{
    return g_build_filename(g_get_user_cache_dir(), "tweeta-desktop", NULL);
}

static guint64 calculate_directory_size(const gchar *path)
{
    guint64 total_size = 0;
    GDir *dir = g_dir_open(path, 0, NULL);
    
    if (!dir) return 0;
    
    const gchar *filename;
    while ((filename = g_dir_read_name(dir)) != NULL) {
        gchar *full_path = g_build_filename(path, filename, NULL);
        
        if (g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
            total_size += calculate_directory_size(full_path);
        } else {
            GFileInfo *info = g_file_query_info(
                g_file_new_for_path(full_path),
                G_FILE_ATTRIBUTE_STANDARD_SIZE,
                G_FILE_QUERY_INFO_NONE,
                NULL,
                NULL
            );
            
            if (info) {
                total_size += g_file_info_get_size(info);
                g_object_unref(info);
            }
        }
        
        g_free(full_path);
    }
    
    g_dir_close(dir);
    return total_size;
}

void refresh_cache_size_display(void)
{
    if (!g_cache_size_label) return;
    
    gchar *cache_dir = get_cache_directory();
    guint64 size = calculate_directory_size(cache_dir);
    g_free(cache_dir);
    
    gchar *size_str;
    if (size < 1024) {
        size_str = g_strdup_printf("Cache size: %" G_GUINT64_FORMAT " bytes", size);
    } else if (size < 1024 * 1024) {
        size_str = g_strdup_printf("Cache size: %.2f KB", size / 1024.0);
    } else {
        size_str = g_strdup_printf("Cache size: %.2f MB", size / (1024.0 * 1024.0));
    }
    
    gtk_label_set_text(GTK_LABEL(g_cache_size_label), size_str);
    g_free(size_str);
}

void on_clear_cache_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    
    gchar *cache_dir = get_cache_directory();
    
    GDir *dir = g_dir_open(cache_dir, 0, NULL);
    if (dir) {
        const gchar *filename;
        while ((filename = g_dir_read_name(dir)) != NULL) {
            gchar *full_path = g_build_filename(cache_dir, filename, NULL);
            
            if (g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
                GFile *file = g_file_new_for_path(full_path);
                g_file_trash(file, NULL, NULL);
                g_object_unref(file);
            } else {
                remove(full_path);
            }
            
            g_free(full_path);
        }
        g_dir_close(dir);
    }
    
    g_free(cache_dir);
    
    refresh_cache_size_display();
    
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    if (GTK_IS_WINDOW(toplevel)) {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(toplevel),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_CLOSE,
            "Cache cleared successfully."
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void on_clear_history_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    if (!GTK_IS_WINDOW(toplevel)) return;
    
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(toplevel),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Clear all search history?"
    );
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "This action cannot be undone."
    );
    
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        GtkWidget *confirm_dialog = gtk_message_dialog_new(
            GTK_WINDOW(toplevel),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_CLOSE,
            "Search history cleared."
        );
        gtk_dialog_run(GTK_DIALOG(confirm_dialog));
        gtk_widget_destroy(confirm_dialog);
    }
}

void on_change_password_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    if (!GTK_IS_WINDOW(toplevel)) return;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Change Password",
        GTK_WINDOW(toplevel),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Change", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 20);
    
    GtkWidget *current_label = gtk_label_new("Current password:");
    gtk_widget_set_halign(current_label, GTK_ALIGN_START);
    GtkWidget *current_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(current_entry), FALSE);
    
    GtkWidget *new_label = gtk_label_new("New password:");
    gtk_widget_set_halign(new_label, GTK_ALIGN_START);
    GtkWidget *new_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(new_entry), FALSE);
    
    GtkWidget *confirm_label = gtk_label_new("Confirm password:");
    gtk_widget_set_halign(confirm_label, GTK_ALIGN_START);
    GtkWidget *confirm_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(confirm_entry), FALSE);
    
    gtk_grid_attach(GTK_GRID(grid), current_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), current_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), new_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), new_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), confirm_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), confirm_entry, 1, 2, 1, 1);
    
    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);
    
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_ACCEPT) {
        const gchar *current = gtk_entry_get_text(GTK_ENTRY(current_entry));
        const gchar *new_pw = gtk_entry_get_text(GTK_ENTRY(new_entry));
        const gchar *confirm = gtk_entry_get_text(GTK_ENTRY(confirm_entry));
        
        if (strlen(new_pw) < 8) {
            GtkWidget *error = gtk_message_dialog_new(
                GTK_WINDOW(toplevel),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Password must be at least 8 characters long."
            );
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);
        } else if (!g_str_equal(new_pw, confirm)) {
            GtkWidget *error = gtk_message_dialog_new(
                GTK_WINDOW(toplevel),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "New passwords do not match."
            );
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);
        } else if (strlen(current) > 0) {
            GtkWidget *info = gtk_message_dialog_new(
                GTK_WINDOW(toplevel),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_CLOSE,
                "Password change functionality not yet implemented."
            );
            gtk_dialog_run(GTK_DIALOG(info));
            gtk_widget_destroy(info);
        }
    }
    
    gtk_widget_destroy(dialog);
}

void on_logout_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    
    perform_logout();
}

void update_settings_username_display(void)
{
    if (!g_settings_username_label) return;
    
    g_mutex_lock(&g_globals_mutex);
    if (g_current_username) {
        gchar *text = g_strdup_printf("Logged in as: @%s", g_current_username);
        gtk_label_set_text(GTK_LABEL(g_settings_username_label), text);
        g_free(text);
    } else {
        gtk_label_set_text(GTK_LABEL(g_settings_username_label), "Not logged in");
    }
    g_mutex_unlock(&g_globals_mutex);
}
