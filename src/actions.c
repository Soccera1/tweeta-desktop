#include <gtk/gtk.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include "actions.h"
#include "constants.h"
#include "types.h"
#include "globals.h"
#include "network.h"
#include "json_utils.h"
#include "session.h"
#include "ui_utils.h"
#include "ui_components.h"
#include "views.h"

// Request tracking to prevent double-unref and race conditions
static GMutex load_tweets_mutex;
static guint active_tweets_request_id = 0;

void update_login_ui()
{
    if (g_current_username) {
        gchar *label_text = g_strdup_printf("Logged in as @%s", g_current_username);
        gtk_label_set_text(GTK_LABEL(g_user_label), label_text);
        gtk_button_set_label(GTK_BUTTON(g_login_button), "Logout");
        gtk_widget_set_sensitive(g_compose_button, TRUE);
        g_free(label_text);
    } else {
        gtk_label_set_text(GTK_LABEL(g_user_label), "Not logged in");
        gtk_button_set_label(GTK_BUTTON(g_login_button), "Login");
        gtk_widget_set_sensitive(g_compose_button, FALSE);
    }
}

void perform_logout()
{
    clear_session();
    g_free(g_auth_token);
    g_auth_token = NULL;
    g_free(g_current_username);
    g_current_username = NULL;
    update_login_ui();
}

gboolean perform_login(const gchar *username, const gchar *password)
{
    struct MemoryStruct chunk;
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

    if (fetch_url(LOGIN_URL, &chunk, post_data)) {
        gchar *token = NULL;
        gchar *uname = NULL;
        if (parse_login_response(chunk.memory, &token, &uname)) {
            g_free(g_auth_token);
            g_auth_token = token;
            g_free(g_current_username);
            g_current_username = uname;
            save_session(g_auth_token, g_current_username);
            success = TRUE;
        }
        free(chunk.memory);
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

static gboolean perform_post_tweet(const gchar *content, const gchar *reply_to_id)
{
    struct MemoryStruct chunk;
    gboolean success = FALSE;
    gchar *post_data = construct_tweet_payload(content, reply_to_id);

    if (fetch_url(POST_TWEET_URL, &chunk, post_data)) {
        success = TRUE;
        free(chunk.memory);
    }
    
    g_free(post_data);
    return success;
}

void on_compose_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar *content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        if (content && strlen(content) > 0) {
             if (perform_post_tweet(content, NULL)) {
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
        }
        g_free(content);
    }
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

    gtk_widget_show_all(dialog);
    g_signal_connect(dialog, "response", G_CALLBACK(on_compose_response), text_view);
}

static gboolean on_tweets_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    // Check if this is still the active request
    g_mutex_lock(&load_tweets_mutex);
    gboolean is_active = (async_data->request_id == active_tweets_request_id);
    g_mutex_unlock(&load_tweets_mutex);
    
    if (!is_active) {
        // This request was superseded, discard it
        if (async_data->tweets) {
            free_tweets(async_data->tweets);
        }
        g_free(async_data);
        return G_SOURCE_REMOVE;
    }
    
    if (async_data->success && async_data->tweets) {
        populate_tweet_list(async_data->list_box, async_data->tweets);
        free_tweets(async_data->tweets);
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new("Failed to load tweets.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_tweets_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk;

    if (fetch_url(PUBLIC_TWEETS_URL, &chunk, NULL)) {
        async_data->tweets = parse_tweets(chunk.memory);
        async_data->success = (async_data->tweets != NULL);
        free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    g_idle_add(on_tweets_loaded, async_data);
    return NULL;
}

void start_loading_tweets(GtkListBox *list_box)
{
    g_mutex_lock(&load_tweets_mutex);
    // Increment request ID to invalidate any pending requests
    active_tweets_request_id++;
    guint current_request_id = active_tweets_request_id;
    g_mutex_unlock(&load_tweets_mutex);
    
    // Clear the list and show loading indicator
    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);
    
    GtkWidget *loading_label = gtk_label_new("Loading tweets...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->request_id = current_request_id;
    
    g_thread_new("tweet-loader", fetch_tweets_thread, data);
}

static gboolean on_profile_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    if (async_data->success && async_data->profile) {
        gchar *stats_str = g_strdup_printf("%d Followers · %d Following · %d Posts", 
                                          async_data->profile->follower_count,
                                          async_data->profile->following_count,
                                          async_data->profile->post_count);
        
        gtk_label_set_text(GTK_LABEL(g_profile_name_label), async_data->profile->name);
        gtk_label_set_text(GTK_LABEL(g_profile_bio_label), async_data->profile->bio ? async_data->profile->bio : "");
        gtk_label_set_text(GTK_LABEL(g_profile_stats_label), stats_str);
        g_free(stats_str);

        gtk_image_set_from_icon_name(GTK_IMAGE(g_profile_avatar_image), "avatar-default", GTK_ICON_SIZE_DND);
        if (async_data->profile->avatar) {
            load_avatar(g_profile_avatar_image, async_data->profile->avatar, 80);
        }

        if (async_data->tweets) {
            populate_tweet_list(GTK_LIST_BOX(g_profile_tweets_list), async_data->tweets);
            free_tweets(async_data->tweets);
        }
    } else {
        gtk_label_set_text(GTK_LABEL(g_profile_name_label), "Error loading profile");
    }

    if (async_data->profile) {
        g_free(async_data->profile->name);
        g_free(async_data->profile->username);
        g_free(async_data->profile->bio);
        g_free(async_data->profile->avatar);
        g_free(async_data->profile);
    }
    g_free(async_data->username);
    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gboolean on_profile_replies_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    if (async_data->success && async_data->tweets) {
        populate_tweet_list(GTK_LIST_BOX(g_profile_replies_list), async_data->tweets);
        free_tweets(async_data->tweets);
    }
    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_profile_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk;
    gchar *url = g_strdup_printf("%s/profile/%s", API_BASE_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL)) {
        async_data->profile = parse_profile(chunk.memory);
        async_data->tweets = parse_tweets(chunk.memory);
        async_data->success = (async_data->profile != NULL);
        free(chunk.memory);
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
    struct MemoryStruct chunk;
    gchar *url = g_strdup_printf("%s/profile/%s/replies", API_BASE_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL)) {
        async_data->tweets = parse_profile_replies(chunk.memory);
        async_data->success = (async_data->tweets != NULL);
        free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }
    g_free(url);
    g_free(async_data->username);

    g_idle_add(on_profile_replies_loaded, async_data);
    return NULL;
}

void show_profile(const gchar *username)
{
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "profile");
    gtk_widget_show(g_back_button);

    gtk_label_set_text(GTK_LABEL(g_profile_name_label), "Loading...");
    gtk_label_set_text(GTK_LABEL(g_profile_bio_label), "");
    gtk_label_set_text(GTK_LABEL(g_profile_stats_label), "");
    
    populate_tweet_list(GTK_LIST_BOX(g_profile_tweets_list), NULL);
    populate_tweet_list(GTK_LIST_BOX(g_profile_replies_list), NULL);

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
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "timeline");
    gtk_widget_hide(g_back_button);
}

