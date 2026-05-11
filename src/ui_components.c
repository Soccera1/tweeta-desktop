#include <string.h>
#include <json-glib/json-glib.h>
#include "actions.h"
#include "constants.h"
#include "globals.h"
#include "json_utils.h"
#include "network.h"
#include "ui_components.h"
#include "ui_utils.h"

static void on_join_community_clicked(GtkButton *button, gpointer user_data);
static void on_community_clicked(GtkListBoxRow *row, gpointer user_data);
static void on_file_selected(GtkFileChooserButton *chooser, gpointer user_data);
extern gboolean on_p2p_contact_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data);

static inline gboolean is_logged_in(void) {
    g_mutex_lock(&g_globals_mutex);
    gboolean logged_in = (g_auth_token != NULL);
    g_mutex_unlock(&g_globals_mutex);
    return logged_in;
}

static inline gboolean is_admin_user(void) {
    g_mutex_lock(&g_globals_mutex);
    gboolean admin = g_is_admin;
    g_mutex_unlock(&g_globals_mutex);
    return admin;
}

static inline gchar* get_community_id_safe(void) {
    g_mutex_lock(&g_globals_mutex);
    gchar *id = g_community_id ? g_strdup(g_community_id) : NULL;
    g_mutex_unlock(&g_globals_mutex);
    return id;
}

static inline gchar* get_current_username_safe(void) {
    g_mutex_lock(&g_globals_mutex);
    gchar *username = g_current_username ? g_strdup(g_current_username) : NULL;
    g_mutex_unlock(&g_globals_mutex);
    return username;
}

static void
refresh_current_dm_messages(void)
{
    const gchar *conversation_id;

    if (!g_dm_messages_list) {
        return;
    }

    conversation_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
    if (conversation_id) {
        start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conversation_id);
    }
}

static void
append_badge(GtkWidget *box, const gchar *text, const gchar *color)
{
    GtkWidget *badge = gtk_label_new(NULL);
    gchar *markup = g_strdup_printf("<span foreground='white' background='%s' size='small' weight='bold'> %s </span>",
                                    color,
                                    text);
    gtk_label_set_markup(GTK_LABEL(badge), markup);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(box), badge, FALSE, FALSE, 0);
}

static void
append_account_badges(GtkWidget *box, gboolean verified, gboolean gold, gboolean gray)
{
    if (gold) {
        append_badge(box, "Gold", "#c88900");
    } else if (gray) {
        append_badge(box, "Gray", "#6c757d");
    } else if (verified) {
        append_badge(box, "Verified", "#1d9bf0");
    }
}

static gchar *
build_tweet_meta_text(struct Tweet *tweet)
{
    GString *meta = g_string_new(NULL);

    g_string_append_printf(meta, "%d replies  %d likes  %d retweets",
                           tweet->reply_count,
                           tweet->like_count,
                           tweet->retweet_count);

    if (tweet->quote_count > 0 || tweet->view_count > 0 || tweet->reaction_count > 0) {
        g_string_append_printf(meta, "  %d quotes  %d views  %d reactions",
                               tweet->quote_count,
                               tweet->view_count,
                               tweet->reaction_count);
    }

    if (tweet->edited_at) {
        g_string_append(meta, "  edited");
    }

    return g_string_free(meta, FALSE);
}

static void
on_like_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    g_debug("on_like_clicked called");
    if (!is_logged_in()) {
        g_debug("on_like_clicked: User not logged in");
        return;
    }

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    gboolean *liked = g_object_get_data(G_OBJECT(widget), "liked_state");

    g_debug("on_like_clicked: tweet_id=%s, current_liked=%d", tweet_id, *liked);
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(LIKE_TWEET_URL, tweet_id);

    if (fetch_url(url, &chunk, "{}", "POST")) {
        g_debug("on_like_clicked: fetch_url succeeded, response: %s", chunk.memory ? chunk.memory : "(null)");
        if (chunk.memory && strstr(chunk.memory, "\"error\"") == NULL) {
            JsonParser *parser = json_parser_new();
            GError *error = NULL;
            if (json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
                JsonNode *root = json_parser_get_root(parser);
                JsonObject *obj = json_node_get_object(root);
                if (json_object_has_member(obj, "liked")) {
                    gboolean new_liked = json_object_get_boolean_member(obj, "liked");
                    *liked = new_liked;
                    update_interaction_cache(tweet_id, new_liked, -1, -1);
                    g_debug("on_like_clicked: API returned liked=%d", new_liked);
                    if (new_liked) {
                        gtk_button_set_label(GTK_BUTTON(widget), "♥ Liked");
                    } else {
                        gtk_button_set_label(GTK_BUTTON(widget), "♡ Like");
                    }
                } else {
                    *liked = !(*liked);
                    update_interaction_cache(tweet_id, *liked, -1, -1);
                    if (*liked) {
                        gtk_button_set_label(GTK_BUTTON(widget), "♥ Liked");
                    } else {
                        gtk_button_set_label(GTK_BUTTON(widget), "♡ Like");
                    }
                }
                g_object_unref(parser);
            } else {
                if (error) g_error_free(error);
                *liked = !(*liked);
                if (*liked) {
                    gtk_button_set_label(GTK_BUTTON(widget), "♥ Liked");
                } else {
                    gtk_button_set_label(GTK_BUTTON(widget), "♡ Like");
                }
            }
        } else if (chunk.memory) {
            g_warning("on_like_clicked: API returned error: %s", chunk.memory);
        }
        g_free(chunk.memory);
    } else {
        g_debug("on_like_clicked: fetch_url failed");
    }

    g_free(url);
}

static void
on_retweet_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    g_debug("on_retweet_clicked called");
    if (!is_logged_in()) {
        g_debug("on_retweet_clicked: User not logged in");
        return;
    }

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    gboolean *retweeted = g_object_get_data(G_OBJECT(widget), "retweeted_state");

    g_debug("on_retweet_clicked: tweet_id=%s, current_retweeted=%d", tweet_id, *retweeted);
    struct MemoryStruct chunk = {0};
    gchar *url = g_strdup_printf(RETWEET_URL, tweet_id);

    if (fetch_url(url, &chunk, "{}", "POST")) {
        g_debug("on_retweet_clicked: fetch_url succeeded, response: %s", chunk.memory ? chunk.memory : "(null)");
        if (chunk.memory && strstr(chunk.memory, "\"error\"") == NULL) {
            JsonParser *parser = json_parser_new();
            GError *error = NULL;
            if (json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
                JsonNode *root = json_parser_get_root(parser);
                JsonObject *obj = json_node_get_object(root);
                if (json_object_has_member(obj, "retweeted")) {
                    gboolean new_retweeted = json_object_get_boolean_member(obj, "retweeted");
                    *retweeted = new_retweeted;
                    g_debug("on_retweet_clicked: API returned retweeted=%d", new_retweeted);
                    update_interaction_cache(tweet_id, -1, new_retweeted, -1);
                    if (new_retweeted) {
                        gtk_button_set_label(GTK_BUTTON(widget), "↻ Retweeted");
                    } else {
                        gtk_button_set_label(GTK_BUTTON(widget), "↻ Retweet");
                    }
                } else {
                    *retweeted = !(*retweeted);
                    update_interaction_cache(tweet_id, -1, *retweeted, -1);
                    if (*retweeted) {
                        gtk_button_set_label(GTK_BUTTON(widget), "↻ Retweeted");
                    } else {
                        gtk_button_set_label(GTK_BUTTON(widget), "↻ Retweet");
                    }
                }
                g_object_unref(parser);
            } else {
                if (error) g_error_free(error);
                *retweeted = !(*retweeted);
                if (*retweeted) {
                    gtk_button_set_label(GTK_BUTTON(widget), "↻ Retweeted");
                } else {
                    gtk_button_set_label(GTK_BUTTON(widget), "↻ Retweet");
                }
            }
        } else if (chunk.memory) {
            g_warning("on_retweet_clicked: API returned error: %s", chunk.memory);
        }
        g_free(chunk.memory);
    } else {
        g_debug("on_retweet_clicked: fetch_url failed");
    }

    g_free(url);
}

static void
on_quote_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    struct QuoteContext *ctx = (struct QuoteContext *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->text_view));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar *content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        if (content && strlen(content) > 0) {
            JsonBuilder *builder = json_builder_new();
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "content");
            json_builder_add_string_value(builder, content);
            json_builder_set_member_name(builder, "source");
            json_builder_add_string_value(builder, "Tweeta Desktop");
            json_builder_set_member_name(builder, "quote_tweet_id");
            json_builder_add_string_value(builder, ctx->quote_id);
            json_builder_end_object(builder);

            JsonGenerator *gen = json_generator_new();
            json_generator_set_root(gen, json_builder_get_root(builder));
            gchar *post_data = json_generator_to_data(gen, NULL);

            struct MemoryStruct chunk = {0};
            if (fetch_url(POST_TWEET_URL, &chunk, post_data, "POST")) {
                start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
                g_free(chunk.memory);
            }

            g_free(post_data);
            g_object_unref(gen);
            g_object_unref(builder);
        }
        g_free(content);
    }

    g_free(ctx->quote_id);
    g_free(ctx);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
on_quote_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    if (!is_logged_in()) {
        return;
    }

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");

    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Quote Tweet",
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Quote", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *text_view = gtk_text_view_new();
    gtk_widget_set_size_request(text_view, 300, 150);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);
    gtk_box_pack_start(GTK_BOX(content_area), text_view, TRUE, TRUE, 0);

    struct QuoteContext *ctx = g_new(struct QuoteContext, 1);
    ctx->text_view = text_view;
    ctx->quote_id = g_strdup(tweet_id);

    gtk_widget_show_all(dialog);
    g_signal_connect(dialog, "response", G_CALLBACK(on_quote_response), ctx);
}

static void
on_retweet_menu_retweet(GtkMenuItem *menuitem, gpointer user_data)
{
    (void)menuitem;
    GtkWidget *btn = GTK_WIDGET(user_data);
    on_retweet_clicked(btn, NULL);
}

static void
on_retweet_menu_quote(GtkMenuItem *menuitem, gpointer user_data)
{
    (void)menuitem;
    GtkWidget *btn = GTK_WIDGET(user_data);
    on_quote_clicked(btn, NULL);
}

static void
on_retweet_button_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    if (!is_logged_in()) {
        return;
    }

    GtkWidget *menu = gtk_menu_new();

    GtkWidget *retweet_item = gtk_menu_item_new_with_label("Retweet");
    GtkWidget *quote_item = gtk_menu_item_new_with_label("Quote");

    g_signal_connect(retweet_item, "activate", G_CALLBACK(on_retweet_menu_retweet), widget);
    g_signal_connect(quote_item, "activate", G_CALLBACK(on_retweet_menu_quote), widget);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), retweet_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quote_item);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), widget, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

static void
on_bookmark_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    g_debug("on_bookmark_clicked called");
    if (!is_logged_in()) {
        g_debug("on_bookmark_clicked: User not logged in");
        return;
    }

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    gboolean *bookmarked = g_object_get_data(G_OBJECT(widget), "bookmarked_state");

    g_debug("on_bookmark_clicked: tweet_id=%s, current_bookmarked=%d", tweet_id, *bookmarked);
    gboolean add = !(*bookmarked);
    const gchar *url = add ? BOOKMARK_ADD_URL : BOOKMARK_REMOVE_URL;

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "postId");
    json_builder_add_string_value(builder, tweet_id);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *post_data = json_generator_to_data(gen, NULL);

    struct MemoryStruct chunk = {0};

    if (fetch_url(url, &chunk, post_data, "POST")) {
        g_debug("on_bookmark_clicked: fetch_url succeeded, response: %s", chunk.memory ? chunk.memory : "(null)");
        if (chunk.memory && strstr(chunk.memory, "\"error\"") == NULL) {
            JsonParser *parser = json_parser_new();
            GError *error = NULL;
            if (json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
                JsonNode *root = json_parser_get_root(parser);
                JsonObject *obj = json_node_get_object(root);
                if (json_object_has_member(obj, "bookmarked")) {
                    gboolean new_bookmarked = json_object_get_boolean_member(obj, "bookmarked");
                    *bookmarked = new_bookmarked;
                    g_debug("on_bookmark_clicked: API returned bookmarked=%d", new_bookmarked);
                    update_interaction_cache(tweet_id, -1, -1, new_bookmarked);
                    if (new_bookmarked) {
                        gtk_button_set_label(GTK_BUTTON(widget), "★ Saved");
                    } else {
                        gtk_button_set_label(GTK_BUTTON(widget), "☆ Bookmark");
                    }
                } else {
                    *bookmarked = !(*bookmarked);
                    update_interaction_cache(tweet_id, -1, -1, *bookmarked);
                    if (*bookmarked) {
                        gtk_button_set_label(GTK_BUTTON(widget), "★ Saved");
                    } else {
                        gtk_button_set_label(GTK_BUTTON(widget), "☆ Bookmark");
                    }
                }
                g_object_unref(parser);
            } else {
                if (error) g_error_free(error);
                *bookmarked = !(*bookmarked);
                if (*bookmarked) {
                    gtk_button_set_label(GTK_BUTTON(widget), "★ Saved");
                } else {
                    gtk_button_set_label(GTK_BUTTON(widget), "☆ Bookmark");
                }
            }
        } else if (chunk.memory) {
            g_warning("on_bookmark_clicked: API returned error: %s", chunk.memory);
        }
        g_free(chunk.memory);
    } else {
        g_debug("on_bookmark_clicked: fetch_url failed");
    }

    g_free(post_data);
    g_object_unref(gen);
    g_object_unref(builder);
}

static void
free_reaction_context(gpointer data)
{
    struct ReactionContext *ctx = (struct ReactionContext *)data;
    if (ctx) {
        g_free(ctx->tweet_id);
        g_free(ctx);
    }
}

static void
on_emoji_selected(GtkFlowBoxChild *child, gpointer user_data)
{
    GtkWidget *dialog = (GtkWidget *)user_data;
    struct ReactionContext *ctx = g_object_get_data(G_OBJECT(dialog), "reaction_context");
    const gchar *emoji_name = g_object_get_data(G_OBJECT(child), "emoji_name");

    g_debug("on_emoji_selected: tweet_id=%s, emoji=%s", ctx ? ctx->tweet_id : "NULL", emoji_name);
    if (emoji_name && ctx && ctx->tweet_id) {
        perform_reaction(ctx->tweet_id, emoji_name);
    }

    gtk_widget_destroy(dialog);
}

