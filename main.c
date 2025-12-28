/*
 * Tweetapus GTK Client
 *
 * A minimal GTK3 X11 client in C for tweetapus.
 *
 * (c) 2025 Lily
 * Licensed under the AGPLv3 license
 */

#include <gtk/gtk.h>
#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <glib/gstdio.h>

#define API_BASE_URL "https://tweeta.tiago.zip/api"
#define BASE_DOMAIN "https://tweeta.tiago.zip"
#define AVATAR_SIZE 48
#define PUBLIC_TWEETS_URL API_BASE_URL "/public-tweets"
#define LOGIN_URL API_BASE_URL "/auth/basic-login"
#define POST_TWEET_URL API_BASE_URL "/tweets/"
#define SEARCH_USERS_URL API_BASE_URL "/search/users"
#define SEARCH_POSTS_URL API_BASE_URL "/search/posts"

// Global Auth State
static gchar *g_auth_token = NULL;
static gchar *g_current_username = NULL;
static GtkWidget *g_login_button = NULL;
static GtkWidget *g_compose_button = NULL;
static GtkWidget *g_user_label = NULL;
static GtkWidget *g_main_list_box = NULL;
static GtkWidget *g_stack = NULL;
static GtkWidget *g_back_button = NULL;

// Search widgets
static GtkWidget *g_search_entry = NULL;
static GtkWidget *g_search_users_list = NULL;
static GtkWidget *g_search_tweets_list = NULL;

// Profile widgets
static GtkWidget *g_profile_name_label = NULL;
static GtkWidget *g_profile_bio_label = NULL;
static GtkWidget *g_profile_stats_label = NULL;
static GtkWidget *g_profile_avatar_image = NULL;
static GtkWidget *g_profile_tweets_list = NULL;
static GtkWidget *g_profile_replies_list = NULL;

// Represents a single tweet
struct Tweet {
  gchar *content;
  gchar *author_name;
  gchar *author_username;
  gchar *author_avatar;
  gchar *id;
};

// Represents profile data
struct Profile {
    gchar *name;
    gchar *username;
    gchar *bio;
    gchar *avatar;
    int follower_count;
    int following_count;
    int post_count;
};

// Memory buffer for curl
struct MemoryStruct {
  char *memory;
  size_t size;
};

// Data to pass between threads
struct AsyncData {
    GtkListBox *list_box;
    GList *tweets;
    GList *users;
    gboolean success;
    struct Profile *profile;
    gchar *username;
    gchar *query;
};

static void start_loading_tweets(GtkListBox *list_box);
static void show_profile(const gchar *username);
static gboolean fetch_url(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data);

struct AvatarData {
    GtkWidget *image;
    gchar *url;
    int size;
};

static gboolean
set_image_pixbuf(gpointer data)
{
    GdkPixbuf *pixbuf = (GdkPixbuf *)((gpointer *)data)[0];
    GtkWidget *image = (GtkWidget *)((gpointer *)data)[1];

    if (GTK_IS_IMAGE(image)) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
    }

    g_object_unref(pixbuf);
    g_object_unref(image);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_avatar_thread(gpointer data)
{
    struct AvatarData *avatar_data = (struct AvatarData *)data;
    struct MemoryStruct chunk;
    
    gchar *full_url;
    if (g_str_has_prefix(avatar_data->url, "http")) {
        full_url = g_strdup(avatar_data->url);
    } else {
        full_url = g_strdup_printf("%s%s", BASE_DOMAIN, avatar_data->url);
    }

    if (fetch_url(full_url, &chunk, NULL)) {
        GInputStream *stream = g_memory_input_stream_new_from_data(chunk.memory, chunk.size, NULL);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale(stream, avatar_data->size, avatar_data->size, TRUE, NULL, NULL);
        
        if (pixbuf) {
            gpointer *params = g_new(gpointer, 2);
            params[0] = pixbuf; // Already has ref from creation
            params[1] = g_object_ref(avatar_data->image);
            g_idle_add(set_image_pixbuf, params);
        }
        g_object_unref(stream);
        free(chunk.memory);
    }

    g_free(full_url);
    g_free(avatar_data->url);
    g_free(avatar_data);
    return NULL;
}

