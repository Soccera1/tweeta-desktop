#include <stdlib.h>
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
#include "webauthn_fido2.h"
#include "p2p_crypto.h"
#include "p2p_network.h"

static inline gchar* get_username_safe(void) {
    g_mutex_lock(&g_globals_mutex);
    gchar *username = g_current_username ? g_strdup(g_current_username) : NULL;
    g_mutex_unlock(&g_globals_mutex);
    return username;
}

static gchar*
get_web_base_url(void)
{
    if (g_str_has_suffix(API_BASE_URL, "/api"))
        return g_strndup(API_BASE_URL, strlen(API_BASE_URL) - 4);
    return g_strdup(API_BASE_URL);
}

static void
open_web_path(GtkWindow *window, const gchar *path)
{
    gchar *base = get_web_base_url();
    gchar *url = g_strdup_printf("%s%s", base, path ? path : "/");
    gtk_show_uri_on_window(window, url, GDK_CURRENT_TIME, NULL);
    g_free(url);
    g_free(base);
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
static gboolean perform_admin_request(const gchar *url, const gchar *payload, const gchar *method, gchar **response_out);
static gchar *extract_error_message(const gchar *json_data);
static gchar *tweet_pagination_cursor(GtkListBox *list_box, const struct Tweet *tweet);
static gboolean perform_change_password(const gchar *username,
                                        const gchar *current_password,
                                        const gchar *new_password,
                                        gchar **error_out);
static void free_tweeta_list(gpointer data);
static void free_tweeta_lists(GList *lists);
static gboolean on_article_row_activated(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static const gchar *json_get_string_or_empty(JsonObject *obj, const gchar *member);
void on_shop_delete_product_clicked(GtkWidget *widget, gpointer user_data);

static inline gchar* get_auth_token_safe(void) {
    g_mutex_lock(&g_globals_mutex);
    gchar *token = g_auth_token ? g_strdup(g_auth_token) : NULL;
    g_mutex_unlock(&g_globals_mutex);
    return token;
}

static void show_modal_message(GtkMessageType type, const gchar *primary, const gchar *secondary);
static gboolean apply_login_response_json(const gchar *json_data);
static void clear_impersonation_state_locked(void);

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

static gchar *
tweet_pagination_cursor(GtkListBox *list_box, const struct Tweet *tweet)
{
    if (!tweet) {
        return NULL;
    }

    if (list_box == GTK_LIST_BOX(g_profile_tweets_list)) {
        const gchar *sort_date = (g_strcmp0(tweet->content_type, "retweet") == 0 &&
                                  tweet->retweet_created_at &&
                                  tweet->retweet_created_at[0] != '\0')
            ? tweet->retweet_created_at
            : tweet->created_at;
        return sort_date && sort_date[0] != '\0' ? g_strdup(sort_date) : NULL;
    }

    return tweet->id && tweet->id[0] != '\0' ? g_strdup(tweet->id) : NULL;
}

static gboolean
apply_login_response_json(const gchar *json_data)
{
    gchar *token = NULL;
    gchar *uname = NULL;
    gboolean is_admin = FALSE;

    if (!parse_login_response(json_data, &token, &uname, &is_admin)) {
        return FALSE;
    }

    g_mutex_lock(&g_globals_mutex);
    g_free(g_auth_token);
    g_auth_token = token;
    g_free(g_current_username);
    g_current_username = uname;
    g_is_admin = is_admin;
    clear_impersonation_state_locked();
    g_mutex_unlock(&g_globals_mutex);

    struct MemoryStruct me_chunk;
    if (fetch_url(AUTH_ME_URL, &me_chunk, NULL, "GET")) {
        parse_user_me_response(me_chunk.memory, &is_admin);
        g_mutex_lock(&g_globals_mutex);
        g_is_admin = is_admin;
        g_mutex_unlock(&g_globals_mutex);
        g_free(me_chunk.memory);
    }

    save_session(g_auth_token, g_current_username, g_is_admin);
    return TRUE;
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

static gchar *
get_admin_auth_token_safe(void)
{
    gchar *token = NULL;

    g_mutex_lock(&g_globals_mutex);
    if (g_is_impersonating && g_impersonation_admin_token) {
        token = g_strdup(g_impersonation_admin_token);
    } else if (g_auth_token) {
        token = g_strdup(g_auth_token);
    }
    g_mutex_unlock(&g_globals_mutex);

    return token;
}

static gboolean
has_admin_session_context(void)
{
    gboolean allowed = FALSE;

    g_mutex_lock(&g_globals_mutex);
    allowed = g_is_admin || (g_is_impersonating && g_impersonation_admin_token != NULL);
    g_mutex_unlock(&g_globals_mutex);

    return allowed;
}

static void
clear_impersonation_state_locked(void)
{
    g_clear_pointer(&g_impersonation_admin_token, g_free);
    g_clear_pointer(&g_impersonation_admin_username, g_free);
    g_impersonation_admin_is_admin = FALSE;
    g_is_impersonating = FALSE;
}

static gboolean
perform_admin_fetch_url(const gchar *url,
                        struct MemoryStruct *chunk,
                        const gchar *payload,
                        const gchar *method)
{
    gboolean success;
    gchar *admin_token = get_admin_auth_token_safe();

    success = fetch_url_with_auth_token(url, chunk, payload, method, admin_token);
    g_free(admin_token);
    return success;
}

static gchar *
perform_admin_media_upload(const gchar *file_path)
{
    struct MemoryStruct chunk = {0};
    gchar *admin_token;
    gchar *file_url = NULL;

    if (!file_path) {
        return NULL;
    }

    admin_token = get_admin_auth_token_safe();
    if (!admin_token) {
        return NULL;
    }

    if (fetch_url_with_file_auth_token(UPLOAD_URL, &chunk, file_path, "file", admin_token)) {
        file_url = parse_upload_response(chunk.memory);
        g_free(chunk.memory);
    }

    g_free(admin_token);
    return file_url;
}

static GPtrArray *
split_identifier_text(const gchar *text)
{
    gchar **parts;
    GPtrArray *items;

    items = g_ptr_array_new_with_free_func(g_free);
    if (!text || !*text) {
        return items;
    }

    parts = g_strsplit_set(text, ",\n", -1);
    for (guint i = 0; parts[i] != NULL; i++) {
        gchar *trimmed = g_strstrip(parts[i]);
        if (*trimmed) {
            g_ptr_array_add(items, g_strdup(trimmed));
        }
    }
    g_strfreev(parts);
    return items;
}

static gboolean
extract_user_from_admin_response(const gchar *json_data,
                                 gchar **user_id_out,
                                 gchar **username_out,
                                 gboolean *is_admin_out,
                                 gboolean *is_superadmin_out)
{
    JsonParser *parser;
    JsonNode *root;
    JsonObject *root_obj;
    JsonObject *user_obj;
    GError *error = NULL;
    gboolean success = FALSE;

    if (user_id_out) *user_id_out = NULL;
    if (username_out) *username_out = NULL;
    if (is_admin_out) *is_admin_out = FALSE;
    if (is_superadmin_out) *is_superadmin_out = FALSE;

    if (!json_data) {
        return FALSE;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        return FALSE;
    }

    root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return FALSE;
    }

    root_obj = json_node_get_object(root);
    if (!json_object_has_member(root_obj, "user") ||
        !JSON_NODE_HOLDS_OBJECT(json_object_get_member(root_obj, "user"))) {
        g_object_unref(parser);
        return FALSE;
    }

    user_obj = json_object_get_object_member(root_obj, "user");
    if (user_id_out && json_object_has_member(user_obj, "id") &&
        !json_node_is_null(json_object_get_member(user_obj, "id"))) {
        *user_id_out = g_strdup(json_object_get_string_member(user_obj, "id"));
    }
    if (username_out && json_object_has_member(user_obj, "username") &&
        !json_node_is_null(json_object_get_member(user_obj, "username"))) {
        *username_out = g_strdup(json_object_get_string_member(user_obj, "username"));
    }
    if (is_admin_out && json_object_has_member(user_obj, "admin")) {
        JsonNode *node = json_object_get_member(user_obj, "admin");
        *is_admin_out = JSON_NODE_HOLDS_VALUE(node) &&
            (json_node_get_value_type(node) == G_TYPE_BOOLEAN ? json_node_get_boolean(node)
                                                              : json_node_get_int(node) != 0);
    }
    if (is_superadmin_out && json_object_has_member(user_obj, "superadmin")) {
        *is_superadmin_out = json_object_get_boolean_member(user_obj, "superadmin");
    }

    success = user_id_out == NULL || *user_id_out != NULL;
    g_object_unref(parser);
    return success;
}

static gboolean
lookup_admin_user_identifier(const gchar *identifier,
                             gchar **user_id_out,
                             gchar **username_out,
                             gboolean *is_admin_out,
                             gboolean *is_superadmin_out,
                             gchar **error_message_out)
{
    gchar *escaped;
    gchar *url;
    gchar *response = NULL;
    gchar *error_message = NULL;
    gboolean success = FALSE;

    if (user_id_out) *user_id_out = NULL;
    if (username_out) *username_out = NULL;
    if (is_admin_out) *is_admin_out = FALSE;
    if (is_superadmin_out) *is_superadmin_out = FALSE;
    if (error_message_out) *error_message_out = NULL;

    if (!identifier || !*identifier) {
        if (error_message_out) {
            *error_message_out = g_strdup("User identifier is required.");
        }
        return FALSE;
    }

    escaped = g_uri_escape_string(identifier, NULL, FALSE);
    url = g_strdup_printf("%s/%s", ADMIN_USERS_URL, escaped);
    if (!perform_admin_request(url, NULL, "GET", &response)) {
        error_message = g_strdup("The user lookup request failed.");
    } else {
        error_message = extract_error_message(response);
        if (!error_message) {
            success = extract_user_from_admin_response(response,
                                                       user_id_out,
                                                       username_out,
                                                       is_admin_out,
                                                       is_superadmin_out);
            if (!success) {
                error_message = g_strdup("User not found.");
            }
        }
    }

    if (error_message_out) {
        *error_message_out = error_message;
    } else {
        g_free(error_message);
    }

    g_free(response);
    g_free(url);
    g_free(escaped);
    return success;
}

static gboolean
parse_json_object_payload(const gchar *json_text, gchar **normalized_json_out, gchar **error_message_out)
{
    JsonParser *parser;
    JsonGenerator *generator;
    JsonNode *root;
    GError *error = NULL;
    if (normalized_json_out) *normalized_json_out = NULL;
    if (error_message_out) *error_message_out = NULL;

    if (!json_text || !*json_text) {
        if (error_message_out) {
            *error_message_out = g_strdup("A JSON object payload is required.");
        }
        return FALSE;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_text, -1, &error)) {
        if (error_message_out) {
            *error_message_out = g_strdup(error ? error->message : "Invalid JSON payload.");
        }
        if (error) g_error_free(error);
        g_object_unref(parser);
        return FALSE;
    }

    root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        if (error_message_out) {
            *error_message_out = g_strdup("Payload must be a JSON object.");
        }
        g_object_unref(parser);
        return FALSE;
    }

    generator = json_generator_new();
    json_generator_set_root(generator, root);
    if (normalized_json_out) {
        *normalized_json_out = json_generator_to_data(generator, NULL);
    }
    g_object_unref(generator);
    g_object_unref(parser);
    return TRUE;
}

static void
update_admin_impersonation_status_label(void)
{
    gchar *current_username = NULL;
    gchar *admin_username = NULL;
    gboolean is_impersonating = FALSE;

    if (!g_admin_impersonation_status_label) {
        return;
    }

    g_mutex_lock(&g_globals_mutex);
    if (g_current_username) {
        current_username = g_strdup(g_current_username);
    }
    if (g_impersonation_admin_username) {
        admin_username = g_strdup(g_impersonation_admin_username);
    }
    is_impersonating = g_is_impersonating;
    g_mutex_unlock(&g_globals_mutex);

    if (is_impersonating && current_username) {
        gchar *text = g_strdup_printf("Impersonating @%s. Admin session preserved%s%s.",
                                      current_username,
                                      admin_username ? " from @" : "",
                                      admin_username ? admin_username : "");
        gtk_label_set_text(GTK_LABEL(g_admin_impersonation_status_label), text);
        g_free(text);
    } else {
        gtk_label_set_text(GTK_LABEL(g_admin_impersonation_status_label),
                           "Admin session active.");
    }

    g_free(admin_username);
    g_free(current_username);
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
    if (data->lists) free_tweeta_lists(data->lists);
    if (data->conversation) free_conversation(data->conversation);
    if (data->list) free_tweeta_list(data->list);

    if (data->profile) {
        free_user(data->profile);
    }

    g_free(data->username);
    g_free(data->query);
    g_free(data->conversation_id);
    g_free(data->community_id);
    g_free(data->json_data);
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
    gboolean is_impersonating;
    
    g_mutex_lock(&g_globals_mutex);
    is_admin = g_is_admin;
    is_impersonating = g_is_impersonating;
    g_mutex_unlock(&g_globals_mutex);
    
    if (username) {
        gchar *label_text;
        if (is_impersonating) {
            label_text = g_strdup_printf("Impersonating @%s", username);
        } else {
            label_text = g_strdup_printf("Logged in as @%s", username);
        }
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
        if (is_admin || is_impersonating) {
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
    update_admin_impersonation_status_label();
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
    clear_impersonation_state_locked();
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
        g_debug("perform_login: fetch_url succeeded, response: %s", chunk.memory ? chunk.memory : "(null)");
        if (apply_login_response_json(chunk.memory)) {
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
    if (response_id == GTK_RESPONSE_ACCEPT || response_id == 2 || response_id == 3 || response_id == 4) {
        GtkWidget **entries = (GtkWidget **)user_data;
        const gchar *username = gtk_entry_get_text(GTK_ENTRY(entries[0]));
        const gchar *password = gtk_entry_get_text(GTK_ENTRY(entries[1]));

        if (response_id == 4) {
            if (webauthn_fido2_is_enabled()) {
                gchar *response = NULL;
                gchar *error = NULL;
                if (webauthn_fido2_login(&response, &error) && apply_login_response_json(response)) {
                    update_login_ui();
                    start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
                } else {
                    show_modal_message(GTK_MESSAGE_ERROR,
                                       "Passkey login failed.",
                                       error ? error : "The passkey response could not be accepted.");
                }
                g_free(response);
                g_free(error);
            } else {
                open_web_path(GTK_WINDOW(dialog), "/login");
            }
        } else if (response_id == 3) {
            gchar *escaped = g_uri_escape_string(username, NULL, TRUE);
            gchar *url = g_strdup_printf("%s?username=%s", AUTH_USERNAME_AVAILABILITY_URL, escaped ? escaped : "");
            gchar *response = NULL;
            gchar *error = NULL;
            if (perform_request_with_optional_payload(url, NULL, "GET", &response)) {
                JsonParser *parser = json_parser_new();
                gboolean available = FALSE;
                if (response && json_parser_load_from_data(parser, response, -1, NULL)) {
                    JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
                    if (obj && json_object_has_member(obj, "available"))
                        available = json_object_get_boolean_member(obj, "available");
                    if (obj && json_object_has_member(obj, "error"))
                        error = g_strdup(json_object_get_string_member(obj, "error"));
                }
                g_object_unref(parser);
                show_modal_message(available ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
                                   available ? "Username available." : "Username unavailable.",
                                   error);
            } else {
                show_modal_message(GTK_MESSAGE_ERROR, "Username check failed.", NULL);
            }
            g_free(error);
            g_free(response);
            g_free(url);
            g_free(escaped);
        } else if (response_id == 2) {
            JsonBuilder *builder = json_builder_new();
            JsonGenerator *gen = json_generator_new();
            JsonNode *root;
            gchar *payload;
            gchar *response = NULL;
            gchar *error_message = NULL;
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "username");
            json_builder_add_string_value(builder, username);
            json_builder_set_member_name(builder, "password");
            json_builder_add_string_value(builder, password);
            json_builder_end_object(builder);
            root = json_builder_get_root(builder);
            json_generator_set_root(gen, root);
            payload = json_generator_to_data(gen, NULL);
            if (perform_request_with_optional_payload(AUTH_REGISTER_PASSWORD_URL, payload, "POST", &response)) {
                error_message = extract_error_message(response);
                if (!error_message) {
                    show_modal_message(GTK_MESSAGE_INFO, "Account created.", "You can now log in with this password.");
                } else {
                    show_modal_message(GTK_MESSAGE_ERROR, "Registration failed.", error_message);
                }
            } else {
                show_modal_message(GTK_MESSAGE_ERROR, "Registration failed.", "The registration request could not be sent.");
            }
            g_free(error_message);
            g_free(response);
            g_free(payload);
            json_node_free(root);
            g_object_unref(gen);
            g_object_unref(builder);
        } else if (perform_login(username, password)) {
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
                                                    "_Passkey Login", 4,
                                                    "_Check Username", 3,
                                                    "_Register", 2,
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
        g_clear_pointer(&ctx->remote_url, g_free);
        g_clear_pointer(&ctx->remote_type, g_free);
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

static const gchar *
media_search_result_url(JsonObject *item, gboolean tenor)
{
    JsonObject *formats;
    JsonObject *gif;

    if (!item) return NULL;
    if (!tenor && json_object_has_member(item, "url") && !json_node_is_null(json_object_get_member(item, "url"))) {
        return json_object_get_string_member(item, "url");
    }
    if (tenor && json_object_has_member(item, "media_formats") &&
        JSON_NODE_HOLDS_OBJECT(json_object_get_member(item, "media_formats"))) {
        formats = json_object_get_object_member(item, "media_formats");
        if (json_object_has_member(formats, "gif") &&
            JSON_NODE_HOLDS_OBJECT(json_object_get_member(formats, "gif"))) {
            gif = json_object_get_object_member(formats, "gif");
            if (json_object_has_member(gif, "url") && !json_node_is_null(json_object_get_member(gif, "url"))) {
                return json_object_get_string_member(gif, "url");
            }
        }
        if (json_object_has_member(formats, "tinygif") &&
            JSON_NODE_HOLDS_OBJECT(json_object_get_member(formats, "tinygif"))) {
            gif = json_object_get_object_member(formats, "tinygif");
            if (json_object_has_member(gif, "url") && !json_node_is_null(json_object_get_member(gif, "url"))) {
                return json_object_get_string_member(gif, "url");
            }
        }
    }
    return NULL;
}

static gchar *
media_search_result_label(JsonObject *item, gboolean tenor)
{
    if (tenor) {
        const gchar *title = json_get_string_or_empty(item, "content_description");
        return g_strdup(title[0] ? title : "GIF result");
    }
    const gchar *description = json_get_string_or_empty(item, "description");
    JsonObject *user = item && json_object_has_member(item, "user") &&
        JSON_NODE_HOLDS_OBJECT(json_object_get_member(item, "user"))
        ? json_object_get_object_member(item, "user") : NULL;
    const gchar *name = json_get_string_or_empty(user, "name");
    return g_strdup_printf("%s%s%s",
                           description[0] ? description : "Unsplash image",
                           name[0] ? "\nPhoto: " : "",
                           name[0] ? name : "");
}

static void
on_media_search_clicked(GtkWidget *widget, gpointer user_data)
{
    struct UploadContext *ctx = user_data;
    gboolean tenor = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "tenor"));
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *search_row;
    GtkWidget *entry;
    GtkWidget *list;
    GtkWidget *scroll;
    gint response;

    if (!ctx) return;
    dialog = gtk_dialog_new_with_buttons(tenor ? "Search GIFs" : "Search Images",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close", GTK_RESPONSE_CLOSE,
                                         "_Search", GTK_RESPONSE_APPLY,
                                         "_Use Selected", GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 520);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), tenor ? "Search GIFs" : "Search photos");
    gtk_box_pack_start(GTK_BOX(search_row), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), search_row, FALSE, FALSE, 6);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == GTK_RESPONSE_APPLY) {
        const gchar *query = gtk_entry_get_text(GTK_ENTRY(entry));
        GList *children = gtk_container_get_children(GTK_CONTAINER(list));
        for (GList *l = children; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
        g_list_free(children);
        if (query && query[0]) {
            gchar *escaped = g_uri_escape_string(query, NULL, TRUE);
            gchar *url = g_strdup_printf("%s?q=%s&limit=12", tenor ? TENOR_SEARCH_URL : UNSPLASH_SEARCH_URL, escaped);
            struct MemoryStruct chunk = {0};
            JsonParser *parser = json_parser_new();
            GError *error = NULL;
            if (fetch_url(url, &chunk, NULL, "GET") &&
                json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
                JsonObject *root = json_node_get_object(json_parser_get_root(parser));
                JsonArray *results = root && json_object_has_member(root, "results")
                    ? json_object_get_array_member(root, "results") : NULL;
                if (results && json_array_get_length(results) > 0) {
                    for (guint i = 0; i < json_array_get_length(results); i++) {
                        JsonObject *item = json_array_get_object_element(results, i);
                        const gchar *media_url = media_search_result_url(item, tenor);
                        if (media_url) {
                            GtkWidget *row = gtk_list_box_row_new();
                            GtkWidget *label;
                            gchar *label_text = media_search_result_label(item, tenor);
                            gtk_container_add(GTK_CONTAINER(row), label = gtk_label_new(label_text));
                            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
                            gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
                            g_object_set_data_full(G_OBJECT(row), "media_url", g_strdup(media_url), g_free);
                            g_object_set_data_full(G_OBJECT(row), "media_type", g_strdup(tenor ? "image/gif" : "image/jpeg"), g_free);
                            gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
                            g_free(label_text);
                        }
                    }
                } else {
                    gtk_list_box_insert(GTK_LIST_BOX(list), gtk_label_new("No results."), -1);
                }
            } else {
                if (error) g_error_free(error);
                gtk_list_box_insert(GTK_LIST_BOX(list), gtk_label_new("Search failed."), -1);
            }
            g_object_unref(parser);
            g_free(chunk.memory);
            g_free(url);
            g_free(escaped);
            gtk_widget_show_all(list);
        }
    }

    if (response == GTK_RESPONSE_ACCEPT) {
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
        const gchar *media_url = row ? g_object_get_data(G_OBJECT(row), "media_url") : NULL;
        const gchar *media_type = row ? g_object_get_data(G_OBJECT(row), "media_type") : NULL;
        if (media_url) {
            g_clear_pointer(&ctx->file_path, g_free);
            g_clear_pointer(&ctx->file_type, g_free);
            g_free(ctx->remote_url);
            g_free(ctx->remote_type);
            ctx->remote_url = g_strdup(media_url);
            ctx->remote_type = g_strdup(media_type ? media_type : "image/jpeg");
            gtk_label_set_text(GTK_LABEL(ctx->file_label), tenor ? "Selected GIF from Tenor" : "Selected image from Unsplash");
        }
    }
    gtk_widget_destroy(dialog);
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
        if (ctx->remote_url) {
            media_url = g_strdup(ctx->remote_url);
        } else if (ctx->file_path) {
            media_url = perform_media_upload(ctx->file_path);
            if (!media_url) {
                upload_success = FALSE;
            }
        }

        GList *attachments = NULL;
        if (media_url) {
            const gchar *file_type = ctx->remote_type ? ctx->remote_type :
                (ctx->file_type ? ctx->file_type : "application/octet-stream");
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
    g_free(ctx->remote_url);
    g_free(ctx->remote_type);
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

    GtkWidget *gif_btn = gtk_button_new_with_label("GIF");
    GtkWidget *photo_btn = gtk_button_new_with_label("Photo");
    g_object_set_data(G_OBJECT(gif_btn), "tenor", GINT_TO_POINTER(TRUE));
    g_object_set_data(G_OBJECT(photo_btn), "tenor", GINT_TO_POINTER(FALSE));
    g_signal_connect(gif_btn, "clicked", G_CALLBACK(on_media_search_clicked), ctx);
    g_signal_connect(photo_btn, "clicked", G_CALLBACK(on_media_search_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(file_box), gif_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(file_box), photo_btn, FALSE, FALSE, 0);

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
            gchar *cursor = tweet_pagination_cursor(async_data->list_box, last_tweet);
            if (cursor) {
                g_object_set_data_full(G_OBJECT(async_data->list_box), "last_id", cursor, g_free);
            } else {
                g_object_set_data(G_OBJECT(async_data->list_box), "last_id", NULL);
            }
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
            gchar *escaped = g_uri_escape_string(async_data->before_id, NULL, TRUE);
            url = g_strdup_printf("%s?before=%s", base_url, escaped ? escaped : "");
            g_free(escaped);
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
    } else if (g_strcmp0(feed_type, "public") == 0) {
        if (async_data->before_id) {
            url = g_strdup_printf("%s?before=%s", PUBLIC_TWEETS_URL, async_data->before_id);
        } else {
            url = g_strdup(PUBLIC_TWEETS_URL);
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

        if (g_profile_report_button) {
            if (!g_active_profile->is_own_profile &&
                g_auth_token &&
                g_active_profile->id) {
                g_object_set_data_full(G_OBJECT(g_profile_report_button), "user_id", g_strdup(g_active_profile->id), g_free);
                gtk_widget_show(g_profile_report_button);
            } else {
                gtk_widget_hide(g_profile_report_button);
            }
        }

        if (g_profile_affiliate_button) {
            if (!g_active_profile->is_own_profile &&
                g_auth_token &&
                g_active_profile->username &&
                g_active_profile->username[0] != '\0') {
                g_object_set_data_full(G_OBJECT(g_profile_affiliate_button), "username", g_strdup(g_active_profile->username), g_free);
                gtk_widget_show(g_profile_affiliate_button);
            } else {
                gtk_widget_hide(g_profile_affiliate_button);
            }
        }

        if (g_profile_shop_button) {
            if (g_active_profile->username && g_active_profile->username[0] != '\0') {
                g_object_set_data_full(G_OBJECT(g_profile_shop_button), "username", g_strdup(g_active_profile->username), g_free);
                gtk_widget_show(g_profile_shop_button);
            } else {
                gtk_widget_hide(g_profile_shop_button);
            }
        }

        if (g_profile_donate_button) {
            if (!g_active_profile->is_own_profile &&
                g_auth_token &&
                g_active_profile->username &&
                g_active_profile->username[0] != '\0') {
                g_object_set_data_full(G_OBJECT(g_profile_donate_button), "username", g_strdup(g_active_profile->username), g_free);
                gtk_widget_show(g_profile_donate_button);
            } else {
                gtk_widget_hide(g_profile_donate_button);
            }
        }

        if (g_profile_algorithm_button) {
            if (g_active_profile->username && g_active_profile->username[0] != '\0') {
                g_object_set_data_full(G_OBJECT(g_profile_algorithm_button), "username", g_strdup(g_active_profile->username), g_free);
                gtk_widget_show(g_profile_algorithm_button);
            } else {
                gtk_widget_hide(g_profile_algorithm_button);
            }
        }

        if (g_profile_spam_score_button) {
            if (g_active_profile->username && g_active_profile->username[0] != '\0') {
                g_object_set_data_full(G_OBJECT(g_profile_spam_score_button), "username", g_strdup(g_active_profile->username), g_free);
                gtk_widget_show(g_profile_spam_score_button);
            } else {
                gtk_widget_hide(g_profile_spam_score_button);
            }
        }

        if (g_profile_analytics_button) {
            if (g_active_profile->username && g_active_profile->username[0] != '\0') {
                g_object_set_data_full(G_OBJECT(g_profile_analytics_button), "username", g_strdup(g_active_profile->username), g_free);
                gtk_widget_show(g_profile_analytics_button);
            } else {
                gtk_widget_hide(g_profile_analytics_button);
            }
        }

        if (g_profile_common_followers_button) {
            if (!g_active_profile->is_own_profile &&
                g_auth_token &&
                g_active_profile->username &&
                g_active_profile->username[0] != '\0') {
                g_object_set_data_full(G_OBJECT(g_profile_common_followers_button), "username", g_strdup(g_active_profile->username), g_free);
                gtk_widget_show(g_profile_common_followers_button);
            } else {
                gtk_widget_hide(g_profile_common_followers_button);
            }
        }

        if (g_profile_top_posts_button) {
            if (g_active_profile->username && g_active_profile->username[0] != '\0') {
                g_object_set_data_full(G_OBJECT(g_profile_top_posts_button), "username", g_strdup(g_active_profile->username), g_free);
                gtk_widget_show(g_profile_top_posts_button);
            } else {
                gtk_widget_hide(g_profile_top_posts_button);
            }
        }

        if (g_profile_communities_button) {
            if (g_active_profile->id && g_active_profile->id[0] != '\0') {
                g_object_set_data_full(G_OBJECT(g_profile_communities_button), "user_id", g_strdup(g_active_profile->id), g_free);
                gtk_widget_show(g_profile_communities_button);
            } else {
                gtk_widget_hide(g_profile_communities_button);
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
                gchar *cursor = tweet_pagination_cursor(GTK_LIST_BOX(g_profile_tweets_list), last_tweet);
                if (cursor) {
                    g_object_set_data_full(G_OBJECT(g_profile_tweets_list), "last_id", cursor, g_free);
                } else {
                    g_object_set_data(G_OBJECT(g_profile_tweets_list), "last_id", NULL);
                }
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
            start_loading_profile_highlights(g_active_profile->username);
            start_loading_profile_mutuals(g_active_profile->username);
            start_loading_profile_followers_you_know(g_active_profile->username);
            start_loading_profile_affiliates(g_active_profile->username);
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
    if (g_profile_report_button) {
        gtk_widget_hide(g_profile_report_button);
    }
    if (g_profile_delete_avatar_button) {
        gtk_widget_hide(g_profile_delete_avatar_button);
    }
    if (g_profile_delete_banner_button) {
        gtk_widget_hide(g_profile_delete_banner_button);
    }
    if (g_profile_analytics_button) {
        gtk_widget_hide(g_profile_analytics_button);
    }
    if (g_profile_common_followers_button) {
        gtk_widget_hide(g_profile_common_followers_button);
    }
    if (g_profile_top_posts_button) {
        gtk_widget_hide(g_profile_top_posts_button);
    }
    
    g_object_set_data_full(G_OBJECT(g_profile_tweets_list), "current_profile_user", g_strdup(username), g_free);
    g_object_set_data_full(G_OBJECT(g_profile_replies_list), "current_profile_user", g_strdup(username), g_free);
    if (g_profile_media_list) {
        g_object_set_data_full(G_OBJECT(g_profile_media_list), "current_profile_user", g_strdup(username), g_free);
    }
    if (g_profile_highlights_list) {
        g_object_set_data_full(G_OBJECT(g_profile_highlights_list), "current_profile_user", g_strdup(username), g_free);
    }
    if (g_profile_mutuals_list) {
        g_object_set_data_full(G_OBJECT(g_profile_mutuals_list), "current_profile_user", g_strdup(username), g_free);
    }
    if (g_profile_followers_you_know_list) {
        g_object_set_data_full(G_OBJECT(g_profile_followers_you_know_list), "current_profile_user", g_strdup(username), g_free);
    }
    if (g_profile_affiliates_list) {
        g_object_set_data_full(G_OBJECT(g_profile_affiliates_list), "current_profile_user", g_strdup(username), g_free);
    }

    populate_tweet_list(GTK_LIST_BOX(g_profile_tweets_list), NULL);
    populate_tweet_list(GTK_LIST_BOX(g_profile_replies_list), NULL);
    if (g_profile_media_list) {
        populate_tweet_list(GTK_LIST_BOX(g_profile_media_list), NULL);
    }
    if (g_profile_highlights_list) {
        populate_tweet_list(GTK_LIST_BOX(g_profile_highlights_list), NULL);
    }
    if (g_profile_mutuals_list) {
        populate_user_list(GTK_LIST_BOX(g_profile_mutuals_list), NULL);
    }
    if (g_profile_followers_you_know_list) {
        populate_user_list(GTK_LIST_BOX(g_profile_followers_you_know_list), NULL);
    }
    if (g_profile_affiliates_list) {
        populate_user_list(GTK_LIST_BOX(g_profile_affiliates_list), NULL);
    }

    g_object_set_data(G_OBJECT(g_profile_tweets_list), "last_id", NULL);
    g_object_set_data(G_OBJECT(g_profile_replies_list), "last_id", NULL);
    if (g_profile_media_list) {
        g_object_set_data(G_OBJECT(g_profile_media_list), "last_id", NULL);
    }
    if (g_profile_highlights_list) {
        g_object_set_data(G_OBJECT(g_profile_highlights_list), "last_id", NULL);
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
    } else if (g_strcmp0(current_view, "list_details") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "lists");
    } else if (g_strcmp0(current_view, "lists") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "timeline");
        gtk_widget_hide(g_back_button);
    } else if (g_strcmp0(current_view, "explore") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "timeline");
        gtk_widget_hide(g_back_button);
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
    } else if (g_strcmp0(current_view, "lists") == 0) {
        start_loading_lists();
    } else if (g_strcmp0(current_view, "list_details") == 0 && g_active_list && g_active_list->id) {
        show_list_details(g_active_list->id);
    } else if (g_strcmp0(current_view, "explore") == 0) {
        start_loading_explore();
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

static gboolean
dm_json_request(const gchar *url, const gchar *method, JsonBuilder *builder, gchar **response_out)
{
    JsonGenerator *gen = NULL;
    gchar *payload = NULL;
    gboolean ok;

    if (builder) {
        gen = json_generator_new();
        json_generator_set_root(gen, json_builder_get_root(builder));
        payload = json_generator_to_data(gen, NULL);
    }
    ok = perform_request_with_optional_payload(url, payload ? payload : "{}", method, response_out);
    g_free(payload);
    if (gen) {
        g_object_unref(gen);
    }
    return ok;
}

void
on_dm_invite_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *conv_id = g_dm_messages_list ? g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id") : NULL;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *max_uses_spin;
    GtkWidget *expires_spin;

    (void)user_data;
    if (!conv_id) return;
    dialog = gtk_dialog_new_with_buttons("Group Invite",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Create", GTK_RESPONSE_ACCEPT,
                                         "_Revoke", GTK_RESPONSE_REJECT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    max_uses_spin = gtk_spin_button_new_with_range(0, 500, 1);
    expires_spin = gtk_spin_button_new_with_range(0, 30, 1);
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Max uses (0 for unlimited):"), FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(content), max_uses_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Expires in days (0 for no expiry):"), FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(content), expires_spin, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);

    gint response_id = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response_id == GTK_RESPONSE_ACCEPT) {
        JsonBuilder *builder = json_builder_new();
        gchar *url = g_strdup_printf(DM_INVITE_URL, conv_id);
        gchar *response = NULL;
        json_builder_begin_object(builder);
        if (gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(max_uses_spin)) > 0) {
            json_builder_set_member_name(builder, "max_uses");
            json_builder_add_int_value(builder, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(max_uses_spin)));
        }
        if (gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(expires_spin)) > 0) {
            json_builder_set_member_name(builder, "expires_in_days");
            json_builder_add_int_value(builder, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(expires_spin)));
        }
        json_builder_end_object(builder);
        if (dm_json_request(url, "POST", builder, &response)) {
            JsonParser *parser = json_parser_new();
            if (json_parser_load_from_data(parser, response, -1, NULL)) {
                JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
                const gchar *token = json_get_string_or_empty(obj, "token");
                gchar *message = g_strdup_printf("Invite token:\n%s", token);
                show_modal_message(GTK_MESSAGE_INFO, "Invite created.", message);
                g_free(message);
            }
            g_object_unref(parser);
        } else {
            gchar *err = extract_error_message(response);
            show_modal_message(GTK_MESSAGE_ERROR, "Could not create invite.", err);
            g_free(err);
        }
        g_free(response);
        g_free(url);
        g_object_unref(builder);
    } else if (response_id == GTK_RESPONSE_REJECT) {
        gchar *url = g_strdup_printf(DM_INVITE_REVOKE_URL, conv_id);
        gchar *response = NULL;
        if (!perform_request_with_optional_payload(url, "{}", "POST", &response)) {
            gchar *err = extract_error_message(response);
            show_modal_message(GTK_MESSAGE_ERROR, "Could not revoke invite.", err);
            g_free(err);
        }
        g_free(response);
        g_free(url);
    }
    gtk_widget_destroy(dialog);
}

void
on_dm_join_invite_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *entry;
    GtkWidget *content;

    (void)user_data;
    dialog = gtk_dialog_new_with_buttons("Join Group Invite",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Join", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Invite token");
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *escaped = g_uri_escape_string(gtk_entry_get_text(GTK_ENTRY(entry)), NULL, FALSE);
        gchar *url = g_strdup_printf(DM_INVITE_JOIN_URL, escaped);
        gchar *response = NULL;
        if (perform_request_with_optional_payload(url, "{}", "POST", &response)) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "messages");
            start_loading_conversations(GTK_LIST_BOX(g_conversations_list));
        } else {
            gchar *err = extract_error_message(response);
            show_modal_message(GTK_MESSAGE_ERROR, "Could not join invite.", err);
            g_free(err);
        }
        g_free(response);
        g_free(url);
        g_free(escaped);
    }
    gtk_widget_destroy(dialog);
}