static void
on_reaction_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    if (!is_logged_in()) {
        return;
    }

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");

    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Reaction",
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, 300, 200);

    GtkWidget *flowbox = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flowbox), GTK_SELECTION_SINGLE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flowbox), 8);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flowbox), TRUE);

    struct ReactionContext *ctx = g_new(struct ReactionContext, 1);
    ctx->tweet_id = g_strdup(tweet_id);
    ctx->parent_window = GTK_WIDGET(window);

    g_object_set_data_full(G_OBJECT(dialog), "reaction_context", ctx, free_reaction_context);

    const gchar *system_emojis[] = {
        "👍", "👎", "❤️", "💔", "😀", "😃", "😄", "😁", "😆", "😅",
        "🤣", "😂", "🙂", "🙃", "😉", "😊", "😇", "🥰", "😍", "🤩",
        "😘", "😗", "😚", "😙", "🥲", "😋", "😛", "😜", "🤪", "😝",
        "🤑", "🤗", "🤭", "🤫", "🤔", "🤐", "🤨", "😐", "😑", "😶",
        "😏", "😒", "🙄", "😬", "🤥", "😌", "😔", "😪", "🤤", "😴",
        "😷", "🤒", "🤕", "🤢", "🤮", "🤧", "🥵", "🥶", "🥴", "😵",
        "🤯", "🤠", "🥳", "🥸", "😎", "🤓", "🧐", "😕", "😟", "🙁",
        "☹️", "😮", "😯", "😲", "😳", "🥺", "😦", "😧", "😨", "😰",
        "😥", "😢", "😭", "😱", "😖", "😣", "😞", "😓", "😩", "😫",
        "🥱", "😤", "😡", "😠", "🤬", "😈", "👿", "💀", "☠️", "💩",
        "🤡", "👹", "👺", "👻", "👽", "👾", "🤖", "😺", "😸", "😹",
        "😻", "😼", "😽", "🙀", "😿", "😾", "🙈", "🙉", "🙊", "💋",
        "💯", "💢", "💥", "💫", "💦", "💨", "🕳️", "💣", "💬", "🔥",
        "✨", "⭐", "🌟", "💫", "🎉", "🎊", "🎁", "🏆", "🥇", "🥈",
        "🥉", "⚽", "🏀", "🎵", "🎶", "🎤", "🎧", "👏", "🙌", "👐",
        "🤲", "🤝", "🙏", "✍️", "💪", "🦾", "🦿", "🦵", "🦶", "👂",
        "🦻", "👃", "🧠", "🦷", "🦴", "👀", "👁️", "👅", "👄", "💘",
        "💝", "💖", "💗", "💓", "💞", "💕", "❣️", "💔", "🧡", "💛",
        "💚", "💙", "💜", "🤎", "🖤", "🤍", "✅", "❌", "❓", "❗",
        NULL
    };

    for (int i = 0; system_emojis[i] != NULL; i++) {
        GtkWidget *label = gtk_label_new(system_emojis[i]);
        GtkWidget *child_widget = gtk_flow_box_child_new();
        gtk_container_add(GTK_CONTAINER(child_widget), label);
        g_object_set_data_full(G_OBJECT(child_widget), "emoji_name", g_strdup(system_emojis[i]), g_free);
        gtk_container_add(GTK_CONTAINER(flowbox), child_widget);
    }

    GList *emojis = fetch_emojis();
    for (GList *l = emojis; l != NULL; l = l->next) {
        struct Emoji *emoji = l->data;
        GtkWidget *emoji_image = gtk_image_new();
        load_avatar(emoji_image, emoji->file_url, 24);

        GtkWidget *child_widget = gtk_flow_box_child_new();
        gtk_container_add(GTK_CONTAINER(child_widget), emoji_image);
        g_object_set_data_full(G_OBJECT(child_widget), "emoji_name", g_strdup(emoji->name), g_free);
        gtk_widget_set_tooltip_text(child_widget, emoji->name);
        gtk_container_add(GTK_CONTAINER(flowbox), child_widget);
    }

    free_emojis(emojis);

    g_signal_connect(flowbox, "child-activated", G_CALLBACK(on_emoji_selected), dialog);

    gtk_container_add(GTK_CONTAINER(scrolled), flowbox);
    gtk_box_pack_start(GTK_BOX(content_area), scrolled, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
}

static void
show_text_dialog(GtkWidget *widget, const gchar *title, const gchar *body)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *scroll;
    GtkWidget *label;
    GtkWidget *toplevel;

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons(title,
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close", GTK_RESPONSE_CLOSE,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, 420, 360);
    label = gtk_label_new(body ? body : "");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), label);
    gtk_box_pack_start(GTK_BOX(content_area), scroll, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
refresh_after_tweet_mutation(const gchar *tweet_id)
{
    const gchar *current_view = NULL;

    if (GTK_IS_STACK(g_stack)) {
        current_view = gtk_stack_get_visible_child_name(GTK_STACK(g_stack));
    }
    if (g_strcmp0(current_view, "conversation") == 0 && tweet_id) {
        show_tweet(tweet_id);
    } else if (g_strcmp0(current_view, "profile") == 0 && g_active_profile && g_active_profile->username) {
        show_profile(g_active_profile->username);
    } else if (g_strcmp0(current_view, "bookmarks") == 0 && GTK_IS_LIST_BOX(g_bookmarks_list)) {
        start_loading_bookmarks(GTK_LIST_BOX(g_bookmarks_list));
    } else if (g_strcmp0(current_view, "community_tweets") == 0 &&
               g_community_id &&
               GTK_IS_LIST_BOX(g_community_tweets_list)) {
        start_loading_community_tweets(GTK_LIST_BOX(g_community_tweets_list), g_community_id);
    } else if (GTK_IS_LIST_BOX(g_main_list_box)) {
        start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
    }
}

static void
on_tweet_edit_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *entry;
    GtkWidget *toplevel;
    const gchar *tweet_id;
    const gchar *current_content;

    (void)user_data;
    tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    current_content = g_object_get_data(G_OBJECT(widget), "tweet_content");
    if (!tweet_id) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Edit Tweet",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), current_content ? current_content : "");
    gtk_box_pack_start(GTK_BOX(content_area), entry, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        if (perform_edit_tweet(tweet_id, gtk_entry_get_text(GTK_ENTRY(entry)))) {
            refresh_after_tweet_mutation(tweet_id);
        }
    }

    gtk_widget_destroy(dialog);
}

static void
on_tweet_delete_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *toplevel;
    const gchar *tweet_id;

    (void)user_data;
    tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    if (!tweet_id) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_OK_CANCEL,
                                    "Delete this tweet?");
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        if (perform_delete_tweet(tweet_id)) {
            refresh_after_tweet_mutation(tweet_id);
        }
    }
    gtk_widget_destroy(dialog);
}

static void
on_tweet_history_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *history_text;

    (void)user_data;
    history_text = fetch_tweet_edit_history_text(g_object_get_data(G_OBJECT(widget), "tweet_id"));
    show_text_dialog(widget, "Edit History", history_text ? history_text : "No edit history available.");
    g_free(history_text);
}

static void
on_tweet_reactions_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *reaction_text;

    (void)user_data;
    reaction_text = fetch_tweet_reactions_text(g_object_get_data(G_OBJECT(widget), "tweet_id"));
    show_text_dialog(widget, "Tweet Reactions", reaction_text ? reaction_text : "No reactions yet.");
    g_free(reaction_text);
}

static void
on_tweet_pin_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *tweet_id;
    gboolean pinned;

    (void)user_data;
    tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    pinned = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "tweet_pinned"));
    if (!tweet_id) {
        return;
    }

    if (perform_toggle_pin_tweet(tweet_id, !pinned)) {
        g_object_set_data(G_OBJECT(widget), "tweet_pinned", GINT_TO_POINTER(!pinned));
        gtk_button_set_label(GTK_BUTTON(widget), pinned ? "Pin" : "Unpin");
        refresh_after_tweet_mutation(tweet_id);
    }
}

static gboolean
perform_tweet_setting_request(const gchar *url, const gchar *member, const gchar *value)
{
    JsonBuilder *builder = json_builder_new();
    JsonGenerator *gen = json_generator_new();
    gchar *payload;
    struct MemoryStruct chunk = {0};
    gboolean ok;

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, member);
    if (value) {
        json_builder_add_string_value(builder, value);
    } else {
        json_builder_add_null_value(builder);
    }
    json_builder_end_object(builder);
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);
    ok = fetch_url(url, &chunk, payload, "PATCH") &&
        chunk.memory && strstr(chunk.memory, "\"error\"") == NULL;
    g_free(chunk.memory);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return ok;
}

static gboolean
perform_tweet_boolean_request(const gchar *url, const gchar *member, gboolean value)
{
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    struct MemoryStruct chunk = {0};
    gboolean ok;

    builder = json_builder_new();
    gen = json_generator_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, member);
    json_builder_add_boolean_value(builder, value);
    json_builder_end_object(builder);
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);
    ok = fetch_url(url, &chunk, payload, "POST") &&
        chunk.memory && strstr(chunk.memory, "\"error\"") == NULL;
    g_free(chunk.memory);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
    return ok;
}

static void
on_tweet_highlight_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    gchar *username;
    GtkWidget *dialog;
    gint response;
    gchar *url;

    (void)user_data;
    if (!tweet_id) return;
    username = get_current_username_safe();
    if (!username) return;

    dialog = gtk_message_dialog_new(GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_NONE,
                                    "Update highlight");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "Keep this post in the Highlights section of your profile?");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                           "_Cancel", GTK_RESPONSE_CANCEL,
                           "_Remove", GTK_RESPONSE_REJECT,
                           "_Highlight", GTK_RESPONSE_ACCEPT,
                           NULL);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_REJECT) {
        url = g_strdup_printf(PROFILE_HIGHLIGHT_URL, username, tweet_id);
        if (perform_tweet_boolean_request(url, "highlighted", response == GTK_RESPONSE_ACCEPT)) {
            refresh_after_tweet_mutation(tweet_id);
            start_loading_profile_highlights(username);
        }
        g_free(url);
    }
    g_free(username);
}

static void
on_tweet_reply_restriction_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    GtkWidget *dialog;
    GtkWidget *combo;
    GtkWidget *content;
    gchar *url;

    (void)user_data;
    if (!tweet_id) return;
    dialog = gtk_dialog_new_with_buttons("Reply Permissions",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "everyone", "Everyone");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "followers", "Followers");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "following", "Following");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "verified", "Verified users");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(content), combo, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        url = g_strdup_printf(TWEET_REPLY_RESTRICTION_URL, tweet_id);
        if (perform_tweet_setting_request(url, "reply_restriction", gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo)))) {
            refresh_after_tweet_mutation(tweet_id);
        }
        g_free(url);
    }
    gtk_widget_destroy(dialog);
}

static void
on_tweet_outline_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    GtkWidget *dialog;
    GtkWidget *entry;
    GtkWidget *content;
    gchar *url;
    const gchar *value;

    (void)user_data;
    if (!tweet_id) return;
    dialog = gtk_dialog_new_with_buttons("Post Outline",
                                         GTK_IS_WINDOW(gtk_widget_get_toplevel(widget)) ? GTK_WINDOW(gtk_widget_get_toplevel(widget)) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         "_Clear", GTK_RESPONSE_REJECT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Outline color");
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_REJECT) {
        url = g_strdup_printf(TWEET_OUTLINE_URL, tweet_id);
        value = response == GTK_RESPONSE_ACCEPT ? gtk_entry_get_text(GTK_ENTRY(entry)) : NULL;
        if (perform_tweet_setting_request(url, "outline", value && value[0] ? value : NULL)) {
            refresh_after_tweet_mutation(tweet_id);
        }
        g_free(url);
    }
    gtk_widget_destroy(dialog);
}

static void
on_reply_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    struct ReplyContext *ctx = (struct ReplyContext *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->text_view));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar *content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        gchar *media_url = NULL;
        gboolean upload_success = TRUE;
        if (ctx->upload.file_path) {
            media_url = perform_media_upload(ctx->upload.file_path);
            if (!media_url) {
                upload_success = FALSE;
            }
        }

        GList *attachments = NULL;
        if (media_url) {
            const gchar *file_type = ctx->upload.file_type ? ctx->upload.file_type : "application/octet-stream";
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
            if (perform_post_tweet(content ? content : "", ctx->reply_to_id, attachments)) {
                refresh_after_tweet_mutation(ctx->reply_to_id);
            } else {
                GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_CLOSE,
                                         "Failed to post reply.");
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
    
    g_free(ctx->reply_to_id);
    g_free(ctx->upload.file_path);
    g_free(ctx->upload.file_type);
    g_free(ctx);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
on_reply_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    if (!is_logged_in()) {
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

    if (!tweet_id || tweet_id[0] == '\0') {
        GtkWidget *error_dialog = gtk_message_dialog_new(window,
                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                 GTK_MESSAGE_ERROR,
                                 GTK_BUTTONS_CLOSE,
                                 "This tweet cannot be replied to right now.");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }

    {
        gchar *url = g_strdup_printf(TWEET_CAN_REPLY_URL, tweet_id);
        struct MemoryStruct chunk = {0};
        gboolean can_reply = FALSE;
        gchar *message = NULL;
        JsonParser *parser = json_parser_new();
        GError *error = NULL;

        if (fetch_url(url, &chunk, NULL, "GET") &&
            json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
            JsonNode *root = json_parser_get_root(parser);
            if (root && JSON_NODE_HOLDS_OBJECT(root)) {
                JsonObject *obj = json_node_get_object(root);
                if (json_object_has_member(obj, "canReply")) {
                    can_reply = json_object_get_boolean_member(obj, "canReply");
                }
                if (!can_reply) {
                    if (json_object_has_member(obj, "reason") &&
                        !json_node_is_null(json_object_get_member(obj, "reason"))) {
                        const gchar *reason = json_object_get_string_member(obj, "reason");
                        if (g_strcmp0(reason, "blocked") == 0) {
                            message = g_strdup("This account has blocked replies from you.");
                        } else if (g_strcmp0(reason, "restriction") == 0) {
                            message = g_strdup("This post has limited who can reply.");
                        }
                    } else if (json_object_has_member(obj, "error") &&
                               !json_node_is_null(json_object_get_member(obj, "error"))) {
                        message = g_strdup(json_object_get_string_member(obj, "error"));
                    }
                }
            }
        }
        if (error) g_error_free(error);
        g_object_unref(parser);
        g_free(chunk.memory);
        g_free(url);

        if (!can_reply) {
            GtkWidget *error_dialog = gtk_message_dialog_new(window,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_INFO,
                                     GTK_BUTTONS_CLOSE,
                                     "You cannot reply to this post.");
            if (message) {
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(error_dialog), "%s", message);
            }
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
            g_free(message);
            return;
        }
        g_free(message);
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Reply to Tweet",
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Reply", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    gchar *replying_to = build_reply_banner_text(username);
    GtkWidget *label = gtk_label_new(replying_to);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_box_pack_start(GTK_BOX(content_area), label, FALSE, FALSE, 5);
    g_free(replying_to);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_widget_set_size_request(text_view, 300, 150);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);
    gtk_box_pack_start(GTK_BOX(content_area), text_view, TRUE, TRUE, 0);

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

    struct ReplyContext *ctx = g_new0(struct ReplyContext, 1);
    ctx->text_view = text_view;
    ctx->reply_to_id = g_strdup(tweet_id);
    ctx->upload.parent_dialog = dialog;
    ctx->upload.file_label = file_label;

    gtk_widget_show_all(dialog);
    g_signal_connect(file_chooser, "file-set", G_CALLBACK(on_file_selected), &ctx->upload);
    g_signal_connect(dialog, "response", G_CALLBACK(on_reply_response), ctx);
}