static void
load_avatar(GtkWidget *image, const gchar *url, int size)
{
    if (!url || strlen(url) == 0) return;

    struct AvatarData *data = g_new(struct AvatarData, 1);
    data->image = image;
    data->url = g_strdup(url);
    data->size = size;

    g_thread_new("avatar-loader", fetch_avatar_thread, data);
}

static void
on_author_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    const gchar *username = g_object_get_data(G_OBJECT(button), "username");
    show_profile(username);
}

static gchar*
get_config_path()
{
    const gchar *config_dir = g_get_user_config_dir();
    gchar *app_dir = g_build_filename(config_dir, "tweetapus-gtk", NULL);
    
    if (g_mkdir_with_parents(app_dir, 0700) == -1) {
        g_warning("Failed to create config directory: %s", app_dir);
    }

    gchar *config_path = g_build_filename(app_dir, "session.json", NULL);
    g_free(app_dir);
    return config_path;
}

static void
save_session(const gchar *token, const gchar *username)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "token");
    json_builder_add_string_value(builder, token);
    json_builder_set_member_name(builder, "username");
    json_builder_add_string_value(builder, username);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *data = json_generator_to_data(gen, NULL);
    
    gchar *path = get_config_path();
    GError *error = NULL;
    if (!g_file_set_contents(path, data, -1, &error)) {
        g_warning("Failed to save session: %s", error->message);
        g_error_free(error);
    }

    g_free(path);
    g_free(data);
    g_object_unref(gen);
    g_object_unref(builder);
}

static void
clear_session()
{
    gchar *path = get_config_path();
    g_unlink(path);
    g_free(path);
}

static void
load_session()
{
    gchar *path = get_config_path();
    gchar *data = NULL;
    GError *error = NULL;

    if (g_file_get_contents(path, &data, NULL, &error)) {
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, data, -1, NULL)) {
            JsonNode *root = json_parser_get_root(parser);
            JsonObject *obj = json_node_get_object(root);
            
            if (json_object_has_member(obj, "token") && json_object_has_member(obj, "username")) {
                g_free(g_auth_token);
                g_free(g_current_username);
                g_auth_token = g_strdup(json_object_get_string_member(obj, "token"));
                g_current_username = g_strdup(json_object_get_string_member(obj, "username"));
            }
        }
        g_object_unref(parser);
        g_free(data);
    } else {
        if (error) g_error_free(error);
    }
    g_free(path);
}

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    g_critical("not enough memory (realloc returned NULL)");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

static gboolean
fetch_url(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data)
{
    CURL *curl_handle;
    CURLcode res;
    struct curl_slist *headers = NULL;

    chunk->memory = malloc(1);
    chunk->size = 0;

    curl_handle = curl_easy_init();
    if (!curl_handle) {
        g_critical("curl_easy_init() failed");
        free(chunk->memory);
        chunk->memory = NULL;
        return FALSE;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (g_auth_token) {
        gchar *auth_header = g_strdup_printf("Authorization: Bearer %s", g_auth_token);
        headers = curl_slist_append(headers, auth_header);
        g_free(auth_header);
    }
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

    if (post_data) {
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_data);
    }

    res = curl_easy_perform(curl_handle);

    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        g_critical("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        free(chunk->memory);
        chunk->memory = NULL;
        chunk->size = 0;
        curl_easy_cleanup(curl_handle);
        return FALSE;
    }

    curl_easy_cleanup(curl_handle);
    return TRUE;
}

static void start_loading_tweets(GtkListBox *list_box);