void on_refresh_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
}

static gboolean on_users_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    if (async_data->success && async_data->users) {
        populate_user_list(async_data->list_box, async_data->users);
        free_users(async_data->users);
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new("No users found.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    g_free(async_data->query);
    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gboolean on_search_tweets_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    if (async_data->success && async_data->tweets) {
        populate_tweet_list(async_data->list_box, async_data->tweets);
        free_tweets(async_data->tweets);
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new("No tweets found.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    g_free(async_data->query);
    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_search_users_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk;
    gchar *escaped_query = g_uri_escape_string(async_data->query, NULL, FALSE);
    gchar *url = g_strdup_printf("%s?q=%s", SEARCH_USERS_URL, escaped_query);
    g_free(escaped_query);

    if (fetch_url(url, &chunk, NULL)) {
        async_data->users = parse_users(chunk.memory);
        async_data->success = TRUE;
        free(chunk.memory);
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
    struct MemoryStruct chunk;
    gchar *escaped_query = g_uri_escape_string(async_data->query, NULL, FALSE);
    gchar *url = g_strdup_printf("%s?q=%s", SEARCH_POSTS_URL, escaped_query);
    g_free(escaped_query);

    if (fetch_url(url, &chunk, NULL)) {
        async_data->tweets = parse_tweets(chunk.memory);
        async_data->success = TRUE;
        free(chunk.memory);
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
    struct MemoryStruct chunk;
    gboolean success = FALSE;
    gchar *url = g_strdup_printf(LIKE_TWEET_URL, tweet_id);

    if (fetch_url(url, &chunk, "{}")) {
        success = TRUE;
        free(chunk.memory);
    }

    g_free(url);
    return success;
}

gboolean perform_retweet(const gchar *tweet_id)
{
    struct MemoryStruct chunk;
    gboolean success = FALSE;
    gchar *url = g_strdup_printf(RETWEET_URL, tweet_id);

    if (fetch_url(url, &chunk, "{}")) {
        success = TRUE;
        free(chunk.memory);
    }

    g_free(url);
    return success;
}

gboolean perform_bookmark(const gchar *tweet_id, gboolean add)
{
    struct MemoryStruct chunk;
    gboolean success = FALSE;
    const gchar *url = add ? BOOKMARK_ADD_URL : BOOKMARK_REMOVE_URL;

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "postId");
    json_builder_add_string_value(builder, tweet_id);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    if (fetch_url(url, &chunk, post_data)) {
        success = TRUE;
        free(chunk.memory);
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);
    return success;
}

gboolean perform_reaction(const gchar *tweet_id, const gchar *emoji)
{
    struct MemoryStruct chunk;
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

    if (fetch_url(url, &chunk, post_data)) {
        success = TRUE;
        free(chunk.memory);
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);
    g_free(url);
    return success;
}

static void free_emoji(gpointer data)
{
    struct Emoji *emoji = data;
    if (emoji) {
        g_free(emoji->id);
        g_free(emoji->name);
        g_free(emoji->file_url);
        g_free(emoji);
    }
}

void free_emojis(GList *emojis)
{
    g_list_free_full(emojis, free_emoji);
}

GList* fetch_emojis(void)
{
    struct MemoryStruct chunk;
    GList *emojis = NULL;

    if (fetch_url(EMOJIS_URL, &chunk, NULL)) {
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
                    emojis = g_list_append(emojis, emoji);
                }
            }
        } else {
            g_error_free(error);
        }
        g_object_unref(parser);
        free(chunk.memory);
    }

    return emojis;
}