struct PollVoteData {
    gchar *tweet_id;
    gchar *option_id;
};

typedef enum {
    MULTI_POLL_INPUT_SINGLE,
    MULTI_POLL_INPUT_MULTI,
    MULTI_POLL_INPUT_TEXT,
    MULTI_POLL_INPUT_NUMBER,
    MULTI_POLL_INPUT_RANKING
} MultiPollInputType;

struct MultiPollStepInput {
    MultiPollInputType type;
    GtkWidget *primary;
    GPtrArray *checks;
    guint option_count;
};

static void
free_poll_vote_data(gpointer data)
{
    struct PollVoteData *vote_data = (struct PollVoteData *)data;
    if (vote_data) {
        g_free(vote_data->tweet_id);
        g_free(vote_data->option_id);
        g_free(vote_data);
    }
}

static void
free_multi_poll_step_input(gpointer data)
{
    struct MultiPollStepInput *input = data;
    if (!input) return;
    if (input->checks)
        g_ptr_array_unref(input->checks);
    g_free(input);
}

static void
on_poll_option_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    struct PollVoteData *vote_data = (struct PollVoteData *)user_data;

    if (!is_logged_in()) {
        return;
    }

    if (perform_poll_vote(vote_data->tweet_id, vote_data->option_id)) {
        show_tweet(vote_data->tweet_id);
    }
}

static const gchar*
json_object_get_string_default(JsonObject *object, const gchar *member, const gchar *fallback)
{
    JsonNode *node;

    if (!object || !json_object_has_member(object, member))
        return fallback;

    node = json_object_get_member(object, member);
    if (!node || !JSON_NODE_HOLDS_VALUE(node))
        return fallback;

    return json_object_get_string_member(object, member);
}

static int
json_object_get_int_default(JsonObject *object, const gchar *member, int fallback)
{
    JsonNode *node;

    if (!object || !json_object_has_member(object, member))
        return fallback;

    node = json_object_get_member(object, member);
    if (!node || !JSON_NODE_HOLDS_VALUE(node))
        return fallback;

    return json_object_get_int_member(object, member);
}

static JsonArray*
json_object_get_array_member_valid(JsonObject *object, const gchar *member)
{
    JsonNode *node;

    if (!object || !member || !json_object_has_member(object, member))
        return NULL;

    node = json_object_get_member(object, member);
    if (!node || !JSON_NODE_HOLDS_ARRAY(node))
        return NULL;

    return json_node_get_array(node);
}

static JsonObject*
json_object_get_object_member_valid(JsonObject *object, const gchar *member)
{
    JsonNode *node;

    if (!object || !member || !json_object_has_member(object, member))
        return NULL;

    node = json_object_get_member(object, member);
    if (!node || !JSON_NODE_HOLDS_OBJECT(node))
        return NULL;

    return json_node_get_object(node);
}

static JsonObject*
json_array_get_object_element_valid(JsonArray *array, guint index)
{
    JsonNode *node;

    if (!array)
        return NULL;

    node = json_array_get_element(array, index);
    if (!node || !JSON_NODE_HOLDS_OBJECT(node))
        return NULL;

    return json_node_get_object(node);
}

static void
show_poll_message(GtkWindow *parent, GtkMessageType type, const gchar *title, const gchar *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_MODAL,
                                               type,
                                               GTK_BUTTONS_OK,
                                               "%s",
                                               title);
    if (message)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
show_dm_payment_message(GtkWindow *parent, GtkMessageType type, const gchar *title, const gchar *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_MODAL,
                                               type,
                                               GTK_BUTTONS_OK,
                                               "%s",
                                               title);
    if (message)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static gboolean
append_ranking_answer(JsonBuilder *builder, const gchar *text, guint option_count)
{
    gboolean *seen = g_new0(gboolean, option_count);
    gchar **parts = g_strsplit(text ? text : "", ",", -1);
    guint count = 0;
    gboolean valid = TRUE;

    json_builder_begin_array(builder);
    for (guint i = 0; parts[i]; i++) {
        gchar *part = g_strstrip(parts[i]);
        if (!*part) continue;
        gchar *end = NULL;
        long one_based = strtol(part, &end, 10);
        if ((end && *end) || one_based < 1 || one_based > (long)option_count ||
            seen[one_based - 1]) {
            valid = FALSE;
            break;
        }
        seen[one_based - 1] = TRUE;
        json_builder_add_int_value(builder, one_based - 1);
        count++;
    }
    json_builder_end_array(builder);

    if (count != option_count)
        valid = FALSE;

    g_strfreev(parts);
    g_free(seen);
    return valid;
}

static void
on_multi_poll_answer_clicked(GtkWidget *widget, gpointer user_data)
{
    struct Poll *poll = user_data;
    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *parent = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    if (!is_logged_in())
        return;

    if (!poll || !poll->steps || !JSON_NODE_HOLDS_ARRAY(poll->steps))
        return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Answer poll",
                                                    parent,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Submit", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

    GPtrArray *inputs = g_ptr_array_new_with_free_func(free_multi_poll_step_input);
    JsonArray *steps = json_node_get_array(poll->steps);
    for (guint i = 0; i < json_array_get_length(steps); i++) {
        JsonObject *step = json_array_get_object_element_valid(steps, i);
        if (!step) continue;

        const gchar *type = json_object_get_string_default(step, "type", "single");
        const gchar *question = json_object_get_string_default(step, "question", "Question");
        JsonArray *options = json_object_get_array_member_valid(step, "options");

        GtkWidget *section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gchar *label_text = g_strdup_printf("<b>%u. %s</b>", i + 1, question);
        GtkWidget *label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), label_text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_box_pack_start(GTK_BOX(section), label, FALSE, FALSE, 0);
        g_free(label_text);

        struct MultiPollStepInput *input = g_new0(struct MultiPollStepInput, 1);
        input->option_count = options ? json_array_get_length(options) : 0;

        if (g_strcmp0(type, "multi") == 0) {
            input->type = MULTI_POLL_INPUT_MULTI;
            input->checks = g_ptr_array_new();
            for (guint j = 0; j < input->option_count; j++) {
                JsonObject *option = json_array_get_object_element_valid(options, j);
                const gchar *text = json_object_get_string_default(option, "text", "Option");
                GtkWidget *check = gtk_check_button_new_with_label(text);
                gtk_box_pack_start(GTK_BOX(section), check, FALSE, FALSE, 0);
                g_ptr_array_add(input->checks, check);
            }
        } else if (g_strcmp0(type, "text") == 0) {
            input->type = MULTI_POLL_INPUT_TEXT;
            input->primary = gtk_entry_new();
            gtk_entry_set_placeholder_text(GTK_ENTRY(input->primary),
                                           json_object_get_string_default(step, "text_placeholder", "Type your answer"));
            gtk_box_pack_start(GTK_BOX(section), input->primary, FALSE, FALSE, 0);
        } else if (g_strcmp0(type, "number") == 0 || g_strcmp0(type, "scale") == 0) {
            int min = g_strcmp0(type, "scale") == 0
                ? json_object_get_int_default(step, "scale_min", 1)
                : json_object_get_int_default(step, "number_min", 0);
            int max = g_strcmp0(type, "scale") == 0
                ? json_object_get_int_default(step, "scale_max", 5)
                : json_object_get_int_default(step, "number_max", 1000000);
            input->type = MULTI_POLL_INPUT_NUMBER;
            input->primary = gtk_spin_button_new_with_range(min, max, 1);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(input->primary), min);
            gtk_box_pack_start(GTK_BOX(section), input->primary, FALSE, FALSE, 0);
        } else if (g_strcmp0(type, "ranking") == 0) {
            input->type = MULTI_POLL_INPUT_RANKING;
            input->primary = gtk_entry_new();
            gtk_entry_set_placeholder_text(GTK_ENTRY(input->primary), "Rank all options, e.g. 2,1,3");
            for (guint j = 0; j < input->option_count; j++) {
                JsonObject *option = json_array_get_object_element_valid(options, j);
                const gchar *text = json_object_get_string_default(option, "text", "Option");
                gchar *option_label = g_strdup_printf("%u. %s", j + 1, text);
                GtkWidget *opt = gtk_label_new(option_label);
                gtk_label_set_xalign(GTK_LABEL(opt), 0.0);
                gtk_box_pack_start(GTK_BOX(section), opt, FALSE, FALSE, 0);
                g_free(option_label);
            }
            gtk_box_pack_start(GTK_BOX(section), input->primary, FALSE, FALSE, 0);
        } else {
            input->type = MULTI_POLL_INPUT_SINGLE;
            input->primary = gtk_combo_box_text_new();
            for (guint j = 0; j < input->option_count; j++) {
                JsonObject *option = json_array_get_object_element_valid(options, j);
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(input->primary),
                                               json_object_get_string_default(option, "text", "Option"));
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(input->primary), 0);
            gtk_box_pack_start(GTK_BOX(section), input->primary, FALSE, FALSE, 0);
        }

        g_ptr_array_add(inputs, input);
        gtk_box_pack_start(GTK_BOX(box), section, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        JsonBuilder *builder = json_builder_new();
        gboolean valid = TRUE;
        json_builder_begin_array(builder);
        for (guint i = 0; i < inputs->len; i++) {
            struct MultiPollStepInput *input = g_ptr_array_index(inputs, i);
            switch (input->type) {
            case MULTI_POLL_INPUT_SINGLE:
                json_builder_add_int_value(builder, gtk_combo_box_get_active(GTK_COMBO_BOX(input->primary)));
                break;
            case MULTI_POLL_INPUT_MULTI: {
                guint selected = 0;
                json_builder_begin_array(builder);
                for (guint j = 0; j < input->checks->len; j++) {
                    GtkWidget *check = g_ptr_array_index(input->checks, j);
                    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check))) {
                        json_builder_add_int_value(builder, j);
                        selected++;
                    }
                }
                json_builder_end_array(builder);
                if (selected == 0) valid = FALSE;
                break;
            }
            case MULTI_POLL_INPUT_TEXT: {
                const gchar *text = gtk_entry_get_text(GTK_ENTRY(input->primary));
                gchar *trimmed = g_strdup(text ? text : "");
                if (!g_strstrip(trimmed)[0]) valid = FALSE;
                g_free(trimmed);
                json_builder_add_string_value(builder, text ? text : "");
                break;
            }
            case MULTI_POLL_INPUT_NUMBER:
                json_builder_add_int_value(builder, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(input->primary)));
                break;
            case MULTI_POLL_INPUT_RANKING:
                valid = append_ranking_answer(builder, gtk_entry_get_text(GTK_ENTRY(input->primary)), input->option_count) && valid;
                break;
            }
        }
        json_builder_end_array(builder);

        JsonNode *answers = json_builder_get_root(builder);
        gchar *message = NULL;
        if (!valid) {
            show_poll_message(GTK_WINDOW(dialog), GTK_MESSAGE_ERROR, "Poll answer incomplete.", "Answer every step before submitting.");
        } else if (perform_poll_multi_vote(tweet_id, answers, &message)) {
            if (message)
                show_poll_message(GTK_WINDOW(dialog), GTK_MESSAGE_INFO, "Poll submitted.", message);
            show_tweet(tweet_id);
        } else {
            show_poll_message(GTK_WINDOW(dialog), GTK_MESSAGE_ERROR, "Poll submission failed.", message);
        }
        g_free(message);
        json_node_free(answers);
        g_object_unref(builder);
    }

    g_ptr_array_unref(inputs);
    gtk_widget_destroy(dialog);
}

static GtkWidget*
create_multi_poll_step_results(JsonObject *step, guint index, int total_votes, gboolean reveal_quiz)
{
    GtkWidget *section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    const gchar *question = json_object_get_string_default(step, "question", "Question");
    const gchar *type = json_object_get_string_default(step, "type", "single");
    gchar *title = g_strdup_printf("<b>%u. %s</b>", index + 1, question);
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), title);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(section), label, FALSE, FALSE, 0);
    g_free(title);

    JsonArray *options = json_object_get_array_member_valid(step, "options");

    if (options && json_array_get_length(options) > 0) {
        for (guint j = 0; j < json_array_get_length(options); j++) {
            JsonObject *option = json_array_get_object_element_valid(options, j);
            const gchar *text = json_object_get_string_default(option, "text", "Option");
            int votes = json_object_get_int_default(option, "vote_count", 0);
            int pct = json_object_get_int_default(option, "percentage", total_votes > 0 ? (votes * 100 / total_votes) : 0);
            gboolean correct = reveal_quiz && json_object_has_member(option, "is_correct") &&
                json_object_get_boolean_member(option, "is_correct");
            gchar *row = NULL;
            if (g_strcmp0(type, "ranking") == 0 && json_object_has_member(option, "average_rank")) {
                double rank = json_object_get_double_member(option, "average_rank");
                row = g_strdup_printf("%s%s · avg rank %.1f", correct ? "✓ " : "", text, rank);
            } else {
                row = g_strdup_printf("%s%s · %d%% (%d)", correct ? "✓ " : "", text, pct, votes);
            }
            GtkWidget *row_label = gtk_label_new(row);
            gtk_label_set_xalign(GTK_LABEL(row_label), 0.0);
            gtk_box_pack_start(GTK_BOX(section), row_label, FALSE, FALSE, 0);
            g_free(row);
        }
    } else if (g_strcmp0(type, "number") == 0) {
        JsonObject *stats = json_object_get_object_member_valid(step, "number_stats");
        if (!stats)
            return section;
        gchar *summary = g_strdup_printf("Average %.1f · min %.1f · max %.1f · %ld responses",
                                         json_object_get_double_member(stats, "average"),
                                         json_object_get_double_member(stats, "min"),
                                         json_object_get_double_member(stats, "max"),
                                         (long)json_object_get_int_member(stats, "count"));
        GtkWidget *summary_label = gtk_label_new(summary);
        gtk_label_set_xalign(GTK_LABEL(summary_label), 0.0);
        gtk_box_pack_start(GTK_BOX(section), summary_label, FALSE, FALSE, 0);
        g_free(summary);
    } else if (g_strcmp0(type, "text") == 0) {
        int responses = json_object_get_int_default(step, "text_response_count", 0);
        gchar *summary = g_strdup_printf("%d text responses", responses);
        GtkWidget *summary_label = gtk_label_new(summary);
        gtk_label_set_xalign(GTK_LABEL(summary_label), 0.0);
        gtk_box_pack_start(GTK_BOX(section), summary_label, FALSE, FALSE, 0);
        g_free(summary);
    }

    return section;
}