static gchar*
construct_tweet_payload(const gchar *content, const gchar *reply_to_id)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, content);

    if (reply_to_id) {
        json_builder_set_member_name(builder, "reply_to");
        json_builder_add_string_value(builder, reply_to_id);
    }

    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    g_object_unref(gen);
    g_object_unref(builder);
    return post_data;
}

static gboolean
perform_post_tweet(const gchar *content, const gchar *reply_to_id)
{
    struct MemoryStruct chunk;
    gboolean success = FALSE;
    gchar *post_data = construct_tweet_payload(content, reply_to_id);

    if (fetch_url(POST_TWEET_URL, &chunk, post_data)) {
        // We expect a success response, maybe check for "success": true
        success = TRUE;
        free(chunk.memory);
    }
    
    g_free(post_data);
    return success;
}

struct ReplyContext {
    GtkWidget *text_view;
    gchar *reply_to_id;
};

static void
on_reply_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    struct ReplyContext *ctx = (struct ReplyContext *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->text_view));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar *content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        if (content && strlen(content) > 0) {
             if (perform_post_tweet(content, ctx->reply_to_id)) {
                start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
             } else {
                GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_CLOSE,
                                         "Failed to post reply.");
                gtk_dialog_run(GTK_DIALOG(error_dialog));
                gtk_widget_destroy(error_dialog);
             }
        }
        g_free(content);
    }
    
    g_free(ctx->reply_to_id);
    g_free(ctx);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
on_reply_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    if (!g_auth_token) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
        GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
        GtkWidget *error_dialog = gtk_message_dialog_new(window,
                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                 GTK_MESSAGE_ERROR,
                                 GTK_BUTTONS_CLOSE,
                                 "You must be logged in to reply.");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");

    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Reply to Tweet",
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Reply", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    gchar *replying_to = g_strdup_printf("Replying to @%s:", username);
    GtkWidget *label = gtk_label_new(replying_to);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_box_pack_start(GTK_BOX(content_area), label, FALSE, FALSE, 5);
    g_free(replying_to);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_widget_set_size_request(text_view, 300, 150);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);
    gtk_box_pack_start(GTK_BOX(content_area), text_view, TRUE, TRUE, 0);

    struct ReplyContext *ctx = g_new(struct ReplyContext, 1);
    ctx->text_view = text_view;
    ctx->reply_to_id = g_strdup(tweet_id);

    gtk_widget_show_all(dialog);
    g_signal_connect(dialog, "response", G_CALLBACK(on_reply_response), ctx);
}

static void
on_compose_response(GtkDialog *dialog, gint response_id, gpointer user_data)
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

static void
on_compose_clicked(GtkWidget *widget, gpointer window)
{
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

static gboolean
parse_login_response(const gchar *json_data, gchar **token_out, gchar **username_out)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    gboolean success = FALSE;

    json_parser_load_from_data(parser, json_data, -1, &error);
    
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        
        if (json_object_has_member(obj, "token") && json_object_has_member(obj, "user")) {
            const gchar *token = json_object_get_string_member(obj, "token");
            JsonObject *user_obj = json_object_get_object_member(obj, "user");
            
            if (user_obj && json_object_has_member(user_obj, "username")) {
                const gchar *uname = json_object_get_string_member(user_obj, "username");
                if (token && uname) {
                    *token_out = g_strdup(token);
                    *username_out = g_strdup(uname);
                    success = TRUE;
                }
            }
        }
    } else {
        // g_warning("JSON Parse error: %s", error->message);
        g_error_free(error);
    }
    g_object_unref(parser);
    return success;
}

static void
update_login_ui()
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

static void
perform_logout()
{
    clear_session();
    g_free(g_auth_token);
    g_auth_token = NULL;
    g_free(g_current_username);
    g_current_username = NULL;
    update_login_ui();
}