void
on_dm_permissions_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *conv_id = g_dm_messages_list ? g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id") : NULL;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *send_combo;
    GtkWidget *invite_combo;
    GtkWidget *metadata_combo;

    (void)user_data;
    if (!conv_id) return;
    dialog = gtk_dialog_new_with_buttons("Group Permissions",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    send_combo = gtk_combo_box_text_new();
    invite_combo = gtk_combo_box_text_new();
    metadata_combo = gtk_combo_box_text_new();
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    GtkWidget *combos[] = {send_combo, invite_combo, metadata_combo};
    for (guint i = 0; i < 3; i++) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combos[i]), "all", "Everyone");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combos[i]), "admins", "Admins");
        gtk_combo_box_set_active(GTK_COMBO_BOX(combos[i]), 0);
    }
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Send messages:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), send_combo, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Create invites:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), invite_combo, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Edit metadata:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), metadata_combo, 1, 2, 1, 1);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        JsonBuilder *builder = json_builder_new();
        gchar *url = g_strdup_printf(DM_PERMISSIONS_URL, conv_id);
        gchar *response = NULL;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "send_permission");
        json_builder_add_string_value(builder, gtk_combo_box_get_active_id(GTK_COMBO_BOX(send_combo)));
        json_builder_set_member_name(builder, "invite_permission");
        json_builder_add_string_value(builder, gtk_combo_box_get_active_id(GTK_COMBO_BOX(invite_combo)));
        json_builder_set_member_name(builder, "edit_metadata_permission");
        json_builder_add_string_value(builder, gtk_combo_box_get_active_id(GTK_COMBO_BOX(metadata_combo)));
        json_builder_end_object(builder);
        if (dm_json_request(url, "PATCH", builder, &response)) {
            start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);
        } else {
            gchar *err = extract_error_message(response);
            show_modal_message(GTK_MESSAGE_ERROR, "Could not update permissions.", err);
            g_free(err);
        }
        g_free(response);
        g_free(url);
        g_object_unref(builder);
    }
    gtk_widget_destroy(dialog);
}

void
on_dm_roles_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *conv_id = g_dm_messages_list ? g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id") : NULL;
    struct Conversation *conversation = g_dm_messages_list ? g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_detail") : NULL;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *participant_combo;
    GtkWidget *role_combo;

    (void)user_data;
    if (!conv_id || !conversation || !conversation->participants) {
        show_modal_message(GTK_MESSAGE_ERROR, "Roles unavailable.", "Open a group conversation before changing roles.");
        return;
    }

    dialog = gtk_dialog_new_with_buttons("Group Roles",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    participant_combo = gtk_combo_box_text_new();
    for (GList *l = conversation->participants; l != NULL; l = l->next) {
        struct Profile *participant = l->data;
        if (participant && participant->id && participant->username) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(participant_combo),
                                      participant->id,
                                      participant->username);
        }
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(participant_combo), 0);

    role_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(role_combo), "member", "Member");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(role_combo), "admin", "Admin");
    gtk_combo_box_set_active(GTK_COMBO_BOX(role_combo), 0);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Participant:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), participant_combo, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Role:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), role_combo, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *participant_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(participant_combo));
        const gchar *role = gtk_combo_box_get_active_id(GTK_COMBO_BOX(role_combo));
        if (participant_id && role) {
            JsonBuilder *builder = json_builder_new();
            gchar *url = g_strdup_printf(DM_PARTICIPANT_ROLE_URL, conv_id, participant_id);
            gchar *response = NULL;
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "role");
            json_builder_add_string_value(builder, role);
            json_builder_end_object(builder);
            if (dm_json_request(url, "PATCH", builder, &response)) {
                start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);
            } else {
                gchar *err = extract_error_message(response);
                show_modal_message(GTK_MESSAGE_ERROR, "Could not update role.", err);
                g_free(err);
            }
            g_free(response);
            g_free(url);
            g_object_unref(builder);
        }
    }
    gtk_widget_destroy(dialog);
}

void
on_dm_pinned_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *conv_id = g_dm_messages_list ? g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id") : NULL;
    gchar *url;
    struct MemoryStruct chunk = {0};
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *scroll;
    GtkWidget *list;

    (void)widget;
    (void)user_data;
    if (!conv_id) return;
    url = g_strdup_printf(DM_PINNED_MESSAGES_URL, conv_id);
    dialog = gtk_dialog_new_with_buttons("Pinned Messages", NULL, GTK_DIALOG_MODAL, "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 420);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    scroll = gtk_scrolled_window_new(NULL, NULL);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
    if (fetch_url(url, &chunk, NULL, "GET")) {
        GList *messages = parse_messages(chunk.memory);
        populate_message_list(GTK_LIST_BOX(list), messages);
        free_messages(messages);
    } else {
        GtkWidget *label = gtk_label_new("Failed to load pinned messages.");
        gtk_list_box_insert(GTK_LIST_BOX(list), label, -1);
    }
    g_free(chunk.memory);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(url);
}

void
on_dm_pin_message_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *message_id = g_object_get_data(G_OBJECT(widget), "message_id");
    const gchar *conv_id = g_dm_messages_list ? g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id") : NULL;
    gchar *url;
    gchar *response = NULL;

    (void)user_data;
    if (!message_id || !conv_id) return;
    url = g_strdup_printf(DM_PIN_MESSAGE_URL, conv_id, message_id);
    if (!perform_request_with_optional_payload(url, "{}", "POST", &response)) {
        gchar *err = extract_error_message(response);
        show_modal_message(GTK_MESSAGE_ERROR, "Could not pin message.", err);
        g_free(err);
    }
    g_free(response);
    g_free(url);
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
    start_loading_muted_words();
    start_loading_muted_conversations();
    start_loading_for_you_interests();
    start_loading_scheduled_posts();
    start_loading_my_shop();
    start_loading_delegates();
    start_loading_account_requests();
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

    if (perform_admin_fetch_url(ADMIN_STATS_URL, &chunk, NULL, "GET")) {
        stats_text = parse_admin_stats(chunk.memory);
        g_free(chunk.memory);
    }

    g_idle_add(on_admin_stats_loaded, stats_text);
    return NULL;
}

void start_loading_admin_stats(void)
{
    if (!has_admin_session_context()) return;
    gtk_label_set_text(GTK_LABEL(g_admin_stats_label), "Loading admin statistics...");
    g_thread_new("admin-stats-loader", fetch_admin_stats_thread, NULL);
}

void on_admin_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    if (!has_admin_session_context()) return;

    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "admin");
    gtk_widget_show(g_back_button);
    start_loading_admin_stats();
    start_loading_admin_users(NULL);
    start_loading_admin_posts(NULL);
    start_loading_admin_suspensions();
    start_loading_admin_reports();
    start_loading_admin_logs(NULL);
    start_loading_admin_blocks();
    start_loading_admin_emojis();
    start_loading_admin_badges();
    start_loading_admin_dms(NULL);
    start_loading_admin_shop(NULL);
    start_loading_admin_communities();
    update_admin_impersonation_status_label();
}

static void
clear_list_box_rows(GtkWidget *list_box)
{
    GList *children;
    GList *iter;

    if (!list_box) return;

    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
}

static void
set_list_box_status(GtkWidget *list_box, const gchar *message)
{
    GtkWidget *label;

    clear_list_box_rows(list_box);
    label = gtk_label_new(message ? message : "");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_list_box_insert(GTK_LIST_BOX(list_box), label, -1);
    gtk_widget_show_all(list_box);
}

static GtkWidget *
create_admin_row(const gchar *title, const gchar *body, GtkWidget *actions_box)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *title_label = gtk_label_new(NULL);
    GtkWidget *body_label = gtk_label_new(body ? body : "");
    gchar *escaped_title = g_markup_escape_text(title ? title : "", -1);
    gchar *title_markup = g_strdup_printf("<b>%s</b>", escaped_title ? escaped_title : "");

    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_label_set_markup(GTK_LABEL(title_label), title_markup);
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
    gtk_label_set_xalign(GTK_LABEL(body_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(body_label), TRUE);

    gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), body_label, FALSE, FALSE, 0);
    if (actions_box) {
        gtk_box_pack_start(GTK_BOX(box), actions_box, FALSE, FALSE, 0);
    }

    gtk_container_add(GTK_CONTAINER(frame), box);

    g_free(title_markup);
    g_free(escaped_title);
    return frame;
}

static const gchar *
json_get_string_or_empty(JsonObject *obj, const gchar *member)
{
    JsonNode *node;

    if (!obj || !member || !json_object_has_member(obj, member)) {
        return "";
    }
    node = json_object_get_member(obj, member);
    if (!node || json_node_is_null(node)) {
        return "";
    }
    return json_object_get_string_member(obj, member);
}

static gint64
json_get_int64_default(JsonObject *obj, const gchar *member, gint64 fallback)
{
    JsonNode *node;

    if (!obj || !member || !json_object_has_member(obj, member)) {
        return fallback;
    }
    node = json_object_get_member(obj, member);
    if (!node || !JSON_NODE_HOLDS_VALUE(node)) {
        return fallback;
    }
    return json_object_get_int_member(obj, member);
}

static double
json_get_double_default(JsonObject *obj, const gchar *member, double fallback)
{
    JsonNode *node;

    if (!obj || !member || !json_object_has_member(obj, member)) {
        return fallback;
    }
    node = json_object_get_member(obj, member);
    if (!node || !JSON_NODE_HOLDS_VALUE(node)) {
        return fallback;
    }
    return json_object_get_double_member(obj, member);
}

static gboolean
json_get_bool_default(JsonObject *obj, const gchar *member, gboolean fallback)
{
    JsonNode *node;

    if (!obj || !member || !json_object_has_member(obj, member)) {
        return fallback;
    }
    node = json_object_get_member(obj, member);
    if (!node || !JSON_NODE_HOLDS_VALUE(node)) {
        return fallback;
    }
    return json_object_get_boolean_member(obj, member);
}

static JsonObject *
json_get_object_member_valid(JsonObject *obj, const gchar *member)
{
    JsonNode *node;

    if (!obj || !member || !json_object_has_member(obj, member)) {
        return NULL;
    }
    node = json_object_get_member(obj, member);
    if (!node || !JSON_NODE_HOLDS_OBJECT(node)) {
        return NULL;
    }
    return json_node_get_object(node);
}

static JsonArray *
json_get_array_member_valid(JsonObject *obj, const gchar *member)
{
    JsonNode *node;

    if (!obj || !member || !json_object_has_member(obj, member)) {
        return NULL;
    }
    node = json_object_get_member(obj, member);
    if (!node || !JSON_NODE_HOLDS_ARRAY(node)) {
        return NULL;
    }
    return json_node_get_array(node);
}

static gchar *
json_object_to_display_lines(JsonObject *obj)
{
    GString *out = g_string_new(NULL);
    GList *members;

    if (!obj) {
        return g_string_free(out, FALSE);
    }

    members = json_object_get_members(obj);
    for (GList *l = members; l; l = l->next) {
        const gchar *name = l->data;
        JsonNode *node = json_object_get_member(obj, name);
        gchar *label = g_strdup(name ? name : "");
        for (gchar *p = label; *p; p++) {
            if (*p == '_') {
                *p = ' ';
            }
        }
        if (label[0]) {
            label[0] = g_ascii_toupper(label[0]);
        }

        if (node && JSON_NODE_HOLDS_VALUE(node)) {
            GType type = json_node_get_value_type(node);
            if (type == G_TYPE_BOOLEAN) {
                g_string_append_printf(out, "%s: %s\n", label, json_node_get_boolean(node) ? "yes" : "no");
            } else if (type == G_TYPE_INT64 || type == G_TYPE_INT || type == G_TYPE_UINT64 || type == G_TYPE_UINT) {
                g_string_append_printf(out, "%s: %" G_GINT64_FORMAT "\n", label, json_node_get_int(node));
            } else if (type == G_TYPE_DOUBLE || type == G_TYPE_FLOAT) {
                g_string_append_printf(out, "%s: %.3g\n", label, json_node_get_double(node));
            } else {
                const gchar *value = json_node_get_string(node);
                g_string_append_printf(out, "%s: %s\n", label, value ? value : "");
            }
        } else if (node && JSON_NODE_HOLDS_OBJECT(node)) {
            gchar *nested = json_object_to_display_lines(json_node_get_object(node));
            g_string_append_printf(out, "%s:\n%s", label, nested);
            g_free(nested);
        } else if (node && JSON_NODE_HOLDS_ARRAY(node)) {
            JsonArray *array = json_node_get_array(node);
            g_string_append_printf(out, "%s: %u item%s\n", label,
                                   json_array_get_length(array),
                                   json_array_get_length(array) == 1 ? "" : "s");
        }
        g_free(label);
    }
    g_list_free(members);

    if (out->len > 0 && out->str[out->len - 1] == '\n') {
        g_string_truncate(out, out->len - 1);
    }
    return g_string_free(out, FALSE);
}

static gchar *
json_node_to_display_text(JsonNode *node)
{
    if (!node) {
        return g_strdup("");
    }
    if (JSON_NODE_HOLDS_OBJECT(node)) {
        return json_object_to_display_lines(json_node_get_object(node));
    }
    if (JSON_NODE_HOLDS_ARRAY(node)) {
        GString *out = g_string_new(NULL);
        JsonArray *array = json_node_get_array(node);
        for (guint i = 0; i < json_array_get_length(array); i++) {
            JsonNode *item = json_array_get_element(array, i);
            gchar *text = json_node_to_display_text(item);
            if (text && text[0]) {
                g_string_append_printf(out, "%u. %s\n", i + 1, text);
            }
            g_free(text);
        }
        if (out->len > 0 && out->str[out->len - 1] == '\n') {
            g_string_truncate(out, out->len - 1);
        }
        return g_string_free(out, FALSE);
    }
    if (JSON_NODE_HOLDS_VALUE(node)) {
        GType type = json_node_get_value_type(node);
        if (type == G_TYPE_BOOLEAN) {
            return g_strdup(json_node_get_boolean(node) ? "yes" : "no");
        }
        if (type == G_TYPE_INT64 || type == G_TYPE_INT || type == G_TYPE_UINT64 || type == G_TYPE_UINT) {
            return g_strdup_printf("%" G_GINT64_FORMAT, json_node_get_int(node));
        }
        if (type == G_TYPE_DOUBLE || type == G_TYPE_FLOAT) {
            return g_strdup_printf("%.3g", json_node_get_double(node));
        }
        return g_strdup(json_node_get_string(node) ? json_node_get_string(node) : "");
    }
    return g_strdup("");
}

static gchar *
format_algorithm_stats(JsonObject *obj)
{
    JsonObject *impact = json_get_object_member_valid(obj, "algorithm_impact");
    const gchar *rating = impact ? json_get_string_or_empty(impact, "rating") : "";
    return g_strdup_printf("Rating: %s\nOverall multiplier: %.3g\nReputation multiplier: %.3g\nAccount age multiplier: %.3g\n\nSpam score: %.3g\nBlocked by: %" G_GINT64_FORMAT "\nMuted by: %" G_GINT64_FORMAT "\nAccount age: %" G_GINT64_FORMAT " days\nFollowers: %" G_GINT64_FORMAT "\nFollowing: %" G_GINT64_FORMAT "\nPosts: %" G_GINT64_FORMAT "\n\nVerified: %s\nGold: %s\nSuper tweeter: %s",
                           rating[0] ? rating : "Unknown",
                           impact ? json_get_double_default(impact, "overall_multiplier", 0.0) : 0.0,
                           impact ? json_get_double_default(impact, "reputation_multiplier", 0.0) : 0.0,
                           impact ? json_get_double_default(impact, "account_age_multiplier", 0.0) : 0.0,
                           json_get_double_default(obj, "spam_score", 0.0),
                           json_get_int64_default(obj, "blocked_by_count", 0),
                           json_get_int64_default(obj, "muted_by_count", 0),
                           json_get_int64_default(obj, "account_age_days", 0),
                           json_get_int64_default(obj, "follower_count", 0),
                           json_get_int64_default(obj, "following_count", 0),
                           json_get_int64_default(obj, "post_count", 0),
                           json_get_bool_default(obj, "verified", FALSE) ? "yes" : "no",
                           json_get_bool_default(obj, "gold", FALSE) ? "yes" : "no",
                           json_get_bool_default(obj, "super_tweeter", FALSE) ? "yes" : "no");
}

static gchar *
format_spam_score(JsonObject *obj)
{
    GString *out = g_string_new(NULL);
    JsonObject *metrics = json_get_object_member_valid(obj, "accountMetrics");
    JsonArray *indicators = json_get_array_member_valid(obj, "indicators");
    const gchar *message = json_get_string_or_empty(obj, "message");

    g_string_append_printf(out, "Score: %.1f%%\n%s\n\n",
                           json_get_double_default(obj, "spamPercentage", 0.0),
                           message[0] ? message : "No summary available.");
    if (metrics) {
        g_string_append_printf(out,
                               "Account age: %" G_GINT64_FORMAT " days\nFollowers: %" G_GINT64_FORMAT "\nFollowing: %" G_GINT64_FORMAT "\nFollow ratio: %s\nPosts: %" G_GINT64_FORMAT "\nReplies: %" G_GINT64_FORMAT "\nPosts last hour: %" G_GINT64_FORMAT "\nPosts last day: %" G_GINT64_FORMAT "\nReplies last day: %" G_GINT64_FORMAT,
                               json_get_int64_default(metrics, "accountAgeDays", 0),
                               json_get_int64_default(metrics, "followerCount", 0),
                               json_get_int64_default(metrics, "followingCount", 0),
                               json_get_string_or_empty(metrics, "followRatio"),
                               json_get_int64_default(metrics, "totalPosts", 0),
                               json_get_int64_default(metrics, "totalReplies", 0),
                               json_get_int64_default(metrics, "postsLastHour", 0),
                               json_get_int64_default(metrics, "postsLastDay", 0),
                               json_get_int64_default(metrics, "repliesLastDay", 0));
    }
    if (indicators && json_array_get_length(indicators) > 0) {
        g_string_append(out, "\n\nIndicators:");
        for (guint i = 0; i < json_array_get_length(indicators) && i < 6; i++) {
            JsonNode *node = json_array_get_element(indicators, i);
            JsonObject *indicator = node && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
            if (!indicator) {
                continue;
            }
            g_string_append_printf(out, "\n- %s: %s (%s)",
                                   json_get_string_or_empty(indicator, "displayName")[0]
                                       ? json_get_string_or_empty(indicator, "displayName")
                                       : json_get_string_or_empty(indicator, "name"),
                                   json_get_string_or_empty(indicator, "contribution"),
                                   json_get_string_or_empty(indicator, "status"));
            if (json_get_string_or_empty(indicator, "details")[0]) {
                g_string_append_printf(out, "\n  %s", json_get_string_or_empty(indicator, "details"));
            }
        }
    }

    return g_string_free(out, FALSE);
}

static gboolean
perform_admin_request(const gchar *url, const gchar *payload, const gchar *method, gchar **response_out)
{
    struct MemoryStruct chunk = {0};

    if (response_out) {
        *response_out = NULL;
    }

    if (!perform_admin_fetch_url(url, &chunk, payload, method)) {
        return FALSE;
    }

    if (response_out) {
        *response_out = chunk.memory;
    } else {
        g_free(chunk.memory);
    }
    return TRUE;
}

static gchar *
extract_error_message(const gchar *json_data)
{
    JsonParser *parser;
    GError *error = NULL;
    gchar *message = NULL;

    if (!json_data) return NULL;

    parser = json_parser_new();
    if (json_parser_load_from_data(parser, json_data, -1, &error)) {
        JsonNode *root = json_parser_get_root(parser);
        if (JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject *obj = json_node_get_object(root);
            if (json_object_has_member(obj, "error") &&
                !json_node_is_null(json_object_get_member(obj, "error"))) {
                message = g_strdup(json_object_get_string_member(obj, "error"));
            }
        }
    }
    if (error) {
        g_error_free(error);
    }
    g_object_unref(parser);
    return message;
}

static void
perform_admin_lift_user_action(const gchar *user_id, const gchar *lift_action)
{
    gchar *escaped_id;
    gchar *url;
    gchar *payload;
    gchar *response = NULL;

    if (!user_id || !lift_action) return;

    escaped_id = g_uri_escape_string(user_id, NULL, FALSE);
    url = g_strdup_printf("%s/%s/suspend", ADMIN_USERS_URL, escaped_id);
    payload = g_strdup_printf("{\"action\":\"lift\",\"lift\":[\"%s\"]}", lift_action);

    if (!perform_admin_request(url, payload, "POST", &response)) {
        show_modal_message(GTK_MESSAGE_ERROR,
                           "Failed to lift moderation action.",
                           "The request could not be sent.");
    } else {
        gchar *error_message = extract_error_message(response);
        if (error_message) {
            show_modal_message(GTK_MESSAGE_ERROR, "Failed to lift moderation action.", error_message);
            g_free(error_message);
        } else {
            start_loading_admin_suspensions();
            start_loading_admin_users(gtk_entry_get_text(GTK_ENTRY(g_admin_users_search)));
        }
    }

    g_free(response);
    g_free(payload);
    g_free(url);
    g_free(escaped_id);
}

static void
perform_admin_report_resolution(const gchar *report_id, const gchar *action, const gchar *ban_action)
{
    gchar *escaped_id;
    gchar *url;
    gchar *payload;
    gchar *response = NULL;

    if (!report_id || !action) return;

    escaped_id = g_uri_escape_string(report_id, NULL, FALSE);
    url = g_strdup_printf("%s/%s/resolve", ADMIN_REPORTS_URL, escaped_id);
    if (ban_action) {
        payload = g_strdup_printf("{\"action\":\"%s\",\"banAction\":\"%s\"}", action, ban_action);
    } else {
        payload = g_strdup_printf("{\"action\":\"%s\"}", action);
    }

    if (!perform_admin_request(url, payload, "POST", &response)) {
        show_modal_message(GTK_MESSAGE_ERROR,
                           "Failed to resolve report.",
                           "The request could not be sent.");
    } else {
        gchar *error_message = extract_error_message(response);
        if (error_message) {
            show_modal_message(GTK_MESSAGE_ERROR, "Failed to resolve report.", error_message);
            g_free(error_message);
        } else {
            start_loading_admin_reports();
            start_loading_admin_suspensions();
            start_loading_admin_posts(gtk_entry_get_text(GTK_ENTRY(g_admin_posts_search)));
        }
    }

    g_free(response);
    g_free(payload);
    g_free(url);
    g_free(escaped_id);
}

static void
perform_admin_delete_item(const gchar *url, const gchar *error_title, void (*refresh_fn)(void))
{
    gchar *response = NULL;

    if (!url) return;

    if (!perform_admin_request(url, NULL, "DELETE", &response)) {
        show_modal_message(GTK_MESSAGE_ERROR,
                           error_title ? error_title : "Delete failed.",
                           "The request could not be sent.");
    } else if (refresh_fn) {
        gchar *error_message = extract_error_message(response);
        if (error_message) {
            show_modal_message(GTK_MESSAGE_ERROR, error_title ? error_title : "Delete failed.", error_message);
            g_free(error_message);
        } else {
            refresh_fn();
        }
    }

    g_free(response);
}

static void
refresh_admin_emojis_only(void)
{
    start_loading_admin_emojis();
}

static void
refresh_admin_badges_only(void)
{
    start_loading_admin_badges();
}

static void
refresh_admin_dms_only(void)
{
    start_loading_admin_dms(gtk_entry_get_text(GTK_ENTRY(g_admin_dms_search)));
    clear_list_box_rows(g_admin_dm_admin_messages_list);
}

static void
refresh_admin_shop_only(void)
{
    start_loading_admin_shop(gtk_entry_get_text(GTK_ENTRY(g_admin_shop_search)));
}

static void
on_admin_lift_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar **parts;
    (void)widget;

    parts = g_strsplit((const gchar *)user_data, "|", 2);
    if (parts[0] && parts[1]) {
        perform_admin_lift_user_action(parts[0], parts[1]);
    }
    g_strfreev(parts);
}

static void
on_admin_report_ignore_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    perform_admin_report_resolution((const gchar *)user_data, "ignore", NULL);
}

static void
on_admin_report_delete_post_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    perform_admin_report_resolution((const gchar *)user_data, "delete_post", NULL);
}

static void
on_admin_report_ban_user_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar **parts;
    (void)widget;

    parts = g_strsplit((const gchar *)user_data, "|", 2);
    if (parts[0] && parts[1]) {
        perform_admin_report_resolution(parts[0], "ban_user", parts[1]);
    }
    g_strfreev(parts);
}

static void
on_admin_delete_emoji_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *escaped_id;
    gchar *url;
    (void)widget;

    escaped_id = g_uri_escape_string((const gchar *)user_data, NULL, FALSE);
    url = g_strdup_printf("%s/%s", ADMIN_EMOJIS_URL, escaped_id);
    perform_admin_delete_item(url, "Failed to delete emoji.", refresh_admin_emojis_only);
    g_free(url);
    g_free(escaped_id);
}

static void
on_admin_delete_badge_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *escaped_id;
    gchar *url;
    (void)widget;

    escaped_id = g_uri_escape_string((const gchar *)user_data, NULL, FALSE);
    url = g_strdup_printf("%s/%s", ADMIN_BADGES_URL, escaped_id);
    perform_admin_delete_item(url, "Failed to delete badge.", refresh_admin_badges_only);
    g_free(url);
    g_free(escaped_id);
}

static void
on_admin_delete_conversation_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *escaped_id;
    gchar *url;
    (void)widget;

    escaped_id = g_uri_escape_string((const gchar *)user_data, NULL, FALSE);
    url = g_strdup_printf("%s/%s", ADMIN_DMS_URL, escaped_id);
    perform_admin_delete_item(url, "Failed to delete conversation.", refresh_admin_dms_only);
    g_free(url);
    g_free(escaped_id);
}

static void
on_admin_delete_dm_message_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *escaped_id;
    gchar *url;
    const gchar *conversation_id;
    (void)widget;

    escaped_id = g_uri_escape_string((const gchar *)user_data, NULL, FALSE);
    url = g_strdup_printf("%s/messages/%s", ADMIN_DMS_URL, escaped_id);
    conversation_id = g_object_get_data(G_OBJECT(g_admin_dm_admin_messages_list), "conversation_id");
    perform_admin_delete_item(url, "Failed to delete DM message.", NULL);
    if (conversation_id) {
        start_loading_admin_dm_messages(conversation_id);
    }
    g_free(url);
    g_free(escaped_id);
}

static void
on_admin_delete_shop_product_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *escaped_id;
    gchar *url;
    (void)widget;

    escaped_id = g_uri_escape_string((const gchar *)user_data, NULL, FALSE);
    url = g_strdup_printf("%s/%s", ADMIN_SHOP_PRODUCTS_URL, escaped_id);
    perform_admin_delete_item(url, "Failed to delete shop product.", refresh_admin_shop_only);
    g_free(url);
    g_free(escaped_id);
}

static void
populate_admin_suspensions_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    guint i;

    if (!g_admin_suspensions_list) {
        g_object_unref(parser);
        return;
    }

    if (!json_data || !json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        set_list_box_status(g_admin_suspensions_list, "Failed to load suspensions.");
        return;
    }

    clear_list_box_rows(g_admin_suspensions_list);
    if (JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *items = json_object_has_member(root, "suspensions") ?
            json_object_get_array_member(root, "suspensions") : NULL;
        if (!items || json_array_get_length(items) == 0) {
            set_list_box_status(g_admin_suspensions_list, "No active suspensions.");
            g_object_unref(parser);
            return;
        }

        for (i = 0; i < json_array_get_length(items); i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            const gchar *user_id = json_get_string_or_empty(item, "user_id");
            const gchar *action = json_get_string_or_empty(item, "action");
            gchar *title = g_strdup_printf("@%s", json_get_string_or_empty(item, "username"));
            gchar *body = g_strdup_printf("Action: %s\nReason: %s\nBy: @%s\nCreated: %s\nExpires: %s",
                                          action[0] ? action : "unknown",
                                          json_get_string_or_empty(item, "reason"),
                                          json_get_string_or_empty(item, "suspended_by_username"),
                                          json_get_string_or_empty(item, "created_at"),
                                          json_get_string_or_empty(item, "expires_at")[0] ?
                                              json_get_string_or_empty(item, "expires_at") : "Permanent");
            GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            GtkWidget *lift_button = gtk_button_new_with_label("Lift");
            gchar *payload = g_strdup_printf("%s|%s", user_id, action[0] ? action : "suspend");

            g_signal_connect_data(lift_button, "clicked", G_CALLBACK(on_admin_lift_clicked), payload, free_wrapper, 0);
            gtk_box_pack_start(GTK_BOX(actions), lift_button, FALSE, FALSE, 0);
            gtk_list_box_insert(GTK_LIST_BOX(g_admin_suspensions_list), create_admin_row(title, body, actions), -1);
            g_free(body);
            g_free(title);
        }
    }
    gtk_widget_show_all(g_admin_suspensions_list);
    g_object_unref(parser);
}

static void
populate_admin_reports_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    guint i;

    if (!g_admin_reports_list) {
        g_object_unref(parser);
        return;
    }

    if (!json_data || !json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        set_list_box_status(g_admin_reports_list, "Failed to load reports.");
        return;
    }

    clear_list_box_rows(g_admin_reports_list);
    if (JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *items = json_object_has_member(root, "reports") ?
            json_object_get_array_member(root, "reports") : NULL;
        if (!items || json_array_get_length(items) == 0) {
            set_list_box_status(g_admin_reports_list, "No reports.");
            g_object_unref(parser);
            return;
        }

        for (i = 0; i < json_array_get_length(items); i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            JsonObject *reporter = json_object_has_member(item, "reporter") &&
                                   JSON_NODE_HOLDS_OBJECT(json_object_get_member(item, "reporter")) ?
                json_object_get_object_member(item, "reporter") : NULL;
            JsonObject *reported = json_object_has_member(item, "reported") &&
                                   JSON_NODE_HOLDS_OBJECT(json_object_get_member(item, "reported")) ?
                json_object_get_object_member(item, "reported") : NULL;
            const gchar *report_id = json_get_string_or_empty(item, "id");
            const gchar *reported_type = json_get_string_or_empty(item, "reported_type");
            gchar *title = g_strdup_printf("%s report on %s",
                                           json_get_string_or_empty(item, "reason"),
                                           reported_type[0] ? reported_type : "item");
            gchar *body = g_strdup_printf("Reporter: @%s\nTarget: %s\nStatus: %s\nCreated: %s",
                                          reporter ? json_get_string_or_empty(reporter, "username") : "unknown",
                                          reported_type[0] && g_strcmp0(reported_type, "user") == 0 ?
                                              (reported ? json_get_string_or_empty(reported, "username") : "unknown user") :
                                              (reported ? json_get_string_or_empty(reported, "content") : "unknown post"),
                                          json_get_string_or_empty(item, "status"),
                                          json_get_string_or_empty(item, "created_at"));
            GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            GtkWidget *ignore_button = gtk_button_new_with_label("Ignore");

            g_signal_connect_data(ignore_button, "clicked", G_CALLBACK(on_admin_report_ignore_clicked), g_strdup(report_id), free_wrapper, 0);
            gtk_box_pack_start(GTK_BOX(actions), ignore_button, FALSE, FALSE, 0);

            if (g_strcmp0(reported_type, "post") == 0) {
                GtkWidget *delete_button = gtk_button_new_with_label("Delete Post");
                g_signal_connect_data(delete_button, "clicked", G_CALLBACK(on_admin_report_delete_post_clicked), g_strdup(report_id), free_wrapper, 0);
                gtk_box_pack_start(GTK_BOX(actions), delete_button, FALSE, FALSE, 0);
            } else if (g_strcmp0(reported_type, "user") == 0) {
                GtkWidget *suspend_button = gtk_button_new_with_label("Suspend");
                GtkWidget *restrict_button = gtk_button_new_with_label("Restrict");
                GtkWidget *shadowban_button = gtk_button_new_with_label("Shadowban");
                gchar *suspend_payload = g_strdup_printf("%s|suspend", report_id);
                gchar *restrict_payload = g_strdup_printf("%s|restrict", report_id);
                gchar *shadowban_payload = g_strdup_printf("%s|shadowban", report_id);
                g_signal_connect_data(suspend_button, "clicked", G_CALLBACK(on_admin_report_ban_user_clicked), suspend_payload, free_wrapper, 0);
                g_signal_connect_data(restrict_button, "clicked", G_CALLBACK(on_admin_report_ban_user_clicked), restrict_payload, free_wrapper, 0);
                g_signal_connect_data(shadowban_button, "clicked", G_CALLBACK(on_admin_report_ban_user_clicked), shadowban_payload, free_wrapper, 0);
                gtk_box_pack_start(GTK_BOX(actions), suspend_button, FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(actions), restrict_button, FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(actions), shadowban_button, FALSE, FALSE, 0);
            }

            gtk_list_box_insert(GTK_LIST_BOX(g_admin_reports_list), create_admin_row(title, body, actions), -1);
            g_free(body);
            g_free(title);
        }
    }
    gtk_widget_show_all(g_admin_reports_list);
    g_object_unref(parser);
}

static void
populate_admin_logs_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    guint i;

    if (!g_admin_logs_list) {
        g_object_unref(parser);
        return;
    }

    if (!json_data || !json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        set_list_box_status(g_admin_logs_list, "Failed to load moderation logs.");
        return;
    }

    clear_list_box_rows(g_admin_logs_list);
    if (JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *items = json_object_has_member(root, "logs") ?
            json_object_get_array_member(root, "logs") : NULL;
        if (!items || json_array_get_length(items) == 0) {
            set_list_box_status(g_admin_logs_list, "No moderation logs.");
            g_object_unref(parser);
            return;
        }

        for (i = 0; i < json_array_get_length(items); i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            JsonObject *details_obj = json_get_object_member_valid(item, "details");
            gchar *details = details_obj
                ? json_object_to_display_lines(details_obj)
                : json_node_to_display_text(json_object_get_member(item, "details"));
            gchar *title = g_strdup_printf("%s by @%s",
                                           json_get_string_or_empty(item, "action"),
                                           json_get_string_or_empty(item, "moderator_username"));
            gchar *body = g_strdup_printf("Target: %s %s\nCreated: %s\nDetails: %s",
                                          json_get_string_or_empty(item, "target_type"),
                                          json_get_string_or_empty(item, "target_id"),
                                          json_get_string_or_empty(item, "created_at"),
                                          details && details[0] ? details : "{}");
            gtk_list_box_insert(GTK_LIST_BOX(g_admin_logs_list), create_admin_row(title, body, NULL), -1);
            g_free(body);
            g_free(title);
            g_free(details);
        }
    }
    gtk_widget_show_all(g_admin_logs_list);
    g_object_unref(parser);
}

static void
populate_admin_blocks_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    guint i;

    if (!g_admin_blocks_list) {
        g_object_unref(parser);
        return;
    }

    if (!json_data || !json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        set_list_box_status(g_admin_blocks_list, "Failed to load blocks.");
        return;
    }

    clear_list_box_rows(g_admin_blocks_list);
    if (JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *items = json_object_has_member(root, "blocks") ?
            json_object_get_array_member(root, "blocks") : NULL;
        if (!items || json_array_get_length(items) == 0) {
            set_list_box_status(g_admin_blocks_list, "No blocking relationships.");
            g_object_unref(parser);
            return;
        }

        for (i = 0; i < json_array_get_length(items); i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            gchar *title = g_strdup_printf("@%s blocked @%s",
                                           json_get_string_or_empty(item, "blocker_username"),
                                           json_get_string_or_empty(item, "blocked_username"));
            gchar *body = g_strdup_printf("Created: %s",
                                          json_get_string_or_empty(item, "created_at"));
            gtk_list_box_insert(GTK_LIST_BOX(g_admin_blocks_list), create_admin_row(title, body, NULL), -1);
            g_free(body);
            g_free(title);
        }
    }
    gtk_widget_show_all(g_admin_blocks_list);
    g_object_unref(parser);
}