GtkWidget*
create_poll_widget(struct Poll *poll, const gchar *tweet_id)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    GtkStyleContext *frame_context = gtk_widget_get_style_context(frame);
    gtk_style_context_add_class(frame_context, "poll-frame");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    GtkWidget *question_label = gtk_label_new(NULL);
    gchar *question_markup = g_strdup_printf("<b>%s</b>", poll->question);
    gtk_label_set_markup(GTK_LABEL(question_label), question_markup);
    g_free(question_markup);
    gtk_label_set_xalign(GTK_LABEL(question_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(question_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), question_label, FALSE, FALSE, 0);

    gboolean multi_poll = poll->kind && g_strcmp0(poll->kind, "single") != 0;

    if (multi_poll && poll->steps && JSON_NODE_HOLDS_ARRAY(poll->steps)) {
        gboolean show_results = !poll->is_active || poll->has_user_answers;
        if (show_results) {
            JsonArray *steps = json_node_get_array(poll->steps);
            gboolean reveal_quiz = g_strcmp0(poll->kind, "quiz") == 0 && poll->has_user_answers;
            if (reveal_quiz && poll->user_total > 0) {
                gchar *score = g_strdup_printf("Score: %d/%d", poll->user_score, poll->user_total);
                GtkWidget *score_label = gtk_label_new(score);
                gtk_label_set_xalign(GTK_LABEL(score_label), 0.0);
                gtk_box_pack_start(GTK_BOX(box), score_label, FALSE, FALSE, 0);
                g_free(score);
            }
            for (guint i = 0; i < json_array_get_length(steps); i++) {
                JsonObject *step = json_array_get_object_element_valid(steps, i);
                if (step)
                    gtk_box_pack_start(GTK_BOX(box),
                                       create_multi_poll_step_results(step, i, poll->total_votes, reveal_quiz),
                                       FALSE, FALSE, 0);
            }
        } else {
            GtkWidget *answer_btn = gtk_button_new_with_label("Answer poll");
            gtk_button_set_relief(GTK_BUTTON(answer_btn), GTK_RELIEF_NORMAL);
            g_object_set_data_full(G_OBJECT(answer_btn), "tweet_id", g_strdup(tweet_id), g_free);
            g_signal_connect(answer_btn, "clicked", G_CALLBACK(on_multi_poll_answer_clicked), poll);
            gtk_box_pack_start(GTK_BOX(box), answer_btn, FALSE, FALSE, 0);
        }

        GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gchar *status_text = poll->is_active
            ? g_strdup_printf("Active · %d submissions", poll->total_votes)
            : g_strdup_printf("Closed · %d submissions", poll->total_votes);
        GtkWidget *status_label = gtk_label_new(status_text);
        gtk_label_set_xalign(GTK_LABEL(status_label), 0.0);
        gtk_style_context_add_class(gtk_widget_get_style_context(status_label), "dim-label");
        gtk_box_pack_start(GTK_BOX(status_box), status_label, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), status_box, FALSE, FALSE, 0);
        g_free(status_text);
        gtk_container_add(GTK_CONTAINER(frame), box);
        return frame;
    }

    // Check if user has voted or poll is closed
    gboolean show_results = !poll->is_active;
    if (!show_results) {
        for (GList *l = poll->options; l != NULL; l = l->next) {
            struct PollOption *option = l->data;
            if (option->voted) {
                show_results = TRUE;
                break;
            }
        }
    }

    // Options
    for (GList *l = poll->options; l != NULL; l = l->next) {
        struct PollOption *option = l->data;

        double percentage = 0.0;
        if (poll->total_votes > 0) {
            percentage = (double)option->vote_count / (double)poll->total_votes * 100.0;
        }

        if (show_results) {
            GtkWidget *option_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

            GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

            // Option text with vote count
            gchar *option_text;
            if (option->voted) {
                option_text = g_strdup_printf("✓ %s", option->option_text);
            } else {
                option_text = g_strdup_printf("  %s", option->option_text);
            }
            GtkWidget *text_label = gtk_label_new(option_text);
            gtk_label_set_xalign(GTK_LABEL(text_label), 0.0);
            g_free(option_text);

            // Percentage label
            gchar *percent_text = g_strdup_printf("%.1f%% (%d)", percentage, option->vote_count);
            GtkWidget *percent_label = gtk_label_new(percent_text);
            gtk_label_set_xalign(GTK_LABEL(percent_label), 1.0);
            g_free(percent_text);

            gtk_box_pack_start(GTK_BOX(hbox), text_label, TRUE, TRUE, 0);
            gtk_box_pack_end(GTK_BOX(hbox), percent_label, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(option_box), hbox, FALSE, FALSE, 0);

            // Progress bar
            GtkWidget *progress = gtk_progress_bar_new();
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), percentage / 100.0);
            gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress), FALSE);

            // Highlight user's vote
            if (option->voted) {
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), 1.0);
            }

            gtk_box_pack_start(GTK_BOX(option_box), progress, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(box), option_box, FALSE, FALSE, 0);
        } else {
            // Voting view with clickable buttons
            GtkWidget *vote_btn = gtk_button_new_with_label(option->option_text);
            gtk_button_set_relief(GTK_BUTTON(vote_btn), GTK_RELIEF_NORMAL);
            gtk_widget_set_halign(vote_btn, GTK_ALIGN_FILL);

            struct PollVoteData *vote_data = g_new0(struct PollVoteData, 1);
            vote_data->tweet_id = g_strdup(tweet_id);
            vote_data->option_id = g_strdup(option->id);

            g_object_set_data_full(G_OBJECT(vote_btn), "poll_vote_data", vote_data, free_poll_vote_data);
            g_signal_connect(vote_btn, "clicked", G_CALLBACK(on_poll_option_clicked), vote_data);

            gtk_box_pack_start(GTK_BOX(box), vote_btn, FALSE, FALSE, 0);
        }
    }

    // Status and total votes
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    gchar *status_text;
    if (poll->is_active) {
        status_text = g_strdup_printf("Active · %d votes", poll->total_votes);
    } else {
        status_text = g_strdup_printf("Closed · %d votes", poll->total_votes);
    }
    GtkWidget *status_label = gtk_label_new(status_text);
    gtk_label_set_xalign(GTK_LABEL(status_label), 0.0);
    GtkStyleContext *status_context = gtk_widget_get_style_context(status_label);
    gtk_style_context_add_class(status_context, "dim-label");
    g_free(status_text);

    gtk_box_pack_start(GTK_BOX(status_box), status_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), status_box, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);

    return frame;
}

static void
on_video_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    const gchar *url = g_object_get_data(G_OBJECT(widget), "url");
    if (url) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
        GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
        gtk_show_uri_on_window(window, url, GDK_CURRENT_TIME, NULL);
    }
}

static void
add_attachments_to_box(GtkBox *box, GList *attachments)
{
    if (!attachments) return;

    for (GList *l = attachments; l != NULL; l = l->next) {
        struct Attachment *attach = l->data;
        if (attach->file_type && g_str_has_prefix(attach->file_type, "image/")) {
            GtkWidget *image = gtk_image_new();
            load_avatar(image, attach->file_url, MEDIA_SIZE);
            gtk_box_pack_start(box, image, FALSE, FALSE, 5);
        } else if (attach->file_type && g_str_has_prefix(attach->file_type, "video/")) {
            GtkWidget *video_btn = gtk_button_new_with_label("Play Video ▶");
            g_object_set_data_full(G_OBJECT(video_btn), "url", g_strdup(attach->file_url), g_free);
            g_signal_connect(video_btn, "clicked", G_CALLBACK(on_video_clicked), NULL);
            gtk_box_pack_start(box, video_btn, FALSE, FALSE, 5);
        } else {
            gchar *link_text = g_strdup_printf("Attachment (%s): %s", 
                                              attach->file_type ? attach->file_type : "unknown", 
                                              attach->file_url);
            GtkWidget *label = gtk_label_new(link_text);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
            gtk_box_pack_start(box, label, FALSE, FALSE, 5);
            g_free(link_text);
        }
    }
}

static gboolean
on_tweet_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)user_data;
    if (event->button != 1) return FALSE;

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    if (tweet_id) {
        show_tweet(tweet_id);
    }
    return TRUE;
}

static void
on_admin_delete_post_activated(GtkMenuItem *menuitem, gpointer user_data)
{
    (void)menuitem;
    const gchar *post_id = (const gchar *)user_data;
    perform_admin_delete_post(post_id);
}

static gboolean
on_admin_post_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)user_data;
    if (event->button == 3) {
        const gchar *post_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
        if (!post_id) return FALSE;

        GtkWidget *menu = gtk_menu_new();
        GtkWidget *delete_item = gtk_menu_item_new_with_label("Admin: Delete Post");
        g_signal_connect_data(delete_item, "activate", G_CALLBACK(on_admin_delete_post_activated), g_strdup(post_id), free_wrapper, 0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), delete_item);
        
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

static void
on_admin_verify_user_activated(GtkMenuItem *menuitem, gpointer user_data)
{
    (void)menuitem;
    const gchar *username = (const gchar *)user_data;
    perform_admin_verify(username, TRUE);
}

static void
on_admin_delete_user_activated(GtkMenuItem *menuitem, gpointer user_data)
{
    (void)menuitem;
    const gchar *username = (const gchar *)user_data;
    perform_admin_delete_user(username);
}

static gboolean
on_admin_user_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)user_data;
    if (event->button == 3) {
        const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
        if (!username) return FALSE;

        GtkWidget *menu = gtk_menu_new();
        
        GtkWidget *verify_item = gtk_menu_item_new_with_label("Admin: Verify User");
        g_signal_connect_data(verify_item, "activate", G_CALLBACK(on_admin_verify_user_activated), g_strdup(username), free_wrapper, 0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), verify_item);

        GtkWidget *delete_item = gtk_menu_item_new_with_label("Admin: Delete User");
        g_signal_connect_data(delete_item, "activate", G_CALLBACK(on_admin_delete_user_activated), g_strdup(username), free_wrapper, 0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), delete_item);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

GtkWidget*
create_quoted_tweet_widget(struct Tweet *tweet)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *avatar_image = gtk_image_new();
    gtk_widget_set_size_request(avatar_image, 20, 20);
    load_avatar(avatar_image, tweet->author_avatar, 20);
    
    const gchar *author_name = (tweet->author_name && tweet->author_name[0] != '\0') ? tweet->author_name : "Unknown";
    const gchar *author_username = (tweet->author_username && tweet->author_username[0] != '\0') ? tweet->author_username : "unknown";
    gchar *author_str = g_strdup_printf("<b>%s</b> <span foreground='gray'>@%s</span>", author_name, author_username);
    GtkWidget *author_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(author_label), author_str);
    g_free(author_str);
    
    gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), author_label, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);
    
    if (tweet->content && strlen(tweet->content) > 0) {
        GtkWidget *content_label = gtk_label_new(tweet->content);
        gtk_label_set_xalign(GTK_LABEL(content_label), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(content_label), TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(content_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(content_label), 50);
        gtk_box_pack_start(GTK_BOX(box), content_label, FALSE, FALSE, 0);
    }
    
    add_attachments_to_box(GTK_BOX(box), tweet->attachments);
    
    gtk_container_add(GTK_CONTAINER(frame), box);
    
    // Make the whole frame clickable to go to the quoted tweet
    GtkWidget *event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(event_box), frame);
    g_object_set_data_full(G_OBJECT(event_box), "tweet_id", g_strdup(tweet->id), g_free);
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_tweet_clicked), NULL);
    
    return event_box;
}

GtkWidget*
create_tweet_widget(struct Tweet *tweet)
{
    return create_tweet_widget_full(tweet, NULL);
}