static gboolean
perform_login(const gchar *username, const gchar *password)
{
    struct MemoryStruct chunk;
    gboolean success = FALSE;

    // Build JSON
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
        } else {
            g_warning("Login failed: %s", chunk.memory);
        }
        free(chunk.memory);
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);

    return success;
}

static void
on_login_response(GtkDialog *dialog, gint response_id, gpointer user_data)
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
    g_free(user_data); // Array of pointers
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
on_login_clicked(GtkWidget *widget, gpointer window)
{
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

static GList*
parse_tweets(const gchar *json_data)
{
    JsonParser *parser;
    GError *error = NULL;
    GList *tweets = NULL;

    parser = json_parser_new();
    json_parser_load_from_data(parser, json_data, -1, &error);
    if (error) {
        g_warning("Unable to parse json: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root_node = json_parser_get_root(parser);
    JsonObject *root_object = json_node_get_object(root_node);
    
    if (json_object_has_member(root_object, "posts")) {
        JsonArray *posts = json_object_get_array_member(root_object, "posts");

        for (guint i = 0; i < json_array_get_length(posts); i++) {
            JsonNode *post_node = json_array_get_element(posts, i);
            JsonObject *post_object = json_node_get_object(post_node);
            JsonObject *author_object = json_object_get_object_member(post_object, "author");

            struct Tweet *tweet = g_new(struct Tweet, 1);
            tweet->content = g_strdup(json_object_get_string_member(post_object, "content"));
            tweet->author_name = g_strdup(json_object_get_string_member(author_object, "name"));
            tweet->author_username = g_strdup(json_object_get_string_member(author_object, "username"));
            if (json_object_has_member(author_object, "avatar") && !json_node_is_null(json_object_get_member(author_object, "avatar"))) {
                tweet->author_avatar = g_strdup(json_object_get_string_member(author_object, "avatar"));
            } else {
                tweet->author_avatar = NULL;
            }

            if (json_object_has_member(post_object, "id")) {
                tweet->id = g_strdup(json_object_get_string_member(post_object, "id"));
            } else {
                tweet->id = NULL;
            }

            tweets = g_list_append(tweets, tweet);
        }
    }

    g_object_unref(parser);
    return tweets;
}

static void
free_tweet(gpointer data)
{
    struct Tweet *tweet = data;
    g_free(tweet->content);
    g_free(tweet->author_name);
    g_free(tweet->author_username);
    g_free(tweet->author_avatar);
    g_free(tweet->id);
    g_free(tweet);
}

static void
free_tweets(GList *tweets)
{
    g_list_free_full(tweets, free_tweet);
}

static void
free_user(gpointer data)
{
    struct Profile *user = data;
    g_free(user->name);
    g_free(user->username);
    g_free(user->bio);
    g_free(user->avatar);
    g_free(user);
}

static void
free_users(GList *users)
{
    g_list_free_full(users, free_user);
}

static GtkWidget*
create_tweet_widget(struct Tweet *tweet)
{
    GtkWidget *outer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    GtkWidget *avatar_image = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(avatar_image, AVATAR_SIZE, AVATAR_SIZE);
    gtk_widget_set_valign(avatar_image, GTK_ALIGN_START);
    load_avatar(avatar_image, tweet->author_avatar, AVATAR_SIZE);

    gchar *author_str = g_strdup_printf("%s (@%s)", tweet->author_name, tweet->author_username);

    GtkWidget *author_btn = gtk_button_new_with_label(author_str);
    gtk_button_set_relief(GTK_BUTTON(author_btn), GTK_RELIEF_NONE);
    gtk_widget_set_halign(author_btn, GTK_ALIGN_START);
    
    // Make author bold
    GtkWidget *label = gtk_bin_get_child(GTK_BIN(author_btn));
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(label), attrs);
    pango_attr_list_unref(attrs);

    g_object_set_data_full(G_OBJECT(author_btn), "username", g_strdup(tweet->author_username), g_free);
    g_signal_connect(author_btn, "clicked", G_CALLBACK(on_author_clicked), NULL);

    GtkWidget *content_label = gtk_label_new(tweet->content);
    gtk_label_set_xalign(GTK_LABEL(content_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(content_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(content_label), TRUE);

    gtk_box_pack_start(GTK_BOX(box), author_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), content_label, FALSE, FALSE, 0);

    // Reply Button
    GtkWidget *reply_btn = gtk_button_new_with_label("Reply");
    // Store ID and Username on the button
    g_object_set_data_full(G_OBJECT(reply_btn), "tweet_id", g_strdup(tweet->id), g_free);
    g_object_set_data_full(G_OBJECT(reply_btn), "username", g_strdup(tweet->author_username), g_free);
    
    g_signal_connect(reply_btn, "clicked", G_CALLBACK(on_reply_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box), reply_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), box, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(outer_box), hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    g_free(author_str);

    return outer_box;
}

static void
populate_tweet_list(GtkListBox *list_box, GList *tweets)
{
    // Clear existing items (e.g. Loading...)
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    // Add new items
    for (GList *l = tweets; l != NULL; l = l->next) {
        GtkWidget *tweet_widget = create_tweet_widget(l->data);
        gtk_widget_show_all(tweet_widget);
        gtk_list_box_insert(list_box, tweet_widget, -1);
    }
}

// Callback executed on the main thread
static gboolean
on_tweets_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    if (async_data->success && async_data->tweets) {
        populate_tweet_list(async_data->list_box, async_data->tweets);
        free_tweets(async_data->tweets);
    } else {
        // Clear "Loading..." and show error
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

// Thread function
static gpointer
fetch_tweets_thread(gpointer data)
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

    // Schedule UI update on main thread
    g_idle_add(on_tweets_loaded, async_data);
    
    return NULL;
}

static void
start_loading_tweets(GtkListBox *list_box)
{
    // Add "Loading..." label
    GtkWidget *loading_label = gtk_label_new("Loading tweets...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    // Prepare async data
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    
    // Spawn thread
    g_thread_new("tweet-loader", fetch_tweets_thread, data);
}

static struct Profile*
parse_profile(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    struct Profile *profile = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "profile")) {
            JsonObject *p_obj = json_object_get_object_member(obj, "profile");
            profile = g_new0(struct Profile, 1);
            profile->name = g_strdup(json_object_get_string_member(p_obj, "name"));
            profile->username = g_strdup(json_object_get_string_member(p_obj, "username"));
            profile->bio = g_strdup(json_object_get_string_member(p_obj, "bio"));
            if (json_object_has_member(p_obj, "avatar") && !json_node_is_null(json_object_get_member(p_obj, "avatar"))) {
                profile->avatar = g_strdup(json_object_get_string_member(p_obj, "avatar"));
            }
            profile->follower_count = json_object_get_int_member(p_obj, "follower_count");
            profile->following_count = json_object_get_int_member(p_obj, "following_count");
            profile->post_count = json_object_get_int_member(p_obj, "post_count");
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return profile;
}

static GList*
parse_profile_replies(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *tweets = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "replies")) {
            JsonArray *replies = json_object_get_array_member(obj, "replies");
            for (guint i = 0; i < json_array_get_length(replies); i++) {
                JsonNode *reply_node = json_array_get_element(replies, i);
                JsonObject *reply_obj = json_node_get_object(reply_node);
                JsonObject *author_obj = json_object_get_object_member(reply_obj, "author");

                struct Tweet *tweet = g_new0(struct Tweet, 1);
                tweet->content = g_strdup(json_object_get_string_member(reply_obj, "content"));
                tweet->author_name = g_strdup(json_object_get_string_member(author_obj, "name"));
                tweet->author_username = g_strdup(json_object_get_string_member(author_obj, "username"));
                if (json_object_has_member(author_obj, "avatar") && !json_node_is_null(json_object_get_member(author_obj, "avatar"))) {
                    tweet->author_avatar = g_strdup(json_object_get_string_member(author_obj, "avatar"));
                }
                tweet->id = g_strdup(json_object_get_string_member(reply_obj, "id"));
                tweets = g_list_append(tweets, tweet);
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return tweets;
}

static void
on_back_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "timeline");
    gtk_widget_hide(g_back_button);
}

static gboolean
on_profile_loaded(gpointer data)
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

        // Populate lists if tweets were fetched (profile endpoint returns first 10 posts)
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

static gboolean
on_profile_replies_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    if (async_data->success && async_data->tweets) {
        populate_tweet_list(GTK_LIST_BOX(g_profile_replies_list), async_data->tweets);
        free_tweets(async_data->tweets);
    }
    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_profile_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk;
    gchar *url = g_strdup_printf("%s/profile/%s", API_BASE_URL, async_data->username);

    if (fetch_url(url, &chunk, NULL)) {
        async_data->profile = parse_profile(chunk.memory);
        // Reuse parse_tweets for posts
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

static gpointer
fetch_profile_replies_thread(gpointer data)
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

static void
show_profile(const gchar *username)
{
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "profile");
    gtk_widget_show(g_back_button);

    gtk_label_set_text(GTK_LABEL(g_profile_name_label), "Loading...");
    gtk_label_set_text(GTK_LABEL(g_profile_bio_label), "");
    gtk_label_set_text(GTK_LABEL(g_profile_stats_label), "");
    
    // Clear lists
    populate_tweet_list(GTK_LIST_BOX(g_profile_tweets_list), NULL);
    populate_tweet_list(GTK_LIST_BOX(g_profile_replies_list), NULL);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->username = g_strdup(username);
    g_thread_new("profile-loader", fetch_profile_thread, data);

    struct AsyncData *reply_data = g_new0(struct AsyncData, 1);
    reply_data->username = g_strdup(username);
    g_thread_new("profile-reply-loader", fetch_profile_replies_thread, reply_data);
}

static void
on_refresh_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
}

