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

static GMutex load_notifications_mutex;
static guint active_notifications_request_id = 0;

static GMutex load_conversations_mutex;
static guint active_conversations_request_id = 0;

static GMutex load_messages_mutex;
static guint active_messages_request_id = 0;

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

    if (fetch_url(LOGIN_URL, &chunk, post_data, "POST")) {
        gchar *token = NULL;
        gchar *uname = NULL;
        if (parse_login_response(chunk.memory, &token, &uname, &g_is_admin)) {
            g_free(g_auth_token);
            g_auth_token = token;
            g_free(g_current_username);
            g_current_username = uname;
            
            // The basic-login endpoint doesn't return admin status, so we fetch /auth/me
            struct MemoryStruct me_chunk;
            if (fetch_url(AUTH_ME_URL, &me_chunk, NULL, "GET")) {
                parse_user_me_response(me_chunk.memory, &g_is_admin);
                free(me_chunk.memory);
            }

            save_session(g_auth_token, g_current_username, g_is_admin);
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

    if (fetch_url(POST_TWEET_URL, &chunk, post_data, "POST")) {
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
        g_free(async_data->before_id);
        g_free(async_data);
        return G_SOURCE_REMOVE;
    }
    
    // Clear loading state on the list box
    g_object_set_data(G_OBJECT(async_data->list_box), "loading_more", GINT_TO_POINTER(FALSE));

    if (async_data->success && async_data->tweets) {
        if (async_data->is_append) {
            // Remove the "loading more" indicator if it exists
            GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
            GtkWidget *last_child = g_list_last(children)->data;
            if (GTK_IS_LABEL(last_child)) {
                const gchar *text = gtk_label_get_text(GTK_LABEL(last_child));
                if (g_strcmp0(text, "Loading more...") == 0) {
                    gtk_widget_destroy(last_child);
                }
            }
            g_list_free(children);

            append_tweets_to_list(async_data->list_box, async_data->tweets);
        } else {
            populate_tweet_list(async_data->list_box, async_data->tweets);
        }

        // Update last_id for infinite scrolling
        GList *last = g_list_last(async_data->tweets);
        if (last) {
            struct Tweet *last_tweet = (struct Tweet *)last->data;
            g_object_set_data_full(G_OBJECT(async_data->list_box), "last_id", g_strdup(last_tweet->id), g_free);
        } else {
            // No more tweets, clear last_id to stop infinite scroll attempts
            g_object_set_data(G_OBJECT(async_data->list_box), "last_id", NULL);
        }

        free_tweets(async_data->tweets);
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
            // Just remove loading indicator on failure for append
            GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
            GtkWidget *last_child = g_list_last(children)->data;
            if (GTK_IS_LABEL(last_child)) {
                const gchar *text = gtk_label_get_text(GTK_LABEL(last_child));
                if (g_strcmp0(text, "Loading more...") == 0) {
                    gtk_widget_destroy(last_child);
                }
            }
            g_list_free(children);
        }
    }

    g_free(async_data->before_id);
    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_tweets_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk;
    gchar *url = NULL;

    const gchar *feed_type = g_object_get_data(G_OBJECT(async_data->list_box), "feed_type");

    if (g_strcmp0(feed_type, "profile_posts") == 0) {
        if (async_data->before_id) {
            url = g_strdup_printf("%s/profile/%s/posts?before=%s", API_BASE_URL, async_data->username, async_data->before_id);
        } else {
            url = g_strdup_printf("%s/profile/%s/posts", API_BASE_URL, async_data->username);
        }
    } else if (g_strcmp0(feed_type, "profile_replies") == 0) {
        if (async_data->before_id) {
            url = g_strdup_printf("%s/profile/%s/replies?before=%s", API_BASE_URL, async_data->username, async_data->before_id);
        } else {
            url = g_strdup_printf("%s/profile/%s/replies", API_BASE_URL, async_data->username);
        }
    } else {
        if (async_data->before_id) {
            url = g_strdup_printf("%s?before=%s", PUBLIC_TWEETS_URL, async_data->before_id);
        } else {
            url = g_strdup(PUBLIC_TWEETS_URL);
        }
    }

    if (fetch_url(url, &chunk, NULL, "GET")) {
        if (g_strcmp0(feed_type, "profile_replies") == 0) {
            async_data->tweets = parse_profile_replies(chunk.memory);
        } else {
            async_data->tweets = parse_tweets(chunk.memory);
        }
        async_data->success = (async_data->tweets != NULL);
        free(chunk.memory);
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
    // Increment request ID to invalidate any pending requests
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

    // Show loading more indicator
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

    // Allow infinite scroll for main, profile posts and profile replies
    if (list_box != g_main_list_box && 
        list_box != g_profile_tweets_list && 
        list_box != g_profile_replies_list) {
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
    load_more_tweets(GTK_LIST_BOX(list_box), last_id);
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

            GList *last = g_list_last(async_data->tweets);
            if (last) {
                struct Tweet *last_tweet = (struct Tweet *)last->data;
                g_object_set_data_full(G_OBJECT(g_profile_tweets_list), "last_id", g_strdup(last_tweet->id), g_free);
            } else {
                g_object_set_data(G_OBJECT(g_profile_tweets_list), "last_id", NULL);
            }

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

        GList *last = g_list_last(async_data->tweets);
        if (last) {
            struct Tweet *last_tweet = (struct Tweet *)last->data;
            g_object_set_data_full(G_OBJECT(g_profile_replies_list), "last_id", g_strdup(last_tweet->id), g_free);
        } else {
            g_object_set_data(G_OBJECT(g_profile_replies_list), "last_id", NULL);
        }

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

    if (fetch_url(url, &chunk, NULL, "GET")) {
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

    if (fetch_url(url, &chunk, NULL, "GET")) {
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
                // This is the main tweet
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
                        // Just after the main tweet, add a "Replies" header
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
            
            // Highlight main tweet
            if (g_strcmp0(t->id, async_data->query) == 0) {
                GtkStyleContext *context = gtk_widget_get_style_context(tweet_widget);
                gtk_style_context_add_class(context, "main-tweet");
            }

            gtk_widget_show_all(tweet_widget);
            gtk_list_box_insert(GTK_LIST_BOX(g_conversation_list), tweet_widget, -1);
        }

        free_tweets(async_data->tweets);
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(g_conversation_list));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new("Tweet not found or error loading.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(GTK_LIST_BOX(g_conversation_list), error_label, -1);
    }

    g_free(async_data->query); // used as tweet_id here
    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_tweet_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk;
    gchar *url = g_strdup_printf(TWEET_DETAILS_URL, async_data->query);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->tweets = parse_tweet_details(chunk.memory);
        async_data->success = (async_data->tweets != NULL);
        free(chunk.memory);
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
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "profile");
    gtk_widget_show(g_back_button);

    gtk_label_set_text(GTK_LABEL(g_profile_name_label), "Loading...");
    gtk_label_set_text(GTK_LABEL(g_profile_bio_label), "");
    gtk_label_set_text(GTK_LABEL(g_profile_stats_label), "");
    
    g_object_set_data_full(G_OBJECT(g_profile_tweets_list), "current_profile_user", g_strdup(username), g_free);
    g_object_set_data_full(G_OBJECT(g_profile_replies_list), "current_profile_user", g_strdup(username), g_free);

    populate_tweet_list(GTK_LIST_BOX(g_profile_tweets_list), NULL);
    populate_tweet_list(GTK_LIST_BOX(g_profile_replies_list), NULL);

    g_object_set_data(G_OBJECT(g_profile_tweets_list), "last_id", NULL);
    g_object_set_data(G_OBJECT(g_profile_replies_list), "last_id", NULL);

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
        if (async_data->notifications) {
            free_notifications(async_data->notifications);
        }
        g_free(async_data);
        return G_SOURCE_REMOVE;
    }

    if (async_data->success && async_data->notifications) {
        populate_notification_list(async_data->list_box, async_data->notifications);
        free_notifications(async_data->notifications);
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new(async_data->success ? "No notifications." : "Failed to load notifications.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_notifications_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk;

    if (fetch_url(NOTIFICATIONS_URL, &chunk, NULL, "GET")) {
        async_data->notifications = parse_notifications(chunk.memory);
        async_data->success = TRUE;
        free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

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
    
    GtkWidget *loading_label = gtk_label_new("Loading notifications...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    data->request_id = current_request_id;
    
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

void on_mark_all_read_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    
    if (!g_auth_token) return;

    struct MemoryStruct chunk;
    if (fetch_url(NOTIFICATIONS_MARK_ALL_READ_URL, &chunk, "", "PATCH")) {
        free(chunk.memory);
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
        if (async_data->conversations) {
            free_conversations(async_data->conversations);
        }
        g_free(async_data);
        return G_SOURCE_REMOVE;
    }

    if (async_data->success && async_data->conversations) {
        populate_conversation_list(async_data->list_box, async_data->conversations);
        free_conversations(async_data->conversations);
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new(async_data->success ? "No conversations." : "Failed to load conversations.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_conversations_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk;

    if (fetch_url(DM_CONVERSATIONS_URL, &chunk, NULL, "GET")) {
        async_data->conversations = parse_conversations(chunk.memory);
        async_data->success = TRUE;
        free(chunk.memory);
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
    
    g_mutex_lock(&load_messages_mutex);
    gboolean is_active = (async_data->request_id == active_messages_request_id);
    g_mutex_unlock(&load_messages_mutex);
    
    if (!is_active) {
        if (async_data->messages) {
            free_messages(async_data->messages);
        }
        g_free(async_data->conversation_id);
        g_free(async_data);
        return G_SOURCE_REMOVE;
    }

    if (async_data->success && async_data->messages) {
        populate_message_list(async_data->list_box, async_data->messages);
        free_messages(async_data->messages);
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new(async_data->success ? "No messages." : "Failed to load messages.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    g_free(async_data->conversation_id);
    g_free(async_data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_messages_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk;
    gchar *url = g_strdup_printf(DM_MESSAGES_URL, async_data->conversation_id);

    if (fetch_url(url, &chunk, NULL, "GET")) {
        async_data->messages = parse_messages(chunk.memory);
        async_data->success = TRUE;
        free(chunk.memory);
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

    if (fetch_url(url, &chunk, NULL, "GET")) {
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

    if (fetch_url(url, &chunk, NULL, "GET")) {
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

    if (fetch_url(url, &chunk, "{}", "POST")) {
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

    if (fetch_url(url, &chunk, "{}", "POST")) {
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

    if (fetch_url(url, &chunk, post_data, "POST")) {
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

    if (fetch_url(url, &chunk, post_data, "POST")) {
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

static gboolean perform_add_note(const gchar *tweet_id, const gchar *note, const gchar *severity)
{
    struct MemoryStruct chunk;
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
        free(chunk.memory);
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
                // Refresh to show the new note
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
    if (!g_auth_token) return;

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