GtkWidget*
create_tweet_widget_full(struct Tweet *tweet, const gchar *op_username)
{
    GtkWidget *outer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    GtkWidget *event_box = gtk_event_box_new();
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    GtkWidget *avatar_image = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(avatar_image, AVATAR_SIZE, AVATAR_SIZE);
    gtk_widget_set_valign(avatar_image, GTK_ALIGN_START);
    load_avatar(avatar_image, tweet->author_avatar, AVATAR_SIZE);

    GtkWidget *author_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gchar *author_str = build_account_label_text(tweet->author_name, tweet->author_username);

    GtkWidget *author_btn = gtk_button_new_with_label(author_str);
    gtk_button_set_relief(GTK_BUTTON(author_btn), GTK_RELIEF_NONE);
    gtk_widget_set_halign(author_btn, GTK_ALIGN_START);
    
    GtkWidget *label = gtk_bin_get_child(GTK_BIN(author_btn));
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(label), attrs);
    pango_attr_list_unref(attrs);

    g_object_set_data_full(G_OBJECT(author_btn), "username", g_strdup(tweet->author_username), g_free);
    g_signal_connect(author_btn, "clicked", G_CALLBACK(on_author_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(author_hbox), author_btn, FALSE, FALSE, 0);
    append_account_badges(author_hbox, tweet->author_verified, tweet->author_gold, tweet->author_gray);

    if (op_username && g_strcmp0(tweet->author_username, op_username) == 0) {
        GtkWidget *op_label = gtk_label_new("OP");
        GtkStyleContext *context = gtk_widget_get_style_context(op_label);
        gtk_style_context_add_class(context, "op-badge");
        
        // Manual styling if CSS classes aren't enough/defined
        gtk_label_set_markup(GTK_LABEL(op_label), "<span foreground='white' background='#007bff' size='small' weight='bold'> OP </span>");
        
        gtk_box_pack_start(GTK_BOX(author_hbox), op_label, FALSE, FALSE, 0);
    }

    GtkWidget *content_label = gtk_label_new(tweet->content);
    gtk_label_set_xalign(GTK_LABEL(content_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(content_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(content_label), TRUE);

    gtk_box_pack_start(GTK_BOX(box), author_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), content_label, FALSE, FALSE, 0);

    gchar *meta_text = build_tweet_meta_text(tweet);
    GtkWidget *meta_label = gtk_label_new(meta_text);
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0);
    GtkStyleContext *meta_context = gtk_widget_get_style_context(meta_label);
    gtk_style_context_add_class(meta_context, "dim-label");
    gtk_box_pack_start(GTK_BOX(box), meta_label, FALSE, FALSE, 0);
    g_free(meta_text);

    if (tweet->note) {
        GtkWidget *note_frame = gtk_frame_new(NULL);
        GtkStyleContext *frame_context = gtk_widget_get_style_context(note_frame);
        gtk_style_context_add_class(frame_context, "note-frame");
        
        if (tweet->note_severity) {
            if (g_strcmp0(tweet->note_severity, "danger") == 0) {
                gtk_style_context_add_class(frame_context, "note-danger");
            } else if (g_strcmp0(tweet->note_severity, "info") == 0) {
                gtk_style_context_add_class(frame_context, "note-info");
            } else {
                gtk_style_context_add_class(frame_context, "note-warning");
            }
        } else {
            gtk_style_context_add_class(frame_context, "note-warning");
        }
        
        GtkWidget *note_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(note_box), 10);
        
        GtkWidget *note_header = gtk_label_new("⚠ Note");
        PangoAttrList *note_attrs = pango_attr_list_new();
        pango_attr_list_insert(note_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes(GTK_LABEL(note_header), note_attrs);
        pango_attr_list_unref(note_attrs);
        gtk_widget_set_halign(note_header, GTK_ALIGN_START);

        GtkWidget *note_label = gtk_label_new(tweet->note);
        gtk_label_set_xalign(GTK_LABEL(note_label), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(note_label), TRUE);

        gtk_box_pack_start(GTK_BOX(note_box), note_header, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(note_box), note_label, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(note_frame), note_box);
        
        gtk_box_pack_start(GTK_BOX(box), note_frame, FALSE, FALSE, 5);
    }

    add_attachments_to_box(GTK_BOX(box), tweet->attachments);

    if (tweet->quote_tweet) {
        GtkWidget *quote_widget = create_quoted_tweet_widget(tweet->quote_tweet);
        gtk_box_pack_start(GTK_BOX(box), quote_widget, FALSE, FALSE, 5);
    }

    if (tweet->poll) {
        GtkWidget *poll_widget = create_poll_widget(tweet->poll, tweet->id);
        gtk_box_pack_start(GTK_BOX(box), poll_widget, FALSE, FALSE, 5);
    }

    gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), box, TRUE, TRUE, 0);
    
    gtk_container_add(GTK_CONTAINER(event_box), hbox);
    g_object_set_data_full(G_OBJECT(event_box), "tweet_id", g_strdup(tweet->id), g_free);
    
    if (is_admin_user()) {
        g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_admin_post_button_press), NULL);
    }
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_tweet_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(outer_box), event_box, TRUE, TRUE, 0);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(button_box, GTK_ALIGN_START);
    gtk_container_set_border_width(GTK_CONTAINER(button_box), 5);

    gboolean *liked_state = g_new(gboolean, 1);
    *liked_state = tweet->liked;
    GtkWidget *like_btn = gtk_button_new_with_label(tweet->liked ? "♥ Liked" : "♡ Like");
    gtk_button_set_relief(GTK_BUTTON(like_btn), GTK_RELIEF_NONE);
    g_object_set_data_full(G_OBJECT(like_btn), "tweet_id", g_strdup(tweet->id), g_free);
    g_object_set_data_full(G_OBJECT(like_btn), "liked_state", liked_state, g_free);
    g_signal_connect(like_btn, "clicked", G_CALLBACK(on_like_clicked), NULL);

    gboolean *retweeted_state = g_new(gboolean, 1);
    *retweeted_state = tweet->retweeted;
    GtkWidget *retweet_btn = gtk_button_new_with_label(tweet->retweeted ? "↻ Retweeted" : "↻ Retweet");
    gtk_button_set_relief(GTK_BUTTON(retweet_btn), GTK_RELIEF_NONE);
    g_object_set_data_full(G_OBJECT(retweet_btn), "tweet_id", g_strdup(tweet->id), g_free);
    g_object_set_data_full(G_OBJECT(retweet_btn), "retweeted_state", retweeted_state, g_free);
    g_signal_connect(retweet_btn, "clicked", G_CALLBACK(on_retweet_button_clicked), NULL);

    GtkWidget *reply_btn = gtk_button_new_with_label("↩ Reply");
    gtk_button_set_relief(GTK_BUTTON(reply_btn), GTK_RELIEF_NONE);
    g_object_set_data_full(G_OBJECT(reply_btn), "tweet_id", g_strdup(tweet->id), g_free);
    g_object_set_data_full(G_OBJECT(reply_btn), "username", g_strdup(tweet->author_username), g_free);
    g_signal_connect(reply_btn, "clicked", G_CALLBACK(on_reply_clicked), NULL);

    gboolean *bookmarked_state = g_new(gboolean, 1);
    *bookmarked_state = tweet->bookmarked;
    GtkWidget *bookmark_btn = gtk_button_new_with_label(tweet->bookmarked ? "★ Saved" : "☆ Bookmark");
    gtk_button_set_relief(GTK_BUTTON(bookmark_btn), GTK_RELIEF_NONE);
    g_object_set_data_full(G_OBJECT(bookmark_btn), "tweet_id", g_strdup(tweet->id), g_free);
    g_object_set_data_full(G_OBJECT(bookmark_btn), "bookmarked_state", bookmarked_state, g_free);
    g_signal_connect(bookmark_btn, "clicked", G_CALLBACK(on_bookmark_clicked), NULL);

    GtkWidget *reaction_btn = gtk_button_new_with_label("😀 React");
    gtk_button_set_relief(GTK_BUTTON(reaction_btn), GTK_RELIEF_NONE);
    g_object_set_data_full(G_OBJECT(reaction_btn), "tweet_id", g_strdup(tweet->id), g_free);
    g_signal_connect(reaction_btn, "clicked", G_CALLBACK(on_reaction_clicked), NULL);

    GtkWidget *history_btn = gtk_button_new_with_label("History");
    gtk_button_set_relief(GTK_BUTTON(history_btn), GTK_RELIEF_NONE);
    g_object_set_data_full(G_OBJECT(history_btn), "tweet_id", g_strdup(tweet->id), g_free);
    g_signal_connect(history_btn, "clicked", G_CALLBACK(on_tweet_history_clicked), NULL);

    GtkWidget *reactions_btn = gtk_button_new_with_label("Reactions");
    gtk_button_set_relief(GTK_BUTTON(reactions_btn), GTK_RELIEF_NONE);
    g_object_set_data_full(G_OBJECT(reactions_btn), "tweet_id", g_strdup(tweet->id), g_free);
    g_signal_connect(reactions_btn, "clicked", G_CALLBACK(on_tweet_reactions_clicked), NULL);

    GtkWidget *translate_btn = gtk_button_new_with_label("Translate");
    gtk_button_set_relief(GTK_BUTTON(translate_btn), GTK_RELIEF_NONE);
    g_object_set_data_full(G_OBJECT(translate_btn), "tweet_content", g_strdup(tweet->content), g_free);
    g_signal_connect(translate_btn, "clicked", G_CALLBACK(on_translate_tweet_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(button_box), like_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), retweet_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), reply_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), bookmark_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), reaction_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), history_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), reactions_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), translate_btn, FALSE, FALSE, 0);

    gchar *current_username = get_current_username_safe();
    if (g_auth_token &&
        (!current_username || !tweet->author_username ||
         g_strcmp0(current_username, tweet->author_username) != 0)) {
        GtkWidget *report_btn = gtk_button_new_with_label("Report");
        GtkWidget *mute_thread_btn = gtk_button_new_with_label("Mute thread");
        gtk_button_set_relief(GTK_BUTTON(report_btn), GTK_RELIEF_NONE);
        gtk_button_set_relief(GTK_BUTTON(mute_thread_btn), GTK_RELIEF_NONE);
        g_object_set_data_full(G_OBJECT(report_btn), "tweet_id", g_strdup(tweet->id), g_free);
        g_object_set_data_full(G_OBJECT(mute_thread_btn), "tweet_id", g_strdup(tweet->id), g_free);
        g_signal_connect(report_btn, "clicked", G_CALLBACK(on_report_tweet_clicked), NULL);
        g_signal_connect(mute_thread_btn, "clicked", G_CALLBACK(on_mute_conversation_clicked), NULL);
        gtk_box_pack_start(GTK_BOX(button_box), report_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(button_box), mute_thread_btn, FALSE, FALSE, 0);
    }

    if (current_username && tweet->author_username &&
        g_strcmp0(current_username, tweet->author_username) == 0) {
        GtkWidget *edit_btn = gtk_button_new_with_label("Edit");
        GtkWidget *delete_btn = gtk_button_new_with_label("Delete");
        GtkWidget *pin_btn = gtk_button_new_with_label(tweet->pinned ? "Unpin" : "Pin");
        GtkWidget *highlight_btn = gtk_button_new_with_label("Highlight");
        GtkWidget *reply_permissions_btn = gtk_button_new_with_label("Replies");
        GtkWidget *outline_btn = gtk_button_new_with_label("Outline");

        gtk_button_set_relief(GTK_BUTTON(edit_btn), GTK_RELIEF_NONE);
        gtk_button_set_relief(GTK_BUTTON(delete_btn), GTK_RELIEF_NONE);
        gtk_button_set_relief(GTK_BUTTON(pin_btn), GTK_RELIEF_NONE);
        gtk_button_set_relief(GTK_BUTTON(highlight_btn), GTK_RELIEF_NONE);
        gtk_button_set_relief(GTK_BUTTON(reply_permissions_btn), GTK_RELIEF_NONE);
        gtk_button_set_relief(GTK_BUTTON(outline_btn), GTK_RELIEF_NONE);

        g_object_set_data_full(G_OBJECT(edit_btn), "tweet_id", g_strdup(tweet->id), g_free);
        g_object_set_data_full(G_OBJECT(edit_btn), "tweet_content", g_strdup(tweet->content), g_free);
        g_object_set_data_full(G_OBJECT(delete_btn), "tweet_id", g_strdup(tweet->id), g_free);
        g_object_set_data_full(G_OBJECT(pin_btn), "tweet_id", g_strdup(tweet->id), g_free);
        g_object_set_data(G_OBJECT(pin_btn), "tweet_pinned", GINT_TO_POINTER(tweet->pinned));
        g_object_set_data_full(G_OBJECT(highlight_btn), "tweet_id", g_strdup(tweet->id), g_free);
        g_object_set_data_full(G_OBJECT(reply_permissions_btn), "tweet_id", g_strdup(tweet->id), g_free);
        g_object_set_data_full(G_OBJECT(outline_btn), "tweet_id", g_strdup(tweet->id), g_free);

        g_signal_connect(edit_btn, "clicked", G_CALLBACK(on_tweet_edit_clicked), NULL);
        g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_tweet_delete_clicked), NULL);
        g_signal_connect(pin_btn, "clicked", G_CALLBACK(on_tweet_pin_clicked), NULL);
        g_signal_connect(highlight_btn, "clicked", G_CALLBACK(on_tweet_highlight_clicked), NULL);
        g_signal_connect(reply_permissions_btn, "clicked", G_CALLBACK(on_tweet_reply_restriction_clicked), NULL);
        g_signal_connect(outline_btn, "clicked", G_CALLBACK(on_tweet_outline_clicked), NULL);

        gtk_box_pack_start(GTK_BOX(button_box), edit_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(button_box), delete_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(button_box), pin_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(button_box), highlight_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(button_box), reply_permissions_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(button_box), outline_btn, FALSE, FALSE, 0);
    }
    g_free(current_username);

    if (is_admin_user()) {
        GtkWidget *note_btn = gtk_button_new_with_label("✎ Note");
        gtk_button_set_relief(GTK_BUTTON(note_btn), GTK_RELIEF_NONE);
        g_object_set_data_full(G_OBJECT(note_btn), "tweet_id", g_strdup(tweet->id), g_free);
        g_signal_connect(note_btn, "clicked", G_CALLBACK(on_note_button_clicked), NULL);
        gtk_box_pack_start(GTK_BOX(button_box), note_btn, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(outer_box), button_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    g_free(author_str);

    return outer_box;
}

void
populate_tweet_list(GtkListBox *list_box, GList *tweets)
{
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    for (GList *l = tweets; l != NULL; l = l->next) {
        GtkWidget *tweet_widget = create_tweet_widget(l->data);
        gtk_widget_show_all(tweet_widget);
        gtk_list_box_insert(list_box, tweet_widget, -1);
    }
}

GtkWidget*
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

    gchar *user_str = build_account_label_text(user->name, user->username);

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
    append_account_badges(box, user->author_verified, user->author_gold, user->author_gray);

    if (user->bio && strlen(user->bio) > 0) {
        GtkWidget *bio_label = gtk_label_new(user->bio);
        gtk_label_set_xalign(GTK_LABEL(bio_label), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(bio_label), TRUE);
        gtk_box_pack_start(GTK_BOX(box), bio_label, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), box, TRUE, TRUE, 0);

    GtkWidget *event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(event_box), hbox);
    g_object_set_data_full(G_OBJECT(event_box), "username", g_strdup(user->username), g_free);
    
    if (is_admin_user()) {
        g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_admin_user_button_press), NULL);
    }

    gtk_box_pack_start(GTK_BOX(outer_box), event_box, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    g_free(user_str);
    return outer_box;
}

void
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

void
append_tweets_to_list(GtkListBox *list_box, GList *tweets)
{
    for (GList *l = tweets; l != NULL; l = l->next) {
        GtkWidget *tweet_widget = create_tweet_widget(l->data);
        gtk_widget_show_all(tweet_widget);
        gtk_list_box_insert(list_box, tweet_widget, -1);
    }
}

static void
on_notification_avatar_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    const gchar *username = g_object_get_data(G_OBJECT(widget), "username");
    if (username && username[0] != '\0') {
        show_profile(username);
    }
}

static gboolean
on_notification_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)user_data;
    if (event->button != 1) return FALSE;

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "related_id");
    const gchar *notification_id = g_object_get_data(G_OBJECT(widget), "notification_id");
    if (notification_id) {
        mark_notification_read(notification_id);
    }
    if (tweet_id) {
        show_tweet(tweet_id);
    }
    return TRUE;
}