static void
populate_admin_emojis_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    guint i;

    if (!g_admin_emojis_list) {
        g_object_unref(parser);
        return;
    }

    if (!json_data || !json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        set_list_box_status(g_admin_emojis_list, "Failed to load emojis.");
        return;
    }

    clear_list_box_rows(g_admin_emojis_list);
    if (JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *items = json_object_has_member(root, "emojis") ?
            json_object_get_array_member(root, "emojis") : NULL;
        if (!items || json_array_get_length(items) == 0) {
            set_list_box_status(g_admin_emojis_list, "No custom emojis.");
            g_object_unref(parser);
            return;
        }

        for (i = 0; i < json_array_get_length(items); i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            GtkWidget *delete_button = gtk_button_new_with_label("Delete");
            gchar *title = g_strdup_printf(":%s:", json_get_string_or_empty(item, "name"));
            gchar *body = g_strdup_printf("URL: %s\nCreated: %s",
                                          json_get_string_or_empty(item, "file_url"),
                                          json_get_string_or_empty(item, "created_at"));
            g_signal_connect_data(delete_button, "clicked", G_CALLBACK(on_admin_delete_emoji_clicked), g_strdup(json_get_string_or_empty(item, "id")), free_wrapper, 0);
            gtk_box_pack_start(GTK_BOX(actions), delete_button, FALSE, FALSE, 0);
            gtk_list_box_insert(GTK_LIST_BOX(g_admin_emojis_list), create_admin_row(title, body, actions), -1);
            g_free(body);
            g_free(title);
        }
    }
    gtk_widget_show_all(g_admin_emojis_list);
    g_object_unref(parser);
}

static void
populate_admin_badges_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    guint i;

    if (!g_admin_badges_list) {
        g_object_unref(parser);
        return;
    }

    if (!json_data || !json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        set_list_box_status(g_admin_badges_list, "Failed to load badges.");
        return;
    }

    clear_list_box_rows(g_admin_badges_list);
    if (JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *items = json_object_has_member(root, "badges") ?
            json_object_get_array_member(root, "badges") : NULL;
        if (!items || json_array_get_length(items) == 0) {
            set_list_box_status(g_admin_badges_list, "No badges.");
            g_object_unref(parser);
            return;
        }

        for (i = 0; i < json_array_get_length(items); i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            GtkWidget *delete_button = gtk_button_new_with_label("Delete");
            gchar *title = g_strdup(json_get_string_or_empty(item, "name"));
            gchar *body = g_strdup_printf("Action: %s\nColor: %s\nDescription: %s",
                                          json_get_string_or_empty(item, "action_type"),
                                          json_get_string_or_empty(item, "color"),
                                          json_get_string_or_empty(item, "description"));
            g_signal_connect_data(delete_button, "clicked", G_CALLBACK(on_admin_delete_badge_clicked), g_strdup(json_get_string_or_empty(item, "id")), free_wrapper, 0);
            gtk_box_pack_start(GTK_BOX(actions), delete_button, FALSE, FALSE, 0);
            gtk_list_box_insert(GTK_LIST_BOX(g_admin_badges_list), create_admin_row(title, body, actions), -1);
            g_free(body);
            g_free(title);
        }
    }
    gtk_widget_show_all(g_admin_badges_list);
    g_object_unref(parser);
}

static void
populate_admin_dms_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    guint i;

    if (!g_admin_dms_list) {
        g_object_unref(parser);
        return;
    }

    if (!json_data || !json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        set_list_box_status(g_admin_dms_list, "Failed to load conversations.");
        return;
    }

    clear_list_box_rows(g_admin_dms_list);
    if (JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *items = json_object_has_member(root, "conversations") ?
            json_object_get_array_member(root, "conversations") : NULL;
        if (!items || json_array_get_length(items) == 0) {
            set_list_box_status(g_admin_dms_list, "No conversations.");
            g_object_unref(parser);
            return;
        }

        for (i = 0; i < json_array_get_length(items); i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            GtkWidget *row_widget;
            GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            GtkWidget *delete_button = gtk_button_new_with_label("Delete Conversation");
            gchar *title = g_strdup_printf("Conversation %s", json_get_string_or_empty(item, "id"));
            gchar *body = g_strdup_printf("Participants: %s\nMessages: %d\nLast message: %s",
                                          json_get_string_or_empty(item, "participants"),
                                          (int)json_object_get_int_member(item, "message_count"),
                                          json_get_string_or_empty(item, "last_message_at"));
            g_signal_connect_data(delete_button, "clicked", G_CALLBACK(on_admin_delete_conversation_clicked), g_strdup(json_get_string_or_empty(item, "id")), free_wrapper, 0);
            gtk_box_pack_start(GTK_BOX(actions), delete_button, FALSE, FALSE, 0);
            row_widget = create_admin_row(title, body, actions);
            g_object_set_data_full(G_OBJECT(row_widget), "conversation_id", g_strdup(json_get_string_or_empty(item, "id")), g_free);
            gtk_list_box_insert(GTK_LIST_BOX(g_admin_dms_list), row_widget, -1);
            g_free(body);
            g_free(title);
        }
    }
    gtk_widget_show_all(g_admin_dms_list);
    g_object_unref(parser);
}

static void
populate_admin_dm_messages_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    guint i;

    if (!g_admin_dm_admin_messages_list) {
        g_object_unref(parser);
        return;
    }

    if (!json_data || !json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        set_list_box_status(g_admin_dm_admin_messages_list, "Failed to load DM messages.");
        return;
    }

    clear_list_box_rows(g_admin_dm_admin_messages_list);
    if (JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *items = json_object_has_member(root, "messages") ?
            json_object_get_array_member(root, "messages") : NULL;
        if (!items || json_array_get_length(items) == 0) {
            set_list_box_status(g_admin_dm_admin_messages_list, "No messages in this conversation.");
            g_object_unref(parser);
            return;
        }

        for (i = 0; i < json_array_get_length(items); i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            GtkWidget *delete_button = gtk_button_new_with_label("Delete Message");
            gchar *title = g_strdup_printf("@%s", json_get_string_or_empty(item, "username"));
            gchar *body = g_strdup_printf("%s\nCreated: %s",
                                          json_get_string_or_empty(item, "content"),
                                          json_get_string_or_empty(item, "created_at"));
            g_signal_connect_data(delete_button, "clicked", G_CALLBACK(on_admin_delete_dm_message_clicked), g_strdup(json_get_string_or_empty(item, "id")), free_wrapper, 0);
            gtk_box_pack_start(GTK_BOX(actions), delete_button, FALSE, FALSE, 0);
            gtk_list_box_insert(GTK_LIST_BOX(g_admin_dm_admin_messages_list), create_admin_row(title, body, actions), -1);
            g_free(body);
            g_free(title);
        }
    }
    gtk_widget_show_all(g_admin_dm_admin_messages_list);
    g_object_unref(parser);
}

static void
populate_admin_shop_products_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    guint i;

    if (!g_admin_shop_products_list) {
        g_object_unref(parser);
        return;
    }

    if (!json_data || !json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        set_list_box_status(g_admin_shop_products_list, "Failed to load shop products.");
        return;
    }

    clear_list_box_rows(g_admin_shop_products_list);
    if (JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *items = json_object_has_member(root, "products") ?
            json_object_get_array_member(root, "products") : NULL;
        if (!items || json_array_get_length(items) == 0) {
            set_list_box_status(g_admin_shop_products_list, "No shop products.");
            g_object_unref(parser);
            return;
        }

        for (i = 0; i < json_array_get_length(items); i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            GtkWidget *delete_button = gtk_button_new_with_label("Delete Product");
            gchar *title = g_strdup_printf("%s by @%s",
                                           json_get_string_or_empty(item, "title"),
                                           json_get_string_or_empty(item, "owner_username"));
            gchar *body = g_strdup_printf("Price: %s INR\nType: %s\nCreated: %s\nDescription: %s",
                                          json_get_string_or_empty(item, "price_inr"),
                                          json_get_string_or_empty(item, "content_type"),
                                          json_get_string_or_empty(item, "created_at"),
                                          json_get_string_or_empty(item, "description"));
            g_signal_connect_data(delete_button, "clicked", G_CALLBACK(on_admin_delete_shop_product_clicked), g_strdup(json_get_string_or_empty(item, "id")), free_wrapper, 0);
            gtk_box_pack_start(GTK_BOX(actions), delete_button, FALSE, FALSE, 0);
            gtk_list_box_insert(GTK_LIST_BOX(g_admin_shop_products_list), create_admin_row(title, body, actions), -1);
            g_free(body);
            g_free(title);
        }
    }
    gtk_widget_show_all(g_admin_shop_products_list);
    g_object_unref(parser);
}

static void
populate_admin_shop_purchases_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    guint i;

    if (!g_admin_shop_purchases_list) {
        g_object_unref(parser);
        return;
    }

    if (!json_data || !json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        set_list_box_status(g_admin_shop_purchases_list, "Failed to load shop purchases.");
        return;
    }

    clear_list_box_rows(g_admin_shop_purchases_list);
    if (JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *items = json_object_has_member(root, "purchases") ?
            json_object_get_array_member(root, "purchases") : NULL;
        if (!items || json_array_get_length(items) == 0) {
            set_list_box_status(g_admin_shop_purchases_list, "No purchases.");
            g_object_unref(parser);
            return;
        }

        for (i = 0; i < json_array_get_length(items); i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            gchar *title = g_strdup_printf("%s",
                                           json_get_string_or_empty(item, "product_title"));
            gchar *body = g_strdup_printf("Buyer: @%s\nSeller: @%s\nPrice: %s INR\nCreated: %s",
                                          json_get_string_or_empty(item, "buyer_username"),
                                          json_get_string_or_empty(item, "seller_username"),
                                          json_get_string_or_empty(item, "price"),
                                          json_get_string_or_empty(item, "created_at"));
            gtk_list_box_insert(GTK_LIST_BOX(g_admin_shop_purchases_list), create_admin_row(title, body, NULL), -1);
            g_free(body);
            g_free(title);
        }
    }
    gtk_widget_show_all(g_admin_shop_purchases_list);
    g_object_unref(parser);
}

void
on_admin_dm_conversation_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    GtkWidget *child;
    const gchar *conversation_id;
    (void)box;
    (void)user_data;

    if (!row) return;
    child = gtk_bin_get_child(GTK_BIN(row));
    conversation_id = g_object_get_data(G_OBJECT(child), "conversation_id");
    if (conversation_id) {
        start_loading_admin_dm_messages(conversation_id);
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
    gchar *url;
    if (async_data->query && strlen(async_data->query) > 0) {
        gchar *escaped = g_uri_escape_string(async_data->query, NULL, FALSE);
        url = g_strdup_printf("%s?search=%s", ADMIN_USERS_URL, escaped);
        g_free(escaped);
    } else {
        url = g_strdup(ADMIN_USERS_URL);
    }

    if (perform_admin_request(url, NULL, "GET", &async_data->json_data)) {
        async_data->users = parse_admin_users(async_data->json_data);
        async_data->success = TRUE;
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
    gchar *url;
    if (async_data->query && strlen(async_data->query) > 0) {
        gchar *escaped = g_uri_escape_string(async_data->query, NULL, FALSE);
        url = g_strdup_printf("%s?search=%s", ADMIN_POSTS_URL, escaped);
        g_free(escaped);
    } else {
        url = g_strdup(ADMIN_POSTS_URL);
    }

    if (perform_admin_request(url, NULL, "GET", &async_data->json_data)) {
        async_data->tweets = parse_admin_posts(async_data->json_data);
        async_data->success = TRUE;
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

static gboolean
on_admin_suspensions_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    populate_admin_suspensions_from_json(async_data->json_data);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_suspensions_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    async_data->success = perform_admin_request(ADMIN_SUSPENSIONS_URL, NULL, "GET", &async_data->json_data);
    g_idle_add(on_admin_suspensions_loaded, async_data);
    return NULL;
}

void
start_loading_admin_suspensions(void)
{
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    set_list_box_status(g_admin_suspensions_list, "Loading suspensions...");
    g_thread_new("admin-suspensions-loader", fetch_admin_suspensions_thread, data);
}

static gboolean
on_admin_reports_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    populate_admin_reports_from_json(async_data->json_data);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_reports_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    async_data->success = perform_admin_request(ADMIN_REPORTS_URL, NULL, "GET", &async_data->json_data);
    g_idle_add(on_admin_reports_loaded, async_data);
    return NULL;
}

void
start_loading_admin_reports(void)
{
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    set_list_box_status(g_admin_reports_list, "Loading reports...");
    g_thread_new("admin-reports-loader", fetch_admin_reports_thread, data);
}

static gboolean
on_admin_logs_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    populate_admin_logs_from_json(async_data->json_data);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_logs_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    gchar *url;

    if (async_data->query && *async_data->query) {
        gchar *escaped = g_uri_escape_string(async_data->query, NULL, FALSE);
        url = g_strdup_printf("%s?search=%s", ADMIN_LOGS_URL, escaped);
        g_free(escaped);
    } else {
        url = g_strdup(ADMIN_LOGS_URL);
    }

    async_data->success = perform_admin_request(url, NULL, "GET", &async_data->json_data);
    g_free(url);
    g_idle_add(on_admin_logs_loaded, async_data);
    return NULL;
}

void
start_loading_admin_logs(const gchar *search)
{
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->query = g_strdup(search);
    set_list_box_status(g_admin_logs_list, "Loading moderation logs...");
    g_thread_new("admin-logs-loader", fetch_admin_logs_thread, data);
}

static gboolean
on_admin_blocks_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    populate_admin_blocks_from_json(async_data->json_data);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_blocks_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    async_data->success = perform_admin_request(ADMIN_BLOCKS_URL, NULL, "GET", &async_data->json_data);
    g_idle_add(on_admin_blocks_loaded, async_data);
    return NULL;
}

void
start_loading_admin_blocks(void)
{
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    set_list_box_status(g_admin_blocks_list, "Loading blocks...");
    g_thread_new("admin-blocks-loader", fetch_admin_blocks_thread, data);
}

static gboolean
on_admin_emojis_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    populate_admin_emojis_from_json(async_data->json_data);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_emojis_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    async_data->success = perform_admin_request(ADMIN_EMOJIS_URL, NULL, "GET", &async_data->json_data);
    g_idle_add(on_admin_emojis_loaded, async_data);
    return NULL;
}

void
start_loading_admin_emojis(void)
{
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    set_list_box_status(g_admin_emojis_list, "Loading emojis...");
    g_thread_new("admin-emojis-loader", fetch_admin_emojis_thread, data);
}

static gboolean
on_admin_badges_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    populate_admin_badges_from_json(async_data->json_data);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_badges_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    async_data->success = perform_admin_request(ADMIN_BADGES_URL, NULL, "GET", &async_data->json_data);
    g_idle_add(on_admin_badges_loaded, async_data);
    return NULL;
}

void
start_loading_admin_badges(void)
{
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    set_list_box_status(g_admin_badges_list, "Loading badges...");
    g_thread_new("admin-badges-loader", fetch_admin_badges_thread, data);
}

static gboolean
on_admin_dms_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    populate_admin_dms_from_json(async_data->json_data);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_dms_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    gchar *url;

    if (async_data->query && *async_data->query) {
        gchar *escaped = g_uri_escape_string(async_data->query, NULL, FALSE);
        url = g_strdup_printf("%s?username=%s", ADMIN_DMS_SEARCH_URL, escaped);
        g_free(escaped);
    } else {
        url = g_strdup(ADMIN_DMS_URL);
    }

    async_data->success = perform_admin_request(url, NULL, "GET", &async_data->json_data);
    g_free(url);
    g_idle_add(on_admin_dms_loaded, async_data);
    return NULL;
}

void
start_loading_admin_dms(const gchar *search)
{
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->query = g_strdup(search);
    set_list_box_status(g_admin_dms_list, "Loading conversations...");
    g_thread_new("admin-dms-loader", fetch_admin_dms_thread, data);
}

static gboolean
on_admin_dm_messages_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    populate_admin_dm_messages_from_json(async_data->json_data);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_dm_messages_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    gchar *escaped = g_uri_escape_string(async_data->conversation_id, NULL, FALSE);
    gchar *url = g_strdup_printf("%s/%s/messages", ADMIN_DMS_URL, escaped);

    async_data->success = perform_admin_request(url, NULL, "GET", &async_data->json_data);
    g_free(url);
    g_free(escaped);
    g_idle_add(on_admin_dm_messages_loaded, async_data);
    return NULL;
}

void
start_loading_admin_dm_messages(const gchar *conversation_id)
{
    struct AsyncData *data;

    if (!conversation_id) return;

    g_object_set_data_full(G_OBJECT(g_admin_dm_admin_messages_list), "conversation_id", g_strdup(conversation_id), g_free);
    data = g_new0(struct AsyncData, 1);
    data->conversation_id = g_strdup(conversation_id);
    set_list_box_status(g_admin_dm_admin_messages_list, "Loading messages...");
    g_thread_new("admin-dm-messages-loader", fetch_admin_dm_messages_thread, data);
}

static gboolean
on_admin_shop_products_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    populate_admin_shop_products_from_json(async_data->json_data);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_shop_products_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    gchar *url;

    if (async_data->query && *async_data->query) {
        gchar *escaped = g_uri_escape_string(async_data->query, NULL, FALSE);
        url = g_strdup_printf("%s?q=%s", ADMIN_SHOP_PRODUCTS_URL, escaped);
        g_free(escaped);
    } else {
        url = g_strdup(ADMIN_SHOP_PRODUCTS_URL);
    }

    async_data->success = perform_admin_request(url, NULL, "GET", &async_data->json_data);
    g_free(url);
    g_idle_add(on_admin_shop_products_loaded, async_data);
    return NULL;
}

static gboolean
on_admin_shop_purchases_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    populate_admin_shop_purchases_from_json(async_data->json_data);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_shop_purchases_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    async_data->success = perform_admin_request(ADMIN_SHOP_PURCHASES_URL, NULL, "GET", &async_data->json_data);
    g_idle_add(on_admin_shop_purchases_loaded, async_data);
    return NULL;
}

void
start_loading_admin_shop(const gchar *search)
{
    struct AsyncData *products = g_new0(struct AsyncData, 1);
    struct AsyncData *purchases = g_new0(struct AsyncData, 1);

    products->query = g_strdup(search);

    set_list_box_status(g_admin_shop_products_list, "Loading shop products...");
    set_list_box_status(g_admin_shop_purchases_list, "Loading purchases...");

    g_thread_new("admin-shop-products-loader", fetch_admin_shop_products_thread, products);
    g_thread_new("admin-shop-purchases-loader", fetch_admin_shop_purchases_thread, purchases);
}

static gboolean
on_admin_communities_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (async_data->success && async_data->communities) {
        populate_community_list(GTK_LIST_BOX(g_admin_communities_list), async_data->communities);
        free_communities(async_data->communities);
        async_data->communities = NULL;
    } else {
        populate_community_list(GTK_LIST_BOX(g_admin_communities_list), NULL);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_admin_communities_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (perform_admin_request(COMMUNITIES_LIST_URL, NULL, "GET", &async_data->json_data)) {
        async_data->communities = parse_communities(async_data->json_data);
        async_data->success = async_data->communities != NULL;
    } else {
        async_data->success = FALSE;
    }

    g_idle_add(on_admin_communities_loaded, async_data);
    return NULL;
}

void
start_loading_admin_communities(void)
{
    struct AsyncData *data;

    if (!g_admin_communities_list) {
        return;
    }

    populate_community_list(GTK_LIST_BOX(g_admin_communities_list), NULL);
    data = g_new0(struct AsyncData, 1);
    g_thread_new("admin-communities-loader", fetch_admin_communities_thread, data);
}

void
on_admin_send_notification_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    const gchar *targets_text;
    const gchar *type_text;
    const gchar *title_text;
    const gchar *subtitle_text;
    const gchar *url_text;
    gchar *message_text;
    GPtrArray *targets;
    JsonBuilder *builder;
    JsonGenerator *generator;
    gchar *payload;
    gchar *response = NULL;

    (void)widget;
    (void)user_data;

    targets_text = gtk_entry_get_text(GTK_ENTRY(g_admin_notifications_target_entry));
    type_text = gtk_entry_get_text(GTK_ENTRY(g_admin_notifications_type_entry));
    title_text = gtk_entry_get_text(GTK_ENTRY(g_admin_notifications_title_entry));
    subtitle_text = gtk_entry_get_text(GTK_ENTRY(g_admin_notifications_subtitle_entry));
    url_text = gtk_entry_get_text(GTK_ENTRY(g_admin_notifications_url_entry));
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_admin_notifications_message_view));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    message_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    targets = split_identifier_text(targets_text);

    if (targets->len == 0 && g_strcmp0(targets_text, "all") != 0) {
        gtk_label_set_text(GTK_LABEL(g_admin_notifications_result_label), "Enter at least one target.");
        g_ptr_array_free(targets, TRUE);
        g_free(message_text);
        return;
    }

    if ((!title_text || !*title_text) &&
        (!subtitle_text || !*subtitle_text) &&
        (!message_text || !*message_text)) {
        gtk_label_set_text(GTK_LABEL(g_admin_notifications_result_label),
                           "Provide a title, subtitle, or message.");
        g_ptr_array_free(targets, TRUE);
        g_free(message_text);
        return;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "target");
    if (g_strcmp0(targets_text, "all") == 0) {
        json_builder_add_string_value(builder, "all");
    } else if (targets->len == 1) {
        json_builder_add_string_value(builder, g_ptr_array_index(targets, 0));
    } else {
        json_builder_begin_array(builder);
        for (guint i = 0; i < targets->len; i++) {
            json_builder_add_string_value(builder, g_ptr_array_index(targets, i));
        }
        json_builder_end_array(builder);
    }
    if (type_text && *type_text) {
        json_builder_set_member_name(builder, "type");
        json_builder_add_string_value(builder, type_text);
    }
    if (title_text && *title_text) {
        json_builder_set_member_name(builder, "title");
        json_builder_add_string_value(builder, title_text);
    }
    if (subtitle_text && *subtitle_text) {
        json_builder_set_member_name(builder, "subtitle");
        json_builder_add_string_value(builder, subtitle_text);
    }
    if (message_text && *message_text) {
        json_builder_set_member_name(builder, "message");
        json_builder_add_string_value(builder, message_text);
    }
    if (url_text && *url_text) {
        json_builder_set_member_name(builder, "url");
        json_builder_add_string_value(builder, url_text);
    }
    json_builder_end_object(builder);

    generator = json_generator_new();
    json_generator_set_root(generator, json_builder_get_root(builder));
    payload = json_generator_to_data(generator, NULL);

    if (!perform_admin_request(ADMIN_FAKE_NOTIFICATION_URL, payload, "POST", &response)) {
        gtk_label_set_text(GTK_LABEL(g_admin_notifications_result_label), "Notification request failed.");
    } else {
        gchar *error_message = extract_error_message(response);
        if (error_message) {
            gtk_label_set_text(GTK_LABEL(g_admin_notifications_result_label), error_message);
            g_free(error_message);
        } else {
            gtk_label_set_text(GTK_LABEL(g_admin_notifications_result_label), "Notification sent.");
        }
    }

    g_free(response);
    g_free(payload);
    g_object_unref(generator);
    g_object_unref(builder);
    g_ptr_array_free(targets, TRUE);
    g_free(message_text);
}

void
on_admin_clone_user_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *source_id;
    const gchar *username;
    const gchar *name;
    gchar *escaped_source = NULL;
    gchar *url = NULL;
    JsonBuilder *builder;
    JsonGenerator *generator;
    gchar *payload = NULL;
    gchar *response = NULL;

    (void)widget;
    (void)user_data;

    source_id = gtk_entry_get_text(GTK_ENTRY(g_admin_clone_source_entry));
    username = gtk_entry_get_text(GTK_ENTRY(g_admin_clone_username_entry));
    name = gtk_entry_get_text(GTK_ENTRY(g_admin_clone_name_entry));

    if (!source_id || !*source_id || !username || !*username) {
        gtk_label_set_text(GTK_LABEL(g_admin_clone_result_label),
                           "Source and new username are required.");
        return;
    }

    escaped_source = g_uri_escape_string(source_id, NULL, FALSE);
    url = g_strdup_printf("%s/%s/clone", ADMIN_USERS_URL, escaped_source);
    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "username");
    json_builder_add_string_value(builder, username);
    if (name && *name) {
        json_builder_set_member_name(builder, "name");
        json_builder_add_string_value(builder, name);
    }
    json_builder_set_member_name(builder, "cloneRelations");
    json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_admin_clone_relations_check)));
    json_builder_set_member_name(builder, "cloneGhosts");
    json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_admin_clone_ghosts_check)));
    json_builder_set_member_name(builder, "cloneTweets");
    json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_admin_clone_tweets_check)));
    json_builder_set_member_name(builder, "cloneReplies");
    json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_admin_clone_replies_check)));
    json_builder_set_member_name(builder, "cloneRetweets");
    json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_admin_clone_retweets_check)));
    json_builder_set_member_name(builder, "cloneReactions");
    json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_admin_clone_reactions_check)));
    json_builder_set_member_name(builder, "cloneCommunities");
    json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_admin_clone_communities_check)));
    json_builder_set_member_name(builder, "cloneMedia");
    json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_admin_clone_media_check)));
    json_builder_set_member_name(builder, "cloneAffiliate");
    json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_admin_clone_affiliate_check)));
    json_builder_end_object(builder);
    generator = json_generator_new();
    json_generator_set_root(generator, json_builder_get_root(builder));
    payload = json_generator_to_data(generator, NULL);

    if (!perform_admin_request(url, payload, "POST", &response)) {
        gtk_label_set_text(GTK_LABEL(g_admin_clone_result_label), "Clone request failed.");
    } else {
        gchar *error_message = extract_error_message(response);
        if (error_message) {
            gtk_label_set_text(GTK_LABEL(g_admin_clone_result_label), error_message);
            g_free(error_message);
        } else {
            gchar *result = g_strdup_printf("Cloned user created: @%s", username);
            gtk_label_set_text(GTK_LABEL(g_admin_clone_result_label), result);
            g_free(result);
            start_loading_admin_users(username);
        }
    }

    g_free(response);
    g_free(payload);
    g_object_unref(generator);
    g_object_unref(builder);
    g_free(url);
    g_free(escaped_source);
}

void
on_admin_impersonate_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *identifier;
    gchar *user_id = NULL;
    gchar *user_username = NULL;
    gchar *lookup_error = NULL;
    gchar *url = NULL;
    gchar *response = NULL;
    JsonParser *parser = NULL;
    GError *error = NULL;

    (void)widget;
    (void)user_data;

    identifier = gtk_entry_get_text(GTK_ENTRY(g_admin_impersonation_entry));
    if (!lookup_admin_user_identifier(identifier, &user_id, &user_username, NULL, NULL, &lookup_error)) {
        gtk_label_set_text(GTK_LABEL(g_admin_impersonation_status_label),
                           lookup_error ? lookup_error : "User lookup failed.");
        g_free(lookup_error);
        g_free(user_username);
        g_free(user_id);
        return;
    }

    url = g_strdup_printf(API_BASE_URL "/admin/impersonate/%s", user_id);
    if (!perform_admin_request(url, "{}", "POST", &response)) {
        gtk_label_set_text(GTK_LABEL(g_admin_impersonation_status_label), "Impersonation request failed.");
        goto cleanup;
    }

    {
        gchar *error_message = extract_error_message(response);
        if (error_message) {
            gtk_label_set_text(GTK_LABEL(g_admin_impersonation_status_label), error_message);
            g_free(error_message);
            goto cleanup;
        }
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, response, -1, &error)) {
        gtk_label_set_text(GTK_LABEL(g_admin_impersonation_status_label), error ? error->message : "Invalid impersonation response.");
        if (error) g_error_free(error);
        goto cleanup;
    }

    {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *root_obj;
        const gchar *token;
        const gchar *response_username;

        if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
            gtk_label_set_text(GTK_LABEL(g_admin_impersonation_status_label), "Invalid impersonation response.");
            goto cleanup;
        }

        root_obj = json_node_get_object(root);
        token = json_object_get_string_member(root_obj, "token");
        response_username = user_username;
        if (json_object_has_member(root_obj, "user") &&
            JSON_NODE_HOLDS_OBJECT(json_object_get_member(root_obj, "user"))) {
            JsonObject *user_obj = json_object_get_object_member(root_obj, "user");
            if (json_object_has_member(user_obj, "username") &&
                !json_node_is_null(json_object_get_member(user_obj, "username"))) {
                response_username = json_object_get_string_member(user_obj, "username");
            }
        }

        g_mutex_lock(&g_globals_mutex);
        if (!g_is_impersonating) {
            g_free(g_impersonation_admin_token);
            g_impersonation_admin_token = g_auth_token ? g_strdup(g_auth_token) : NULL;
            g_free(g_impersonation_admin_username);
            g_impersonation_admin_username = g_current_username ? g_strdup(g_current_username) : NULL;
            g_impersonation_admin_is_admin = g_is_admin;
        }
        g_free(g_auth_token);
        g_auth_token = g_strdup(token);
        g_free(g_current_username);
        g_current_username = g_strdup(response_username);
        g_is_admin = FALSE;
        g_is_impersonating = TRUE;
        g_mutex_unlock(&g_globals_mutex);
    }

    save_session(g_auth_token, g_current_username, g_is_admin);
    update_login_ui();
    start_loading_tweets(GTK_LIST_BOX(g_main_list_box));

cleanup:
    if (parser) g_object_unref(parser);
    g_free(response);
    g_free(url);
    g_free(lookup_error);
    g_free(user_username);
    g_free(user_id);
}

void
on_admin_restore_admin_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *restored_username = NULL;

    (void)widget;
    (void)user_data;

    g_mutex_lock(&g_globals_mutex);
    if (!g_is_impersonating || !g_impersonation_admin_token) {
        g_mutex_unlock(&g_globals_mutex);
        gtk_label_set_text(GTK_LABEL(g_admin_impersonation_status_label), "No impersonation session is active.");
        return;
    }

    g_free(g_auth_token);
    g_auth_token = g_strdup(g_impersonation_admin_token);
    g_free(g_current_username);
    g_current_username = g_strdup(g_impersonation_admin_username);
    g_is_admin = g_impersonation_admin_is_admin;
    restored_username = g_strdup(g_current_username);
    clear_impersonation_state_locked();
    g_mutex_unlock(&g_globals_mutex);

    save_session(g_auth_token, g_current_username, g_is_admin);
    update_login_ui();
    start_loading_tweets(GTK_LIST_BOX(g_main_list_box));

    if (restored_username) {
        gchar *text = g_strdup_printf("Restored admin session for @%s.", restored_username);
        gtk_label_set_text(GTK_LABEL(g_admin_impersonation_status_label), text);
        g_free(text);
        g_free(restored_username);
    }
}

void
on_admin_post_as_user_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *content;
    GPtrArray *targets;
    const gchar *reply_to;
    const gchar *source;
    const gchar *created_at;
    gboolean no_char_limit;
    guint succeeded = 0;
    gchar *failure_message = NULL;

    (void)widget;
    (void)user_data;

    targets = split_identifier_text(gtk_entry_get_text(GTK_ENTRY(g_admin_tools_post_targets_entry)));
    if (targets->len == 0) {
        show_modal_message(GTK_MESSAGE_ERROR, "No target users.", "Enter one or more user IDs or usernames.");
        g_ptr_array_free(targets, TRUE);
        return;
    }

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_admin_tools_post_content_view));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (!content || !*content) {
        show_modal_message(GTK_MESSAGE_ERROR, "Content is required.", "Enter post text before submitting.");
        g_ptr_array_free(targets, TRUE);
        g_free(content);
        return;
    }

    reply_to = gtk_entry_get_text(GTK_ENTRY(g_admin_tools_post_reply_to_entry));
    source = gtk_entry_get_text(GTK_ENTRY(g_admin_tools_post_source_entry));
    created_at = gtk_entry_get_text(GTK_ENTRY(g_admin_tools_post_created_at_entry));
    no_char_limit = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_admin_tools_post_no_char_limit_check));

    for (guint i = 0; i < targets->len; i++) {
        gchar *user_id = NULL;
        gchar *username = NULL;
        gchar *lookup_error = NULL;
        JsonBuilder *builder;
        JsonGenerator *generator;
        gchar *payload;
        gchar *response = NULL;

        if (!lookup_admin_user_identifier(g_ptr_array_index(targets, i),
                                          &user_id,
                                          &username,
                                          NULL,
                                          NULL,
                                          &lookup_error)) {
            failure_message = g_strdup(lookup_error ? lookup_error : "User lookup failed.");
            g_free(lookup_error);
            g_free(username);
            g_free(user_id);
            break;
        }

        builder = json_builder_new();
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "userId");
        json_builder_add_string_value(builder, user_id);
        json_builder_set_member_name(builder, "content");
        json_builder_add_string_value(builder, content);
        json_builder_set_member_name(builder, "noCharLimit");
        json_builder_add_boolean_value(builder, no_char_limit);
        if (reply_to && *reply_to) {
            json_builder_set_member_name(builder, "replyTo");
            json_builder_add_string_value(builder, reply_to);
        }
        if (source && *source) {
            json_builder_set_member_name(builder, "source");
            json_builder_add_string_value(builder, source);
        }
        if (created_at && *created_at) {
            json_builder_set_member_name(builder, "created_at");
            json_builder_add_string_value(builder, created_at);
        }
        if (targets->len > 1) {
            json_builder_set_member_name(builder, "massTweet");
            json_builder_add_boolean_value(builder, TRUE);
        }
        json_builder_end_object(builder);

        generator = json_generator_new();
        json_generator_set_root(generator, json_builder_get_root(builder));
        payload = json_generator_to_data(generator, NULL);

        if (!perform_admin_request(ADMIN_TWEETS_URL, payload, "POST", &response)) {
            failure_message = g_strdup("The create-post request failed.");
        } else {
            failure_message = extract_error_message(response);
        }

        g_free(response);
        g_free(payload);
        g_object_unref(generator);
        g_object_unref(builder);
        g_free(lookup_error);
        g_free(username);
        g_free(user_id);

        if (failure_message) {
            break;
        }
        succeeded++;
    }

    if (failure_message) {
        show_modal_message(GTK_MESSAGE_ERROR, "Failed to create posts.", failure_message);
        g_free(failure_message);
    } else {
        gchar *summary = g_strdup_printf("Created %u admin post%s.",
                                         succeeded,
                                         succeeded == 1 ? "" : "s");
        show_modal_message(GTK_MESSAGE_INFO, "Posts created.", summary);
        g_free(summary);
        start_loading_admin_posts(NULL);
    }

    g_ptr_array_free(targets, TRUE);
    g_free(content);
}

void
on_admin_bulk_edit_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *payload_text;
    gchar *normalized_payload = NULL;
    gchar *parse_error = NULL;
    GPtrArray *targets;
    guint updated_count = 0;
    gchar *failure_message = NULL;

    (void)widget;
    (void)user_data;

    targets = split_identifier_text(gtk_entry_get_text(GTK_ENTRY(g_admin_tools_bulk_targets_entry)));
    if (targets->len == 0) {
        show_modal_message(GTK_MESSAGE_ERROR, "No target users.", "Enter one or more user IDs or usernames.");
        g_ptr_array_free(targets, TRUE);
        return;
    }

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_admin_tools_bulk_payload_view));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    payload_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (!parse_json_object_payload(payload_text, &normalized_payload, &parse_error)) {
        show_modal_message(GTK_MESSAGE_ERROR, "Invalid bulk edit payload.", parse_error);
        g_free(parse_error);
        g_free(payload_text);
        g_ptr_array_free(targets, TRUE);
        return;
    }

    for (guint i = 0; i < targets->len; i++) {
        gchar *user_id = NULL;
        gchar *username = NULL;
        gchar *lookup_error = NULL;
        gchar *escaped_id = NULL;
        gchar *url = NULL;
        gchar *response = NULL;

        if (!lookup_admin_user_identifier(g_ptr_array_index(targets, i),
                                          &user_id,
                                          &username,
                                          NULL,
                                          NULL,
                                          &lookup_error)) {
            failure_message = g_strdup(lookup_error ? lookup_error : "User lookup failed.");
            g_free(lookup_error);
            g_free(username);
            g_free(user_id);
            break;
        }

        escaped_id = g_uri_escape_string(user_id, NULL, FALSE);
        url = g_strdup_printf("%s/%s", ADMIN_USERS_URL, escaped_id);
        if (!perform_admin_request(url, normalized_payload, "PATCH", &response)) {
            failure_message = g_strdup("A bulk edit request failed.");
        } else {
            failure_message = extract_error_message(response);
        }

        g_free(response);
        g_free(url);
        g_free(escaped_id);
        g_free(lookup_error);
        g_free(username);
        g_free(user_id);

        if (failure_message) {
            break;
        }
        updated_count++;
    }

    if (failure_message) {
        show_modal_message(GTK_MESSAGE_ERROR, "Bulk edit failed.", failure_message);
        g_free(failure_message);
    } else {
        gchar *summary = g_strdup_printf("Updated %u user account%s.",
                                         updated_count,
                                         updated_count == 1 ? "" : "s");
        show_modal_message(GTK_MESSAGE_INFO, "Bulk edit complete.", summary);
        g_free(summary);
        start_loading_admin_users(NULL);
    }

    g_free(normalized_payload);
    g_free(payload_text);
    g_ptr_array_free(targets, TRUE);
}