static GList*
parse_users(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    GList *users = NULL;

    json_parser_load_from_data(parser, json_data, -1, &error);
    if (!error) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "users")) {
            JsonArray *users_array = json_object_get_array_member(obj, "users");
            for (guint i = 0; i < json_array_get_length(users_array); i++) {
                JsonNode *user_node = json_array_get_element(users_array, i);
                JsonObject *user_obj = json_node_get_object(user_node);
                struct Profile *user = g_new0(struct Profile, 1);
                user->name = g_strdup(json_object_get_string_member(user_obj, "name"));
                user->username = g_strdup(json_object_get_string_member(user_obj, "username"));
                
                if (json_object_has_member(user_obj, "bio") && !json_node_is_null(json_object_get_member(user_obj, "bio"))) {
                    user->bio = g_strdup(json_object_get_string_member(user_obj, "bio"));
                } else {
                    user->bio = NULL;
                }
                if (json_object_has_member(user_obj, "avatar") && !json_node_is_null(json_object_get_member(user_obj, "avatar"))) {
                    user->avatar = g_strdup(json_object_get_string_member(user_obj, "avatar"));
                }
                user->follower_count = json_object_has_member(user_obj, "follower_count") ? json_object_get_int_member(user_obj, "follower_count") : 0;
                users = g_list_append(users, user);
            }
        }
    } else {
        g_error_free(error);
    }
    g_object_unref(parser);
    return users;
}