GtkWidget*
create_notification_widget(struct Notification *notif)
{
    GtkWidget *outer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 10);
    
    if (!notif->read) {
        GtkStyleContext *context = gtk_widget_get_style_context(outer_box);
        gtk_style_context_add_class(context, "unread-notification");
    }

    GtkWidget *avatar_image = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(avatar_image, 32, 32);
    gtk_widget_set_valign(avatar_image, GTK_ALIGN_START);
    if (notif->actor_avatar) {
        load_avatar(avatar_image, notif->actor_avatar, 32);
    }

    GtkWidget *avatar_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(avatar_btn), GTK_RELIEF_NONE);
    gtk_container_add(GTK_CONTAINER(avatar_btn), avatar_image);
    g_object_set_data_full(G_OBJECT(avatar_btn), "username", g_strdup(notif->actor_username), g_free);
    g_signal_connect(avatar_btn, "clicked", G_CALLBACK(on_notification_avatar_clicked), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    
    gchar *notif_text;
    const gchar *actor_name = notif->actor_name ? notif->actor_name : notif->actor_username;
    const gchar *badge_suffix = "";
    if (notif->actor_gold) {
        badge_suffix = " <span foreground='#c88900'>[Gold]</span>";
    } else if (notif->actor_verified) {
        badge_suffix = " <span foreground='#1d9bf0'>[Verified]</span>";
    }
    if (g_strcmp0(notif->type, "like") == 0) {
        notif_text = g_strdup_printf("<b>%s</b>%s liked your tweet", actor_name, badge_suffix);
    } else if (g_strcmp0(notif->type, "retweet") == 0) {
        notif_text = g_strdup_printf("<b>%s</b>%s retweeted your tweet", actor_name, badge_suffix);
    } else if (g_strcmp0(notif->type, "reply") == 0) {
        notif_text = g_strdup_printf("<b>%s</b>%s replied to your tweet", actor_name, badge_suffix);
    } else if (g_strcmp0(notif->type, "follow") == 0) {
        notif_text = g_strdup_printf("<b>%s</b>%s followed you", actor_name, badge_suffix);
    } else if (g_strcmp0(notif->type, "mention") == 0) {
        notif_text = g_strdup_printf("<b>%s</b>%s mentioned you", actor_name, badge_suffix);
    } else if (g_strcmp0(notif->type, "quote") == 0) {
        notif_text = g_strdup_printf("<b>%s</b>%s quoted your tweet", actor_name, badge_suffix);
    } else if (g_strcmp0(notif->type, "reaction") == 0) {
        notif_text = g_strdup_printf("<b>%s</b>%s reacted to your tweet", actor_name, badge_suffix);
    } else {
        notif_text = g_strdup_printf("<b>%s</b>%s: %s", actor_name, badge_suffix, notif->content);
    }

    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), notif_text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    g_free(notif_text);

    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    if (notif->content && strlen(notif->content) > 0 && g_strcmp0(notif->type, "dm_message") != 0) {
        GtkWidget *content_label = gtk_label_new(notif->content);
        gtk_label_set_xalign(GTK_LABEL(content_label), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(content_label), TRUE);
        GtkStyleContext *context = gtk_widget_get_style_context(content_label);
        gtk_style_context_add_class(context, "dim-label");
        gtk_box_pack_start(GTK_BOX(vbox), content_label, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(hbox), avatar_btn, FALSE, FALSE, 0);
    
    GtkWidget *content_event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(content_event_box), vbox);
    g_object_set_data_full(G_OBJECT(content_event_box), "notification_id", g_strdup(notif->id), g_free);
    if (notif->related_id) {
        g_object_set_data_full(G_OBJECT(content_event_box), "related_id", g_strdup(notif->related_id), g_free);
    }
    g_signal_connect(content_event_box, "button-press-event", G_CALLBACK(on_notification_clicked), NULL);
    
    gtk_box_pack_start(GTK_BOX(hbox), content_event_box, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(outer_box), hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    return outer_box;
}

void
populate_notification_list(GtkListBox *list_box, GList *notifications)
{
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    for (GList *l = notifications; l != NULL; l = l->next) {
        GtkWidget *notif_widget = create_notification_widget(l->data);
        gtk_widget_show_all(notif_widget);
        gtk_list_box_insert(list_box, notif_widget, -1);
    }
}

void
append_notifications_to_list(GtkListBox *list_box, GList *notifications)
{
    for (GList *l = notifications; l != NULL; l = l->next) {
        GtkWidget *notif_widget = create_notification_widget(l->data);
        gtk_widget_show_all(notif_widget);
        gtk_list_box_insert(list_box, notif_widget, -1);
    }
}

static void
on_conversation_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    const gchar *conv_id = g_object_get_data(G_OBJECT(widget), "conversation_id");
    const gchar *display_name = g_object_get_data(G_OBJECT(widget), "display_name");
    
    if (conv_id) {
        GtkWidget *status_label = g_object_get_data(G_OBJECT(g_dm_messages_list), "composer_status_label");
        gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "dm_messages");
        gtk_label_set_text(GTK_LABEL(g_dm_title_label), display_name ? display_name : "Messages");
        if (g_dm_info_label) {
            gtk_label_set_text(GTK_LABEL(g_dm_info_label), "");
        }
        g_object_set_data_full(G_OBJECT(g_dm_messages_list), "conversation_id", g_strdup(conv_id), g_free);
        g_object_set_data_full(G_OBJECT(g_dm_messages_list), "reply_to_id", NULL, g_free);
        g_object_set_data_full(G_OBJECT(g_dm_messages_list), "reply_preview", NULL, g_free);
        g_object_set_data_full(G_OBJECT(g_dm_messages_list), "pending_file_path", NULL, g_free);
        g_object_set_data_full(G_OBJECT(g_dm_messages_list), "pending_file_type", NULL, g_free);
        g_object_set_data(G_OBJECT(g_dm_entry), "typing_active", GINT_TO_POINTER(FALSE));
        if (status_label) {
            gtk_label_set_text(GTK_LABEL(status_label), "");
            gtk_widget_hide(status_label);
        }
        start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);

        // Mark as read
        gchar *url = g_strdup_printf(DM_MARK_READ_URL, conv_id);
        struct MemoryStruct chunk = {0};
        if (fetch_url(url, &chunk, "", "PATCH")) {
            g_free(chunk.memory);
        }
        g_free(url);
    }
}

GtkWidget*
create_conversation_widget(struct Conversation *conv)
{
    GtkWidget *event_box = gtk_event_box_new();
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 10);
    
    GtkWidget *avatar_image = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(avatar_image, 48, 48);
    if (conv->display_avatar) {
        load_avatar(avatar_image, conv->display_avatar, 48);
    }

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    
    GtkWidget *name_label = gtk_label_new(NULL);
    gchar *name_markup = g_strdup_printf("<b>%s</b>", conv->display_name ? conv->display_name : "Unknown");
    gtk_label_set_markup(GTK_LABEL(name_label), name_markup);
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
    g_free(name_markup);

    GtkWidget *last_msg_label = gtk_label_new(conv->last_message_content ? conv->last_message_content : "");
    gtk_label_set_xalign(GTK_LABEL(last_msg_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(last_msg_label), PANGO_ELLIPSIZE_END);
    GtkStyleContext *context = gtk_widget_get_style_context(last_msg_label);
    gtk_style_context_add_class(context, "dim-label");

    gchar *details_text = NULL;
    if (conv->participant_count > 1 && conv->last_message_sender) {
        details_text = g_strdup_printf("%d participants  •  Last from %s",
                                       conv->participant_count,
                                       conv->last_message_sender);
    } else if (conv->participant_count > 1) {
        details_text = g_strdup_printf("%d participants", conv->participant_count);
    } else if (conv->last_message_sender) {
        details_text = g_strdup_printf("Last from %s", conv->last_message_sender);
    }

    gtk_box_pack_start(GTK_BOX(vbox), name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), last_msg_label, FALSE, FALSE, 0);
    if (details_text) {
        GtkWidget *details_label = gtk_label_new(details_text);
        gtk_label_set_xalign(GTK_LABEL(details_label), 0.0);
        GtkStyleContext *details_context = gtk_widget_get_style_context(details_label);
        gtk_style_context_add_class(details_context, "dim-label");
        gtk_box_pack_start(GTK_BOX(vbox), details_label, FALSE, FALSE, 0);
        g_free(details_text);
    }

    gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    if (conv->unread_count > 0) {
        gchar *unread_text = g_strdup_printf("%d", conv->unread_count);
        GtkWidget *badge = gtk_label_new(unread_text);
        g_free(unread_text);
        gtk_box_pack_end(GTK_BOX(hbox), badge, FALSE, FALSE, 0);
    }

    gtk_container_add(GTK_CONTAINER(event_box), hbox);
    g_object_set_data_full(G_OBJECT(event_box), "conversation_id", g_strdup(conv->id), g_free);
    g_object_set_data_full(G_OBJECT(event_box), "display_name", g_strdup(conv->display_name), g_free);
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_conversation_clicked), NULL);

    GtkWidget *outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(outer_vbox), event_box, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer_vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    return outer_vbox;
}

void
populate_conversation_list(GtkListBox *list_box, GList *conversations)
{
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    for (GList *l = conversations; l != NULL; l = l->next) {
        GtkWidget *conv_widget = create_conversation_widget(l->data);
        gtk_widget_show_all(conv_widget);
        gtk_list_box_insert(list_box, conv_widget, -1);
    }

    /* Add P2P contacts to the conversation list */
    if (g_p2p_session) {
        g_mutex_lock(&g_p2p_session->session_mutex);
        GHashTableIter p2p_iter;
        gpointer key, value;
        g_hash_table_iter_init(&p2p_iter, g_p2p_session->contacts);
        while (g_hash_table_iter_next(&p2p_iter, &key, &value)) {
            struct P2PContact *contact = value;
            if (!contact) continue;

            GtkWidget *event_box = gtk_event_box_new();
            GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_container_set_border_width(GTK_CONTAINER(hbox), 10);
            
            /* Use a lock icon to indicate encrypted */
            GtkWidget *avatar_image = gtk_image_new_from_icon_name("security-high", GTK_ICON_SIZE_DIALOG);
            gtk_widget_set_size_request(avatar_image, 48, 48);

            GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            
            GtkWidget *name_label = gtk_label_new(NULL);
            gchar *name_markup = g_strdup_printf("<b>%s</b> <span foreground=\"#2ecc71\" size=\"small\">[Encrypted]</span>", 
                contact->display_name ? contact->display_name : contact->username);
            gtk_label_set_markup(GTK_LABEL(name_label), name_markup);
            gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
            g_free(name_markup);

            GtkWidget *last_msg_label = gtk_label_new("P2P encrypted messaging");
            gtk_label_set_xalign(GTK_LABEL(last_msg_label), 0.0);
            gtk_label_set_ellipsize(GTK_LABEL(last_msg_label), PANGO_ELLIPSIZE_END);
            GtkStyleContext *context = gtk_widget_get_style_context(last_msg_label);
            gtk_style_context_add_class(context, "dim-label");

            gtk_box_pack_start(GTK_BOX(vbox), name_label, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(vbox), last_msg_label, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

            gtk_container_add(GTK_CONTAINER(event_box), hbox);
            g_object_set_data_full(G_OBJECT(event_box), "p2p_contact_username", g_strdup(contact->username), g_free);
            g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_p2p_contact_clicked), NULL);

            GtkWidget *outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            gtk_box_pack_start(GTK_BOX(outer_vbox), event_box, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(outer_vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

            gtk_widget_show_all(outer_vbox);
            gtk_list_box_insert(list_box, outer_vbox, -1);
        }
        g_mutex_unlock(&g_p2p_session->session_mutex);
    }
}

static void
on_dm_message_reply_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *status_label;
    const gchar *message_id;
    const gchar *content;

    (void)user_data;
    if (!g_dm_messages_list) {
        return;
    }

    message_id = g_object_get_data(G_OBJECT(widget), "message_id");
    content = g_object_get_data(G_OBJECT(widget), "message_content");
    status_label = g_object_get_data(G_OBJECT(g_dm_messages_list), "composer_status_label");

    g_object_set_data_full(G_OBJECT(g_dm_messages_list), "reply_to_id", g_strdup(message_id), g_free);
    g_object_set_data_full(G_OBJECT(g_dm_messages_list), "reply_preview", g_strdup(content), g_free);
    if (status_label) {
        gchar *preview = NULL;
        if (content) {
            preview = g_strdup(content);
            if (strlen(preview) > 48) {
                preview[48] = '\0';
            }
        }
        gtk_label_set_text(GTK_LABEL(status_label), preview ? preview : "Replying");
        gtk_widget_show(status_label);
        g_free(preview);
    }
    gtk_widget_grab_focus(g_dm_entry);
}

static void
on_dm_message_react_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *entry;
    GtkWidget *content_area;
    GtkWidget *toplevel;
    const gchar *message_id;

    (void)user_data;
    message_id = g_object_get_data(G_OBJECT(widget), "message_id");
    if (!message_id) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("React to Message",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_React", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Emoji");
    gtk_box_pack_start(GTK_BOX(content_area), entry, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *emoji = gtk_entry_get_text(GTK_ENTRY(entry));
        if (emoji && emoji[0] != '\0') {
            gchar *url = g_strdup_printf(DM_MESSAGE_REACTIONS_URL, message_id);
            JsonBuilder *builder = json_builder_new();
            JsonGenerator *gen = json_generator_new();
            gchar *payload;
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "emoji");
            json_builder_add_string_value(builder, emoji);
            json_builder_end_object(builder);
            json_generator_set_root(gen, json_builder_get_root(builder));
            payload = json_generator_to_data(gen, NULL);
            if (fetch_url(url, &((struct MemoryStruct){0}), payload, "POST")) {
                refresh_current_dm_messages();
            }
            g_free(payload);
            g_object_unref(gen);
            g_object_unref(builder);
            g_free(url);
        }
    }

    gtk_widget_destroy(dialog);
}

static void
on_dm_message_edit_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *entry;
    GtkWidget *content_area;
    GtkWidget *toplevel;
    const gchar *message_id;
    const gchar *current_content;

    (void)user_data;
    message_id = g_object_get_data(G_OBJECT(widget), "message_id");
    current_content = g_object_get_data(G_OBJECT(widget), "message_content");
    if (!message_id) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Edit Message",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), current_content ? current_content : "");
    gtk_box_pack_start(GTK_BOX(content_area), entry, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *url = g_strdup_printf(DM_MESSAGE_EDIT_URL, message_id);
        JsonBuilder *builder = json_builder_new();
        JsonGenerator *gen = json_generator_new();
        gchar *payload;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "content");
        json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(entry)));
        json_builder_end_object(builder);
        json_generator_set_root(gen, json_builder_get_root(builder));
        payload = json_generator_to_data(gen, NULL);
        if (fetch_url(url, &((struct MemoryStruct){0}), payload, "PUT")) {
            refresh_current_dm_messages();
        }
        g_free(payload);
        g_object_unref(gen);
        g_object_unref(builder);
        g_free(url);
    }

    gtk_widget_destroy(dialog);
}