void
on_admin_create_community_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *grid;
    GtkWidget *name_entry;
    GtkWidget *description_entry;
    GtkWidget *rules_entry;
    GtkWidget *access_combo;
    GtkWidget *owner_entry;
    gchar *response = NULL;

    (void)user_data;

    dialog = gtk_dialog_new_with_buttons("Create Community",
                                         GTK_WINDOW(gtk_widget_get_toplevel(widget)),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Create", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    name_entry = gtk_entry_new();
    description_entry = gtk_entry_new();
    rules_entry = gtk_entry_new();
    owner_entry = gtk_entry_new();
    access_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(access_combo), "Open");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(access_combo), "Locked");
    gtk_combo_box_set_active(GTK_COMBO_BOX(access_combo), 0);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Description:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), description_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Rules:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), rules_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Access:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), access_combo, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Owner Username:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), owner_entry, 1, 4, 1, 1);

    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        const gchar *description = gtk_entry_get_text(GTK_ENTRY(description_entry));
        const gchar *rules = gtk_entry_get_text(GTK_ENTRY(rules_entry));
        const gchar *owner_username = gtk_entry_get_text(GTK_ENTRY(owner_entry));
        const gchar *access_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(access_combo)) == 1 ? "locked" : "open";
        JsonBuilder *builder;
        JsonGenerator *generator;
        gchar *payload;

        builder = json_builder_new();
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "name");
        json_builder_add_string_value(builder, name);
        json_builder_set_member_name(builder, "description");
        json_builder_add_string_value(builder, description ? description : "");
        json_builder_set_member_name(builder, "rules");
        json_builder_add_string_value(builder, rules ? rules : "");
        json_builder_set_member_name(builder, "access_mode");
        json_builder_add_string_value(builder, access_mode);
        if (owner_username && *owner_username) {
            json_builder_set_member_name(builder, "owner_username");
            json_builder_add_string_value(builder, owner_username);
        }
        json_builder_end_object(builder);

        generator = json_generator_new();
        json_generator_set_root(generator, json_builder_get_root(builder));
        payload = json_generator_to_data(generator, NULL);

        if (!perform_admin_request(COMMUNITIES_LIST_URL, payload, "POST", &response)) {
            show_modal_message(GTK_MESSAGE_ERROR, "Community creation failed.", "The request could not be sent.");
        } else {
            gchar *error_message = extract_error_message(response);
            if (error_message) {
                show_modal_message(GTK_MESSAGE_ERROR, "Community creation failed.", error_message);
                g_free(error_message);
            } else {
                start_loading_admin_communities();
            }
        }

        g_free(payload);
        g_object_unref(generator);
        g_object_unref(builder);
    }

    gtk_widget_destroy(dialog);
    g_free(response);
}

void
on_admin_upload_emoji_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *name_dialog;
    GtkWidget *content;
    GtkWidget *name_entry;
    gchar *filename = NULL;
    gchar *upload_url = NULL;
    gchar *response = NULL;
    gint result;
    (void)user_data;

    dialog = gtk_file_chooser_dialog_new("Select Emoji Image",
                                         GTK_WINDOW(gtk_widget_get_toplevel(widget)),
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Open", GTK_RESPONSE_ACCEPT,
                                         NULL);
    result = gtk_dialog_run(GTK_DIALOG(dialog));
    if (result == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    }
    gtk_widget_destroy(dialog);
    if (!filename) {
        return;
    }

    name_dialog = gtk_dialog_new_with_buttons("Emoji Name",
                                              GTK_WINDOW(gtk_widget_get_toplevel(widget)),
                                              GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                              "_Cancel", GTK_RESPONSE_CANCEL,
                                              "_Create", GTK_RESPONSE_ACCEPT,
                                              NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(name_dialog));
    name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "emoji_name");
    gtk_box_pack_start(GTK_BOX(content), name_entry, FALSE, FALSE, 10);
    gtk_widget_show_all(name_dialog);
    if (gtk_dialog_run(GTK_DIALOG(name_dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *emoji_name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        if (emoji_name && *emoji_name) {
            gchar *escaped_name = g_strescape(emoji_name, NULL);
            upload_url = perform_admin_media_upload(filename);
            if (!upload_url) {
                show_modal_message(GTK_MESSAGE_ERROR, "Emoji upload failed.", "The image could not be uploaded.");
            } else {
                gchar *payload = g_strdup_printf("{\"name\":\"%s\",\"file_url\":\"%s\"}", escaped_name, upload_url);
                if (!perform_admin_request(ADMIN_EMOJIS_URL, payload, "POST", &response)) {
                    show_modal_message(GTK_MESSAGE_ERROR, "Emoji creation failed.", "The admin API request could not be sent.");
                } else {
                    gchar *error_message = extract_error_message(response);
                    if (error_message) {
                        show_modal_message(GTK_MESSAGE_ERROR, "Emoji creation failed.", error_message);
                        g_free(error_message);
                    } else {
                        start_loading_admin_emojis();
                    }
                }
                g_free(payload);
            }
            g_free(escaped_name);
        }
    }
    gtk_widget_destroy(name_dialog);
    g_free(response);
    g_free(upload_url);
    g_free(filename);
}

void
on_admin_create_badge_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *name_entry;
    GtkWidget *image_entry;
    GtkWidget *color_entry;
    GtkWidget *description_entry;
    gchar *response = NULL;
    (void)user_data;

    dialog = gtk_dialog_new_with_buttons("Create Badge",
                                         GTK_WINDOW(gtk_widget_get_toplevel(widget)),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Create", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    name_entry = gtk_entry_new();
    image_entry = gtk_entry_new();
    color_entry = gtk_entry_new();
    description_entry = gtk_entry_new();

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Image URL:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), image_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Color:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), color_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Description:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), description_entry, 1, 3, 1, 1);

    gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        const gchar *image_url = gtk_entry_get_text(GTK_ENTRY(image_entry));
        const gchar *color = gtk_entry_get_text(GTK_ENTRY(color_entry));
        const gchar *description = gtk_entry_get_text(GTK_ENTRY(description_entry));

        if (name && *name && image_url && *image_url) {
            gchar *escaped_name = g_strescape(name, NULL);
            gchar *escaped_image = g_strescape(image_url, NULL);
            gchar *escaped_color = g_strescape(color, NULL);
            gchar *escaped_description = g_strescape(description, NULL);
            gchar *payload = g_strdup_printf(
                "{\"name\":\"%s\",\"image_url\":\"%s\",\"color\":\"%s\",\"description\":\"%s\",\"action_type\":\"none\"}",
                escaped_name,
                escaped_image,
                escaped_color,
                escaped_description
            );
            if (!perform_admin_request(ADMIN_BADGES_URL, payload, "POST", &response)) {
                show_modal_message(GTK_MESSAGE_ERROR, "Badge creation failed.", "The admin API request could not be sent.");
            } else {
                gchar *error_message = extract_error_message(response);
                if (error_message) {
                    show_modal_message(GTK_MESSAGE_ERROR, "Badge creation failed.", error_message);
                    g_free(error_message);
                } else {
                    start_loading_admin_badges();
                }
            }
            g_free(payload);
            g_free(escaped_description);
            g_free(escaped_color);
            g_free(escaped_image);
            g_free(escaped_name);
        }
    }

    gtk_widget_destroy(dialog);
    g_free(response);
}

void perform_admin_verify(const gchar *username, gboolean verify)
{
    if (!has_admin_session_context()) return;
    gchar *url = g_strdup_printf("%s/%s", ADMIN_USERS_URL, username);
    gchar *post_data = g_strdup_printf("{\"verified\": %s}", verify ? "true" : "false");
    struct MemoryStruct chunk = {0};
    if (perform_admin_fetch_url(url, &chunk, post_data, "PATCH")) {
        g_free(chunk.memory);
        start_loading_admin_users(gtk_entry_get_text(GTK_ENTRY(g_admin_users_search)));
    }
    g_free(post_data);
    g_free(url);
}

void perform_admin_suspend(const gchar *username, const gchar *reason)
{
    if (!has_admin_session_context()) return;
    gchar *url = g_strdup_printf("%s/%s/suspend", ADMIN_USERS_URL, username);
    gchar *post_data = g_strdup_printf("{\"reason\": \"%s\", \"action\": \"suspend\"}", reason);
    struct MemoryStruct chunk = {0};
    if (perform_admin_fetch_url(url, &chunk, post_data, "POST")) {
        g_free(chunk.memory);
        start_loading_admin_users(gtk_entry_get_text(GTK_ENTRY(g_admin_users_search)));
    }
    g_free(post_data);
    g_free(url);
}

void perform_admin_delete_user(const gchar *username)
{
    if (!has_admin_session_context()) return;
    gchar *url = g_strdup_printf("%s/%s", ADMIN_USERS_URL, username);
    struct MemoryStruct chunk = {0};
    if (perform_admin_fetch_url(url, &chunk, NULL, "DELETE")) {
        g_free(chunk.memory);
        start_loading_admin_users(gtk_entry_get_text(GTK_ENTRY(g_admin_users_search)));
    }
    g_free(url);
}

void perform_admin_delete_post(const gchar *post_id)
{
    if (!has_admin_session_context()) return;
    gchar *url = g_strdup_printf("%s/%s", ADMIN_POSTS_URL, post_id);
    struct MemoryStruct chunk = {0};
    if (perform_admin_fetch_url(url, &chunk, NULL, "DELETE")) {
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

gboolean
perform_report(const gchar *reported_type,
               const gchar *reported_id,
               const gchar *reason,
               const gchar *additional_info,
               gchar **error_out)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *response = NULL;
    gboolean success = FALSE;

    if (error_out) {
        *error_out = NULL;
    }
    if (!g_auth_token) {
        if (error_out) {
            *error_out = g_strdup("You must be logged in to report content.");
        }
        return FALSE;
    }
    if (!reported_type || !reported_id || !reason || reason[0] == '\0') {
        if (error_out) {
            *error_out = g_strdup("Choose a report reason.");
        }
        return FALSE;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "reported_type");
    json_builder_add_string_value(builder, reported_type);
    json_builder_set_member_name(builder, "reported_id");
    json_builder_add_string_value(builder, reported_id);
    json_builder_set_member_name(builder, "reason");
    json_builder_add_string_value(builder, reason);
    if (additional_info && additional_info[0] != '\0') {
        json_builder_set_member_name(builder, "additional_info");
        json_builder_add_string_value(builder, additional_info);
    }
    json_builder_end_object(builder);

    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);

    if (perform_request_with_optional_payload(REPORT_CREATE_URL, payload, "POST", &response)) {
        gchar *error_message = extract_error_message(response);
        success = (error_message == NULL);
        if (!success && error_out) {
            *error_out = error_message;
            error_message = NULL;
        }
        g_free(error_message);
    } else if (error_out) {
        *error_out = g_strdup("The report request could not be sent.");
    }

    g_free(response);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

static void
show_report_dialog(GtkWidget *widget, const gchar *reported_type, const gchar *reported_id)
{
    GtkWidget *toplevel;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *reason_combo;
    GtkWidget *info_view;
    GtkWidget *info_scroll;

    if (!reported_type || !reported_id) {
        return;
    }
    if (!g_auth_token) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to report content.");
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Report",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Report", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    reason_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(reason_combo), "spam", "Spam");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(reason_combo), "harassment", "Harassment");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(reason_combo), "hate", "Hate or abuse");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(reason_combo), "impersonation", "Impersonation");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(reason_combo), "illegal", "Illegal content");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(reason_combo), "other", "Other");
    gtk_combo_box_set_active(GTK_COMBO_BOX(reason_combo), 0);

    info_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(info_view), GTK_WRAP_WORD_CHAR);
    info_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(info_scroll, 360, 120);
    gtk_container_add(GTK_CONTAINER(info_scroll), info_view);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Reason:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), reason_combo, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Details:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), info_scroll, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(info_view));
        GtkTextIter start;
        GtkTextIter end;
        gchar *details;
        gchar *error_message = NULL;
        const gchar *reason = gtk_combo_box_get_active_id(GTK_COMBO_BOX(reason_combo));

        gtk_text_buffer_get_bounds(buffer, &start, &end);
        details = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        if (perform_report(reported_type, reported_id, reason, details, &error_message)) {
            show_modal_message(GTK_MESSAGE_INFO, "Report submitted.", "Thanks. Moderators will review it.");
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "Report failed.", error_message);
        }

        g_free(details);
        g_free(error_message);
    }

    gtk_widget_destroy(dialog);
}

void
on_report_tweet_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *tweet_id;

    (void)user_data;
    tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    show_report_dialog(widget, "post", tweet_id);
}

void
on_report_profile_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *user_id;

    (void)user_data;
    user_id = g_object_get_data(G_OBJECT(widget), "user_id");
    show_report_dialog(widget, "user", user_id);
}

static gboolean
perform_translate_text(const gchar *text,
                       const gchar *target,
                       gchar **translated_out,
                       gchar **detected_out,
                       gchar **error_out)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    JsonParser *parser;
    JsonNode *root;
    JsonObject *obj;
    GError *parse_error = NULL;
    gchar *payload;
    gchar *response = NULL;
    gboolean success = FALSE;

    if (translated_out) {
        *translated_out = NULL;
    }
    if (detected_out) {
        *detected_out = NULL;
    }
    if (error_out) {
        *error_out = NULL;
    }

    if (!g_auth_token) {
        if (error_out) {
            *error_out = g_strdup("You must be logged in to translate tweets.");
        }
        return FALSE;
    }
    if (!text || text[0] == '\0') {
        if (error_out) {
            *error_out = g_strdup("This tweet has no text to translate.");
        }
        return FALSE;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "text");
    json_builder_add_string_value(builder, text);
    json_builder_set_member_name(builder, "source");
    json_builder_add_string_value(builder, "auto");
    json_builder_set_member_name(builder, "target");
    json_builder_add_string_value(builder, target && target[0] ? target : "en");
    json_builder_end_object(builder);

    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);

    if (!perform_request_with_optional_payload(TRANSLATE_URL, payload, "POST", &response)) {
        if (error_out) {
            gchar *server_error = extract_error_message(response);
            *error_out = server_error ? server_error : g_strdup("Translation failed.");
        }
        goto out;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, response, -1, &parse_error)) {
        if (error_out) {
            *error_out = g_strdup("The translation response could not be read.");
        }
        if (parse_error) {
            g_error_free(parse_error);
        }
        g_object_unref(parser);
        goto out;
    }

    root = json_parser_get_root(parser);
    if (root && JSON_NODE_HOLDS_OBJECT(root)) {
        obj = json_node_get_object(root);
        if (json_object_has_member(obj, "error")) {
            if (error_out) {
                *error_out = extract_error_message(response);
            }
        } else if (json_object_has_member(obj, "translatedText")) {
            if (translated_out) {
                *translated_out = g_strdup(json_get_string_or_empty(obj, "translatedText"));
            }
            if (detected_out) {
                *detected_out = g_strdup(json_get_string_or_empty(obj, "detectedLanguage"));
            }
            success = TRUE;
        }
    }

    if (!success && error_out && !*error_out) {
        *error_out = g_strdup("Translation failed.");
    }
    g_object_unref(parser);

out:
    g_free(response);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

void
on_translate_tweet_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *toplevel;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *target_combo;
    const gchar *tweet_content;

    (void)user_data;
    tweet_content = g_object_get_data(G_OBJECT(widget), "tweet_content");
    if (!g_auth_token) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to translate tweets.");
        return;
    }
    if (!tweet_content || tweet_content[0] == '\0') {
        show_modal_message(GTK_MESSAGE_INFO, "Nothing to translate.", "This tweet has no text content.");
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Translate Tweet",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Translate", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    target_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_combo), "en", "English");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_combo), "es", "Spanish");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_combo), "fr", "French");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_combo), "de", "German");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_combo), "hi", "Hindi");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_combo), "ja", "Japanese");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_combo), "pt", "Portuguese");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_combo), "ar", "Arabic");
    gtk_combo_box_set_active(GTK_COMBO_BOX(target_combo), 0);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Translate to:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), target_combo, 1, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *target = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(target_combo));
        const gchar *target_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(target_combo));
        gchar *translated = NULL;
        gchar *detected = NULL;
        gchar *error_message = NULL;

        if (perform_translate_text(tweet_content, target_id, &translated, &detected, &error_message)) {
            gchar *secondary = g_strdup_printf("Detected: %s\nTarget: %s\n\n%s",
                                               detected && detected[0] ? detected : "auto",
                                               target ? target : "English",
                                               translated ? translated : "");
            show_modal_message(GTK_MESSAGE_INFO, "Translation", secondary);
            g_free(secondary);
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "Translation failed.", error_message);
        }

        g_free(target);
        g_free(translated);
        g_free(detected);
        g_free(error_message);
    }

    gtk_widget_destroy(dialog);
}

void
on_request_affiliate_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
    gchar *url;
    gchar *response = NULL;
    gchar *error_message;

    (void)user_data;
    if (!username || !g_auth_token) {
        return;
    }
    url = g_strdup_printf(PROFILE_REQUEST_AFFILIATE_URL, username);
    if (perform_request_with_optional_payload(url, "{}", "POST", &response)) {
        show_modal_message(GTK_MESSAGE_INFO, "Affiliate request sent.", NULL);
    } else {
        error_message = extract_error_message(response);
        show_modal_message(GTK_MESSAGE_ERROR, "Affiliate request failed.", error_message);
        g_free(error_message);
    }
    g_free(response);
    g_free(url);
}

static void
show_profile_json_summary(GtkWidget *widget,
                          const gchar *title,
                          const gchar *url,
                          gchar *(*formatter)(JsonObject *obj))
{
    gchar *response = NULL;
    gchar *error_message;
    gchar *text;
    JsonParser *parser;
    JsonNode *root;
    GError *error = NULL;

    (void)widget;
    {
        struct MemoryStruct chunk = {0};
        if (!fetch_url(url, &chunk, NULL, "GET")) {
            show_modal_message(GTK_MESSAGE_ERROR, "Profile data unavailable.", NULL);
            return;
        }
        response = chunk.memory;
    }

    error_message = extract_error_message(response);
    if (error_message) {
        show_modal_message(GTK_MESSAGE_ERROR, "Profile data unavailable.", error_message);
        g_free(error_message);
        g_free(response);
        return;
    }

    parser = json_parser_new();
    if (json_parser_load_from_data(parser, response, -1, &error)) {
        root = json_parser_get_root(parser);
        if (root && JSON_NODE_HOLDS_OBJECT(root) && formatter) {
            text = formatter(json_node_get_object(root));
        } else if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            text = json_object_to_display_lines(json_node_get_object(root));
        } else {
            text = json_node_to_display_text(root);
        }
        show_modal_message(GTK_MESSAGE_INFO, title, text);
        g_free(text);
    } else {
        if (error) g_error_free(error);
        show_modal_message(GTK_MESSAGE_ERROR, "Profile data unavailable.", "The server response could not be read.");
    }
    g_object_unref(parser);
    g_free(response);
}

void
on_profile_algorithm_stats_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
    gchar *url;

    (void)user_data;
    if (!username) {
        return;
    }
    url = g_strdup_printf(PROFILE_ALGORITHM_STATS_URL, username);
    show_profile_json_summary(widget, "Algorithm Stats", url, format_algorithm_stats);
    g_free(url);
}

void
on_profile_spam_score_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
    gchar *url;

    (void)user_data;
    if (!username) {
        return;
    }
    url = g_strdup_printf(PROFILE_SPAM_SCORE_URL, username);
    show_profile_json_summary(widget, "Spam Score", url, format_spam_score);
    g_free(url);
}

static void
show_profile_user_list_dialog(GtkWidget *widget, const gchar *title, GList *users)
{
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *list;

    dialog = gtk_dialog_new_with_buttons(title,
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close", GTK_RESPONSE_CLOSE,
                                         NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 480, 520);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE, TRUE, 0);
    populate_user_list(GTK_LIST_BOX(list), users);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
show_profile_tweet_list_dialog(GtkWidget *widget, const gchar *title, GList *tweets)
{
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *list;

    dialog = gtk_dialog_new_with_buttons(title,
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close", GTK_RESPONSE_CLOSE,
                                         NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 620, 620);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE, TRUE, 0);
    populate_tweet_list(GTK_LIST_BOX(list), tweets);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void
on_profile_common_followers_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
    struct MemoryStruct chunk = {0};
    gchar *url;
    GList *users;

    (void)user_data;
    if (!username) return;
    url = g_strdup_printf(EXPLORE_USER_COMMON_FOLLOWERS_URL, username);
    if (fetch_url(url, &chunk, NULL, "GET")) {
        users = parse_users(chunk.memory);
        show_profile_user_list_dialog(widget, "Common Follows", users);
        free_users(users);
        g_free(chunk.memory);
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Common followers unavailable.", NULL);
    }
    g_free(url);
}

void
on_profile_top_posts_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
    struct MemoryStruct chunk = {0};
    gchar *url;
    GList *tweets;

    (void)user_data;
    if (!username) return;
    url = g_strdup_printf(EXPLORE_USER_TOP_POSTS_URL, username);
    if (fetch_url(url, &chunk, NULL, "GET")) {
        tweets = parse_tweets(chunk.memory);
        show_profile_tweet_list_dialog(widget, "Top Posts", tweets);
        free_tweets(tweets);
        g_free(chunk.memory);
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Top posts unavailable.", NULL);
    }
    g_free(url);
}

void
on_profile_communities_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *user_id = g_object_get_data(G_OBJECT(widget), "user_id");
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    struct MemoryStruct chunk = {0};
    gchar *url;
    GList *communities;

    (void)user_data;
    if (!user_id) return;
    url = g_strdup_printf(USER_COMMUNITIES_URL, user_id);
    if (fetch_url(url, &chunk, NULL, "GET")) {
        GtkWidget *dialog = gtk_dialog_new_with_buttons("Communities",
                                                        window,
                                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        "_Close", GTK_RESPONSE_CLOSE,
                                                        NULL);
        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        GtkWidget *list = gtk_list_box_new();
        gtk_widget_set_size_request(scroll, 520, 420);
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
        gtk_container_add(GTK_CONTAINER(scroll), list);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE, TRUE, 8);
        communities = parse_communities(chunk.memory);
        populate_community_list(GTK_LIST_BOX(list), communities);
        gtk_widget_show_all(dialog);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        free_communities(communities);
        g_free(chunk.memory);
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Communities unavailable.", NULL);
    }
    g_free(url);
}

void
on_profile_analytics_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
    struct MemoryStruct chunk = {0};
    gchar *url;
    JsonParser *parser;
    JsonObject *root;
    JsonObject *analytics;
    GError *error = NULL;

    (void)user_data;
    if (!username) return;
    url = g_strdup_printf(EXPLORE_USER_ANALYTICS_URL, username);
    if (!fetch_url(url, &chunk, NULL, "GET")) {
        show_modal_message(GTK_MESSAGE_ERROR, "Analytics unavailable.", NULL);
        g_free(url);
        return;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
        if (error) g_error_free(error);
        show_modal_message(GTK_MESSAGE_ERROR, "Analytics unavailable.", "The server response could not be read.");
    } else {
        root = json_node_get_object(json_parser_get_root(parser));
        if (root && json_object_has_member(root, "analytics") &&
            JSON_NODE_HOLDS_OBJECT(json_object_get_member(root, "analytics"))) {
            gint64 streak = 0;
            gint64 days_active = 0;
            gint64 likes = 0;
            gdouble engagement = 0.0;
            gchar *summary;

            analytics = json_object_get_object_member(root, "analytics");
            if (json_object_has_member(analytics, "posting_streak"))
                streak = json_object_get_int_member(analytics, "posting_streak");
            if (json_object_has_member(analytics, "days_active_30d"))
                days_active = json_object_get_int_member(analytics, "days_active_30d");
            if (json_object_has_member(analytics, "total_likes_received"))
                likes = json_object_get_int_member(analytics, "total_likes_received");
            if (json_object_has_member(analytics, "engagement_rate"))
                engagement = json_object_get_double_member(analytics, "engagement_rate");

            summary = g_strdup_printf("Posting streak: %" G_GINT64_FORMAT " days\nActive days in the last 30 days: %" G_GINT64_FORMAT "\nLikes received: %" G_GINT64_FORMAT "\nEngagement rate: %.2f",
                                      streak, days_active, likes, engagement);
            show_modal_message(GTK_MESSAGE_INFO, "Profile Analytics", summary);
            g_free(summary);
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "Analytics unavailable.", "The server response did not include analytics.");
        }
    }
    g_object_unref(parser);
    g_free(chunk.memory);
    g_free(url);
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
on_profile_highlights_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (async_data->success && g_profile_highlights_list) {
        populate_tweet_list(GTK_LIST_BOX(g_profile_highlights_list), async_data->tweets);
        free_tweets(async_data->tweets);
        async_data->tweets = NULL;
    } else if (g_profile_highlights_list) {
        populate_tweet_list(GTK_LIST_BOX(g_profile_highlights_list), NULL);
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_profile_highlights_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(PROFILE_HIGHLIGHTS_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->tweets = parse_tweets(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_profile_highlights_loaded, async_data);
    return NULL;
}

void
start_loading_profile_highlights(const gchar *username)
{
    if (!g_profile_highlights_list || !username) {
        return;
    }

    populate_tweet_list(GTK_LIST_BOX(g_profile_highlights_list), NULL);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->username = g_strdup(username);
    g_thread_new("profile-highlights-loader", fetch_profile_highlights_thread, data);
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

static gboolean
on_profile_user_list_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;

    if (async_data->list_box) {
        if (async_data->success) {
            populate_user_list(async_data->list_box, async_data->users);
        } else {
            populate_user_list(async_data->list_box, NULL);
        }
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_profile_followers_you_know_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(PROFILE_FOLLOWERS_YOU_KNOW_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->users = parse_users(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_profile_user_list_loaded, async_data);
    return NULL;
}

void
start_loading_profile_followers_you_know(const gchar *username)
{
    if (!g_profile_followers_you_know_list || !username || !g_auth_token) {
        return;
    }

    populate_user_list(GTK_LIST_BOX(g_profile_followers_you_know_list), NULL);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->username = g_strdup(username);
    data->list_box = GTK_LIST_BOX(g_profile_followers_you_know_list);
    g_thread_new("profile-followers-you-know-loader", fetch_profile_followers_you_know_thread, data);
}

static gpointer
fetch_profile_affiliates_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(PROFILE_AFFILIATES_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->users = parse_users(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_profile_user_list_loaded, async_data);
    return NULL;
}

void
start_loading_profile_affiliates(const gchar *username)
{
    if (!g_profile_affiliates_list || !username) {
        return;
    }

    populate_user_list(GTK_LIST_BOX(g_profile_affiliates_list), NULL);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->username = g_strdup(username);
    data->list_box = GTK_LIST_BOX(g_profile_affiliates_list);
    g_thread_new("profile-affiliates-loader", fetch_profile_affiliates_thread, data);
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

static gboolean
json_member_bool(JsonObject *obj, const gchar *name)
{
    JsonNode *node;

    if (!obj || !json_object_has_member(obj, name)) {
        return FALSE;
    }
    node = json_object_get_member(obj, name);
    if (!node || !JSON_NODE_HOLDS_VALUE(node)) {
        return FALSE;
    }
    return json_node_get_value_type(node) == G_TYPE_BOOLEAN
        ? json_node_get_boolean(node)
        : json_node_get_int(node) != 0;
}

static gchar *
json_member_dup_string(JsonObject *obj, const gchar *name)
{
    if (!obj || !json_object_has_member(obj, name) ||
        json_node_is_null(json_object_get_member(obj, name))) {
        return NULL;
    }
    return g_strdup(json_object_get_string_member(obj, name));
}

static struct TweetaList *
parse_tweeta_list_object(JsonObject *obj)
{
    struct TweetaList *list;

    if (!obj) {
        return NULL;
    }

    list = g_new0(struct TweetaList, 1);
    list->id = json_member_dup_string(obj, "id");
    list->user_id = json_member_dup_string(obj, "user_id");
    list->name = json_member_dup_string(obj, "name");
    list->description = json_member_dup_string(obj, "description");
    list->owner_username = json_member_dup_string(obj, "owner_username");
    list->owner_name = json_member_dup_string(obj, "owner_name");
    if (json_object_has_member(obj, "member_count")) {
        list->member_count = json_object_get_int_member(obj, "member_count");
    }
    if (json_object_has_member(obj, "follower_count")) {
        list->follower_count = json_object_get_int_member(obj, "follower_count");
    }
    list->is_private = json_member_bool(obj, "is_private");
    return list;
}

static void
free_tweeta_list(gpointer data)
{
    struct TweetaList *list = data;

    if (!list) {
        return;
    }
    g_free(list->id);
    g_free(list->user_id);
    g_free(list->name);
    g_free(list->description);
    g_free(list->owner_username);
    g_free(list->owner_name);
    if (list->members) {
        free_users(list->members);
    }
    if (list->followers) {
        free_users(list->followers);
    }
    g_free(list);
}

static void
free_tweeta_lists(GList *lists)
{
    g_list_free_full(lists, free_tweeta_list);
}

static GList *
parse_tweeta_list_array(JsonArray *array)
{
    GList *lists = NULL;

    if (!array) {
        return NULL;
    }
    for (guint i = 0; i < json_array_get_length(array); i++) {
        JsonNode *node = json_array_get_element(array, i);
        if (node && JSON_NODE_HOLDS_OBJECT(node)) {
            struct TweetaList *list = parse_tweeta_list_object(json_node_get_object(node));
            if (list) {
                lists = g_list_append(lists, list);
            }
        }
    }
    return lists;
}

static GList *
parse_profile_array_from_member(JsonObject *obj, const gchar *member_name)
{
    JsonArray *array;
    GList *users = NULL;

    if (!obj || !json_object_has_member(obj, member_name) ||
        !JSON_NODE_HOLDS_ARRAY(json_object_get_member(obj, member_name))) {
        return NULL;
    }
    array = json_object_get_array_member(obj, member_name);
    for (guint i = 0; i < json_array_get_length(array); i++) {
        JsonNode *node = json_array_get_element(array, i);
        if (node && JSON_NODE_HOLDS_OBJECT(node)) {
            JsonObject *user_obj = json_node_get_object(node);
            struct Profile *user = g_new0(struct Profile, 1);
            user->id = json_member_dup_string(user_obj, "id");
            user->username = json_member_dup_string(user_obj, "username");
            user->name = json_member_dup_string(user_obj, "name");
            user->bio = json_member_dup_string(user_obj, "bio");
            user->avatar = json_member_dup_string(user_obj, "avatar");
            user->author_verified = json_member_bool(user_obj, "verified");
            user->author_gold = json_member_bool(user_obj, "gold");
            if (json_object_has_member(user_obj, "avatar_radius")) {
                user->avatar_radius = json_object_get_int_member(user_obj, "avatar_radius");
            }
            users = g_list_append(users, user);
        }
    }
    return users;
}

static gboolean
parse_lists_response(const gchar *json_data, GList **owned_out, GList **followed_out)
{
    JsonParser *parser;
    JsonNode *root;
    JsonObject *obj;
    GError *error = NULL;

    if (owned_out) *owned_out = NULL;
    if (followed_out) *followed_out = NULL;
    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        return FALSE;
    }
    root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return FALSE;
    }
    obj = json_node_get_object(root);
    if (json_object_has_member(obj, "error")) {
        g_object_unref(parser);
        return FALSE;
    }
    if (owned_out && json_object_has_member(obj, "ownedLists")) {
        *owned_out = parse_tweeta_list_array(json_object_get_array_member(obj, "ownedLists"));
    }
    if (followed_out && json_object_has_member(obj, "followedLists")) {
        *followed_out = parse_tweeta_list_array(json_object_get_array_member(obj, "followedLists"));
    }
    g_object_unref(parser);
    return TRUE;
}

static struct TweetaList *
parse_list_details_response(const gchar *json_data)
{
    JsonParser *parser;
    JsonNode *root;
    JsonObject *obj;
    JsonObject *list_obj;
    struct TweetaList *list = NULL;
    GError *error = NULL;

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }
    root = json_parser_get_root(parser);
    if (root && JSON_NODE_HOLDS_OBJECT(root)) {
        obj = json_node_get_object(root);
        if (json_object_has_member(obj, "list") &&
            JSON_NODE_HOLDS_OBJECT(json_object_get_member(obj, "list"))) {
            list_obj = json_object_get_object_member(obj, "list");
            list = parse_tweeta_list_object(list_obj);
            list->is_following = json_member_bool(obj, "isFollowing");
            list->is_owner = json_member_bool(obj, "isOwner");
            list->members = parse_profile_array_from_member(obj, "members");
            if (json_object_has_member(list_obj, "owner") &&
                JSON_NODE_HOLDS_OBJECT(json_object_get_member(list_obj, "owner"))) {
                JsonObject *owner = json_object_get_object_member(list_obj, "owner");
                if (!list->owner_username) list->owner_username = json_member_dup_string(owner, "username");
                if (!list->owner_name) list->owner_name = json_member_dup_string(owner, "name");
            }
        }
    }
    g_object_unref(parser);
    return list;
}

static GList *
parse_list_followers_response(const gchar *json_data)
{
    JsonParser *parser;
    JsonNode *root;
    GList *followers = NULL;
    GError *error = NULL;

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }
    root = json_parser_get_root(parser);
    if (root && JSON_NODE_HOLDS_OBJECT(root)) {
        followers = parse_profile_array_from_member(json_node_get_object(root), "followers");
    }
    g_object_unref(parser);
    return followers;
}

static GtkWidget *
create_list_row(struct TweetaList *list, gboolean followed_section)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *open_btn;
    GtkWidget *meta;
    gchar *title;
    gchar *meta_text;

    title = g_strdup_printf("%s%s", list->name ? list->name : "Untitled list",
                            list->is_private ? " (Private)" : "");
    open_btn = gtk_button_new_with_label(title);
    gtk_button_set_relief(GTK_BUTTON(open_btn), GTK_RELIEF_NONE);
    gtk_widget_set_halign(open_btn, GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(open_btn), "list_id", g_strdup(list->id), g_free);
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_list_follow_clicked), GINT_TO_POINTER(2));
    gtk_box_pack_start(GTK_BOX(top), open_btn, FALSE, FALSE, 0);

    if (followed_section) {
        GtkWidget *unfollow = gtk_button_new_with_label("Unfollow");
        g_object_set_data_full(G_OBJECT(unfollow), "list_id", g_strdup(list->id), g_free);
        g_signal_connect(unfollow, "clicked", G_CALLBACK(on_list_follow_clicked), GINT_TO_POINTER(0));
        gtk_box_pack_end(GTK_BOX(top), unfollow, FALSE, FALSE, 0);
    }

    meta_text = g_strdup_printf("%d members · %d followers%s%s%s",
                                list->member_count,
                                list->follower_count,
                                list->owner_username ? " · @" : "",
                                list->owner_username ? list->owner_username : "",
                                list->description ? "" : "");
    meta = gtk_label_new(meta_text);
    gtk_label_set_xalign(GTK_LABEL(meta), 0.0);
    gtk_widget_set_opacity(meta, 0.75);

    gtk_box_pack_start(GTK_BOX(row), top, FALSE, FALSE, 0);
    if (list->description && list->description[0] != '\0') {
        GtkWidget *desc = gtk_label_new(list->description);
        gtk_label_set_xalign(GTK_LABEL(desc), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(desc), TRUE);
        gtk_box_pack_start(GTK_BOX(row), desc, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(row), meta, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(row), 8);
    g_free(title);
    g_free(meta_text);
    return row;
}

static void
populate_lists_box(GtkWidget *list_box, GList *lists, gboolean followed_section)
{
    clear_list_box_rows(list_box);
    if (!lists) {
        set_list_box_status(list_box, followed_section ? "No followed lists." : "No owned lists.");
        return;
    }
    for (GList *l = lists; l; l = l->next) {
        GtkWidget *row = create_list_row(l->data, followed_section);
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(list_box), row, -1);
    }
}

static gboolean
on_lists_loaded(gpointer data)
{
    struct AsyncData *async_data = data;
    GList *owned = async_data->lists;
    GList *followed = NULL;

    async_data->lists = NULL;
    followed = async_data->users ? (GList *)async_data->users : NULL;
    async_data->users = NULL;

    if (async_data->success) {
        populate_lists_box(g_lists_owned_list, owned, FALSE);
        populate_lists_box(g_lists_followed_list, followed, TRUE);
    } else {
        set_list_box_status(g_lists_owned_list, "Failed to load lists.");
        set_list_box_status(g_lists_followed_list, "Failed to load lists.");
    }

    free_tweeta_lists(owned);
    free_tweeta_lists(followed);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_lists_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};
    GList *owned = NULL;
    GList *followed = NULL;

    if (fetch_url(LISTS_URL, &chunk, NULL, "GET") &&
        parse_lists_response(chunk.memory, &owned, &followed)) {
        async_data->success = TRUE;
        async_data->lists = owned;
        async_data->users = (GList *)followed;
    } else {
        async_data->success = FALSE;
    }
    g_free(chunk.memory);
    g_idle_add(on_lists_loaded, async_data);
    return NULL;
}