static GtkWidget*
create_user_widget(struct Profile *user)
{
    GtkWidget *outer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    GtkWidget *avatar_image = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(avatar_image, AVATAR_SIZE, AVATAR_SIZE);
    gtk_widget_set_valign(avatar_image, GTK_ALIGN_START);
    load_avatar(avatar_image, user->avatar, AVATAR_SIZE);

    gchar *user_str = g_strdup_printf("%s (@%s)", user->name, user->username);

    GtkWidget *user_btn = gtk_button_new_with_label(user_str);
    gtk_button_set_relief(GTK_BUTTON(user_btn), GTK_RELIEF_NONE);
    gtk_widget_set_halign(user_btn, GTK_ALIGN_START);
    
    GtkWidget *label = gtk_bin_get_child(GTK_BIN(user_btn));
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(label), attrs);
    pango_attr_list_unref(attrs);

    g_object_set_data_full(G_OBJECT(user_btn), "username", g_strdup(user->username), g_free);
    g_signal_connect(user_btn, "clicked", G_CALLBACK(on_author_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(box), user_btn, FALSE, FALSE, 0);

    if (user->bio && strlen(user->bio) > 0) {
        GtkWidget *bio_label = gtk_label_new(user->bio);
        gtk_label_set_xalign(GTK_LABEL(bio_label), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(bio_label), TRUE);
        gtk_box_pack_start(GTK_BOX(box), bio_label, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), box, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(outer_box), hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    g_free(user_str);
    return outer_box;
}

static void
populate_user_list(GtkListBox *list_box, GList *users)
{
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    for (GList *l = users; l != NULL; l = l->next) {
        GtkWidget *user_widget = create_user_widget(l->data);
        gtk_widget_show_all(user_widget);
        gtk_list_box_insert(list_box, user_widget, -1);
    }
}

static gboolean
on_users_loaded(gpointer data)
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

static gboolean
on_search_tweets_loaded(gpointer data)
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

static gpointer
fetch_search_users_thread(gpointer data)
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

static gpointer
fetch_search_tweets_thread(gpointer data)
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

static void
perform_search(const gchar *query)
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

static void
on_search_activated(GtkEntry *entry, gpointer user_data)
{
    (void)user_data;
    const gchar *query = gtk_entry_get_text(entry);
    if (query && strlen(query) > 0) {
        perform_search(query);
    }
}

static GtkWidget*
create_profile_view()
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

    gtk_box_pack_start(GTK_BOX(vbox), g_profile_name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_profile_bio_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_profile_stats_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);

    GtkWidget *notebook = gtk_notebook_new();
    
    // Tweets tab
    GtkWidget *tweets_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_tweets_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_tweets_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(tweets_scroll), g_profile_tweets_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tweets_scroll, gtk_label_new("Tweets"));

    // Replies tab
    GtkWidget *replies_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_replies_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_replies_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(replies_scroll), g_profile_replies_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), replies_scroll, gtk_label_new("Replies"));

    gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE, 0);

    return box;
}

static GtkWidget*
create_search_view()
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

static GtkWidget*
create_window()
{
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Tweetapus Desktop");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 800);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Header Bar
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Tweetapus");
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

    // Refresh Button (Left)
    GtkWidget *refresh_button = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), refresh_button);

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
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_main_list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(timeline_scroll), g_main_list_box);
    gtk_stack_add_named(GTK_STACK(g_stack), timeline_scroll, "timeline");

    // Profile View
    GtkWidget *profile_view = create_profile_view();
    gtk_stack_add_named(GTK_STACK(g_stack), profile_view, "profile");

    // Search View
    GtkWidget *search_view = create_search_view();
    gtk_stack_add_named(GTK_STACK(g_stack), search_view, "search");

    return window;
}

int main(int argc, char *argv[]) {

    GtkWidget *window;



    gtk_init(&argc, &argv);

    curl_global_init(CURL_GLOBAL_ALL);



    window = create_window();



    load_session();

    update_login_ui();



    // Initial Show

    gtk_widget_show_all(window);



    // Start loading

    start_loading_tweets(GTK_LIST_BOX(g_main_list_box));



    gtk_main();



    curl_global_cleanup();

    g_free(g_auth_token);

    g_free(g_current_username);



    return 0;

}