static void
on_dm_message_delete_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *toplevel;
    GtkWidget *dialog;
    const gchar *message_id;

    (void)user_data;
    message_id = g_object_get_data(G_OBJECT(widget), "message_id");
    if (!message_id) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_OK_CANCEL,
                                    "Delete this message?");
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        gchar *url = g_strdup_printf(DM_MESSAGE_DELETE_URL, message_id);
        struct MemoryStruct chunk = {0};
        if (fetch_url(url, &chunk, NULL, "DELETE")) {
            g_free(chunk.memory);
            refresh_current_dm_messages();
        }
        g_free(url);
    }
    gtk_widget_destroy(dialog);
}

static void
refresh_current_dm_conversation(void)
{
    if (!g_dm_messages_list) return;
    const gchar *conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
    if (conv_id)
        start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);
}

static gboolean
parse_payment_start_response(const gchar *json_data, gchar **order_id_out, gchar **payment_url_out, gchar **error_out)
{
    JsonParser *parser = json_parser_new();
    gboolean ok = FALSE;
    if (order_id_out) *order_id_out = NULL;
    if (payment_url_out) *payment_url_out = NULL;
    if (error_out) *error_out = NULL;

    if (json_data && json_parser_load_from_data(parser, json_data, -1, NULL)) {
        JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
        if (obj && json_object_has_member(obj, "paymentUrl") &&
            !json_node_is_null(json_object_get_member(obj, "paymentUrl")) &&
            json_object_has_member(obj, "orderId") &&
            !json_node_is_null(json_object_get_member(obj, "orderId"))) {
            if (order_id_out)
                *order_id_out = g_strdup(json_object_get_string_member(obj, "orderId"));
            if (payment_url_out)
                *payment_url_out = g_strdup(json_object_get_string_member(obj, "paymentUrl"));
            ok = TRUE;
        } else if (obj && error_out && json_object_has_member(obj, "error")) {
            *error_out = g_strdup(json_object_get_string_member(obj, "error"));
        }
    }

    g_object_unref(parser);
    return ok;
}

static void
on_dm_payment_refresh_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    const gchar *message_id = g_object_get_data(G_OBJECT(widget), "message_id");
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    if (!message_id) return;

    gchar *url = g_strdup_printf(MPI_PAYMENT_BY_MESSAGE_URL, message_id);
    struct MemoryStruct chunk = {0};
    if (fetch_url(url, &chunk, NULL, "GET")) {
        show_dm_payment_message(window, GTK_MESSAGE_INFO, "Payment status refreshed.", NULL);
        refresh_current_dm_conversation();
        g_free(chunk.memory);
    } else {
        show_dm_payment_message(window, GTK_MESSAGE_ERROR, "Payment status unavailable.", NULL);
    }
    g_free(url);
}

static void
confirm_dm_payment(GtkWindow *window, const gchar *message_id, const gchar *order_id)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Confirm payment",
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Confirm", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new("Complete payment in the opened page, then paste the transaction id.");
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Transaction ID");
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *txn_id = gtk_entry_get_text(GTK_ENTRY(entry));
        if (txn_id && *txn_id) {
            gchar *url = g_strdup_printf(MPI_REQUEST_CONFIRM_URL, message_id);
            JsonBuilder *builder = json_builder_new();
            JsonGenerator *gen = json_generator_new();
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "orderId");
            json_builder_add_string_value(builder, order_id);
            json_builder_set_member_name(builder, "transactionId");
            json_builder_add_string_value(builder, txn_id);
            json_builder_end_object(builder);
            JsonNode *root = json_builder_get_root(builder);
            json_generator_set_root(gen, root);
            gchar *payload = json_generator_to_data(gen, NULL);
            struct MemoryStruct chunk = {0};
            if (fetch_url(url, &chunk, payload, "POST")) {
                show_dm_payment_message(window, GTK_MESSAGE_INFO, "Payment confirmed.", NULL);
                refresh_current_dm_conversation();
                g_free(chunk.memory);
            } else {
                show_dm_payment_message(window, GTK_MESSAGE_ERROR, "Payment confirmation failed.", NULL);
            }
            g_free(payload);
            json_node_free(root);
            g_object_unref(gen);
            g_object_unref(builder);
            g_free(url);
        }
    }
    gtk_widget_destroy(dialog);
}

static void
on_dm_payment_pay_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    const gchar *message_id = g_object_get_data(G_OBJECT(widget), "message_id");
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    if (!message_id) return;

    gchar *url = g_strdup_printf(MPI_REQUEST_PAY_URL, message_id);
    struct MemoryStruct chunk = {0};
    if (fetch_url(url, &chunk, "", "POST")) {
        gchar *order_id = NULL;
        gchar *payment_url = NULL;
        gchar *error = NULL;
        if (parse_payment_start_response(chunk.memory, &order_id, &payment_url, &error)) {
            if (payment_url && *payment_url)
                gtk_show_uri_on_window(window, payment_url, GDK_CURRENT_TIME, NULL);
            confirm_dm_payment(window, message_id, order_id);
        } else {
            show_dm_payment_message(window, GTK_MESSAGE_ERROR, "Could not start payment.", error);
        }
        g_free(order_id);
        g_free(payment_url);
        g_free(error);
        g_free(chunk.memory);
    } else {
        show_dm_payment_message(window, GTK_MESSAGE_ERROR, "Could not start payment.", NULL);
    }
    g_free(url);
}

static GtkWidget*
create_dm_payment_card(struct DirectMessage *msg, gboolean is_own_message)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);

    const gchar *kind = msg->mpi_kind ? msg->mpi_kind : msg->message_type;
    const gchar *status = msg->mpi_status ? msg->mpi_status : "pending";
    const gchar *net = msg->mpi_net ? msg->mpi_net : "";
    const gchar *gross = msg->mpi_gross ? msg->mpi_gross : "";
    const gchar *title = g_strcmp0(kind, "request") == 0 ? "Payment request" :
                         g_strcmp0(kind, "donate") == 0 ? "Donation" : "Payment";
    gchar *summary = g_strdup_printf("<b>%s</b> · ₹%s%s%s · %s",
                                     title,
                                     net,
                                     gross[0] ? " (payer total ₹" : "",
                                     gross[0] ? gross : "",
                                     status);
    if (gross[0]) {
        gchar *fixed = g_strdup_printf("<b>%s</b> · ₹%s (payer total ₹%s) · %s",
                                       title, net, gross, status);
        g_free(summary);
        summary = fixed;
    }
    GtkWidget *summary_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(summary_label), summary);
    gtk_label_set_xalign(GTK_LABEL(summary_label), 0.0);
    gtk_box_pack_start(GTK_BOX(box), summary_label, FALSE, FALSE, 0);
    g_free(summary);

    const gchar *note = msg->mpi_note && msg->mpi_note[0] ? msg->mpi_note : msg->content;
    if (note && note[0]) {
        GtkWidget *note_label = gtk_label_new(note);
        gtk_label_set_xalign(GTK_LABEL(note_label), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(note_label), TRUE);
        gtk_box_pack_start(GTK_BOX(box), note_label, FALSE, FALSE, 0);
    }

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    if (g_strcmp0(kind, "request") == 0 && g_strcmp0(status, "pending") == 0 && !is_own_message) {
        GtkWidget *pay_btn = gtk_button_new_with_label("Pay");
        gtk_button_set_relief(GTK_BUTTON(pay_btn), GTK_RELIEF_NONE);
        g_object_set_data_full(G_OBJECT(pay_btn), "message_id", g_strdup(msg->id), g_free);
        g_signal_connect(pay_btn, "clicked", G_CALLBACK(on_dm_payment_pay_clicked), NULL);
        gtk_box_pack_start(GTK_BOX(buttons), pay_btn, FALSE, FALSE, 0);
    }
    if (msg->mpi_payment_link_url && msg->mpi_payment_link_url[0]) {
        GtkWidget *open_btn = gtk_button_new_with_label("Open payout");
        gtk_button_set_relief(GTK_BUTTON(open_btn), GTK_RELIEF_NONE);
        g_object_set_data_full(G_OBJECT(open_btn), "url", g_strdup(msg->mpi_payment_link_url), g_free);
        g_signal_connect(open_btn, "clicked", G_CALLBACK(on_video_clicked), NULL);
        gtk_box_pack_start(GTK_BOX(buttons), open_btn, FALSE, FALSE, 0);
    }
    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh");
    gtk_button_set_relief(GTK_BUTTON(refresh_btn), GTK_RELIEF_NONE);
    g_object_set_data_full(G_OBJECT(refresh_btn), "message_id", g_strdup(msg->id), g_free);
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_dm_payment_refresh_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(buttons), refresh_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    return frame;
}

GtkWidget*
create_message_widget(struct DirectMessage *msg)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *avatar_image = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_MENU);
    gtk_widget_set_size_request(avatar_image, 32, 32);
    if (msg->avatar) {
        load_avatar(avatar_image, msg->avatar, 32);
    }
    gchar *current_username = get_current_username_safe();
    gboolean is_own_message = current_username && msg->username &&
                              g_strcmp0(current_username, msg->username) == 0;

    gchar *header_text = NULL;
    if (msg->verified) {
        header_text = g_strdup_printf("<b>%s</b> <span foreground='#1d9bf0'>[Verified]</span> (@%s) · %s%s",
                                      msg->name ? msg->name : "Unknown",
                                      msg->username ? msg->username : "unknown",
                                      msg->created_at ? msg->created_at : "",
                                      msg->edited_at ? " · edited" : "");
    } else {
        header_text = g_strdup_printf("<b>%s</b> (@%s) · %s%s",
                                      msg->name ? msg->name : "Unknown",
                                      msg->username ? msg->username : "unknown",
                                      msg->created_at ? msg->created_at : "",
                                      msg->edited_at ? " · edited" : "");
    }
    GtkWidget *header_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_label), header_text);
    gtk_label_set_xalign(GTK_LABEL(header_label), 0.0);
    g_free(header_text);

    gboolean is_mpi_message = msg->message_type &&
        (g_strcmp0(msg->message_type, "mpi_request") == 0 ||
         g_strcmp0(msg->message_type, "mpi_send") == 0 ||
         g_strcmp0(msg->message_type, "mpi_donate") == 0);

    GtkWidget *content_label = gtk_label_new(msg->is_deleted ? "[Deleted message]" : msg->content);
    gtk_label_set_xalign(GTK_LABEL(content_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(content_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(content_label), TRUE);

    gtk_box_pack_start(GTK_BOX(vbox), header_label, FALSE, FALSE, 0);
    if (msg->message_type && g_strcmp0(msg->message_type, "text") != 0 && !is_mpi_message) {
        gchar *type_text = g_strdup_printf("Type: %s", msg->message_type);
        GtkWidget *type_label = gtk_label_new(type_text);
        gtk_label_set_xalign(GTK_LABEL(type_label), 0.0);
        GtkStyleContext *type_context = gtk_widget_get_style_context(type_label);
        gtk_style_context_add_class(type_context, "dim-label");
        gtk_box_pack_start(GTK_BOX(vbox), type_label, FALSE, FALSE, 0);
        g_free(type_text);
    }
    if (msg->reply_to || msg->reply_preview) {
        gchar *reply_text = g_strdup_printf("Replying to %s%s%s",
                                            msg->reply_to ? msg->reply_to : "message",
                                            msg->reply_preview ? ": " : "",
                                            msg->reply_preview ? msg->reply_preview : "");
        GtkWidget *reply_label = gtk_label_new(reply_text);
        gtk_label_set_xalign(GTK_LABEL(reply_label), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(reply_label), TRUE);
        GtkStyleContext *reply_context = gtk_widget_get_style_context(reply_label);
        gtk_style_context_add_class(reply_context, "dim-label");
        gtk_box_pack_start(GTK_BOX(vbox), reply_label, FALSE, FALSE, 0);
        g_free(reply_text);
    }
    if (is_mpi_message && !msg->is_deleted) {
        gtk_box_pack_start(GTK_BOX(vbox), create_dm_payment_card(msg, is_own_message), FALSE, FALSE, 0);
    } else {
        gtk_box_pack_start(GTK_BOX(vbox), content_label, FALSE, FALSE, 0);
    }

    if (!msg->is_deleted) {
        add_attachments_to_box(GTK_BOX(vbox), msg->attachments);
    }

    if (msg->reactions_summary && msg->reactions_summary[0] != '\0') {
        GtkWidget *reactions_label = gtk_label_new(msg->reactions_summary);
        gtk_label_set_xalign(GTK_LABEL(reactions_label), 0.0);
        GtkStyleContext *reactions_context = gtk_widget_get_style_context(reactions_label);
        gtk_style_context_add_class(reactions_context, "dim-label");
        gtk_box_pack_start(GTK_BOX(vbox), reactions_label, FALSE, FALSE, 0);
    }

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *reply_btn = gtk_button_new_with_label("Reply");
    GtkWidget *react_btn = gtk_button_new_with_label("React");
    GtkWidget *pin_btn = gtk_button_new_with_label("Pin");
    gtk_button_set_relief(GTK_BUTTON(reply_btn), GTK_RELIEF_NONE);
    gtk_button_set_relief(GTK_BUTTON(react_btn), GTK_RELIEF_NONE);
    gtk_button_set_relief(GTK_BUTTON(pin_btn), GTK_RELIEF_NONE);
    g_object_set_data_full(G_OBJECT(reply_btn), "message_id", g_strdup(msg->id), g_free);
    g_object_set_data_full(G_OBJECT(reply_btn), "message_content", g_strdup(msg->content), g_free);
    g_object_set_data_full(G_OBJECT(react_btn), "message_id", g_strdup(msg->id), g_free);
    g_object_set_data_full(G_OBJECT(pin_btn), "message_id", g_strdup(msg->id), g_free);
    g_signal_connect(reply_btn, "clicked", G_CALLBACK(on_dm_message_reply_clicked), NULL);
    g_signal_connect(react_btn, "clicked", G_CALLBACK(on_dm_message_react_clicked), NULL);
    g_signal_connect(pin_btn, "clicked", G_CALLBACK(on_dm_pin_message_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), reply_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), react_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), pin_btn, FALSE, FALSE, 0);

    if (is_own_message && !msg->is_deleted) {
        GtkWidget *edit_btn = gtk_button_new_with_label("Edit");
        GtkWidget *delete_btn = gtk_button_new_with_label("Delete");
        gtk_button_set_relief(GTK_BUTTON(edit_btn), GTK_RELIEF_NONE);
        gtk_button_set_relief(GTK_BUTTON(delete_btn), GTK_RELIEF_NONE);
        g_object_set_data_full(G_OBJECT(edit_btn), "message_id", g_strdup(msg->id), g_free);
        g_object_set_data_full(G_OBJECT(edit_btn), "message_content", g_strdup(msg->content), g_free);
        g_object_set_data_full(G_OBJECT(delete_btn), "message_id", g_strdup(msg->id), g_free);
        g_signal_connect(edit_btn, "clicked", G_CALLBACK(on_dm_message_edit_clicked), NULL);
        g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_dm_message_delete_clicked), NULL);
        gtk_box_pack_start(GTK_BOX(button_box), edit_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(button_box), delete_btn, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    g_free(current_username);

    return hbox;
}