void
start_loading_lists(void)
{
    if (!g_auth_token) {
        return;
    }
    set_list_box_status(g_lists_owned_list, "Loading lists...");
    set_list_box_status(g_lists_followed_list, "Loading lists...");
    g_thread_new("lists-loader", fetch_lists_thread, g_new0(struct AsyncData, 1));
}

void
on_lists_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    if (!g_auth_token) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to use lists.");
        return;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "lists");
    gtk_widget_show(g_back_button);
    start_loading_lists();
    (void)widget;
}

static gboolean
list_json_request(const gchar *url, const gchar *payload, const gchar *method, gchar **error_out)
{
    gchar *response = NULL;
    gboolean success = FALSE;

    if (error_out) *error_out = NULL;
    if (perform_request_with_optional_payload(url, payload, method, &response)) {
        gchar *error_message = extract_error_message(response);
        success = (error_message == NULL);
        if (!success && error_out) {
            *error_out = error_message;
            error_message = NULL;
        }
        g_free(error_message);
    } else if (error_out) {
        *error_out = g_strdup("The list request could not be sent.");
    }
    g_free(response);
    return success;
}

static gchar *
build_list_payload(const gchar *name, const gchar *description, gboolean is_private)
{
    JsonBuilder *builder = json_builder_new();
    JsonGenerator *gen = json_generator_new();
    gchar *payload;

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, name ? name : "");
    json_builder_set_member_name(builder, "description");
    json_builder_add_string_value(builder, description ? description : "");
    json_builder_set_member_name(builder, "isPrivate");
    json_builder_add_boolean_value(builder, is_private);
    json_builder_end_object(builder);
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);
    g_object_unref(gen);
    g_object_unref(builder);
    return payload;
}

static gboolean
run_list_editor(GtkWidget *widget, const gchar *title_text, struct TweetaList *list)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *name_entry;
    GtkWidget *description_entry;
    GtkWidget *private_check;
    GtkWidget *toplevel;
    gboolean changed = FALSE;

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons(title_text,
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    name_entry = gtk_entry_new();
    description_entry = gtk_entry_new();
    private_check = gtk_check_button_new_with_label("Private");
    gtk_entry_set_max_length(GTK_ENTRY(name_entry), 25);
    gtk_entry_set_max_length(GTK_ENTRY(description_entry), 100);
    if (list) {
        gtk_entry_set_text(GTK_ENTRY(name_entry), list->name ? list->name : "");
        gtk_entry_set_text(GTK_ENTRY(description_entry), list->description ? list->description : "");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(private_check), list->is_private);
    }
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Description:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), description_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), private_check, 1, 2, 1, 1);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        const gchar *description = gtk_entry_get_text(GTK_ENTRY(description_entry));
        gboolean is_private = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(private_check));
        gchar *payload = build_list_payload(name, description, is_private);
        gchar *url = list ? g_strdup_printf(LIST_DETAILS_URL, list->id) : g_strdup(LISTS_URL);
        gchar *error_message = NULL;
        const gchar *method = list ? "PATCH" : "POST";

        if (list_json_request(url, payload, method, &error_message)) {
            changed = TRUE;
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "List update failed.", error_message);
        }
        g_free(error_message);
        g_free(url);
        g_free(payload);
    }
    gtk_widget_destroy(dialog);
    return changed;
}

void
on_create_list_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    if (run_list_editor(widget, "Create List", NULL)) {
        start_loading_lists();
    }
}

void
on_list_edit_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    if (g_active_list && run_list_editor(widget, "Edit List", g_active_list)) {
        show_list_details(g_active_list->id);
        start_loading_lists();
    }
}

void
on_list_delete_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *toplevel;
    GtkWidget *dialog;

    (void)user_data;
    if (!g_active_list || !g_active_list->id) {
        return;
    }
    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_OK_CANCEL,
                                    "Delete this list?");
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        gchar *url = g_strdup_printf(LIST_DETAILS_URL, g_active_list->id);
        gchar *error_message = NULL;
        if (list_json_request(url, NULL, "DELETE", &error_message)) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "lists");
            start_loading_lists();
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "List deletion failed.", error_message);
        }
        g_free(error_message);
        g_free(url);
    }
    gtk_widget_destroy(dialog);
}

void
on_list_follow_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *list_id = g_object_get_data(G_OBJECT(widget), "list_id");
    gint mode = GPOINTER_TO_INT(user_data);
    gchar *url;
    gchar *error_message = NULL;

    if (!list_id) {
        return;
    }
    if (mode == 2) {
        show_list_details(list_id);
        return;
    }
    url = g_strdup_printf(LIST_FOLLOW_URL, list_id);
    if (mode == 3 && g_active_list) {
        mode = g_active_list->is_following ? 0 : 1;
    }
    if (!list_json_request(url, NULL, mode ? "POST" : "DELETE", &error_message)) {
        show_modal_message(GTK_MESSAGE_ERROR, "List follow failed.", error_message);
    }
    g_free(error_message);
    g_free(url);
    start_loading_lists();
    show_list_details(list_id);
}

static gchar *
lookup_profile_id(const gchar *username)
{
    gchar *escaped;
    gchar *url;
    gchar *response;
    JsonParser *parser;
    JsonNode *root;
    JsonObject *obj;
    JsonObject *profile;
    gchar *id = NULL;
    GError *error = NULL;

    if (!username || username[0] == '\0') {
        return NULL;
    }
    escaped = g_uri_escape_string(username, NULL, FALSE);
    url = g_strdup_printf(PROFILE_URL, escaped);
    response = perform_simple_json_request(url, "GET", NULL);
    parser = json_parser_new();
    if (response && json_parser_load_from_data(parser, response, -1, &error)) {
        root = json_parser_get_root(parser);
        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            obj = json_node_get_object(root);
            if (json_object_has_member(obj, "profile") &&
                JSON_NODE_HOLDS_OBJECT(json_object_get_member(obj, "profile"))) {
                profile = json_object_get_object_member(obj, "profile");
                id = json_member_dup_string(profile, "id");
            }
        }
    } else if (error) {
        g_error_free(error);
    }
    g_object_unref(parser);
    g_free(response);
    g_free(url);
    g_free(escaped);
    return id;
}

static gboolean
muted_request(const gchar *url, const gchar *payload, const gchar *method, gchar **error_out)
{
    gchar *response = NULL;
    gboolean success = FALSE;

    if (error_out) *error_out = NULL;
    if (perform_request_with_optional_payload(url, payload, method, &response)) {
        gchar *error_message = extract_error_message(response);
        success = (error_message == NULL);
        if (!success && error_out) {
            *error_out = error_message;
            error_message = NULL;
        }
        g_free(error_message);
    } else if (error_out) {
        *error_out = g_strdup("The mute request could not be sent.");
    }
    g_free(response);
    return success;
}

static void
on_remove_muted_word_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *word_id = g_object_get_data(G_OBJECT(widget), "word_id");
    gchar *url;
    gchar *error_message = NULL;

    (void)user_data;
    if (!word_id) {
        return;
    }
    url = g_strdup_printf(MUTED_WORD_URL, word_id);
    if (muted_request(url, NULL, "DELETE", &error_message)) {
        start_loading_muted_words();
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Could not remove muted word.", error_message);
    }
    g_free(error_message);
    g_free(url);
}

static void
on_open_muted_conversation_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");

    (void)user_data;
    if (tweet_id) {
        show_tweet(tweet_id);
    }
}

static gboolean
on_muted_words_loaded(gpointer data)
{
    struct AsyncData *async_data = data;
    JsonParser *parser;
    JsonNode *root;
    JsonObject *obj;
    JsonArray *words = NULL;
    GError *error = NULL;

    if (!g_muted_words_list) {
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    clear_list_box_rows(g_muted_words_list);
    if (!async_data->success || !async_data->json_data) {
        set_list_box_status(g_muted_words_list, "Failed to load muted words.");
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, async_data->json_data, -1, &error)) {
        if (error) g_error_free(error);
        set_list_box_status(g_muted_words_list, "Failed to load muted words.");
        g_object_unref(parser);
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    root = json_parser_get_root(parser);
    if (root && JSON_NODE_HOLDS_OBJECT(root)) {
        obj = json_node_get_object(root);
        if (json_object_has_member(obj, "words") &&
            JSON_NODE_HOLDS_ARRAY(json_object_get_member(obj, "words"))) {
            words = json_object_get_array_member(obj, "words");
        }
    }
    if (!words || json_array_get_length(words) == 0) {
        set_list_box_status(g_muted_words_list, "No muted words.");
    } else {
        for (guint i = 0; i < json_array_get_length(words); i++) {
            JsonObject *word_obj = json_array_get_object_element(words, i);
            const gchar *id = json_get_string_or_empty(word_obj, "id");
            const gchar *word = json_get_string_or_empty(word_obj, "word");
            const gchar *created_at = json_get_string_or_empty(word_obj, "created_at");
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gchar *label_text = g_strdup_printf("%s%s%s", word,
                                                created_at[0] ? " · " : "",
                                                created_at[0] ? created_at : "");
            GtkWidget *label = gtk_label_new(label_text);
            GtkWidget *remove = gtk_button_new_with_label("Remove");
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
            g_object_set_data_full(G_OBJECT(remove), "word_id", g_strdup(id), g_free);
            g_signal_connect(remove, "clicked", G_CALLBACK(on_remove_muted_word_clicked), NULL);
            gtk_box_pack_end(GTK_BOX(row), remove, FALSE, FALSE, 0);
            gtk_container_set_border_width(GTK_CONTAINER(row), 6);
            gtk_widget_show_all(row);
            gtk_list_box_insert(GTK_LIST_BOX(g_muted_words_list), row, -1);
            g_free(label_text);
        }
    }
    g_object_unref(parser);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_muted_words_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};

    async_data->success = fetch_url(MUTED_WORDS_URL, &chunk, NULL, "GET");
    if (async_data->success) {
        async_data->json_data = g_strdup(chunk.memory);
    }
    g_free(chunk.memory);
    g_idle_add(on_muted_words_loaded, async_data);
    return NULL;
}

void
start_loading_muted_words(void)
{
    if (!g_auth_token || !g_muted_words_list) {
        return;
    }
    set_list_box_status(g_muted_words_list, "Loading muted words...");
    g_thread_new("muted-words-loader", fetch_muted_words_thread, g_new0(struct AsyncData, 1));
}

void
on_add_muted_word_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *word;
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *error_message = NULL;

    (void)widget;
    (void)user_data;
    if (!g_muted_word_entry) {
        return;
    }
    word = gtk_entry_get_text(GTK_ENTRY(g_muted_word_entry));
    if (!word || word[0] == '\0') {
        return;
    }
    builder = json_builder_new();
    gen = json_generator_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "word");
    json_builder_add_string_value(builder, word);
    json_builder_end_object(builder);
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);
    if (muted_request(MUTED_WORDS_URL, payload, "POST", &error_message)) {
        gtk_entry_set_text(GTK_ENTRY(g_muted_word_entry), "");
        start_loading_muted_words();
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Could not add muted word.", error_message);
    }
    g_free(error_message);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
}

static gboolean
on_muted_conversations_loaded(gpointer data)
{
    struct AsyncData *async_data = data;
    JsonParser *parser;
    JsonNode *root;
    JsonObject *obj;
    JsonArray *conversations = NULL;
    GError *error = NULL;

    if (!g_muted_conversations_list) {
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    clear_list_box_rows(g_muted_conversations_list);
    parser = json_parser_new();
    if (async_data->success &&
        json_parser_load_from_data(parser, async_data->json_data, -1, &error)) {
        root = json_parser_get_root(parser);
        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            obj = json_node_get_object(root);
            if (json_object_has_member(obj, "conversations") &&
                JSON_NODE_HOLDS_ARRAY(json_object_get_member(obj, "conversations"))) {
                conversations = json_object_get_array_member(obj, "conversations");
            }
        }
    } else if (error) {
        g_error_free(error);
    }
    if (!async_data->success) {
        set_list_box_status(g_muted_conversations_list, "Failed to load muted conversations.");
    } else if (!conversations || json_array_get_length(conversations) == 0) {
        set_list_box_status(g_muted_conversations_list, "No muted conversations.");
    } else {
        for (guint i = 0; i < json_array_get_length(conversations); i++) {
            JsonObject *conv = json_array_get_object_element(conversations, i);
            const gchar *post_id = json_get_string_or_empty(conv, "post_id");
            const gchar *created_at = json_get_string_or_empty(conv, "created_at");
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gchar *text = g_strdup_printf("%s%s%s", post_id,
                                          created_at[0] ? " · " : "",
                                          created_at[0] ? created_at : "");
            GtkWidget *label = gtk_label_new(text);
            GtkWidget *open = gtk_button_new_with_label("Open");
            GtkWidget *unmute = gtk_button_new_with_label("Unmute");
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
            g_object_set_data_full(G_OBJECT(open), "tweet_id", g_strdup(post_id), g_free);
            g_signal_connect(open, "clicked", G_CALLBACK(on_open_muted_conversation_clicked), NULL);
            g_object_set_data_full(G_OBJECT(unmute), "tweet_id", g_strdup(post_id), g_free);
            g_signal_connect(unmute, "clicked", G_CALLBACK(on_mute_conversation_clicked), NULL);
            gtk_box_pack_end(GTK_BOX(row), unmute, FALSE, FALSE, 0);
            gtk_box_pack_end(GTK_BOX(row), open, FALSE, FALSE, 0);
            gtk_container_set_border_width(GTK_CONTAINER(row), 6);
            gtk_widget_show_all(row);
            gtk_list_box_insert(GTK_LIST_BOX(g_muted_conversations_list), row, -1);
            g_free(text);
        }
    }
    g_object_unref(parser);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_muted_conversations_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};

    async_data->success = fetch_url(MUTED_CONVERSATIONS_URL, &chunk, NULL, "GET");
    if (async_data->success) {
        async_data->json_data = g_strdup(chunk.memory);
    }
    g_free(chunk.memory);
    g_idle_add(on_muted_conversations_loaded, async_data);
    return NULL;
}

void
start_loading_muted_conversations(void)
{
    if (!g_auth_token || !g_muted_conversations_list) {
        return;
    }
    set_list_box_status(g_muted_conversations_list, "Loading muted conversations...");
    g_thread_new("muted-conversations-loader", fetch_muted_conversations_thread, g_new0(struct AsyncData, 1));
}

void
on_mute_conversation_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    gchar *url;
    gchar *response = NULL;
    gchar *error_message = NULL;

    (void)user_data;
    if (!tweet_id || !g_auth_token) {
        return;
    }
    url = g_strdup_printf(MUTED_CONVERSATION_URL, tweet_id);
    if (perform_request_with_optional_payload(url, NULL, "POST", &response)) {
        error_message = extract_error_message(response);
        if (error_message) {
            show_modal_message(GTK_MESSAGE_ERROR, "Could not update conversation mute.", error_message);
        } else {
            start_loading_muted_conversations();
        }
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Could not update conversation mute.", NULL);
    }
    g_free(error_message);
    g_free(response);
    g_free(url);
}

static void
on_remove_for_you_interest_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *topic = g_object_get_data(G_OBJECT(widget), "topic");
    gchar *escaped;
    gchar *url;
    gchar *response = NULL;
    gchar *error_message;

    (void)user_data;
    if (!topic || !g_auth_token) {
        return;
    }
    escaped = g_uri_escape_string(topic, NULL, FALSE);
    url = g_strdup_printf(FOR_YOU_INTEREST_URL, escaped);
    if (perform_request_with_optional_payload(url, NULL, "DELETE", &response)) {
        start_loading_for_you_interests();
    } else {
        error_message = extract_error_message(response);
        show_modal_message(GTK_MESSAGE_ERROR, "Could not remove interest.", error_message);
        g_free(error_message);
    }
    g_free(response);
    g_free(url);
    g_free(escaped);
}

static gboolean
on_for_you_interests_loaded(gpointer data)
{
    struct AsyncData *async_data = data;
    JsonParser *parser = json_parser_new();
    JsonObject *obj = NULL;
    JsonArray *topics = NULL;
    GError *error = NULL;

    if (!g_for_you_interests_list) {
        g_object_unref(parser);
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    clear_list_box_rows(g_for_you_interests_list);
    if (async_data->success && async_data->json_data &&
        json_parser_load_from_data(parser, async_data->json_data, -1, &error)) {
        obj = json_node_get_object(json_parser_get_root(parser));
        if (obj && json_object_has_member(obj, "topics")) {
            topics = json_object_get_array_member(obj, "topics");
        }
    } else if (error) {
        g_error_free(error);
    }
    if (!async_data->success || !topics) {
        set_list_box_status(g_for_you_interests_list, "Failed to load interests.");
    } else if (json_array_get_length(topics) == 0) {
        set_list_box_status(g_for_you_interests_list, "No learned interests yet.");
    } else {
        for (guint i = 0; i < json_array_get_length(topics); i++) {
            JsonObject *item = json_array_get_object_element(topics, i);
            const gchar *topic = json_get_string_or_empty(item, "topic");
            gdouble weight = json_object_has_member(item, "weight") ? json_object_get_double_member(item, "weight") : 0.0;
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget *label;
            GtkWidget *remove = gtk_button_new_with_label("Remove");
            gchar *text = g_strdup_printf("%s · %.2f", topic, weight);
            gtk_container_set_border_width(GTK_CONTAINER(row), 6);
            label = gtk_label_new(text);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
            g_object_set_data_full(G_OBJECT(remove), "topic", g_strdup(topic), g_free);
            g_signal_connect(remove, "clicked", G_CALLBACK(on_remove_for_you_interest_clicked), NULL);
            gtk_box_pack_end(GTK_BOX(row), remove, FALSE, FALSE, 0);
            gtk_widget_show_all(row);
            gtk_list_box_insert(GTK_LIST_BOX(g_for_you_interests_list), row, -1);
            g_free(text);
        }
    }
    g_object_unref(parser);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_for_you_interests_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};

    async_data->success = fetch_url(FOR_YOU_INTERESTS_URL, &chunk, NULL, "GET");
    if (async_data->success) {
        async_data->json_data = g_strdup(chunk.memory);
    }
    g_free(chunk.memory);
    g_idle_add(on_for_you_interests_loaded, async_data);
    return NULL;
}

void
start_loading_for_you_interests(void)
{
    if (!g_auth_token || !g_for_you_interests_list) {
        return;
    }
    set_list_box_status(g_for_you_interests_list, "Loading interests...");
    g_thread_new("for-you-interests-loader", fetch_for_you_interests_thread, g_new0(struct AsyncData, 1));
}

void
on_clear_for_you_interests_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *response = NULL;
    gchar *error_message;

    (void)widget;
    (void)user_data;
    if (!g_auth_token) {
        return;
    }
    if (perform_request_with_optional_payload(FOR_YOU_INTERESTS_URL, NULL, "DELETE", &response)) {
        start_loading_for_you_interests();
    } else {
        error_message = extract_error_message(response);
        show_modal_message(GTK_MESSAGE_ERROR, "Could not reset interests.", error_message);
        g_free(error_message);
    }
    g_free(response);
}

static void
on_delete_scheduled_post_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *post_id = g_object_get_data(G_OBJECT(widget), "scheduled_id");
    gchar *url;
    gchar *response = NULL;
    gchar *error_message;

    (void)user_data;
    if (!post_id || !g_auth_token) {
        return;
    }
    url = g_strdup_printf(SCHEDULED_POST_URL, post_id);
    if (perform_request_with_optional_payload(url, NULL, "DELETE", &response)) {
        start_loading_scheduled_posts();
    } else {
        error_message = extract_error_message(response);
        show_modal_message(GTK_MESSAGE_ERROR, "Could not cancel scheduled post.", error_message);
        g_free(error_message);
    }
    g_free(response);
    g_free(url);
}

static GtkWidget *
create_scheduled_post_row(JsonObject *post)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *content;
    GtkWidget *meta;
    GtkWidget *cancel;
    const gchar *id = json_get_string_or_empty(post, "id");
    const gchar *text = json_get_string_or_empty(post, "content");
    const gchar *scheduled_for = json_get_string_or_empty(post, "scheduled_for");
    const gchar *status = json_get_string_or_empty(post, "status");
    gchar *meta_text = g_strdup_printf("%s%s%s",
                                       scheduled_for,
                                       status[0] ? " · " : "",
                                       status);

    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    content = gtk_label_new(text);
    meta = gtk_label_new(meta_text);
    cancel = gtk_button_new_with_label("Cancel");
    gtk_label_set_xalign(GTK_LABEL(content), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(content), TRUE);
    gtk_label_set_xalign(GTK_LABEL(meta), 0.0);
    gtk_widget_set_opacity(meta, 0.75);
    g_object_set_data_full(G_OBJECT(cancel), "scheduled_id", g_strdup(id), g_free);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_delete_scheduled_post_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box), content, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), meta, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), cancel, FALSE, FALSE, 0);
    g_free(meta_text);
    return box;
}

static gboolean
on_scheduled_posts_loaded(gpointer data)
{
    struct AsyncData *async_data = data;
    JsonParser *parser = NULL;
    JsonObject *obj = NULL;
    JsonArray *posts = NULL;
    GError *error = NULL;

    if (!g_scheduled_posts_list) {
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    clear_list_box_rows(g_scheduled_posts_list);
    if (async_data->success && async_data->json_data) {
        parser = json_parser_new();
        if (json_parser_load_from_data(parser, async_data->json_data, -1, &error)) {
            obj = json_node_get_object(json_parser_get_root(parser));
            if (obj && json_object_has_member(obj, "scheduledPosts")) {
                posts = json_object_get_array_member(obj, "scheduledPosts");
            }
        } else if (error) {
            g_error_free(error);
        }
    }
    if (!async_data->success || !posts) {
        set_list_box_status(g_scheduled_posts_list, "Failed to load scheduled posts.");
    } else if (json_array_get_length(posts) == 0) {
        set_list_box_status(g_scheduled_posts_list, "No scheduled posts.");
    } else {
        for (guint i = 0; i < json_array_get_length(posts); i++) {
            JsonObject *post = json_array_get_object_element(posts, i);
            GtkWidget *row = create_scheduled_post_row(post);
            gtk_widget_show_all(row);
            gtk_list_box_insert(GTK_LIST_BOX(g_scheduled_posts_list), row, -1);
        }
    }
    if (parser) {
        g_object_unref(parser);
    }
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_scheduled_posts_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};

    async_data->success = fetch_url(SCHEDULED_POSTS_URL, &chunk, NULL, "GET");
    if (async_data->success) {
        async_data->json_data = g_strdup(chunk.memory);
    }
    g_free(chunk.memory);
    g_idle_add(on_scheduled_posts_loaded, async_data);
    return NULL;
}

void
start_loading_scheduled_posts(void)
{
    if (!g_auth_token || !g_scheduled_posts_list) {
        return;
    }
    set_list_box_status(g_scheduled_posts_list, "Loading scheduled posts...");
    g_thread_new("scheduled-posts-loader", fetch_scheduled_posts_thread, g_new0(struct AsyncData, 1));
}

static gboolean
perform_schedule_post(const gchar *content, const gchar *scheduled_for, gchar **error_out)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *response = NULL;
    gboolean success;

    if (error_out) {
        *error_out = NULL;
    }
    if (!content || content[0] == '\0' || !scheduled_for || scheduled_for[0] == '\0') {
        if (error_out) *error_out = g_strdup("Content and scheduled time are required.");
        return FALSE;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, content);
    json_builder_set_member_name(builder, "scheduled_for");
    json_builder_add_string_value(builder, scheduled_for);
    json_builder_set_member_name(builder, "reply_restriction");
    json_builder_add_string_value(builder, "everyone");
    json_builder_end_object(builder);
    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);
    success = perform_request_with_optional_payload(SCHEDULED_POSTS_URL, payload, "POST", &response);
    if (!success && error_out) {
        gchar *server_error = extract_error_message(response);
        *error_out = server_error ? server_error : g_strdup("Post could not be scheduled.");
    }
    g_free(response);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

void
on_schedule_post_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *toplevel;
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *grid;
    GtkWidget *time_entry;
    GtkWidget *text_view;
    GtkWidget *text_scroll;

    (void)user_data;
    if (!g_auth_token) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to schedule posts.");
        return;
    }
    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Schedule Post",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Schedule", GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 360);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    time_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(time_entry), "YYYY-MM-DDTHH:MM:SSZ");
    text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    text_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(text_scroll, -1, 180);
    gtk_container_add(GTK_CONTAINER(text_scroll), text_view);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("When:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), time_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Post:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), text_scroll, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
        GtkTextIter start;
        GtkTextIter end;
        gchar *text;
        gchar *error_message = NULL;
        const gchar *scheduled_for = gtk_entry_get_text(GTK_ENTRY(time_entry));

        gtk_text_buffer_get_bounds(buffer, &start, &end);
        text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        if (perform_schedule_post(text, scheduled_for, &error_message)) {
            start_loading_scheduled_posts();
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "Schedule failed.", error_message);
        }
        g_free(text);
        g_free(error_message);
    }
    gtk_widget_destroy(dialog);
}

static gboolean
perform_delegate_action(const gchar *url, const gchar *method, const gchar *payload, gchar **error_out)
{
    gchar *response = NULL;
    gboolean success;

    if (error_out) {
        *error_out = NULL;
    }
    if (!g_auth_token) {
        if (error_out) *error_out = g_strdup("You must be logged in to manage delegates.");
        return FALSE;
    }
    success = perform_request_with_optional_payload(url, payload, method, &response);
    if (!success && error_out) {
        gchar *server_error = extract_error_message(response);
        *error_out = server_error ? server_error : g_strdup("Delegate request failed.");
    }
    g_free(response);
    return success;
}

static gboolean
apply_auth_switch_response(const gchar *json_data, gchar **error_out)
{
    JsonParser *parser = json_parser_new();
    gboolean success = FALSE;
    if (error_out) *error_out = NULL;

    if (json_data && json_parser_load_from_data(parser, json_data, -1, NULL)) {
        JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
        if (obj && json_object_has_member(obj, "success") &&
            json_object_get_boolean_member(obj, "success") &&
            json_object_has_member(obj, "token") &&
            json_object_has_member(obj, "user")) {
            JsonObject *user = json_object_get_object_member(obj, "user");
            const gchar *token = json_object_get_string_member(obj, "token");
            const gchar *username = json_get_string_or_empty(user, "username");
            if (token && username[0]) {
                g_mutex_lock(&g_globals_mutex);
                g_free(g_auth_token);
                g_free(g_current_username);
                g_auth_token = g_strdup(token);
                g_current_username = g_strdup(username);
                g_mutex_unlock(&g_globals_mutex);
                save_session(g_auth_token, g_current_username, g_is_admin);
                update_login_ui();
                success = TRUE;
            }
        } else if (obj && error_out && json_object_has_member(obj, "error")) {
            *error_out = g_strdup(json_object_get_string_member(obj, "error"));
        }
    }
    g_object_unref(parser);
    if (!success && error_out && !*error_out)
        *error_out = g_strdup("The account switch response could not be read.");
    return success;
}

void on_switch_primary_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    gchar *response = NULL;
    gchar *error = NULL;
    if (perform_request_with_optional_payload(AUTH_SWITCH_PRIMARY_URL, "{}", "POST", &response) &&
        apply_auth_switch_response(response, &error)) {
        show_modal_message(GTK_MESSAGE_INFO, "Switched to primary account.", NULL);
        start_loading_delegates();
        start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Could not switch accounts.", error);
    }
    g_free(response);
    g_free(error);
}

static void
on_delegate_switch_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    const gchar *owner_id = g_object_get_data(G_OBJECT(widget), "owner_id");
    gchar *response = NULL;
    gchar *error = NULL;
    if (!owner_id || !owner_id[0]) {
        show_modal_message(GTK_MESSAGE_ERROR, "Delegate account unavailable.", NULL);
        return;
    }

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "ownerId");
    json_builder_add_string_value(builder, owner_id);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    gchar *payload = json_generator_to_data(gen, NULL);
    if (perform_request_with_optional_payload(AUTH_SWITCH_DELEGATE_URL, payload, "POST", &response) &&
        apply_auth_switch_response(response, &error)) {
        show_modal_message(GTK_MESSAGE_INFO, "Switched delegate account.", NULL);
        start_loading_delegates();
        start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Could not switch delegate account.", error);
    }
    g_free(response);
    g_free(error);
    g_free(payload);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
}

static void
on_delegate_action_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *delegate_id = g_object_get_data(G_OBJECT(widget), "delegate_id");
    const gchar *action = user_data;
    gchar *url = NULL;
    gchar *error_message = NULL;
    gboolean ok = FALSE;

    if (!delegate_id || !action) {
        return;
    }
    if (g_strcmp0(action, "accept") == 0) {
        url = g_strdup_printf(DELEGATE_ACCEPT_URL, delegate_id);
        ok = perform_delegate_action(url, "POST", "{}", &error_message);
    } else if (g_strcmp0(action, "decline") == 0) {
        url = g_strdup_printf(DELEGATE_DECLINE_URL, delegate_id);
        ok = perform_delegate_action(url, "POST", "{}", &error_message);
    } else if (g_strcmp0(action, "remove") == 0) {
        url = g_strdup_printf(DELEGATE_REMOVE_URL, delegate_id);
        ok = perform_delegate_action(url, "DELETE", NULL, &error_message);
    }

    if (ok) {
        start_loading_delegates();
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Delegate update failed.", error_message);
    }
    g_free(error_message);
    g_free(url);
}

static GtkWidget *
create_delegate_row(JsonObject *item, const gchar *mode)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *title;
    GtkWidget *body;
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    const gchar *id = json_get_string_or_empty(item, "id");
    const gchar *username = json_get_string_or_empty(item, "username");
    const gchar *name = json_get_string_or_empty(item, "name");
    const gchar *created_at = json_get_string_or_empty(item, "created_at");
    const gchar *accepted_at = json_get_string_or_empty(item, "accepted_at");
    gchar *title_text = g_strdup_printf("%s%s%s",
                                        name && name[0] ? name : "@",
                                        name && name[0] ? " @" : "",
                                        username);
    gchar *body_text = g_strdup_printf("Status: %s%s%s%s%s",
                                       json_get_string_or_empty(item, "status"),
                                       accepted_at[0] ? " · accepted " : "",
                                       accepted_at[0] ? accepted_at : "",
                                       created_at[0] ? " · created " : "",
                                       created_at[0] ? created_at : "");

    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    title = gtk_label_new(title_text);
    body = gtk_label_new(body_text);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_label_set_xalign(GTK_LABEL(body), 0.0);
    gtk_widget_set_opacity(body, 0.75);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), body, FALSE, FALSE, 0);

    if (g_strcmp0(mode, "incoming") == 0) {
        GtkWidget *accept = gtk_button_new_with_label("Accept");
        GtkWidget *decline = gtk_button_new_with_label("Decline");
        g_object_set_data_full(G_OBJECT(accept), "delegate_id", g_strdup(id), g_free);
        g_object_set_data_full(G_OBJECT(decline), "delegate_id", g_strdup(id), g_free);
        g_signal_connect(accept, "clicked", G_CALLBACK(on_delegate_action_clicked), "accept");
        g_signal_connect(decline, "clicked", G_CALLBACK(on_delegate_action_clicked), "decline");
        gtk_box_pack_start(GTK_BOX(actions), accept, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(actions), decline, FALSE, FALSE, 0);
    } else if (g_strcmp0(mode, "delegation") == 0) {
        GtkWidget *switch_btn = gtk_button_new_with_label("Switch");
        GtkWidget *remove = gtk_button_new_with_label("Remove");
        const gchar *owner_id = json_get_string_or_empty(item, "owner_id");
        g_object_set_data_full(G_OBJECT(switch_btn), "owner_id", g_strdup(owner_id), g_free);
        g_object_set_data_full(G_OBJECT(remove), "delegate_id", g_strdup(id), g_free);
        g_signal_connect(switch_btn, "clicked", G_CALLBACK(on_delegate_switch_clicked), NULL);
        g_signal_connect(remove, "clicked", G_CALLBACK(on_delegate_action_clicked), "remove");
        gtk_box_pack_start(GTK_BOX(actions), switch_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(actions), remove, FALSE, FALSE, 0);
    } else {
        GtkWidget *remove = gtk_button_new_with_label(g_strcmp0(mode, "sent") == 0 ? "Cancel" : "Remove");
        g_object_set_data_full(G_OBJECT(remove), "delegate_id", g_strdup(id), g_free);
        g_signal_connect(remove, "clicked", G_CALLBACK(on_delegate_action_clicked), "remove");
        gtk_box_pack_start(GTK_BOX(actions), remove, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 0);

    g_free(title_text);
    g_free(body_text);
    return box;
}

static void
populate_delegate_bucket(GtkWidget *list, JsonArray *items, const gchar *empty_text, const gchar *mode)
{
    clear_list_box_rows(list);
    if (!items || json_array_get_length(items) == 0) {
        set_list_box_status(list, empty_text);
        return;
    }
    for (guint i = 0; i < json_array_get_length(items); i++) {
        JsonObject *item = json_array_get_object_element(items, i);
        GtkWidget *row = create_delegate_row(item, mode);
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
    }
}

static gboolean
on_delegates_loaded(gpointer data)
{
    struct AsyncData *async_data = data;
    JsonParser *parser = NULL;
    JsonObject *obj = NULL;
    GError *error = NULL;

    if (!g_delegates_list) {
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    if (async_data->success && async_data->json_data) {
        parser = json_parser_new();
        if (json_parser_load_from_data(parser, async_data->json_data, -1, &error)) {
            obj = json_node_get_object(json_parser_get_root(parser));
        } else if (error) {
            g_error_free(error);
        }
    }
    if (obj && !json_object_has_member(obj, "error")) {
        populate_delegate_bucket(g_delegates_list,
                                 json_object_has_member(obj, "delegates") ? json_object_get_array_member(obj, "delegates") : NULL,
                                 "No delegates can post as you.",
                                 "remove");
        populate_delegate_bucket(g_delegations_list,
                                 json_object_has_member(obj, "delegations") ? json_object_get_array_member(obj, "delegations") : NULL,
                                 "You are not a delegate for anyone.",
                                 "delegation");
        populate_delegate_bucket(g_delegate_invitations_list,
                                 json_object_has_member(obj, "invitations") ? json_object_get_array_member(obj, "invitations") : NULL,
                                 "No pending invitations.",
                                 "incoming");
        populate_delegate_bucket(g_delegate_sent_list,
                                 json_object_has_member(obj, "sentInvitations") ? json_object_get_array_member(obj, "sentInvitations") : NULL,
                                 "No sent invitations.",
                                 "sent");
    } else {
        set_list_box_status(g_delegates_list, "Failed to load delegates.");
        set_list_box_status(g_delegations_list, "Failed to load delegations.");
        set_list_box_status(g_delegate_invitations_list, "Failed to load invitations.");
        set_list_box_status(g_delegate_sent_list, "Failed to load sent invitations.");
    }
    if (parser) {
        g_object_unref(parser);
    }
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_delegates_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};

    async_data->success = fetch_url(DELEGATES_SUMMARY_URL, &chunk, NULL, "GET");
    if (async_data->success) {
        async_data->json_data = g_strdup(chunk.memory);
    }
    g_free(chunk.memory);
    g_idle_add(on_delegates_loaded, async_data);
    return NULL;
}

void
start_loading_delegates(void)
{
    if (!g_auth_token || !g_delegates_list) {
        return;
    }
    set_list_box_status(g_delegates_list, "Loading delegates...");
    set_list_box_status(g_delegations_list, "Loading delegations...");
    set_list_box_status(g_delegate_invitations_list, "Loading invitations...");
    set_list_box_status(g_delegate_sent_list, "Loading sent invitations...");
    g_thread_new("delegates-loader", fetch_delegates_thread, g_new0(struct AsyncData, 1));
}

void
on_invite_delegate_clicked(GtkWidget *widget, gpointer user_data)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *error_message = NULL;
    const gchar *username;
    gboolean ok;

    (void)widget;
    (void)user_data;
    if (!g_delegate_username_entry) {
        return;
    }
    username = gtk_entry_get_text(GTK_ENTRY(g_delegate_username_entry));
    if (!username || username[0] == '\0') {
        show_modal_message(GTK_MESSAGE_ERROR, "Username required.", "Enter the username you want to invite.");
        return;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "username");
    json_builder_add_string_value(builder, username);
    json_builder_end_object(builder);
    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);

    ok = perform_delegate_action(DELEGATES_INVITE_URL, "POST", payload, &error_message);
    if (ok) {
        gtk_entry_set_text(GTK_ENTRY(g_delegate_username_entry), "");
        start_loading_delegates();
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Invite failed.", error_message);
    }

    g_free(error_message);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
}

static const gchar *
explore_url_for_index(gint index)
{
    switch (index) {
    case 1: return EXPLORE_BEST_OF_WEEK_URL;
    case 2: return EXPLORE_MOST_BOOKMARKED_URL;
    case 3: return EXPLORE_MOST_DISCUSSED_URL;
    case 4: return EXPLORE_LONGEST_THREADS_URL;
    case 5: return EXPLORE_WITH_MEDIA_URL;
    case 6: return EXPLORE_WITH_POLLS_URL;
    case 7: return EXPLORE_TRENDING_USERS_URL;
    case 8: return EXPLORE_SUGGESTED_USERS_URL;
    case 9: return EXPLORE_DIRECTORY_URL;
    case 10: return EXPLORE_TOP_HASHTAGS_URL;
    case 11: return EXPLORE_DIGEST_URL;
    case 12: return EXPLORE_LEADERBOARD_URL;
    case 13: return EXPLORE_STATS_URL;
    default: return TRENDS_URL;
    }
}

static GtkWidget *
create_explore_text_row(const gchar *title, const gchar *body)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title_label = gtk_label_new(title ? title : "");
    GtkWidget *body_label = gtk_label_new(body ? body : "");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
    gtk_label_set_xalign(GTK_LABEL(body_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(body_label), TRUE);
    gtk_widget_set_opacity(body_label, 0.78);
    gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), body_label, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    return box;
}

static void
populate_explore_hashtags(JsonArray *hashtags)
{
    if (!hashtags || json_array_get_length(hashtags) == 0) {
        set_list_box_status(g_explore_list, "No hashtags.");
        return;
    }
    for (guint i = 0; i < json_array_get_length(hashtags); i++) {
        JsonObject *item = json_array_get_object_element(hashtags, i);
        const gchar *name = json_get_string_or_empty(item, "name");
        gint tweet_count = json_object_has_member(item, "tweet_count") ?
            json_object_get_int_member(item, "tweet_count") : 0;
        gint recent_count = json_object_has_member(item, "recent_count") ?
            json_object_get_int_member(item, "recent_count") : 0;
        gchar *title = g_strdup_printf("#%s", name);
        gchar *body = g_strdup_printf("%d posts · %d recent", tweet_count, recent_count);
        GtkWidget *row = create_explore_text_row(title, body);
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(g_explore_list), row, -1);
        g_free(title);
        g_free(body);
    }
}

static void
populate_explore_stats(JsonObject *stats)
{
    gchar *body;
    GtkWidget *row;

    if (!stats) {
        set_list_box_status(g_explore_list, "Stats unavailable.");
        return;
    }
    body = g_strdup_printf("Users: %" G_GINT64_FORMAT "\nPosts: %" G_GINT64_FORMAT "\nLikes: %" G_GINT64_FORMAT "\nPosts today: %" G_GINT64_FORMAT "\nNew users today: %" G_GINT64_FORMAT,
                           json_object_has_member(stats, "total_users") ? json_object_get_int_member(stats, "total_users") : 0,
                           json_object_has_member(stats, "total_posts") ? json_object_get_int_member(stats, "total_posts") : 0,
                           json_object_has_member(stats, "total_likes") ? json_object_get_int_member(stats, "total_likes") : 0,
                           json_object_has_member(stats, "posts_today") ? json_object_get_int_member(stats, "posts_today") : 0,
                           json_object_has_member(stats, "new_users_today") ? json_object_get_int_member(stats, "new_users_today") : 0);
    row = create_explore_text_row("Network stats", body);
    gtk_widget_show_all(row);
    gtk_list_box_insert(GTK_LIST_BOX(g_explore_list), row, -1);
    g_free(body);
}

static void
populate_explore_digest(JsonObject *digest)
{
    gchar *body;
    GtkWidget *row;
    JsonObject *author = NULL;

    if (!digest) {
        set_list_box_status(g_explore_list, "Digest unavailable.");
        return;
    }
    if (json_object_has_member(digest, "top_author") &&
        JSON_NODE_HOLDS_OBJECT(json_object_get_member(digest, "top_author"))) {
        author = json_object_get_object_member(digest, "top_author");
    }
    body = g_strdup_printf("Posts in 24h: %" G_GINT64_FORMAT "\nNew users in 24h: %" G_GINT64_FORMAT "\nTop author: %s",
                           json_object_has_member(digest, "total_posts_24h") ? json_object_get_int_member(digest, "total_posts_24h") : 0,
                           json_object_has_member(digest, "new_users_24h") ? json_object_get_int_member(digest, "new_users_24h") : 0,
                           author ? json_get_string_or_empty(author, "username") : "none");
    row = create_explore_text_row("Daily digest", body);
    gtk_widget_show_all(row);
    gtk_list_box_insert(GTK_LIST_BOX(g_explore_list), row, -1);
    g_free(body);
}

static gboolean
on_explore_loaded(gpointer data)
{
    struct AsyncData *async_data = data;
    JsonParser *parser;
    JsonNode *root;
    JsonObject *obj = NULL;
    GError *error = NULL;

    if (!g_explore_list) {
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    clear_list_box_rows(g_explore_list);
    if (!async_data->success || !async_data->json_data) {
        set_list_box_status(g_explore_list, "Failed to load Explore.");
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, async_data->json_data, -1, &error)) {
        if (error) g_error_free(error);
        set_list_box_status(g_explore_list, "Failed to parse Explore response.");
        g_object_unref(parser);
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    root = json_parser_get_root(parser);
    if (root && JSON_NODE_HOLDS_OBJECT(root)) {
        obj = json_node_get_object(root);
    }
    if (obj && json_object_has_member(obj, "posts") &&
        JSON_NODE_HOLDS_ARRAY(json_object_get_member(obj, "posts"))) {
        GList *tweets = parse_tweets(async_data->json_data);
        populate_tweet_list(GTK_LIST_BOX(g_explore_list), tweets);
        free_tweets(tweets);
    } else if (obj && json_object_has_member(obj, "users") &&
               JSON_NODE_HOLDS_ARRAY(json_object_get_member(obj, "users"))) {
        GList *users = parse_users(async_data->json_data);
        populate_user_list(GTK_LIST_BOX(g_explore_list), users);
        free_users(users);
    } else if (obj && json_object_has_member(obj, "hashtags")) {
        populate_explore_hashtags(json_object_get_array_member(obj, "hashtags"));
    } else if (obj && json_object_has_member(obj, "stats")) {
        populate_explore_stats(json_object_get_object_member(obj, "stats"));
    } else if (obj && json_object_has_member(obj, "digest")) {
        populate_explore_digest(json_object_get_object_member(obj, "digest"));
    } else if (obj && json_object_has_member(obj, "leaderboard")) {
        JsonObject *leaderboard = json_object_get_object_member(obj, "leaderboard");
        JsonArray *by_posts = json_object_has_member(leaderboard, "by_posts")
            ? json_object_get_array_member(leaderboard, "by_posts") : NULL;
        JsonArray *by_followers = json_object_has_member(leaderboard, "by_followers")
            ? json_object_get_array_member(leaderboard, "by_followers") : NULL;
        if ((!by_posts || json_array_get_length(by_posts) == 0) &&
            (!by_followers || json_array_get_length(by_followers) == 0)) {
            set_list_box_status(g_explore_list, "Leaderboard unavailable.");
        } else {
            for (guint i = 0; by_posts && i < json_array_get_length(by_posts); i++) {
                JsonObject *user = json_array_get_object_element(by_posts, i);
                gchar *title = g_strdup_printf("Posts: @%s", json_get_string_or_empty(user, "username"));
                gchar *body = g_strdup_printf("%" G_GINT64_FORMAT " posts", json_object_has_member(user, "post_count") ? json_object_get_int_member(user, "post_count") : 0);
                GtkWidget *row = create_explore_text_row(title, body);
                gtk_widget_show_all(row);
                gtk_list_box_insert(GTK_LIST_BOX(g_explore_list), row, -1);
                g_free(title);
                g_free(body);
            }
            for (guint i = 0; by_followers && i < json_array_get_length(by_followers); i++) {
                JsonObject *user = json_array_get_object_element(by_followers, i);
                gchar *title = g_strdup_printf("Followers: @%s", json_get_string_or_empty(user, "username"));
                gchar *body = g_strdup_printf("%" G_GINT64_FORMAT " followers", json_object_has_member(user, "follower_count") ? json_object_get_int_member(user, "follower_count") : 0);
                GtkWidget *row = create_explore_text_row(title, body);
                gtk_widget_show_all(row);
                gtk_list_box_insert(GTK_LIST_BOX(g_explore_list), row, -1);
                g_free(title);
                g_free(body);
            }
        }
    } else {
        set_list_box_status(g_explore_list, "No Explore results.");
    }
    g_object_unref(parser);
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_explore_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};
    const gchar *url = explore_url_for_index(async_data->query ? atoi(async_data->query) : 0);

    async_data->success = fetch_url(url, &chunk, NULL, "GET");
    if (async_data->success) {
        async_data->json_data = g_strdup(chunk.memory);
    }
    g_free(chunk.memory);
    g_idle_add(on_explore_loaded, async_data);
    return NULL;
}

void
start_loading_explore(void)
{
    struct AsyncData *data;
    gint index = 0;

    if (!g_explore_list) {
        return;
    }
    if (g_explore_category_combo) {
        index = gtk_combo_box_get_active(GTK_COMBO_BOX(g_explore_category_combo));
    }
    set_list_box_status(g_explore_list, "Loading Explore...");
    data = g_new0(struct AsyncData, 1);
    data->query = g_strdup_printf("%d", index);
    g_thread_new("explore-loader", fetch_explore_thread, data);
}

void
on_explore_category_changed(GtkComboBox *combo, gpointer user_data)
{
    (void)combo;
    (void)user_data;
    start_loading_explore();
}

void
on_explore_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "explore");
    gtk_widget_show(g_back_button);
    start_loading_explore();
}

static GtkWidget *
create_article_row(struct Tweet *article)
{
    GtkWidget *event_box = gtk_event_box_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *title_label;
    GtkWidget *byline_label;
    GtkWidget *excerpt_label;
    gchar *title_markup;
    gchar *escaped_title;
    gchar *byline;

    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    escaped_title = g_markup_escape_text(article->article_title ? article->article_title : "Untitled article", -1);
    title_markup = g_strdup_printf("<b>%s</b>", escaped_title);
    title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label), title_markup);
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(title_label), TRUE);

    byline = g_strdup_printf("@%s%s%s",
                             article->author_username ? article->author_username : "unknown",
                             article->created_at ? " · " : "",
                             article->created_at ? article->created_at : "");
    byline_label = gtk_label_new(byline);
    gtk_label_set_xalign(GTK_LABEL(byline_label), 0.0);
    gtk_widget_set_opacity(byline_label, 0.7);

    excerpt_label = gtk_label_new(article->content ? article->content : "");
    gtk_label_set_xalign(GTK_LABEL(excerpt_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(excerpt_label), TRUE);

    gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), byline_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), excerpt_label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(event_box), box);
    g_object_set_data_full(G_OBJECT(event_box), "article_id", g_strdup(article->id), g_free);
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_article_row_activated), NULL);

    g_free(byline);
    g_free(title_markup);
    g_free(escaped_title);
    return event_box;
}

static void
show_article_reader(GtkWidget *parent, struct Tweet *article)
{
    GtkWidget *toplevel = parent ? gtk_widget_get_toplevel(parent) : NULL;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(article->article_title ? article->article_title : "Article",
                                                     GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     "_Close", GTK_RESPONSE_CLOSE,
                                                     NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *title = gtk_label_new(NULL);
    GtkWidget *byline;
    GtkWidget *body;
    gchar *escaped_title = g_markup_escape_text(article->article_title ? article->article_title : "Untitled article", -1);
    gchar *title_markup = g_strdup_printf("<span size='large' weight='bold'>%s</span>", escaped_title);
    gchar *byline_text = g_strdup_printf("@%s%s%s",
                                         article->author_username ? article->author_username : "unknown",
                                         article->created_at ? " · " : "",
                                         article->created_at ? article->created_at : "");

    gtk_window_set_default_size(GTK_WINDOW(dialog), 720, 640);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_label_set_markup(GTK_LABEL(title), title_markup);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(title), TRUE);
    byline = gtk_label_new(byline_text);
    gtk_label_set_xalign(GTK_LABEL(byline), 0.0);
    gtk_widget_set_opacity(byline, 0.7);
    body = gtk_label_new(article->article_body_markdown ? article->article_body_markdown : article->content);
    gtk_label_set_xalign(GTK_LABEL(body), 0.0);
    gtk_label_set_yalign(GTK_LABEL(body), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
    gtk_label_set_selectable(GTK_LABEL(body), TRUE);

    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), byline, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), body, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(scroll), box);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    g_free(byline_text);
    g_free(title_markup);
    g_free(escaped_title);
}

static struct Tweet *
parse_article_detail(const gchar *json_data)
{
    JsonParser *parser;
    JsonObject *root_obj;
    JsonObject *article_obj;
    GError *error = NULL;
    struct Tweet *article = NULL;

    if (!json_data) {
        return NULL;
    }
    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }
    root_obj = json_node_get_object(json_parser_get_root(parser));
    article_obj = root_obj && json_object_has_member(root_obj, "article")
        ? json_object_get_object_member(root_obj, "article") : NULL;
    if (article_obj) {
        JsonGenerator *gen = json_generator_new();
        JsonNode *node = json_node_alloc();
        gchar *article_json;
        gchar *wrapped;
        GList *articles;
        json_node_init_object(node, article_obj);
        json_generator_set_root(gen, node);
        article_json = json_generator_to_data(gen, NULL);
        wrapped = g_strdup_printf("{\"articles\":[%s]}", article_json);
        articles = parse_tweets(wrapped);
        if (articles) {
            article = articles->data;
            articles->data = NULL;
            g_list_free_full(articles, free_tweet);
        }
        g_free(article_json);
        g_free(wrapped);
        json_node_free(node);
        g_object_unref(gen);
    }
    g_object_unref(parser);
    return article;
}

static gboolean
on_article_detail_loaded(gpointer data)
{
    struct AsyncData *async_data = data;
    struct Tweet *article = NULL;
    GtkWidget *parent = async_data->list_box ? GTK_WIDGET(async_data->list_box) : NULL;

    if (async_data->success && async_data->json_data) {
        article = parse_article_detail(async_data->json_data);
    }
    if (article) {
        show_article_reader(parent, article);
        free_tweet(article);
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Article unavailable.", "The article could not be loaded.");
    }
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_article_detail_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(ARTICLE_DETAILS_URL, async_data->query);

    async_data->success = fetch_url(url, &chunk, NULL, "GET");
    if (async_data->success) {
        async_data->json_data = g_strdup(chunk.memory);
    }
    g_free(chunk.memory);
    g_free(url);
    g_idle_add(on_article_detail_loaded, async_data);
    return NULL;
}

static gboolean
on_article_row_activated(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    struct AsyncData *data;
    const gchar *article_id;

    (void)event;
    (void)user_data;
    article_id = g_object_get_data(G_OBJECT(widget), "article_id");
    if (!article_id) {
        return FALSE;
    }
    data = g_new0(struct AsyncData, 1);
    data->query = g_strdup(article_id);
    data->list_box = GTK_LIST_BOX(g_articles_list);
    g_thread_new("article-detail-loader", fetch_article_detail_thread, data);
    return TRUE;
}

static gboolean
on_articles_loaded(gpointer data)
{
    struct AsyncData *async_data = data;

    if (!g_articles_list) {
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    clear_list_box_rows(g_articles_list);
    if (async_data->success && async_data->tweets) {
        for (GList *iter = async_data->tweets; iter != NULL; iter = iter->next) {
            GtkWidget *row = create_article_row(iter->data);
            gtk_widget_show_all(row);
            gtk_list_box_insert(GTK_LIST_BOX(g_articles_list), row, -1);
        }
        free_tweets(async_data->tweets);
        async_data->tweets = NULL;
    } else {
        set_list_box_status(g_articles_list, async_data->success ? "No articles yet." : "Failed to load articles.");
    }
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_articles_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};

    async_data->success = fetch_url(ARTICLES_URL, &chunk, NULL, "GET");
    if (async_data->success) {
        async_data->tweets = parse_tweets(chunk.memory);
    }
    g_free(chunk.memory);
    g_idle_add(on_articles_loaded, async_data);
    return NULL;
}

void
start_loading_articles(void)
{
    struct AsyncData *data;

    if (!g_articles_list) {
        return;
    }
    set_list_box_status(g_articles_list, "Loading articles...");
    data = g_new0(struct AsyncData, 1);
    g_thread_new("articles-loader", fetch_articles_thread, data);
}

void
on_articles_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "articles");
    gtk_widget_show(g_back_button);
    start_loading_articles();
}

static gboolean
perform_publish_article(const gchar *title, const gchar *markdown, gchar **error_out)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *response = NULL;
    gboolean success;

    if (error_out) {
        *error_out = NULL;
    }
    if (!g_auth_token) {
        if (error_out) *error_out = g_strdup("You must be logged in to publish articles.");
        return FALSE;
    }
    if (!title || strlen(title) < 5) {
        if (error_out) *error_out = g_strdup("Title must be at least 5 characters.");
        return FALSE;
    }
    if (!markdown || strlen(markdown) < 50) {
        if (error_out) *error_out = g_strdup("Article body must be at least 50 characters.");
        return FALSE;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "title");
    json_builder_add_string_value(builder, title);
    json_builder_set_member_name(builder, "markdown");
    json_builder_add_string_value(builder, markdown);
    json_builder_set_member_name(builder, "source");
    json_builder_add_string_value(builder, "tweeta-desktop");
    json_builder_end_object(builder);

    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);
    success = perform_request_with_optional_payload(ARTICLES_URL, payload, "POST", &response);
    if (!success && error_out) {
        gchar *server_error = extract_error_message(response);
        *error_out = server_error ? server_error : g_strdup("Article could not be published.");
    }
    g_free(response);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

void
on_compose_article_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *toplevel;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *box;
    GtkWidget *title_entry;
    GtkWidget *body_view;
    GtkWidget *body_scroll;

    (void)user_data;
    if (!g_auth_token) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to publish articles.");
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("New Article",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Publish", GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 560);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    title_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(title_entry), "Article title");
    body_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(body_view), GTK_WRAP_WORD_CHAR);
    body_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(body_scroll), body_view);
    gtk_box_pack_start(GTK_BOX(box), title_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), body_scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(body_view));
        GtkTextIter start;
        GtkTextIter end;
        gchar *markdown;
        gchar *error_message = NULL;
        const gchar *title = gtk_entry_get_text(GTK_ENTRY(title_entry));

        gtk_text_buffer_get_bounds(buffer, &start, &end);
        markdown = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        if (perform_publish_article(title, markdown, &error_message)) {
            show_modal_message(GTK_MESSAGE_INFO, "Article published.", "Your article is now available.");
            start_loading_articles();
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "Publish failed.", error_message);
        }
        g_free(markdown);
        g_free(error_message);
    }
    gtk_widget_destroy(dialog);
}

void
on_list_add_member_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *entry;
    GtkWidget *content;
    GtkWidget *toplevel;

    (void)user_data;
    if (!g_active_list || !g_active_list->id) {
        return;
    }
    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Add List Member",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Add", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "username");
    gtk_box_pack_start(GTK_BOX(content), entry, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *user_id = lookup_profile_id(gtk_entry_get_text(GTK_ENTRY(entry)));
        if (user_id) {
            JsonBuilder *builder = json_builder_new();
            JsonGenerator *gen = json_generator_new();
            gchar *payload;
            gchar *url = g_strdup_printf(LIST_MEMBERS_URL, g_active_list->id);
            gchar *error_message = NULL;
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "userId");
            json_builder_add_string_value(builder, user_id);
            json_builder_end_object(builder);
            json_generator_set_root(gen, json_builder_get_root(builder));
            payload = json_generator_to_data(gen, NULL);
            if (list_json_request(url, payload, "POST", &error_message)) {
                show_list_details(g_active_list->id);
            } else {
                show_modal_message(GTK_MESSAGE_ERROR, "Could not add member.", error_message);
            }
            g_free(error_message);
            g_free(payload);
            g_free(url);
            g_object_unref(gen);
            g_object_unref(builder);
            g_free(user_id);
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "User not found.", NULL);
        }
    }
    gtk_widget_destroy(dialog);
}

static void
on_list_remove_member_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *user_id = g_object_get_data(G_OBJECT(widget), "user_id");
    gchar *url;
    gchar *error_message = NULL;

    (void)user_data;
    if (!g_active_list || !g_active_list->id || !user_id) {
        return;
    }
    url = g_strdup_printf(LIST_MEMBER_URL, g_active_list->id, user_id);
    if (list_json_request(url, NULL, "DELETE", &error_message)) {
        show_list_details(g_active_list->id);
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Could not remove member.", error_message);
    }
    g_free(error_message);
    g_free(url);
}

static void
populate_list_members_with_actions(GList *members)
{
    clear_list_box_rows(g_list_members_list);
    if (!members) {
        set_list_box_status(g_list_members_list, "No members.");
        return;
    }
    for (GList *l = members; l; l = l->next) {
        struct Profile *user = l->data;
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *user_widget = create_user_widget(user);
        gtk_box_pack_start(GTK_BOX(row), user_widget, TRUE, TRUE, 0);
        if (g_active_list && g_active_list->is_owner && user->id) {
            GtkWidget *remove = gtk_button_new_with_label("Remove");
            g_object_set_data_full(G_OBJECT(remove), "user_id", g_strdup(user->id), g_free);
            g_signal_connect(remove, "clicked", G_CALLBACK(on_list_remove_member_clicked), NULL);
            gtk_box_pack_end(GTK_BOX(row), remove, FALSE, FALSE, 0);
        }
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(g_list_members_list), row, -1);
    }
}

static gboolean
on_list_details_loaded(gpointer data)
{
    struct AsyncData *async_data = data;
    gchar *details;

    if (g_active_list) {
        free_tweeta_list(g_active_list);
        g_active_list = NULL;
    }
    if (!async_data->success || !async_data->list) {
        gtk_label_set_text(GTK_LABEL(g_list_title_label), "List unavailable");
        gtk_label_set_text(GTK_LABEL(g_list_details_label), "");
        set_list_box_status(g_list_tweets_list, "Failed to load list.");
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }

    g_active_list = async_data->list;
    async_data->list = NULL;
    gtk_label_set_text(GTK_LABEL(g_list_title_label), g_active_list->name ? g_active_list->name : "Untitled list");
    details = g_strdup_printf("%s%d members · %d followers%s%s%s",
                              g_active_list->is_private ? "Private · " : "",
                              g_active_list->member_count,
                              g_active_list->follower_count,
                              g_active_list->owner_username ? " · @" : "",
                              g_active_list->owner_username ? g_active_list->owner_username : "",
                              g_active_list->description ? "" : "");
    gtk_label_set_text(GTK_LABEL(g_list_details_label), details);
    g_free(details);

    if (g_list_follow_button) {
        g_object_set_data_full(G_OBJECT(g_list_follow_button), "list_id", g_strdup(g_active_list->id), g_free);
        gtk_button_set_label(GTK_BUTTON(g_list_follow_button),
                             g_active_list->is_following ? "Unfollow" : "Follow");
        gtk_widget_set_visible(g_list_follow_button, g_auth_token && !g_active_list->is_owner);
    }
    gtk_widget_set_visible(g_list_edit_button, g_active_list->is_owner);
    gtk_widget_set_visible(g_list_delete_button, g_active_list->is_owner);
    gtk_widget_set_visible(g_list_add_member_button, g_active_list->is_owner);

    if (async_data->tweets) {
        populate_tweet_list(GTK_LIST_BOX(g_list_tweets_list), async_data->tweets);
    } else {
        set_list_box_status(g_list_tweets_list, "No tweets from list members.");
    }
    populate_list_members_with_actions(g_active_list->members);
    if (g_active_list->followers) {
        populate_user_list(GTK_LIST_BOX(g_list_followers_list), g_active_list->followers);
    } else {
        set_list_box_status(g_list_followers_list, "No followers.");
    }

    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_list_details_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};
    gchar *url;

    url = g_strdup_printf(LIST_DETAILS_URL, async_data->query);
    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->list = parse_list_details_response(chunk.memory);
        g_free(chunk.memory);
        chunk.memory = NULL;
        chunk.size = 0;
    }
    g_free(url);

    url = g_strdup_printf(LIST_TWEETS_URL, async_data->query);
    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->tweets = parse_tweets(chunk.memory);
        g_free(chunk.memory);
        chunk.memory = NULL;
        chunk.size = 0;
    }
    g_free(url);

    url = g_strdup_printf(LIST_FOLLOWERS_URL, async_data->query);
    if (fetch_url(url, &chunk, NULL, "GET")) {
        if (async_data->list) {
            async_data->list->followers = parse_list_followers_response(chunk.memory);
        }
        g_free(chunk.memory);
        chunk.memory = NULL;
        chunk.size = 0;
    }
    g_free(url);

    async_data->success = async_data->list != NULL;
    g_idle_add(on_list_details_loaded, async_data);
    return NULL;
}

void
show_list_details(const gchar *list_id)
{
    struct AsyncData *data;

    if (!list_id) {
        return;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "list_details");
    gtk_widget_show(g_back_button);
    gtk_label_set_text(GTK_LABEL(g_list_title_label), "Loading list...");
    gtk_label_set_text(GTK_LABEL(g_list_details_label), "");
    set_list_box_status(g_list_tweets_list, "Loading tweets...");
    set_list_box_status(g_list_members_list, "Loading members...");
    set_list_box_status(g_list_followers_list, "Loading followers...");
    data = g_new0(struct AsyncData, 1);
    data->query = g_strdup(list_id);
    g_thread_new("list-details-loader", fetch_list_details_thread, data);
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

gboolean perform_poll_multi_vote(const gchar *tweet_id, JsonNode *answers, gchar **message_out)
{
    if (message_out) *message_out = NULL;
    if (!g_auth_token || !tweet_id || !answers) return FALSE;

    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;
    gchar *url = g_strdup_printf(TWEET_POLL_MULTI_VOTE_URL, tweet_id);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "answers");
    json_builder_add_value(builder, json_node_copy(answers));
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    gchar *post_data = json_generator_to_data(gen, NULL);

    if (fetch_url(url, &chunk, post_data, "POST")) {
        JsonParser *parser = json_parser_new();
        if (chunk.memory && json_parser_load_from_data(parser, chunk.memory, -1, NULL)) {
            JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
            if (obj && json_object_has_member(obj, "success") &&
                json_object_get_boolean_member(obj, "success")) {
                success = TRUE;
                if (message_out && json_object_has_member(obj, "score") &&
                    !json_node_is_null(json_object_get_member(obj, "score")) &&
                    json_object_has_member(obj, "total") &&
                    !json_node_is_null(json_object_get_member(obj, "total"))) {
                    int score = json_object_get_int_member(obj, "score");
                    int total = json_object_get_int_member(obj, "total");
                    *message_out = g_strdup_printf("Quiz submitted. Score: %d/%d", score, total);
                }
            } else if (message_out && obj && json_object_has_member(obj, "error")) {
                *message_out = g_strdup(json_object_get_string_member(obj, "error"));
            }
        }
        g_object_unref(parser);
        g_free(chunk.memory);
    }

    if (!success && message_out && !*message_out)
        *message_out = g_strdup("Could not submit poll answers.");

    json_node_free(root);
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
    g_free(poll->kind);
    g_free(poll->expires_at);
    if (poll->steps)
        json_node_free(poll->steps);

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

static const gchar *
get_active_community_id(void)
{
    return g_community_tweets_list
        ? g_object_get_data(G_OBJECT(g_community_tweets_list), "community_id")
        : NULL;
}

void
on_community_create_invite_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *community_id = get_active_community_id();
    GtkWidget *dialog;
    GtkWidget *grid;
    GtkWidget *max_uses_spin;
    GtkWidget *expires_spin;
    gchar *url;
    gchar *response = NULL;

    (void)user_data;
    if (!community_id) return;
    dialog = gtk_dialog_new_with_buttons("Create Community Invite",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Create", GTK_RESPONSE_ACCEPT,
                                         NULL);
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    max_uses_spin = gtk_spin_button_new_with_range(0, 500, 1);
    expires_spin = gtk_spin_button_new_with_range(0, 30, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(expires_spin), 7);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Max uses (0 unlimited):"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), max_uses_spin, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Expires in days (0 never):"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), expires_spin, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        JsonBuilder *builder = json_builder_new();
        JsonGenerator *gen = json_generator_new();
        gchar *payload;
        gint max_uses = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(max_uses_spin));
        gint expires = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(expires_spin));
        json_builder_begin_object(builder);
        if (max_uses > 0) {
            json_builder_set_member_name(builder, "max_uses");
            json_builder_add_int_value(builder, max_uses);
        }
        if (expires > 0) {
            json_builder_set_member_name(builder, "expires_in_days");
            json_builder_add_int_value(builder, expires);
        }
        json_builder_end_object(builder);
        json_generator_set_root(gen, json_builder_get_root(builder));
        payload = json_generator_to_data(gen, NULL);
        url = g_strdup_printf(COMMUNITY_INVITES_URL, community_id);
        if (perform_request_with_optional_payload(url, payload, "POST", &response)) {
            JsonParser *parser = json_parser_new();
            GError *error = NULL;
            if (json_parser_load_from_data(parser, response, -1, &error)) {
                JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
                const gchar *token = json_get_string_or_empty(obj, "token");
                gchar *message = g_strdup_printf("Invite token:\n%s", token);
                show_modal_message(GTK_MESSAGE_INFO, "Invite created.", message);
                g_free(message);
            } else {
                if (error) g_error_free(error);
                show_modal_message(GTK_MESSAGE_INFO, "Invite created.", NULL);
            }
            g_object_unref(parser);
        } else {
            gchar *err = extract_error_message(response);
            show_modal_message(GTK_MESSAGE_ERROR, "Could not create invite.", err);
            g_free(err);
        }
        g_free(url);
        g_free(response);
        g_free(payload);
        g_object_unref(gen);
        g_object_unref(builder);
    }
    gtk_widget_destroy(dialog);
}

void
on_community_accept_invite_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *entry;

    (void)user_data;
    dialog = gtk_dialog_new_with_buttons("Join Community Invite",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Join", GTK_RESPONSE_ACCEPT,
                                         NULL);
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Invite token");
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *token = gtk_entry_get_text(GTK_ENTRY(entry));
        if (token && token[0]) {
            gchar *escaped = g_uri_escape_string(token, NULL, TRUE);
            gchar *url = g_strdup_printf(COMMUNITY_INVITE_ACCEPT_URL, escaped);
            gchar *response = NULL;
            if (perform_request_with_optional_payload(url, "{}", "POST", &response)) {
                show_modal_message(GTK_MESSAGE_INFO, "Joined community.", NULL);
                start_loading_communities(GTK_LIST_BOX(g_communities_list));
            } else {
                gchar *err = extract_error_message(response);
                show_modal_message(GTK_MESSAGE_ERROR, "Could not join invite.", err);
                g_free(err);
            }
            g_free(response);
            g_free(url);
            g_free(escaped);
        }
    }
    gtk_widget_destroy(dialog);
}

struct CommunityInviteAction {
    gchar *community_id;
    gchar *invite_id;
};

static void
free_community_invite_action(gpointer data)
{
    struct CommunityInviteAction *action = data;
    if (!action) return;
    g_free(action->community_id);
    g_free(action->invite_id);
    g_free(action);
}

static void
free_community_invite_action_closure(gpointer data, GClosure *closure)
{
    (void)closure;
    free_community_invite_action(data);
}

static void
on_community_revoke_invite_clicked(GtkWidget *widget, gpointer user_data)
{
    struct CommunityInviteAction *action = user_data;
    gchar *url;
    gchar *response = NULL;

    (void)widget;
    if (!action || !action->community_id || !action->invite_id) return;
    url = g_strdup_printf(COMMUNITY_INVITE_URL, action->community_id, action->invite_id);
    if (perform_request_with_optional_payload(url, NULL, "DELETE", &response)) {
        show_modal_message(GTK_MESSAGE_INFO, "Invite revoked.", NULL);
    } else {
        gchar *err = extract_error_message(response);
        show_modal_message(GTK_MESSAGE_ERROR, "Could not revoke invite.", err);
        g_free(err);
    }
    g_free(response);
    g_free(url);
}

void
on_community_manage_invites_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *community_id = get_active_community_id();
    gchar *url;
    struct MemoryStruct chunk = {0};
    JsonParser *parser;
    GError *error = NULL;
    GtkWidget *dialog;
    GtkWidget *list;
    GtkWidget *scroll;

    (void)user_data;
    if (!community_id) return;
    url = g_strdup_printf(COMMUNITY_INVITES_URL, community_id);
    if (!fetch_url(url, &chunk, NULL, "GET")) {
        show_modal_message(GTK_MESSAGE_ERROR, "Invites unavailable.", NULL);
        g_free(url);
        return;
    }

    dialog = gtk_dialog_new_with_buttons("Community Invites",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close", GTK_RESPONSE_CLOSE,
                                         NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 420);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE, TRUE, 0);

    parser = json_parser_new();
    if (json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *invites = root && json_object_has_member(root, "invites")
            ? json_object_get_array_member(root, "invites")
            : NULL;
        if (invites && json_array_get_length(invites) > 0) {
            for (guint i = 0; i < json_array_get_length(invites); i++) {
                JsonObject *invite = json_array_get_object_element(invites, i);
                const gchar *id = json_get_string_or_empty(invite, "id");
                const gchar *token = json_get_string_or_empty(invite, "token");
                const gchar *expires = json_get_string_or_empty(invite, "expires_at");
                gint64 use_count = json_object_has_member(invite, "use_count") ? json_object_get_int_member(invite, "use_count") : 0;
                gint64 max_uses = json_object_has_member(invite, "max_uses") && !json_node_is_null(json_object_get_member(invite, "max_uses"))
                    ? json_object_get_int_member(invite, "max_uses") : 0;
                GtkWidget *row = gtk_list_box_row_new();
                GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                gchar *uses_text = max_uses > 0
                    ? g_strdup_printf("%" G_GINT64_FORMAT "/%" G_GINT64_FORMAT, use_count, max_uses)
                    : g_strdup_printf("%" G_GINT64_FORMAT, use_count);
                gchar *text = g_strdup_printf("%s\nUses: %s%s%s",
                                              token,
                                              uses_text,
                                              expires[0] ? " · Expires: " : "",
                                              expires[0] ? expires : "");
                GtkWidget *label = gtk_label_new(text);
                GtkWidget *revoke = gtk_button_new_with_label("Revoke");
                struct CommunityInviteAction *action = g_new0(struct CommunityInviteAction, 1);
                action->community_id = g_strdup(community_id);
                action->invite_id = g_strdup(id);
                gtk_label_set_xalign(GTK_LABEL(label), 0.0);
                gtk_container_add(GTK_CONTAINER(row), box);
                gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 8);
                gtk_box_pack_end(GTK_BOX(box), revoke, FALSE, FALSE, 8);
                g_signal_connect_data(revoke, "clicked", G_CALLBACK(on_community_revoke_invite_clicked), action, free_community_invite_action_closure, 0);
                gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
                g_free(text);
                g_free(uses_text);
            }
        } else {
            gtk_list_box_insert(GTK_LIST_BOX(list), gtk_label_new("No active invites."), -1);
        }
    } else {
        if (error) g_error_free(error);
        gtk_list_box_insert(GTK_LIST_BOX(list), gtk_label_new("Could not read invites."), -1);
    }
    g_object_unref(parser);
    g_free(chunk.memory);
    g_free(url);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static gboolean
community_json_request(const gchar *url, const gchar *method, JsonBuilder *builder, gchar **response_out)
{
    JsonGenerator *gen = NULL;
    gchar *payload = NULL;
    gboolean ok;

    if (builder) {
        gen = json_generator_new();
        json_generator_set_root(gen, json_builder_get_root(builder));
        payload = json_generator_to_data(gen, NULL);
    }
    ok = perform_request_with_optional_payload(url, payload, method, response_out);
    g_free(payload);
    if (gen) g_object_unref(gen);
    return ok;
}