void
populate_message_list(GtkListBox *list_box, GList *messages)
{
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    // Messages come in descending order from API, we want to show them in order
    for (GList *l = g_list_last(messages); l != NULL; l = l->prev) {
        GtkWidget *msg_widget = create_message_widget(l->data);
        gtk_widget_show_all(msg_widget);
        gtk_list_box_insert(list_box, msg_widget, -1);
    }
    
    // Scroll to bottom
    GtkWidget *scrolled = gtk_widget_get_parent(GTK_WIDGET(list_box));
    if (GTK_IS_VIEWPORT(scrolled)) {
        scrolled = gtk_widget_get_parent(scrolled);
    }
    if (GTK_IS_SCROLLED_WINDOW(scrolled)) {
        GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
        gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
    }
}

GtkWidget* create_community_widget(struct Community *community)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_container_add(GTK_CONTAINER(row), box);

    // Community icon
    GtkWidget *icon = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DND);
    gtk_widget_set_size_request(icon, 48, 48);
    if (community->icon_url) {
        load_avatar(icon, community->icon_url, 48);
    }
    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);

    // Info box
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(box), info_box, TRUE, TRUE, 0);

    // Name and badge row
    GtkWidget *name_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(info_box), name_row, FALSE, FALSE, 0);

    // Community name (bold)
    GtkWidget *name_label = gtk_label_new(community->name);
    gtk_widget_set_halign(name_label, GTK_ALIGN_START);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(name_label), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(name_row), name_label, FALSE, FALSE, 0);

    // Access mode badge
    const gchar *badge_color = NULL;
    if (g_strcmp0(community->access_mode, "public") == 0) {
        badge_color = "#4CAF50";  // Green
    } else if (g_strcmp0(community->access_mode, "private") == 0) {
        badge_color = "#F44336";  // Red
    } else if (g_strcmp0(community->access_mode, "restricted") == 0) {
        badge_color = "#FF9800";  // Orange
    }

    if (badge_color) {
        GtkWidget *badge = gtk_label_new(community->access_mode);
        gtk_widget_set_halign(badge, GTK_ALIGN_START);
        gchar *badge_markup = g_strdup_printf("<span bgcolor=\"%s\" fgcolor=\"white\" size=\"small\"> %s </span>",
                                              badge_color, community->access_mode);
        gtk_label_set_markup(GTK_LABEL(badge), badge_markup);
        g_free(badge_markup);
        gtk_box_pack_start(GTK_BOX(name_row), badge, FALSE, FALSE, 0);
    }

    // Description (ellipsized)
    if (community->description) {
        GtkWidget *desc_label = gtk_label_new(community->description);
        gtk_widget_set_halign(desc_label, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(desc_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(desc_label), 50);
        gtk_label_set_line_wrap(GTK_LABEL(desc_label), FALSE);
        gtk_box_pack_start(GTK_BOX(info_box), desc_label, FALSE, FALSE, 0);
    }

    // Member count
    gchar *members_text = g_strdup_printf("%d members", community->member_count);
    GtkWidget *members_label = gtk_label_new(members_text);
    gtk_widget_set_halign(members_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(members_label, 0.6);
    gtk_box_pack_start(GTK_BOX(info_box), members_label, FALSE, FALSE, 0);
    g_free(members_text);

    // Join/Leave button
    GtkWidget *join_btn = gtk_button_new_with_label(community->is_member ? "Leave" : "Join");
    gtk_widget_set_valign(join_btn, GTK_ALIGN_CENTER);
    g_object_set_data_full(G_OBJECT(join_btn), "community_id", g_strdup(community->id), g_free);
    g_object_set_data(G_OBJECT(join_btn), "is_member", GINT_TO_POINTER(community->is_member));
    g_signal_connect(join_btn, "clicked", G_CALLBACK(on_join_community_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(box), join_btn, FALSE, FALSE, 0);

    // Store community data on row for click handler
    g_object_set_data_full(G_OBJECT(row), "community_id", g_strdup(community->id), g_free);
    g_object_set_data_full(G_OBJECT(row), "community_name", g_strdup(community->name), g_free);
    g_object_set_data_full(G_OBJECT(row), "community_description", g_strdup(community->description), g_free);
    g_object_set_data_full(G_OBJECT(row), "community_rules", g_strdup(community->rules), g_free);
    g_object_set_data_full(G_OBJECT(row), "community_access_mode", g_strdup(community->access_mode), g_free);
    g_signal_connect(row, "activate", G_CALLBACK(on_community_clicked), NULL);

    gtk_widget_show_all(row);
    return row;
}

void populate_community_list(GtkListBox *list_box, GList *communities)
{
    // Clear existing items
    GList *children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    if (!communities) {
        GtkWidget *empty_label = gtk_label_new("No communities found.");
        gtk_widget_set_margin_top(empty_label, 20);
        gtk_widget_show(empty_label);
        gtk_list_box_insert(list_box, empty_label, -1);
        return;
    }

    for (GList *l = communities; l != NULL; l = l->next) {
        struct Community *community = (struct Community *)l->data;
        GtkWidget *widget = create_community_widget(community);
        gtk_list_box_insert(list_box, widget, -1);
    }
}

static void on_community_clicked(GtkListBoxRow *row, gpointer user_data)
{
    (void)user_data;
    const gchar *community_id = g_object_get_data(G_OBJECT(row), "community_id");
    const gchar *community_name = g_object_get_data(G_OBJECT(row), "community_name");
    const gchar *community_description = g_object_get_data(G_OBJECT(row), "community_description");
    const gchar *community_rules = g_object_get_data(G_OBJECT(row), "community_rules");
    const gchar *community_access_mode = g_object_get_data(G_OBJECT(row), "community_access_mode");
    if (!community_id) return;
    
    // Set current community ID (thread-safe)
    gchar *old_id = get_community_id_safe();
    g_mutex_lock(&g_globals_mutex);
    g_free(g_community_id);
    g_community_id = g_strdup(community_id);
    g_mutex_unlock(&g_globals_mutex);
    g_free(old_id);

    // Show community tweets
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "community_tweets");
    gtk_widget_show(g_back_button);
    if (g_community_tweets_list) {
        g_object_set_data_full(G_OBJECT(g_community_tweets_list), "community_id", g_strdup(community_id), g_free);
        g_object_set_data_full(G_OBJECT(g_community_tweets_list), "community_name", g_strdup(community_name), g_free);
        g_object_set_data_full(G_OBJECT(g_community_tweets_list), "community_description", g_strdup(community_description), g_free);
        g_object_set_data_full(G_OBJECT(g_community_tweets_list), "community_rules", g_strdup(community_rules), g_free);
        g_object_set_data_full(G_OBJECT(g_community_tweets_list), "community_access_mode", g_strdup(community_access_mode), g_free);
    }
    if (g_community_title_label) {
        gtk_label_set_text(GTK_LABEL(g_community_title_label), community_name ? community_name : "Community");
    }
    if (g_community_details_label) {
        gtk_label_set_text(GTK_LABEL(g_community_details_label), community_description ? community_description : "");
    }

    // Load community tweets using the action function
    start_loading_community_tweets(GTK_LIST_BOX(g_community_tweets_list), community_id);
}

static void on_join_community_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    const gchar *community_id = g_object_get_data(G_OBJECT(button), "community_id");
    gboolean is_member = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "is_member"));

    if (!community_id) return;

    gboolean success;
    if (is_member) {
        success = perform_leave_community(community_id);
    } else {
        success = perform_join_community(community_id);
    }

    if (success) {
        // Toggle button state
        gtk_button_set_label(button, is_member ? "Join" : "Leave");
        g_object_set_data(G_OBJECT(button), "is_member", GINT_TO_POINTER(!is_member));

        // Refresh community list to show updated counts
        start_loading_communities(GTK_LIST_BOX(g_communities_list));
    } else {
        GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(button));
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(toplevel),
                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                 GTK_MESSAGE_ERROR,
                                 GTK_BUTTONS_CLOSE,
                                 "Failed to %s community.",
                                 is_member ? "leave" : "join");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
    }
}

// Enhanced compose dialog with media upload
static void on_file_selected(GtkFileChooserButton *chooser, gpointer user_data)
{
    struct UploadContext *ctx = (struct UploadContext *)user_data;
    gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));

    g_debug("on_file_selected: filename=%s", filename ? filename : "(null)");

    if (filename) {
        g_free(ctx->file_path);
        ctx->file_path = filename;

        // Update label with selected filename
        gchar *basename = g_path_get_basename(filename);
        gchar *label_text = g_strdup_printf("Selected: %s", basename);
        gtk_label_set_text(GTK_LABEL(ctx->file_label), label_text);
        g_free(label_text);
        g_free(basename);

        g_free(ctx->file_type);
        ctx->file_type = detect_mime_type(filename);
        g_debug("on_file_selected: detected mime_type=%s for file=%s", ctx->file_type ? ctx->file_type : "(null)", filename);
    } else {
        g_debug("on_file_selected: no file selected");
    }
}

static gboolean perform_post_tweet_with_media(const gchar *content, const gchar *media_url, const gchar *file_type)
{
    GList *attachments = build_attachment_list(media_url, file_type);
    gboolean success = perform_post_tweet(content ? content : "", NULL, attachments);
    
    if (attachments) {
        g_list_free_full(attachments, free_attachment_payload);
    }

    return success;
}

void on_compose_with_media_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    struct UploadContext *ctx = (struct UploadContext *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkWidget *text_view = g_object_get_data(G_OBJECT(dialog), "text_view");
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar *content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        gchar *media_url = NULL;
        gboolean upload_success = TRUE;

        g_debug("on_compose_with_media_response: ctx->file_path=%s, ctx->file_type=%s", 
                ctx->file_path ? ctx->file_path : "(null)", 
                ctx->file_type ? ctx->file_type : "(null)");

        // Upload media if selected
        if (ctx->file_path) {
            g_debug("on_compose_with_media_response: uploading media file");
            media_url = perform_media_upload(ctx->file_path);
            if (!media_url) {
                g_debug("on_compose_with_media_response: media upload failed");
                upload_success = FALSE;
            } else {
                g_debug("on_compose_with_media_response: media upload succeeded, url=%s", media_url);
            }
        } else {
            g_debug("on_compose_with_media_response: no file to upload");
        }

        gboolean has_text = FALSE;
        if (content) {
            gchar *trimmed = g_strdup(content);
            g_strstrip(trimmed);
            has_text = (trimmed[0] != '\0');
            g_free(trimmed);
            g_debug("on_compose_with_media_response: has_text=%d", has_text);
        }
        gboolean has_attachment = (media_url != NULL);
        g_debug("on_compose_with_media_response: upload_success=%d, has_attachment=%d", upload_success, has_attachment);

        if (upload_success && (has_text || has_attachment)) {
            const gchar *file_type = ctx->file_type ? ctx->file_type : "application/octet-stream";
            if (perform_post_tweet_with_media(content ? content : "", media_url, file_type)) {
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
                                     "Failed to upload media.");
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
        }

        g_free(content);
        g_free(media_url);
    }

    g_free(ctx->file_path);
    g_free(ctx->file_type);
    g_free(ctx);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void on_compose_with_media_clicked(GtkWidget *widget, gpointer window)
{
    (void)widget;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Compose Tweet",
                                                    GTK_WINDOW(window),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Tweet", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    // Text view for tweet content
    GtkWidget *text_view = gtk_text_view_new();
    gtk_widget_set_size_request(text_view, 300, 150);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_box_pack_start(GTK_BOX(content_area), text_view, TRUE, TRUE, 0);
    g_object_set_data(G_OBJECT(dialog), "text_view", text_view);

    // File chooser for media
    GtkWidget *file_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(file_box, 10);
    gtk_box_pack_start(GTK_BOX(content_area), file_box, FALSE, FALSE, 0);

    GtkWidget *file_chooser = gtk_file_chooser_button_new("Select Media", GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_file_chooser_button_set_title(GTK_FILE_CHOOSER_BUTTON(file_chooser), "Select Image/Video");

    // Set file filter for images and videos
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images and Videos");
    gtk_file_filter_add_mime_type(filter, "image/png");
    gtk_file_filter_add_mime_type(filter, "image/jpeg");
    gtk_file_filter_add_mime_type(filter, "image/gif");
    gtk_file_filter_add_mime_type(filter, "image/webp");
    gtk_file_filter_add_mime_type(filter, "video/mp4");
    gtk_file_filter_add_pattern(filter, "*.png");
    gtk_file_filter_add_pattern(filter, "*.jpg");
    gtk_file_filter_add_pattern(filter, "*.jpeg");
    gtk_file_filter_add_pattern(filter, "*.gif");
    gtk_file_filter_add_pattern(filter, "*.webp");
    gtk_file_filter_add_pattern(filter, "*.mp4");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_chooser), filter);

    gtk_box_pack_start(GTK_BOX(file_box), file_chooser, FALSE, FALSE, 0);

    // Label to show selected file
    GtkWidget *file_label = gtk_label_new("No file selected");
    gtk_widget_set_halign(file_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(file_label, 0.6);
    gtk_box_pack_start(GTK_BOX(file_box), file_label, TRUE, TRUE, 0);

    // Create upload context
    struct UploadContext *ctx = g_new0(struct UploadContext, 1);
    ctx->parent_dialog = dialog;
    ctx->file_label = file_label;

    g_signal_connect(file_chooser, "file-set", G_CALLBACK(on_file_selected), ctx);

    gtk_widget_show_all(dialog);
    g_signal_connect(dialog, "response", G_CALLBACK(on_compose_with_media_response), ctx);
}