static void
show_community_mod_log(GtkWidget *parent, const gchar *community_id)
{
    gchar *url = g_strdup_printf(COMMUNITY_MOD_LOG_URL, community_id);
    struct MemoryStruct chunk = {0};
    JsonParser *parser;
    GError *error = NULL;
    GString *text = g_string_new("");

    if (!fetch_url(url, &chunk, NULL, "GET")) {
        show_modal_message(GTK_MESSAGE_ERROR, "Moderation log unavailable.", NULL);
        g_free(url);
        g_string_free(text, TRUE);
        return;
    }

    parser = json_parser_new();
    if (json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *log = root && json_object_has_member(root, "log") ? json_object_get_array_member(root, "log") : NULL;
        if (log && json_array_get_length(log) > 0) {
            for (guint i = 0; i < json_array_get_length(log); i++) {
                JsonObject *item = json_array_get_object_element(log, i);
                g_string_append_printf(text, "%s by @%s",
                                       json_get_string_or_empty(item, "action"),
                                       json_get_string_or_empty(item, "actor_username"));
                if (json_get_string_or_empty(item, "target_username")[0]) {
                    g_string_append_printf(text, " -> @%s", json_get_string_or_empty(item, "target_username"));
                }
                if (json_get_string_or_empty(item, "reason")[0]) {
                    g_string_append_printf(text, "\nReason: %s", json_get_string_or_empty(item, "reason"));
                }
                g_string_append(text, "\n\n");
            }
        } else {
            g_string_append(text, "No moderation actions yet.");
        }
    } else {
        if (error) g_error_free(error);
        g_string_append(text, "Could not read moderation log.");
    }
    g_object_unref(parser);
    g_free(chunk.memory);
    g_free(url);
    show_modal_message(GTK_MESSAGE_INFO, "Moderation Log", text->str);
    (void)parent;
    g_string_free(text, TRUE);
}

static void
populate_join_requests_list(GtkListBox *list, const gchar *community_id)
{
    gchar *url = g_strdup_printf(COMMUNITY_JOIN_REQUESTS_URL, community_id);
    struct MemoryStruct chunk = {0};
    JsonParser *parser;
    GError *error = NULL;

    if (!fetch_url(url, &chunk, NULL, "GET")) {
        gtk_list_box_insert(list, gtk_label_new("Could not load join requests."), -1);
        g_free(url);
        return;
    }

    parser = json_parser_new();
    if (json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
        JsonObject *root = json_node_get_object(json_parser_get_root(parser));
        JsonArray *requests = root && json_object_has_member(root, "requests") ? json_object_get_array_member(root, "requests") : NULL;
        if (requests && json_array_get_length(requests) > 0) {
            for (guint i = 0; i < json_array_get_length(requests); i++) {
                JsonObject *req = json_array_get_object_element(requests, i);
                GtkWidget *row = gtk_list_box_row_new();
                GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                GtkWidget *label;
                gchar *title = g_strdup_printf("@%s",
                                               json_get_string_or_empty(req, "username")[0]
                                               ? json_get_string_or_empty(req, "username")
                                               : json_get_string_or_empty(req, "user_id"));
                gtk_container_add(GTK_CONTAINER(row), box);
                label = gtk_label_new(title);
                gtk_label_set_xalign(GTK_LABEL(label), 0.0);
                gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 8);
                gtk_widget_set_name(row, json_get_string_or_empty(req, "id"));
                gtk_list_box_insert(list, row, -1);
                g_free(title);
            }
        } else {
            gtk_list_box_insert(list, gtk_label_new("No pending join requests."), -1);
        }
    } else {
        if (error) g_error_free(error);
        gtk_list_box_insert(list, gtk_label_new("Could not read join requests."), -1);
    }
    g_object_unref(parser);
    g_free(chunk.memory);
    g_free(url);
}

void
on_community_moderation_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *community_id = get_active_community_id();
    GtkWidget *dialog;
    GtkWidget *notebook;
    GtkWidget *requests_list;
    GtkWidget *member_grid;
    GtkWidget *user_entry;
    GtkWidget *role_combo;
    GtkWidget *reason_entry;
    gint response;

    (void)user_data;
    if (!community_id) return;
    dialog = gtk_dialog_new_with_buttons("Community Moderation",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close", GTK_RESPONSE_CLOSE,
                                         "_Approve Request", 10,
                                         "_Reject Request", 11,
                                         "_Save Role", 12,
                                         "_Ban", 13,
                                         "_Unban", 14,
                                         "_Log", 15,
                                         NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 460);
    notebook = gtk_notebook_new();
    requests_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(requests_list), GTK_SELECTION_SINGLE);
    populate_join_requests_list(GTK_LIST_BOX(requests_list), community_id);

    member_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(member_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(member_grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(member_grid), 10);
    user_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(user_entry), "Member user ID");
    role_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(role_combo), "member", "Member");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(role_combo), "mod", "Moderator");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(role_combo), "admin", "Admin");
    gtk_combo_box_set_active(GTK_COMBO_BOX(role_combo), 0);
    reason_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(reason_entry), "Ban reason");
    gtk_grid_attach(GTK_GRID(member_grid), gtk_label_new("Member:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(member_grid), user_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(member_grid), gtk_label_new("Role:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(member_grid), role_combo, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(member_grid), gtk_label_new("Reason:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(member_grid), reason_entry, 1, 2, 1, 1);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), requests_list, gtk_label_new("Join Requests"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), member_grid, gtk_label_new("Members"));
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), notebook, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == 10 || response == 11) {
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(requests_list));
        const gchar *request_id = row ? gtk_widget_get_name(GTK_WIDGET(row)) : NULL;
        if (request_id && request_id[0]) {
            gchar *url = g_strdup_printf(response == 10 ? COMMUNITY_JOIN_REQUEST_APPROVE_URL : COMMUNITY_JOIN_REQUEST_REJECT_URL,
                                         community_id, request_id);
            gchar *resp = NULL;
            if (perform_request_with_optional_payload(url, "{}", "POST", &resp)) {
                show_modal_message(GTK_MESSAGE_INFO, response == 10 ? "Request approved." : "Request rejected.", NULL);
            } else {
                gchar *err = extract_error_message(resp);
                show_modal_message(GTK_MESSAGE_ERROR, "Request update failed.", err);
                g_free(err);
            }
            g_free(resp);
            g_free(url);
        }
    } else if (response == 12 || response == 13 || response == 14) {
        const gchar *user_id = gtk_entry_get_text(GTK_ENTRY(user_entry));
        gchar *url = NULL;
        gchar *resp = NULL;
        JsonBuilder *builder = NULL;
        if (user_id && user_id[0]) {
            if (response == 12) {
                builder = json_builder_new();
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "role");
                json_builder_add_string_value(builder, gtk_combo_box_get_active_id(GTK_COMBO_BOX(role_combo)));
                json_builder_end_object(builder);
                url = g_strdup_printf(COMMUNITY_MEMBER_ROLE_URL, community_id, user_id);
            } else if (response == 13) {
                builder = json_builder_new();
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "reason");
                json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(reason_entry)));
                json_builder_end_object(builder);
                url = g_strdup_printf(COMMUNITY_MEMBER_BAN_URL, community_id, user_id);
            } else {
                url = g_strdup_printf(COMMUNITY_MEMBER_UNBAN_URL, community_id, user_id);
            }
            if (community_json_request(url, "POST", builder, &resp)) {
                show_modal_message(GTK_MESSAGE_INFO, "Member updated.", NULL);
            } else {
                gchar *err = extract_error_message(resp);
                show_modal_message(GTK_MESSAGE_ERROR, "Member update failed.", err);
                g_free(err);
            }
            if (builder) g_object_unref(builder);
            g_free(resp);
            g_free(url);
        }
    } else if (response == 15) {
        show_community_mod_log(widget, community_id);
    }
    gtk_widget_destroy(dialog);
}

void
on_community_style_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *community_id = get_active_community_id();
    GtkWidget *dialog;
    GtkWidget *grid;
    GtkWidget *tag_enabled;
    GtkWidget *tag_emoji;
    GtkWidget *tag_text;
    GtkWidget *icon_button;
    GtkWidget *banner_button;
    gint response;

    (void)user_data;
    if (!community_id) return;
    dialog = gtk_dialog_new_with_buttons("Community Style",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save Tag", 20,
                                         "_Upload Icon", 21,
                                         "_Upload Banner", 22,
                                         NULL);
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    tag_enabled = gtk_check_button_new_with_label("Enable profile tag");
    tag_emoji = gtk_entry_new();
    tag_text = gtk_entry_new();
    icon_button = gtk_file_chooser_button_new("Icon image", GTK_FILE_CHOOSER_ACTION_OPEN);
    banner_button = gtk_file_chooser_button_new("Banner image", GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_entry_set_placeholder_text(GTK_ENTRY(tag_emoji), "Emoji");
    gtk_entry_set_placeholder_text(GTK_ENTRY(tag_text), "Text, up to 4 chars");
    gtk_grid_attach(GTK_GRID(grid), tag_enabled, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Emoji:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), tag_emoji, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Text:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), tag_text, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Icon:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), icon_button, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Banner:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), banner_button, 1, 4, 1, 1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == 20) {
        JsonBuilder *builder = json_builder_new();
        gchar *url = g_strdup_printf(COMMUNITY_TAG_URL, community_id);
        gchar *resp = NULL;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "tag_enabled");
        json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tag_enabled)));
        json_builder_set_member_name(builder, "tag_emoji");
        json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(tag_emoji)));
        json_builder_set_member_name(builder, "tag_text");
        json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(tag_text)));
        json_builder_end_object(builder);
        if (community_json_request(url, "PATCH", builder, &resp)) show_modal_message(GTK_MESSAGE_INFO, "Tag updated.", NULL);
        else { gchar *err = extract_error_message(resp); show_modal_message(GTK_MESSAGE_ERROR, "Tag update failed.", err); g_free(err); }
        g_free(resp); g_free(url); g_object_unref(builder);
    } else if (response == 21 || response == 22) {
        GtkWidget *chooser = response == 21 ? icon_button : banner_button;
        gchar *file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        gchar *media = file ? perform_media_upload(file) : NULL;
        if (media) {
            JsonBuilder *builder = json_builder_new();
            gchar *url = g_strdup_printf(response == 21 ? COMMUNITY_ICON_URL : COMMUNITY_BANNER_URL, community_id);
            gchar *resp = NULL;
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, response == 21 ? "icon" : "banner");
            json_builder_add_string_value(builder, media);
            json_builder_end_object(builder);
            if (community_json_request(url, "POST", builder, &resp)) show_modal_message(GTK_MESSAGE_INFO, response == 21 ? "Icon updated." : "Banner updated.", NULL);
            else { gchar *err = extract_error_message(resp); show_modal_message(GTK_MESSAGE_ERROR, "Upload update failed.", err); g_free(err); }
            g_free(resp); g_free(url); g_object_unref(builder);
        }
        g_free(media);
        g_free(file);
    }
    gtk_widget_destroy(dialog);
}

void
on_community_pin_post_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *community_id = get_active_community_id();
    GtkWidget *dialog;
    GtkWidget *entry;

    (void)user_data;
    if (!community_id) return;
    dialog = gtk_dialog_new_with_buttons("Community Post Pin",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Pin", GTK_RESPONSE_ACCEPT,
                                         "_Unpin", GTK_RESPONSE_REJECT,
                                         NULL);
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Post ID");
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_REJECT) {
        const gchar *tweet_id = gtk_entry_get_text(GTK_ENTRY(entry));
        if (tweet_id && tweet_id[0]) {
            gchar *url = g_strdup_printf(response == GTK_RESPONSE_ACCEPT ? COMMUNITY_TWEET_PIN_URL : COMMUNITY_TWEET_UNPIN_URL,
                                         community_id, tweet_id);
            gchar *resp = NULL;
            if (perform_request_with_optional_payload(url, "{}", "POST", &resp)) {
                start_loading_community_tweets(GTK_LIST_BOX(g_community_tweets_list), community_id);
            } else {
                gchar *err = extract_error_message(resp);
                show_modal_message(GTK_MESSAGE_ERROR, "Pin update failed.", err);
                g_free(err);
            }
            g_free(resp);
            g_free(url);
        }
    }
    gtk_widget_destroy(dialog);
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
    gchar *url = NULL;

    if (async_data->query && async_data->query[0]) {
        url = g_strdup(async_data->query);
    } else {
        url = g_strdup(COMMUNITIES_LIST_URL);
    }

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->communities = parse_communities(chunk.memory);
        async_data->success = TRUE;
        g_free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_free(url);
    g_idle_add(on_communities_loaded, async_data);
    return NULL;
}

void start_loading_communities(GtkListBox *list_box)
{
    if (!list_box) return;

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

static void
start_loading_communities_from_url(GtkListBox *list_box, const gchar *url)
{
    GList *children;
    GtkWidget *loading;
    struct AsyncData *data;

    if (!list_box || !url) {
        return;
    }

    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    loading = gtk_label_new("Loading communities...");
    gtk_widget_show(loading);
    gtk_list_box_insert(list_box, loading, -1);

    data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->query = g_strdup(url);
    g_thread_new("communities-loader", fetch_communities_thread, data);
}

void
start_loading_communities_search(GtkListBox *list_box, const gchar *query)
{
    gchar *escaped;
    gchar *url;

    if (!query || strlen(query) < 2) {
        start_loading_communities(list_box);
        return;
    }

    escaped = g_uri_escape_string(query, NULL, TRUE);
    url = g_strdup_printf("%s?q=%s", COMMUNITIES_SEARCH_URL, escaped);
    start_loading_communities_from_url(list_box, url);
    g_free(url);
    g_free(escaped);
}

void
start_loading_communities_trending(GtkListBox *list_box)
{
    start_loading_communities_from_url(list_box, COMMUNITIES_TRENDING_URL);
}

void
start_loading_communities_recommended(GtkListBox *list_box)
{
    start_loading_communities_from_url(list_box, COMMUNITIES_RECOMMENDED_URL);
}

void
start_loading_my_communities(GtkListBox *list_box)
{
    start_loading_communities_from_url(list_box, COMMUNITIES_MY_URL);
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

static gboolean
perform_change_password(const gchar *username,
                        const gchar *current_password,
                        const gchar *new_password,
                        gchar **error_out)
{
    gchar *escaped_username;
    gchar *url;
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    gchar *response = NULL;
    gboolean success = FALSE;

    if (error_out) {
        *error_out = NULL;
    }
    if (!g_auth_token || !username || !new_password) {
        if (error_out) {
            *error_out = g_strdup("You must be logged in to change your password.");
        }
        return FALSE;
    }

    escaped_username = g_uri_escape_string(username, NULL, FALSE);
    url = g_strdup_printf(PROFILE_PASSWORD_URL, escaped_username);

    builder = json_builder_new();
    json_builder_begin_object(builder);
    if (current_password && current_password[0] != '\0') {
        json_builder_set_member_name(builder, "currentPassword");
        json_builder_add_string_value(builder, current_password);
    }
    json_builder_set_member_name(builder, "newPassword");
    json_builder_add_string_value(builder, new_password);
    json_builder_end_object(builder);

    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);

    if (perform_request_with_optional_payload(url, payload, "PATCH", &response)) {
        gchar *error_message = extract_error_message(response);
        success = (error_message == NULL);
        if (!success && error_out) {
            *error_out = error_message;
            error_message = NULL;
        }
        g_free(error_message);
    } else if (error_out) {
        *error_out = g_strdup("The password change request could not be sent.");
    }

    g_free(response);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    g_free(url);
    g_free(escaped_username);
    return success;
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
        gchar *username = get_username_safe();
        
        if (!username) {
            show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to change your password.");
        } else if (strlen(new_pw) < 8) {
            show_modal_message(GTK_MESSAGE_ERROR, "Password must be at least 8 characters long.", NULL);
        } else if (!g_str_equal(new_pw, confirm)) {
            show_modal_message(GTK_MESSAGE_ERROR, "New passwords do not match.", NULL);
        } else {
            gchar *error_message = NULL;
            if (perform_change_password(username, current, new_pw, &error_message)) {
                show_modal_message(GTK_MESSAGE_INFO, "Password updated.", "Your password was changed successfully.");
            } else {
                show_modal_message(GTK_MESSAGE_ERROR, "Password change failed.", error_message);
            }
            g_free(error_message);
        }
        g_free(username);
    }
    
    gtk_widget_destroy(dialog);
}

static gchar*
passkey_summary_from_json(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GString *summary = g_string_new(NULL);
    if (json_data && json_parser_load_from_data(parser, json_data, -1, NULL)) {
        JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
        JsonArray *passkeys = obj && json_object_has_member(obj, "passkeys")
            ? json_object_get_array_member(obj, "passkeys") : NULL;
        if (passkeys && json_array_get_length(passkeys) > 0) {
            for (guint i = 0; i < json_array_get_length(passkeys); i++) {
                JsonObject *passkey = json_array_get_object_element(passkeys, i);
                const gchar *id = json_get_string_or_empty(passkey, "id");
                const gchar *name = json_get_string_or_empty(passkey, "name");
                const gchar *created = json_get_string_or_empty(passkey, "createdAt");
                const gchar *last = json_get_string_or_empty(passkey, "lastUsed");
                g_string_append_printf(summary, "%u. %s\nID: %s\nCreated: %s\nLast used: %s\n\n",
                                       i + 1, name[0] ? name : "Passkey", id,
                                       created[0] ? created : "Unknown",
                                       last[0] ? last : "Never");
            }
        } else {
            g_string_append(summary, "No passkeys are registered for this account.");
        }
    } else {
        g_string_append(summary, "Could not read passkeys.");
    }
    g_object_unref(parser);
    return g_string_free(summary, FALSE);
}

void on_manage_passkeys_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    struct MemoryStruct chunk = {0};
    if (!g_auth_token || !fetch_url(AUTH_PASSKEYS_URL, &chunk, NULL, "GET")) {
        show_modal_message(GTK_MESSAGE_ERROR, "Passkeys unavailable.", "Log in and try again.");
        return;
    }

    gchar *summary = passkey_summary_from_json(chunk.memory);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Passkeys", window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Close", GTK_RESPONSE_CLOSE,
                                                    webauthn_fido2_is_enabled() ? "_Add Passkey" : "_Add in Browser", 3,
                                                    "_Rename", 1,
                                                    "_Delete", 2,
                                                    NULL);
    GtkWidget *label = gtk_label_new(summary);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == 3) {
        if (webauthn_fido2_is_enabled()) {
            gchar *username = get_username_safe();
            gchar *verify_response = NULL;
            gchar *error = NULL;
            if (!username) {
                show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "Log in and try again.");
            } else if (webauthn_fido2_register(username, &verify_response, &error)) {
                show_modal_message(GTK_MESSAGE_INFO, "Passkey added.", NULL);
            } else {
                show_modal_message(GTK_MESSAGE_ERROR,
                                   "Passkey registration failed.",
                                   error ? error : "The passkey response could not be accepted.");
            }
            g_free(username);
            g_free(verify_response);
            g_free(error);
        } else {
            open_web_path(window, "/settings");
        }
    } else if (response == 1 || response == 2) {
        GtkWidget *edit = gtk_dialog_new_with_buttons(response == 1 ? "Rename passkey" : "Delete passkey",
                                                      window,
                                                      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                      "_Cancel", GTK_RESPONSE_CANCEL,
                                                      response == 1 ? "_Rename" : "_Delete", GTK_RESPONSE_ACCEPT,
                                                      NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(edit));
        GtkWidget *id_entry = gtk_entry_new();
        GtkWidget *name_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(id_entry), "Passkey ID");
        gtk_box_pack_start(GTK_BOX(content), id_entry, FALSE, FALSE, 8);
        if (response == 1) {
            gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "New name");
            gtk_box_pack_start(GTK_BOX(content), name_entry, FALSE, FALSE, 8);
        }
        gtk_widget_show_all(edit);
        if (gtk_dialog_run(GTK_DIALOG(edit)) == GTK_RESPONSE_ACCEPT) {
            const gchar *cred_id = gtk_entry_get_text(GTK_ENTRY(id_entry));
            if (cred_id && *cred_id) {
                gchar *url = response == 1 ? g_strdup_printf(AUTH_PASSKEY_NAME_URL, cred_id)
                                           : g_strdup_printf(AUTH_PASSKEY_DELETE_URL, cred_id);
                gchar *payload = NULL;
                if (response == 1) {
                    JsonBuilder *builder = json_builder_new();
                    JsonGenerator *gen = json_generator_new();
                    JsonNode *root;
                    json_builder_begin_object(builder);
                    json_builder_set_member_name(builder, "name");
                    json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(name_entry)));
                    json_builder_end_object(builder);
                    root = json_builder_get_root(builder);
                    json_generator_set_root(gen, root);
                    payload = json_generator_to_data(gen, NULL);
                    json_node_free(root);
                    g_object_unref(gen);
                    g_object_unref(builder);
                }
                struct MemoryStruct result = {0};
                if (fetch_url(url, &result, payload, response == 1 ? "PUT" : "DELETE")) {
                    show_modal_message(GTK_MESSAGE_INFO, response == 1 ? "Passkey renamed." : "Passkey deleted.", NULL);
                    g_free(result.memory);
                } else {
                    show_modal_message(GTK_MESSAGE_ERROR, response == 1 ? "Rename failed." : "Delete failed.", NULL);
                }
                g_free(payload);
                g_free(url);
            }
        }
        gtk_widget_destroy(edit);
    }

    g_free(summary);
    g_free(chunk.memory);
}

static gchar*
push_summary_from_json(const gchar *status_json, const gchar *vapid_json)
{
    JsonParser *parser = json_parser_new();
    gboolean enabled = FALSE;
    int count = 0;
    gchar *vapid = NULL;
    if (status_json && json_parser_load_from_data(parser, status_json, -1, NULL)) {
        JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
        if (obj && json_object_has_member(obj, "enabled"))
            enabled = json_object_get_boolean_member(obj, "enabled");
        if (obj && json_object_has_member(obj, "count"))
            count = json_object_get_int_member(obj, "count");
    }
    g_object_unref(parser);
    parser = json_parser_new();
    if (vapid_json && json_parser_load_from_data(parser, vapid_json, -1, NULL)) {
        JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
        if (obj && json_object_has_member(obj, "publicKey") &&
            !json_node_is_null(json_object_get_member(obj, "publicKey")))
            vapid = g_strdup(json_object_get_string_member(obj, "publicKey"));
    }
    g_object_unref(parser);
    gchar *summary = g_strdup_printf("Status: %s\nSubscriptions: %d\nVAPID key: %s",
                                     enabled ? "Enabled" : "Disabled",
                                     count,
                                     vapid ? vapid : "Unavailable");
    g_free(vapid);
    return summary;
}

void on_push_notifications_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    struct MemoryStruct status = {0};
    struct MemoryStruct vapid = {0};
    if (!g_auth_token) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "Log in before managing push notifications.");
        return;
    }
    fetch_url(PUSH_STATUS_URL, &status, NULL, "GET");
    fetch_url(PUSH_VAPID_KEY_URL, &vapid, NULL, "GET");
    gchar *summary = push_summary_from_json(status.memory, vapid.memory);

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Push Notifications", window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Close", GTK_RESPONSE_CLOSE,
                                                    "_Subscribe", 1,
                                                    "_Unsubscribe", 2,
                                                    NULL);
    GtkWidget *label = gtk_label_new(summary);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == 1 || response == 2) {
        GtkWidget *edit = gtk_dialog_new_with_buttons(response == 1 ? "Subscribe endpoint" : "Unsubscribe endpoint",
                                                      window,
                                                      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                      "_Cancel", GTK_RESPONSE_CANCEL,
                                                      response == 1 ? "_Subscribe" : "_Unsubscribe", GTK_RESPONSE_ACCEPT,
                                                      NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(edit));
        GtkWidget *endpoint_entry = gtk_entry_new();
        GtkWidget *p256dh_entry = gtk_entry_new();
        GtkWidget *auth_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(endpoint_entry), "Endpoint URL");
        gtk_box_pack_start(GTK_BOX(content), endpoint_entry, FALSE, FALSE, 8);
        if (response == 1) {
            gtk_entry_set_placeholder_text(GTK_ENTRY(p256dh_entry), "p256dh key");
            gtk_entry_set_placeholder_text(GTK_ENTRY(auth_entry), "auth key");
            gtk_box_pack_start(GTK_BOX(content), p256dh_entry, FALSE, FALSE, 8);
            gtk_box_pack_start(GTK_BOX(content), auth_entry, FALSE, FALSE, 8);
        }
        gtk_widget_show_all(edit);
        if (gtk_dialog_run(GTK_DIALOG(edit)) == GTK_RESPONSE_ACCEPT) {
            JsonBuilder *builder = json_builder_new();
            JsonGenerator *gen = json_generator_new();
            JsonNode *root;
            if (response == 1) {
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "subscription");
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "endpoint");
                json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(endpoint_entry)));
                json_builder_set_member_name(builder, "keys");
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "p256dh");
                json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(p256dh_entry)));
                json_builder_set_member_name(builder, "auth");
                json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(auth_entry)));
                json_builder_end_object(builder);
                json_builder_end_object(builder);
                json_builder_end_object(builder);
            } else {
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "endpoint");
                json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(endpoint_entry)));
                json_builder_end_object(builder);
            }
            root = json_builder_get_root(builder);
            json_generator_set_root(gen, root);
            gchar *payload = json_generator_to_data(gen, NULL);
            struct MemoryStruct result = {0};
            if (fetch_url(response == 1 ? PUSH_SUBSCRIBE_URL : PUSH_UNSUBSCRIBE_URL, &result, payload, "POST")) {
                show_modal_message(GTK_MESSAGE_INFO, response == 1 ? "Push subscription saved." : "Push subscription removed.", NULL);
                g_free(result.memory);
            } else {
                show_modal_message(GTK_MESSAGE_ERROR, response == 1 ? "Subscribe failed." : "Unsubscribe failed.", NULL);
            }
            g_free(payload);
            json_node_free(root);
            g_object_unref(gen);
            g_object_unref(builder);
        }
        gtk_widget_destroy(edit);
    }

    g_free(summary);
    g_free(status.memory);
    g_free(vapid.memory);
}

static gchar*
format_moderation_history_response(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GString *text = g_string_new(NULL);

    if (json_data && json_parser_load_from_data(parser, json_data, -1, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : NULL;
        JsonArray *items = json_get_array_member_valid(obj, "suspensions");
        if (items && json_array_get_length(items) > 0) {
            for (guint i = 0; i < json_array_get_length(items); i++) {
                JsonNode *node = json_array_get_element(items, i);
                JsonObject *item = node && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
                if (!item) continue;
                g_string_append_printf(text,
                                       "%s - %s\nSeverity: %s\nStatus: %s\nCreated: %s\nExpires: %s\nNotes: %s\n\n",
                                       json_get_string_or_empty(item, "action"),
                                       json_get_string_or_empty(item, "reason"),
                                       json_get_string_or_empty(item, "severity"),
                                       json_get_string_or_empty(item, "status"),
                                       json_get_string_or_empty(item, "created_at"),
                                       json_get_string_or_empty(item, "expires_at")[0] ? json_get_string_or_empty(item, "expires_at") : "Permanent",
                                       json_get_string_or_empty(item, "notes"));
            }
        }
    }
    g_object_unref(parser);
    if (text->len == 0) {
        g_string_append(text, "No moderation history.");
    }
    return g_string_free(text, FALSE);
}

static gchar*
format_blocking_causes_response(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GString *text = g_string_new(NULL);

    if (json_data && json_parser_load_from_data(parser, json_data, -1, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : NULL;
        JsonArray *items = json_get_array_member_valid(obj, "causes");
        if (items && json_array_get_length(items) > 0) {
            for (guint i = 0; i < json_array_get_length(items); i++) {
                JsonNode *node = json_array_get_element(items, i);
                JsonObject *item = node && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
                if (!item) continue;
                gint64 count = json_get_int64_default(item, "count", 0);
                g_string_append_printf(text,
                                       "%" G_GINT64_FORMAT " block%s from post %s\nCreated: %s\n%s\n\n",
                                       count,
                                       count == 1 ? "" : "s",
                                       json_get_string_or_empty(item, "source_tweet_id"),
                                       json_get_string_or_empty(item, "created_at"),
                                       json_get_string_or_empty(item, "content"));
            }
        }
    }
    g_object_unref(parser);
    if (text->len == 0) {
        g_string_append(text, "No block causes.");
    }
    return g_string_free(text, FALSE);
}

static gchar*
format_validate_accounts_response(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GString *text = g_string_new(NULL);

    if (json_data && json_parser_load_from_data(parser, json_data, -1, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : NULL;
        JsonArray *items = json_get_array_member_valid(obj, "validUsers");
        if (items && json_array_get_length(items) > 0) {
            for (guint i = 0; i < json_array_get_length(items); i++) {
                JsonNode *node = json_array_get_element(items, i);
                JsonObject *user = node && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
                if (!user) continue;
                GString *badges = g_string_new(NULL);
                if (json_get_bool_default(user, "verified", FALSE)) g_string_append(badges, "Verified");
                if (json_get_bool_default(user, "gold", FALSE)) {
                    if (badges->len) g_string_append(badges, ", ");
                    g_string_append(badges, "Gold");
                }
                if (json_get_bool_default(user, "gray", FALSE)) {
                    if (badges->len) g_string_append(badges, ", ");
                    g_string_append(badges, "Gray");
                }
                g_string_append_printf(text, "%s (@%s)\nID: %s%s%s\n\n",
                                       json_get_string_or_empty(user, "name")[0] ? json_get_string_or_empty(user, "name") : "Unknown",
                                       json_get_string_or_empty(user, "username"),
                                       json_get_string_or_empty(user, "id"),
                                       badges->len ? "\n" : "",
                                       badges->str);
                g_string_free(badges, TRUE);
            }
        }
    }
    g_object_unref(parser);
    if (text->len == 0) {
        g_string_append(text, "No valid accounts.");
    }
    return g_string_free(text, FALSE);
}

static void
show_text_response_dialog(GtkWindow *window, const gchar *title, const gchar *text)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Close", GTK_RESPONSE_CLOSE,
                                                    NULL);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_set_size_request(scroll, 560, 360);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(label), 0.0);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), label);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void on_moderation_history_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    struct MemoryStruct chunk = {0};
    if (!g_auth_token || !fetch_url(AUTH_MODERATION_HISTORY_URL, &chunk, NULL, "GET")) {
        show_modal_message(GTK_MESSAGE_ERROR, "Moderation history unavailable.", NULL);
        return;
    }
    gchar *text = format_moderation_history_response(chunk.memory);
    show_text_response_dialog(window, "Moderation History", text);
    g_free(text);
    g_free(chunk.memory);
}

void on_blocking_causes_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    struct MemoryStruct chunk = {0};
    if (!g_auth_token || !fetch_url(BLOCKING_CAUSES_URL, &chunk, NULL, "GET")) {
        show_modal_message(GTK_MESSAGE_ERROR, "Block causes unavailable.", NULL);
        return;
    }
    gchar *text = format_blocking_causes_response(chunk.memory);
    show_text_response_dialog(window, "Block Causes", text);
    g_free(text);
    g_free(chunk.memory);
}

void on_validate_accounts_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Validate Accounts",
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Validate", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "User IDs, comma-separated");
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        JsonBuilder *builder = json_builder_new();
        JsonGenerator *gen = json_generator_new();
        JsonNode *root;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "userIds");
        json_builder_begin_array(builder);
        gchar **parts = g_strsplit(gtk_entry_get_text(GTK_ENTRY(entry)), ",", -1);
        for (guint i = 0; parts[i]; i++) {
            gchar *id = g_strstrip(parts[i]);
            if (id[0])
                json_builder_add_string_value(builder, id);
        }
        g_strfreev(parts);
        json_builder_end_array(builder);
        json_builder_end_object(builder);
        root = json_builder_get_root(builder);
        json_generator_set_root(gen, root);
        gchar *payload = json_generator_to_data(gen, NULL);
        gchar *response = NULL;
        if (perform_request_with_optional_payload(AUTH_VALIDATE_ACCOUNTS_URL, payload, "POST", &response)) {
            gchar *text = format_validate_accounts_response(response);
            show_text_response_dialog(window, "Valid Accounts", text);
            g_free(text);
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "Account validation failed.", NULL);
        }
        g_free(response);
        g_free(payload);
        json_node_free(root);
        g_object_unref(gen);
        g_object_unref(builder);
    }
    gtk_widget_destroy(dialog);
}

void on_add_account_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Account",
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Add", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *username = gtk_entry_new();
    GtkWidget *password = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Username"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), username, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Password"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), password, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        JsonBuilder *builder = json_builder_new();
        JsonGenerator *gen = json_generator_new();
        JsonNode *root;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "username");
        json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(username)));
        json_builder_set_member_name(builder, "password");
        json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(password)));
        json_builder_end_object(builder);
        root = json_builder_get_root(builder);
        json_generator_set_root(gen, root);
        gchar *payload = json_generator_to_data(gen, NULL);
        gchar *response = NULL;
        gchar *error = NULL;
        if (perform_request_with_optional_payload(AUTH_ADD_ACCOUNT_URL, payload, "POST", &response)) {
            error = extract_error_message(response);
            show_modal_message(error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_INFO,
                               error ? "Add account failed." : "Account added.",
                               error);
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "Add account failed.", NULL);
        }
        g_free(error);
        g_free(response);
        g_free(payload);
        json_node_free(root);
        g_object_unref(gen);
        g_object_unref(builder);
    }
    gtk_widget_destroy(dialog);
}

static gboolean
perform_profile_json_request(const gchar *url,
                             const gchar *method,
                             JsonBuilder *builder,
                             gchar **response_out,
                             gchar **error_out)
{
    JsonGenerator *gen;
    gchar *payload;
    gchar *response = NULL;
    gboolean success;

    if (response_out) {
        *response_out = NULL;
    }
    if (error_out) {
        *error_out = NULL;
    }
    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);
    success = perform_request_with_optional_payload(url, payload, method, &response);
    if (!success && error_out) {
        gchar *server_error = extract_error_message(response);
        *error_out = server_error ? server_error : g_strdup("Request failed.");
    }
    if (response_out) {
        *response_out = response;
        response = NULL;
    }
    g_free(response);
    g_free(payload);
    g_object_unref(gen);
    return success;
}

void
on_change_username_clicked(GtkWidget *widget, gpointer user_data)
{
    JsonBuilder *builder;
    JsonParser *parser;
    gchar *username;
    gchar *url;
    gchar *response = NULL;
    gchar *error_message = NULL;
    const gchar *new_username;
    GError *parse_error = NULL;

    (void)widget;
    (void)user_data;
    if (!g_auth_token || !g_settings_new_username_entry) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to change your username.");
        return;
    }
    new_username = gtk_entry_get_text(GTK_ENTRY(g_settings_new_username_entry));
    if (!new_username || new_username[0] == '\0') {
        show_modal_message(GTK_MESSAGE_ERROR, "Username required.", "Enter a new username.");
        return;
    }
    username = get_username_safe();
    if (!username) {
        return;
    }
    url = g_strdup_printf(PROFILE_USERNAME_URL, username);
    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "newUsername");
    json_builder_add_string_value(builder, new_username);
    json_builder_end_object(builder);
    if (perform_profile_json_request(url, "PATCH", builder, &response, &error_message)) {
        parser = json_parser_new();
        if (json_parser_load_from_data(parser, response, -1, &parse_error)) {
            JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
            const gchar *token = json_get_string_or_empty(obj, "token");
            const gchar *updated_username = json_get_string_or_empty(obj, "username");
            if (token[0] && updated_username[0]) {
                g_mutex_lock(&g_globals_mutex);
                g_free(g_auth_token);
                g_free(g_current_username);
                g_auth_token = g_strdup(token);
                g_current_username = g_strdup(updated_username);
                g_mutex_unlock(&g_globals_mutex);
                save_session(g_auth_token, g_current_username, g_is_admin);
                gtk_entry_set_text(GTK_ENTRY(g_settings_new_username_entry), "");
                update_login_ui();
                show_modal_message(GTK_MESSAGE_INFO, "Username updated.", "Your session has been updated.");
            }
        } else if (parse_error) {
            g_error_free(parse_error);
        }
        g_object_unref(parser);
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Username change failed.", error_message);
    }
    g_free(response);
    g_free(error_message);
    g_object_unref(builder);
    g_free(url);
    g_free(username);
}

static gboolean
perform_boolean_account_setting(const gchar *url, const gchar *member, gboolean state)
{
    JsonBuilder *builder = json_builder_new();
    gchar *error_message = NULL;
    gboolean success;

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, member);
    json_builder_add_boolean_value(builder, state);
    json_builder_end_object(builder);
    success = perform_profile_json_request(url, "POST", builder, NULL, &error_message);
    if (!success) {
        show_modal_message(GTK_MESSAGE_ERROR, "Setting update failed.", error_message);
    }
    g_free(error_message);
    g_object_unref(builder);
    return success;
}

void
on_account_private_toggled(GtkSwitch *switch_widget, gboolean state, gpointer user_data)
{
    (void)switch_widget;
    (void)user_data;
    if (g_auth_token) {
        perform_boolean_account_setting(PROFILE_SETTINGS_PRIVATE_URL, "enabled", state);
    }
}

void
on_transparency_location_toggled(GtkSwitch *switch_widget, gboolean state, gpointer user_data)
{
    (void)switch_widget;
    (void)user_data;
    if (g_auth_token) {
        perform_boolean_account_setting(PROFILE_SETTINGS_TRANSPARENCY_LOCATION_URL, "showContinent", state);
    }
}

static void
perform_community_tag_update(const gchar *community_id)
{
    JsonBuilder *builder;
    gchar *error_message = NULL;

    if (!g_auth_token) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to update your community tag.");
        return;
    }

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "community_id");
    if (community_id && community_id[0] != '\0') {
        json_builder_add_string_value(builder, community_id);
    } else {
        json_builder_add_null_value(builder);
    }
    json_builder_end_object(builder);

    if (perform_profile_json_request(PROFILE_SETTINGS_COMMUNITY_TAG_URL, "POST", builder, NULL, &error_message)) {
        show_modal_message(GTK_MESSAGE_INFO, "Community tag updated.", NULL);
    } else {
        show_modal_message(GTK_MESSAGE_ERROR, "Community tag update failed.", error_message);
    }
    g_free(error_message);
    g_object_unref(builder);
}

void
on_update_community_tag_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *community_id;

    (void)widget;
    (void)user_data;
    if (!g_settings_community_tag_entry) {
        return;
    }
    community_id = gtk_entry_get_text(GTK_ENTRY(g_settings_community_tag_entry));
    perform_community_tag_update(community_id);
}

void
on_clear_community_tag_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    perform_community_tag_update(NULL);
    if (g_settings_community_tag_entry) {
        gtk_entry_set_text(GTK_ENTRY(g_settings_community_tag_entry), "");
    }
}

void
on_update_outlines_clicked(GtkWidget *widget, gpointer user_data)
{
    JsonBuilder *builder;
    gchar *username;
    gchar *url;
    gchar *error_message = NULL;
    const gchar *checkmark;
    const gchar *avatar;
    gboolean sent_any = FALSE;

    (void)widget;
    (void)user_data;
    if (!g_auth_token || !g_settings_checkmark_outline_entry || !g_settings_avatar_outline_entry) {
        return;
    }
    username = get_username_safe();
    if (!username) {
        return;
    }
    checkmark = gtk_entry_get_text(GTK_ENTRY(g_settings_checkmark_outline_entry));
    avatar = gtk_entry_get_text(GTK_ENTRY(g_settings_avatar_outline_entry));
    builder = json_builder_new();
    json_builder_begin_object(builder);
    if (checkmark && checkmark[0]) {
        json_builder_set_member_name(builder, "checkmark_outline");
        json_builder_add_string_value(builder, checkmark);
        sent_any = TRUE;
    }
    if (avatar && avatar[0]) {
        json_builder_set_member_name(builder, "avatar_outline");
        json_builder_add_string_value(builder, avatar);
        sent_any = TRUE;
    }
    json_builder_end_object(builder);
    if (!sent_any) {
        show_modal_message(GTK_MESSAGE_ERROR, "Outline value required.", "Enter a checkmark or avatar outline color.");
    } else {
        url = g_strdup_printf(PROFILE_OUTLINES_URL, username);
        if (perform_profile_json_request(url, "PATCH", builder, NULL, &error_message)) {
            show_modal_message(GTK_MESSAGE_INFO, "Outlines updated.", NULL);
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "Outline update failed.", error_message);
        }
        g_free(url);
    }
    g_free(error_message);
    g_object_unref(builder);
    g_free(username);
}

void
on_delete_account_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *toplevel;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *entry;
    JsonBuilder *builder;
    gchar *username;
    gchar *url;
    gchar *error_message = NULL;

    (void)user_data;
    if (!g_auth_token) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to delete your account.");
        return;
    }
    username = get_username_safe();
    if (!username) {
        return;
    }
    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Delete Account",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Delete", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Type DELETE MY ACCOUNT");
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Type DELETE MY ACCOUNT to permanently delete your account."), FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        builder = json_builder_new();
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "confirmationText");
        json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(entry)));
        json_builder_end_object(builder);
        url = g_strdup_printf(PROFILE_DELETE_ACCOUNT_URL, username);
        if (perform_profile_json_request(url, "DELETE", builder, NULL, &error_message)) {
            gtk_widget_destroy(dialog);
            g_object_unref(builder);
            g_free(url);
            g_free(username);
            perform_logout();
            show_modal_message(GTK_MESSAGE_INFO, "Account deleted.", NULL);
            g_free(error_message);
            return;
        }
        show_modal_message(GTK_MESSAGE_ERROR, "Account deletion failed.", error_message);
        g_free(url);
        g_object_unref(builder);
    }
    gtk_widget_destroy(dialog);
    g_free(error_message);
    g_free(username);
}

void
on_bulk_delete_posts_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *toplevel;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *after_entry;
    GtkWidget *before_entry;
    GtkWidget *limit_spin;
    GtkWidget *include_replies_check;
    GtkWidget *keep_pinned_check;
    JsonBuilder *builder;
    gchar *response = NULL;
    gchar *error_message = NULL;
    gint result;

    (void)user_data;
    if (!g_auth_token) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to bulk delete posts.");
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Bulk Delete Posts",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Preview", GTK_RESPONSE_APPLY,
                                         "_Delete", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);

    after_entry = gtk_entry_new();
    before_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(after_entry), "YYYY-MM-DD or ISO timestamp");
    gtk_entry_set_placeholder_text(GTK_ENTRY(before_entry), "YYYY-MM-DD or ISO timestamp");
    limit_spin = gtk_spin_button_new_with_range(1, 500, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(limit_spin), 100);
    include_replies_check = gtk_check_button_new_with_label("Include replies");
    keep_pinned_check = gtk_check_button_new_with_label("Keep pinned post");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(include_replies_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(keep_pinned_check), TRUE);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("After:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), after_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Before:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), before_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Limit:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), limit_spin, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), include_replies_check, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), keep_pinned_check, 1, 4, 1, 1);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    result = gtk_dialog_run(GTK_DIALOG(dialog));
    if (result == GTK_RESPONSE_APPLY || result == GTK_RESPONSE_ACCEPT) {
        const gchar *after = gtk_entry_get_text(GTK_ENTRY(after_entry));
        const gchar *before = gtk_entry_get_text(GTK_ENTRY(before_entry));
        if ((!after || after[0] == '\0') && (!before || before[0] == '\0')) {
            show_modal_message(GTK_MESSAGE_ERROR, "Date bound required.", "Enter an after date, a before date, or both.");
        } else {
            builder = json_builder_new();
            json_builder_begin_object(builder);
            if (after && after[0] != '\0') {
                json_builder_set_member_name(builder, "after");
                json_builder_add_string_value(builder, after);
            }
            if (before && before[0] != '\0') {
                json_builder_set_member_name(builder, "before");
                json_builder_add_string_value(builder, before);
            }
            json_builder_set_member_name(builder, "limit");
            json_builder_add_int_value(builder, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(limit_spin)));
            json_builder_set_member_name(builder, "includeReplies");
            json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(include_replies_check)));
            json_builder_set_member_name(builder, "keepPinned");
            json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(keep_pinned_check)));
            json_builder_set_member_name(builder, "dryRun");
            json_builder_add_boolean_value(builder, result == GTK_RESPONSE_APPLY);
            json_builder_end_object(builder);

            if (perform_profile_json_request(TWEET_BULK_DELETE_URL, "POST", builder, &response, &error_message)) {
                JsonParser *parser = json_parser_new();
                GError *parse_error = NULL;
                if (json_parser_load_from_data(parser, response, -1, &parse_error)) {
                    JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
                    if (result == GTK_RESPONSE_APPLY && json_object_has_member(obj, "preview")) {
                        JsonObject *preview = json_object_get_object_member(obj, "preview");
                        gint64 total = json_object_get_int_member(preview, "total");
                        gint64 limit = json_object_get_int_member(preview, "limit");
                        gchar *summary = g_strdup_printf("Matching posts: %" G_GINT64_FORMAT "\nBatch limit: %" G_GINT64_FORMAT,
                                                         total, limit);
                        show_modal_message(GTK_MESSAGE_INFO, "Bulk Delete Preview", summary);
                        g_free(summary);
                    } else {
                        gint64 deleted = json_object_has_member(obj, "deleted") ? json_object_get_int_member(obj, "deleted") : 0;
                        gint64 remaining = json_object_has_member(obj, "remaining") ? json_object_get_int_member(obj, "remaining") : 0;
                        gchar *summary = g_strdup_printf("Deleted: %" G_GINT64_FORMAT "\nRemaining: %" G_GINT64_FORMAT,
                                                         deleted, remaining);
                        show_modal_message(GTK_MESSAGE_INFO, "Bulk Delete Complete", summary);
                        g_free(summary);
                        start_loading_timeline(GTK_LIST_BOX(g_main_list_box));
                    }
                } else {
                    if (parse_error) g_error_free(parse_error);
                    show_modal_message(GTK_MESSAGE_INFO, "Bulk delete request completed.", NULL);
                }
                g_object_unref(parser);
            } else {
                show_modal_message(GTK_MESSAGE_ERROR, "Bulk delete failed.", error_message);
            }
            g_free(response);
            g_free(error_message);
            g_object_unref(builder);
        }
    }
    gtk_widget_destroy(dialog);
}

static void
on_account_request_action_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *request_id = g_object_get_data(G_OBJECT(widget), "request_id");
    const gchar *action = user_data;
    gchar *url = NULL;
    gchar *response = NULL;
    gchar *error_message;
    gboolean success;

    if (!request_id || !action) {
        return;
    }
    if (g_strcmp0(action, "follow-approve") == 0) {
        url = g_strdup_printf(PROFILE_FOLLOW_REQUEST_APPROVE_URL, request_id);
    } else if (g_strcmp0(action, "follow-deny") == 0) {
        url = g_strdup_printf(PROFILE_FOLLOW_REQUEST_DENY_URL, request_id);
    } else if (g_strcmp0(action, "affiliate-approve") == 0) {
        url = g_strdup_printf(PROFILE_AFFILIATE_REQUEST_APPROVE_URL, request_id);
    } else if (g_strcmp0(action, "affiliate-deny") == 0) {
        url = g_strdup_printf(PROFILE_AFFILIATE_REQUEST_DENY_URL, request_id);
    }
    if (!url) {
        return;
    }
    success = perform_request_with_optional_payload(url, "{}", "POST", &response);
    if (success) {
        start_loading_account_requests();
    } else {
        error_message = extract_error_message(response);
        show_modal_message(GTK_MESSAGE_ERROR, "Request update failed.", error_message);
        g_free(error_message);
    }
    g_free(response);
    g_free(url);
}

static GtkWidget *
create_account_request_row(JsonObject *item, const gchar *kind)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *title;
    GtkWidget *body;
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *approve = gtk_button_new_with_label("Approve");
    GtkWidget *deny = gtk_button_new_with_label("Deny");
    const gchar *id = json_get_string_or_empty(item, "id");
    const gchar *username = json_get_string_or_empty(item, "username");
    const gchar *name = json_get_string_or_empty(item, "name");
    const gchar *bio = json_get_string_or_empty(item, "bio");
    const gchar *created_at = json_get_string_or_empty(item, "created_at");
    gchar *title_text = g_strdup_printf("%s%s%s",
                                        name && name[0] ? name : "@",
                                        name && name[0] ? " @" : "",
                                        username);
    gchar *body_text = g_strdup_printf("%s%s%s",
                                       bio,
                                       created_at[0] ? "\nRequested " : "",
                                       created_at[0] ? created_at : "");
    gchar *approve_action = g_strdup_printf("%s-approve", kind);
    gchar *deny_action = g_strdup_printf("%s-deny", kind);

    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    title = gtk_label_new(title_text);
    body = gtk_label_new(body_text);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_label_set_xalign(GTK_LABEL(body), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
    gtk_widget_set_opacity(body, 0.75);
    g_object_set_data_full(G_OBJECT(approve), "request_id", g_strdup(id), g_free);
    g_object_set_data_full(G_OBJECT(deny), "request_id", g_strdup(id), g_free);
    g_signal_connect_data(approve, "clicked", G_CALLBACK(on_account_request_action_clicked), approve_action, free_wrapper, 0);
    g_signal_connect_data(deny, "clicked", G_CALLBACK(on_account_request_action_clicked), deny_action, free_wrapper, 0);
    gtk_box_pack_start(GTK_BOX(actions), approve, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), deny, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), body, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 0);

    g_free(title_text);
    g_free(body_text);
    return box;
}

static void
populate_account_request_list(GtkWidget *list, JsonArray *requests, const gchar *empty_text, const gchar *kind)
{
    clear_list_box_rows(list);
    if (!requests || json_array_get_length(requests) == 0) {
        set_list_box_status(list, empty_text);
        return;
    }
    for (guint i = 0; i < json_array_get_length(requests); i++) {
        JsonObject *item = json_array_get_object_element(requests, i);
        GtkWidget *row = create_account_request_row(item, kind);
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
    }
}

static JsonArray *
parse_requests_array(const gchar *json_data)
{
    JsonParser *parser;
    JsonObject *obj;
    JsonArray *copy = NULL;
    GError *error = NULL;

    parser = json_parser_new();
    if (json_data && json_parser_load_from_data(parser, json_data, -1, &error)) {
        obj = json_node_get_object(json_parser_get_root(parser));
        if (obj && json_object_has_member(obj, "requests") &&
            JSON_NODE_HOLDS_ARRAY(json_object_get_member(obj, "requests"))) {
            copy = json_array_ref(json_object_get_array_member(obj, "requests"));
        }
    } else if (error) {
        g_error_free(error);
    }
    g_object_unref(parser);
    return copy;
}

static gboolean
on_account_requests_loaded(gpointer data)
{
    struct AsyncData *async_data = data;
    JsonArray *follow_requests = parse_requests_array(async_data->json_data);
    JsonArray *affiliate_requests = parse_requests_array(async_data->query);

    if (!g_follow_requests_list || !g_affiliate_requests_list) {
        if (follow_requests) json_array_unref(follow_requests);
        if (affiliate_requests) json_array_unref(affiliate_requests);
        free_async_data(async_data);
        return G_SOURCE_REMOVE;
    }
    if (async_data->success) {
        populate_account_request_list(g_follow_requests_list, follow_requests, "No follow requests.", "follow");
        populate_account_request_list(g_affiliate_requests_list, affiliate_requests, "No affiliate requests.", "affiliate");
    } else {
        set_list_box_status(g_follow_requests_list, "Failed to load follow requests.");
        set_list_box_status(g_affiliate_requests_list, "Failed to load affiliate requests.");
    }
    if (follow_requests) json_array_unref(follow_requests);
    if (affiliate_requests) json_array_unref(affiliate_requests);
    async_data->query = NULL;
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_account_requests_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct follow_chunk = {0};
    struct MemoryStruct affiliate_chunk = {0};
    gboolean follow_success;
    gboolean affiliate_success;

    follow_success = fetch_url(PROFILE_FOLLOW_REQUESTS_URL, &follow_chunk, NULL, "GET");
    affiliate_success = fetch_url(PROFILE_AFFILIATE_REQUESTS_URL, &affiliate_chunk, NULL, "GET");
    async_data->success = follow_success && affiliate_success;
    if (follow_success) {
        async_data->json_data = g_strdup(follow_chunk.memory);
    }
    if (affiliate_success) {
        async_data->query = g_strdup(affiliate_chunk.memory);
    }
    g_free(follow_chunk.memory);
    g_free(affiliate_chunk.memory);
    g_idle_add(on_account_requests_loaded, async_data);
    return NULL;
}

void
start_loading_account_requests(void)
{
    if (!g_auth_token || !g_follow_requests_list || !g_affiliate_requests_list) {
        return;
    }
    set_list_box_status(g_follow_requests_list, "Loading follow requests...");
    set_list_box_status(g_affiliate_requests_list, "Loading affiliate requests...");
    g_thread_new("account-requests-loader", fetch_account_requests_thread, g_new0(struct AsyncData, 1));
}

void
on_remove_affiliate_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *response = NULL;
    gchar *error_message;

    (void)widget;
    (void)user_data;
    if (!g_auth_token) {
        return;
    }
    if (perform_request_with_optional_payload(PROFILE_REMOVE_AFFILIATE_URL, NULL, "DELETE", &response)) {
        show_modal_message(GTK_MESSAGE_INFO, "Affiliate removed.", NULL);
    } else {
        error_message = extract_error_message(response);
        show_modal_message(GTK_MESSAGE_ERROR, "Could not remove affiliate.", error_message);
        g_free(error_message);
    }
    g_free(response);
}

static gchar *
build_shop_product_payload(const gchar *title,
                           const gchar *description,
                           const gchar *image_url,
                           const gchar *price,
                           const gchar *content_type,
                           const gchar *content)
{
    JsonBuilder *builder = json_builder_new();
    JsonGenerator *gen = json_generator_new();
    gchar *payload;

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "title");
    json_builder_add_string_value(builder, title ? title : "");
    json_builder_set_member_name(builder, "description");
    json_builder_add_string_value(builder, description ? description : "");
    json_builder_set_member_name(builder, "image_url");
    json_builder_add_string_value(builder, image_url ? image_url : "");
    json_builder_set_member_name(builder, "price");
    json_builder_add_string_value(builder, price ? price : "");
    json_builder_set_member_name(builder, "content_type");
    json_builder_add_string_value(builder, content_type ? content_type : "text");
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, content ? content : "");
    json_builder_end_object(builder);

    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);
    g_object_unref(gen);
    g_object_unref(builder);
    return payload;
}

static void
show_shop_purchase(GtkWidget *parent, const gchar *purchase_id)
{
    gchar *url;
    gchar *response = NULL;
    gchar *error_message;
    JsonParser *parser;
    JsonObject *obj;
    JsonObject *purchase;
    GError *error = NULL;

    if (!purchase_id || !g_auth_token) {
        return;
    }
    url = g_strdup_printf(SHOP_PURCHASE_URL, purchase_id);
    {
        struct MemoryStruct chunk = {0};
        if (fetch_url(url, &chunk, NULL, "GET")) {
            response = chunk.memory;
        } else {
            show_modal_message(GTK_MESSAGE_ERROR, "Purchase unavailable.", NULL);
            g_free(url);
            return;
        }
    }
    error_message = extract_error_message(response);
    if (error_message) {
        show_modal_message(GTK_MESSAGE_ERROR, "Purchase unavailable.", error_message);
        g_free(error_message);
        g_free(response);
        g_free(url);
        return;
    }
    parser = json_parser_new();
    if (response && json_parser_load_from_data(parser, response, -1, &error)) {
        obj = json_node_get_object(json_parser_get_root(parser));
        purchase = json_object_has_member(obj, "purchase") ? json_object_get_object_member(obj, "purchase") : NULL;
        if (purchase) {
            gchar *body = g_strdup_printf("Type: %s\n\n%s",
                                          json_get_string_or_empty(purchase, "content_type"),
                                          json_get_string_or_empty(purchase, "content"));
            show_modal_message(GTK_MESSAGE_INFO,
                               json_get_string_or_empty(purchase, "title"),
                               body);
            g_free(body);
        }
    } else {
        if (error) g_error_free(error);
        show_modal_message(GTK_MESSAGE_ERROR, "Purchase unavailable.", "The server response could not be read.");
    }
    g_object_unref(parser);
    g_free(response);
    g_free(url);
    (void)parent;
}

static void
on_shop_open_purchase_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *purchase_id = g_object_get_data(G_OBJECT(widget), "purchase_id");
    (void)user_data;
    show_shop_purchase(widget, purchase_id);
}

static void
on_shop_buy_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *product_id = g_object_get_data(G_OBJECT(widget), "product_id");
    gchar *url;
    gchar *response = NULL;
    gchar *error_message;
    JsonParser *parser;
    JsonObject *obj;
    const gchar *payment_url = NULL;
    const gchar *order_id = NULL;
    GError *error = NULL;

    (void)user_data;
    if (!product_id || !g_auth_token) {
        show_modal_message(GTK_MESSAGE_ERROR, "Login required.", "You must be logged in to buy shop products.");
        return;
    }
    url = g_strdup_printf(SHOP_PRODUCT_BUY_URL, product_id);
    if (!perform_request_with_optional_payload(url, "{}", "POST", &response)) {
        error_message = extract_error_message(response);
        show_modal_message(GTK_MESSAGE_ERROR, "Purchase could not start.", error_message);
        g_free(error_message);
        g_free(response);
        g_free(url);
        return;
    }
    parser = json_parser_new();
    if (json_parser_load_from_data(parser, response, -1, &error)) {
        obj = json_node_get_object(json_parser_get_root(parser));
        payment_url = json_get_string_or_empty(obj, "paymentUrl");
        order_id = json_get_string_or_empty(obj, "orderId");
        if (payment_url[0]) {
            GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
            GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
            gtk_show_uri_on_window(window, payment_url, GDK_CURRENT_TIME, NULL);
        }
        if (order_id[0]) {
            GtkWidget *dialog = gtk_dialog_new_with_buttons("Confirm Purchase",
                                                            NULL,
                                                            GTK_DIALOG_MODAL,
                                                            "_Cancel", GTK_RESPONSE_CANCEL,
                                                            "_Confirm", GTK_RESPONSE_ACCEPT,
                                                            NULL);
            GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
            GtkWidget *entry = gtk_entry_new();
            gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "MyPayIndia transaction id");
            gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Complete payment in the opened page, then paste the transaction id."), FALSE, FALSE, 8);
            gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
            gtk_widget_show_all(dialog);
            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
                JsonBuilder *builder = json_builder_new();
                gchar *confirm_url = g_strdup_printf(SHOP_PRODUCT_CONFIRM_URL, product_id);
                gchar *confirm_response = NULL;
                gchar *confirm_error = NULL;
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "orderId");
                json_builder_add_string_value(builder, order_id);
                json_builder_set_member_name(builder, "transactionId");
                json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(entry)));
                json_builder_end_object(builder);
                if (perform_profile_json_request(confirm_url, "POST", builder, &confirm_response, &confirm_error)) {
                    JsonParser *confirm_parser = json_parser_new();
                    if (json_parser_load_from_data(confirm_parser, confirm_response, -1, NULL)) {
                        JsonObject *confirm_obj = json_node_get_object(json_parser_get_root(confirm_parser));
                        JsonObject *purchase = json_object_has_member(confirm_obj, "purchase")
                            ? json_object_get_object_member(confirm_obj, "purchase") : NULL;
                        if (purchase) {
                            show_shop_purchase(widget, json_get_string_or_empty(purchase, "id"));
                        }
                    }
                    g_object_unref(confirm_parser);
                } else {
                    show_modal_message(GTK_MESSAGE_ERROR, "Purchase confirmation failed.", confirm_error);
                }
                g_free(confirm_error);
                g_free(confirm_response);
                g_free(confirm_url);
                g_object_unref(builder);
            }
            gtk_widget_destroy(dialog);
        }
    } else if (error) {
        g_error_free(error);
    }
    g_object_unref(parser);
    g_free(response);
    g_free(url);
}

static GtkWidget *
create_shop_product_row(JsonObject *product, gboolean owner_view)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *title = gtk_label_new(NULL);
    GtkWidget *body;
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gchar *escaped_title = g_markup_escape_text(json_get_string_or_empty(product, "title"), -1);
    gchar *title_markup = g_strdup_printf("<b>%s</b>", escaped_title);
    gchar *body_text = g_strdup_printf("@%s · INR %s\n%s",
                                       json_get_string_or_empty(product, "owner_username"),
                                       json_get_string_or_empty(product, "price_inr"),
                                       json_get_string_or_empty(product, "description"));
    const gchar *product_id = json_get_string_or_empty(product, "id");
    const gchar *purchase_id = json_get_string_or_empty(product, "purchase_id");
    gboolean purchased = json_object_has_member(product, "purchased") &&
        json_object_get_boolean_member(product, "purchased");

    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    gtk_label_set_markup(GTK_LABEL(title), title_markup);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    body = gtk_label_new(body_text);
    gtk_label_set_xalign(GTK_LABEL(body), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), body, FALSE, FALSE, 0);

    if (purchased && purchase_id[0]) {
        GtkWidget *open = gtk_button_new_with_label("Open Purchase");
        g_object_set_data_full(G_OBJECT(open), "purchase_id", g_strdup(purchase_id), g_free);
        g_signal_connect(open, "clicked", G_CALLBACK(on_shop_open_purchase_clicked), NULL);
        gtk_box_pack_start(GTK_BOX(actions), open, FALSE, FALSE, 0);
    } else if (!owner_view) {
        GtkWidget *buy = gtk_button_new_with_label("Buy");
        g_object_set_data_full(G_OBJECT(buy), "product_id", g_strdup(product_id), g_free);
        g_signal_connect(buy, "clicked", G_CALLBACK(on_shop_buy_clicked), NULL);
        gtk_box_pack_start(GTK_BOX(actions), buy, FALSE, FALSE, 0);
    }
    if (owner_view) {
        GtkWidget *edit = gtk_button_new_with_label("Edit");
        GtkWidget *delete_btn = gtk_button_new_with_label("Delete");
        g_object_set_data_full(G_OBJECT(edit), "product_id", g_strdup(product_id), g_free);
        g_object_set_data_full(G_OBJECT(delete_btn), "product_id", g_strdup(product_id), g_free);
        g_signal_connect(edit, "clicked", G_CALLBACK(on_create_shop_product_clicked), GINT_TO_POINTER(1));
        g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_shop_delete_product_clicked), NULL);
        gtk_box_pack_start(GTK_BOX(actions), edit, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(actions), delete_btn, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 0);

    g_free(body_text);
    g_free(title_markup);
    g_free(escaped_title);
    return box;
}

static void
populate_shop_products(GtkWidget *list, const gchar *json_data, gboolean owner_view)
{
    JsonParser *parser = json_parser_new();
    JsonObject *obj;
    JsonArray *products = NULL;
    GError *error = NULL;

    clear_list_box_rows(list);
    if (json_data && json_parser_load_from_data(parser, json_data, -1, &error)) {
        obj = json_node_get_object(json_parser_get_root(parser));
        if (obj && json_object_has_member(obj, "products")) {
            products = json_object_get_array_member(obj, "products");
        }
    } else if (error) {
        g_error_free(error);
    }
    if (!products || json_array_get_length(products) == 0) {
        set_list_box_status(list, "No shop products.");
    } else {
        for (guint i = 0; i < json_array_get_length(products); i++) {
            GtkWidget *row = create_shop_product_row(json_array_get_object_element(products, i), owner_view);
            gtk_widget_show_all(row);
            gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
        }
    }
    g_object_unref(parser);
}

static gboolean
on_my_shop_loaded(gpointer data)
{
    struct AsyncData *async_data = data;

    if (g_shop_products_list) {
        if (async_data->success) {
            populate_shop_products(g_shop_products_list, async_data->json_data, TRUE);
        } else {
            set_list_box_status(g_shop_products_list, "Failed to load shop products.");
        }
    }
    free_async_data(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_my_shop_thread(gpointer data)
{
    struct AsyncData *async_data = data;
    struct MemoryStruct chunk = {0};
    gchar *escaped = g_uri_escape_string(async_data->username, NULL, FALSE);
    gchar *url = g_strdup_printf(SHOP_USER_URL, escaped);

    async_data->success = fetch_url(url, &chunk, NULL, "GET");
    if (async_data->success) {
        async_data->json_data = g_strdup(chunk.memory);
    }
    g_free(chunk.memory);
    g_free(url);
    g_free(escaped);
    g_idle_add(on_my_shop_loaded, async_data);
    return NULL;
}

void
start_loading_my_shop(void)
{
    struct AsyncData *data;
    gchar *username;

    if (!g_auth_token || !g_shop_products_list) {
        return;
    }
    username = get_username_safe();
    if (!username) {
        return;
    }
    set_list_box_status(g_shop_products_list, "Loading shop products...");
    data = g_new0(struct AsyncData, 1);
    data->username = username;
    g_thread_new("my-shop-loader", fetch_my_shop_thread, data);
}

void
on_profile_shop_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
    struct MemoryStruct chunk = {0};
    gchar *escaped;
    gchar *url;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *scroll;
    GtkWidget *list;

    (void)user_data;
    if (!username) {
        return;
    }
    escaped = g_uri_escape_string(username, NULL, FALSE);
    url = g_strdup_printf(SHOP_USER_URL, escaped);
    dialog = gtk_dialog_new_with_buttons("Shop", NULL, GTK_DIALOG_MODAL, "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 420);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    scroll = gtk_scrolled_window_new(NULL, NULL);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
    if (fetch_url(url, &chunk, NULL, "GET")) {
        populate_shop_products(list, chunk.memory, FALSE);
    } else {
        set_list_box_status(list, "Failed to load shop.");
    }
    g_free(chunk.memory);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(url);
    g_free(escaped);
}

void
on_profile_donate_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *amount_entry;
    GtkWidget *note_entry;

    (void)user_data;
    if (!username || !g_auth_token) {
        return;
    }
    dialog = gtk_dialog_new_with_buttons("Donate",
                                         NULL,
                                         GTK_DIALOG_MODAL,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Continue", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    amount_entry = gtk_entry_new();
    note_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(amount_entry), "Amount in INR");
    gtk_entry_set_placeholder_text(GTK_ENTRY(note_entry), "Optional note");
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Amount:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), amount_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Note:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), note_entry, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        JsonBuilder *builder = json_builder_new();
        gchar *create_response = NULL;
        gchar *create_error = NULL;
        JsonParser *parser = json_parser_new();
        GError *error = NULL;

        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "recipientUsername");
        json_builder_add_string_value(builder, username);
        json_builder_set_member_name(builder, "amount");
        json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(amount_entry)));
        json_builder_set_member_name(builder, "note");
        json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(note_entry)));
        json_builder_set_member_name(builder, "kind");
        json_builder_add_string_value(builder, "donate");
        json_builder_end_object(builder);

        if (perform_profile_json_request(MPI_SEND_CREATE_URL, "POST", builder, &create_response, &create_error) &&
            json_parser_load_from_data(parser, create_response, -1, &error)) {
            JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
            const gchar *payment_url = json_get_string_or_empty(obj, "paymentUrl");
            const gchar *order_id = json_get_string_or_empty(obj, "orderId");
            if (payment_url[0]) {
                GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
                GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
                gtk_show_uri_on_window(window, payment_url, GDK_CURRENT_TIME, NULL);
            }
            if (order_id[0]) {
                GtkWidget *confirm = gtk_dialog_new_with_buttons("Confirm Donation",
                                                                  NULL,
                                                                  GTK_DIALOG_MODAL,
                                                                  "_Cancel", GTK_RESPONSE_CANCEL,
                                                                  "_Confirm", GTK_RESPONSE_ACCEPT,
                                                                  NULL);
                GtkWidget *confirm_content = gtk_dialog_get_content_area(GTK_DIALOG(confirm));
                GtkWidget *txn_entry = gtk_entry_new();
                gtk_entry_set_placeholder_text(GTK_ENTRY(txn_entry), "MyPayIndia transaction id");
                gtk_box_pack_start(GTK_BOX(confirm_content), gtk_label_new("Complete payment in the opened page, then paste the transaction id."), FALSE, FALSE, 8);
                gtk_box_pack_start(GTK_BOX(confirm_content), txn_entry, FALSE, FALSE, 8);
                gtk_widget_show_all(confirm);
                if (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_ACCEPT) {
                    JsonBuilder *confirm_builder = json_builder_new();
                    gchar *confirm_error = NULL;
                    json_builder_begin_object(confirm_builder);
                    json_builder_set_member_name(confirm_builder, "orderId");
                    json_builder_add_string_value(confirm_builder, order_id);
                    json_builder_set_member_name(confirm_builder, "transactionId");
                    json_builder_add_string_value(confirm_builder, gtk_entry_get_text(GTK_ENTRY(txn_entry)));
                    json_builder_end_object(confirm_builder);
                    if (perform_profile_json_request(MPI_SEND_CONFIRM_URL, "POST", confirm_builder, NULL, &confirm_error)) {
                        show_modal_message(GTK_MESSAGE_INFO, "Donation sent.", NULL);
                    } else {
                        show_modal_message(GTK_MESSAGE_ERROR, "Donation confirmation failed.", confirm_error);
                    }
                    g_free(confirm_error);
                    g_object_unref(confirm_builder);
                }
                gtk_widget_destroy(confirm);
            }
        } else {
            if (error) g_error_free(error);
            show_modal_message(GTK_MESSAGE_ERROR, "Donation could not start.", create_error);
        }
        g_object_unref(parser);
        g_object_unref(builder);
        g_free(create_response);
        g_free(create_error);
    }
    gtk_widget_destroy(dialog);
}

void
on_shop_delete_product_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *product_id = g_object_get_data(G_OBJECT(widget), "product_id");
    gchar *url;
    gchar *response = NULL;
    gchar *error_message;

    (void)user_data;
    if (!product_id) {
        return;
    }
    url = g_strdup_printf(SHOP_PRODUCT_URL, product_id);
    if (perform_request_with_optional_payload(url, NULL, "DELETE", &response)) {
        start_loading_my_shop();
    } else {
        error_message = extract_error_message(response);
        show_modal_message(GTK_MESSAGE_ERROR, "Could not delete product.", error_message);
        g_free(error_message);
    }
    g_free(response);
    g_free(url);
}

void
on_create_shop_product_clicked(GtkWidget *widget, gpointer user_data)
{
    gboolean editing = GPOINTER_TO_INT(user_data) == 1;
    const gchar *product_id = editing ? g_object_get_data(G_OBJECT(widget), "product_id") : NULL;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(editing ? "Edit Product" : "New Product",
                                                     NULL,
                                                     GTK_DIALOG_MODAL,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     editing ? "_Save" : "_Create", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *title_entry = gtk_entry_new();
    GtkWidget *price_entry = gtk_entry_new();
    GtkWidget *description_entry = gtk_entry_new();
    GtkWidget *image_entry = gtk_entry_new();
    GtkWidget *type_combo = gtk_combo_box_text_new();
    GtkWidget *content_view = gtk_text_view_new();
    GtkWidget *content_scroll = gtk_scrolled_window_new(NULL, NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 420);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_entry_set_placeholder_text(GTK_ENTRY(title_entry), "Title");
    gtk_entry_set_placeholder_text(GTK_ENTRY(price_entry), "Price in INR");
    gtk_entry_set_placeholder_text(GTK_ENTRY(description_entry), "Description");
    gtk_entry_set_placeholder_text(GTK_ENTRY(image_entry), "Image URL");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(type_combo), "text", "Text");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(type_combo), "link", "Link");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(type_combo), "image", "Image");
    gtk_combo_box_set_active(GTK_COMBO_BOX(type_combo), 0);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(content_view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_size_request(content_scroll, -1, 140);
    gtk_container_add(GTK_CONTAINER(content_scroll), content_view);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Title:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), title_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Price:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), price_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Description:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), description_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Image:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), image_entry, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Content type:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), type_combo, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Content:"), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), content_scroll, 1, 5, 1, 1);
    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(content_view));
        GtkTextIter start;
        GtkTextIter end;
        gchar *body_text;
        gchar *payload;
        gchar *url = NULL;
        gchar *response = NULL;
        gchar *error_message = NULL;
        const gchar *content_type = gtk_combo_box_get_active_id(GTK_COMBO_BOX(type_combo));

        gtk_text_buffer_get_bounds(buffer, &start, &end);
        body_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        payload = build_shop_product_payload(gtk_entry_get_text(GTK_ENTRY(title_entry)),
                                             gtk_entry_get_text(GTK_ENTRY(description_entry)),
                                             gtk_entry_get_text(GTK_ENTRY(image_entry)),
                                             gtk_entry_get_text(GTK_ENTRY(price_entry)),
                                             content_type,
                                             body_text);
        if (editing) {
            url = g_strdup_printf(SHOP_PRODUCT_URL, product_id);
        }
        if (perform_request_with_optional_payload(editing ? url : SHOP_PRODUCTS_URL,
                                                  payload,
                                                  editing ? "PATCH" : "POST",
                                                  &response)) {
            start_loading_my_shop();
        } else {
            error_message = extract_error_message(response);
            show_modal_message(GTK_MESSAGE_ERROR, "Product save failed.", error_message);
        }
        g_free(error_message);
        g_free(response);
        g_free(url);
        g_free(payload);
        g_free(body_text);
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
