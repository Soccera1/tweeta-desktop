#include <string.h>
#include "ui_components.h"
#include "ui_utils.h"
#include "json_utils.h"
#include "network.h"
#include "constants.h"
#include "globals.h"
#include "actions.h"

static void
on_like_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    if (!g_auth_token) {
        return;
    }

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    gboolean *liked = g_object_get_data(G_OBJECT(widget), "liked_state");

    if (perform_like(tweet_id)) {
        *liked = !(*liked);
        if (*liked) {
            gtk_button_set_label(GTK_BUTTON(widget), "♥ Liked");
        } else {
            gtk_button_set_label(GTK_BUTTON(widget), "♡ Like");
        }
    }
}

static void
on_retweet_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    if (!g_auth_token) {
        return;
    }

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    gboolean *retweeted = g_object_get_data(G_OBJECT(widget), "retweeted_state");

    if (perform_retweet(tweet_id)) {
        *retweeted = !(*retweeted);
        if (*retweeted) {
            gtk_button_set_label(GTK_BUTTON(widget), "↻ Retweeted");
        } else {
            gtk_button_set_label(GTK_BUTTON(widget), "↻ Retweet");
        }
    }
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
            json_builder_set_member_name(builder, "quote");
            json_builder_add_string_value(builder, ctx->quote_id);
            json_builder_end_object(builder);

            JsonGenerator *gen = json_generator_new();
            json_generator_set_root(gen, json_builder_get_root(builder));
            gchar *post_data = json_generator_to_data(gen, NULL);

            struct MemoryStruct chunk;
            if (fetch_url(POST_TWEET_URL, &chunk, post_data)) {
                start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
                free(chunk.memory);
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
    if (!g_auth_token) {
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
    if (!g_auth_token) {
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
    if (!g_auth_token) {
        return;
    }

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "tweet_id");
    gboolean *bookmarked = g_object_get_data(G_OBJECT(widget), "bookmarked_state");

    if (perform_bookmark(tweet_id, !(*bookmarked))) {
        *bookmarked = !(*bookmarked);
        if (*bookmarked) {
            gtk_button_set_label(GTK_BUTTON(widget), "★ Saved");
        } else {
            gtk_button_set_label(GTK_BUTTON(widget), "☆ Bookmark");
        }
    }
}

static void
on_emoji_selected(GtkFlowBoxChild *child, gpointer user_data)
{
    struct ReactionContext *ctx = (struct ReactionContext *)user_data;
    const gchar *emoji_name = g_object_get_data(G_OBJECT(child), "emoji_name");

    if (emoji_name && ctx->tweet_id) {
        perform_reaction(ctx->tweet_id, emoji_name);
    }

    GtkWidget *dialog = gtk_widget_get_ancestor(GTK_WIDGET(child), GTK_TYPE_DIALOG);
    if (dialog) {
        gtk_widget_destroy(dialog);
    }

    g_free(ctx->tweet_id);
    g_free(ctx);
}

static void
on_reaction_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    if (!g_auth_token) {
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

    if (emojis == NULL) {
        const gchar *default_emojis[] = {"👍", "❤️", "😂", "😮", "😢", "😡", "🔥", "👏", NULL};
        for (int i = 0; default_emojis[i] != NULL; i++) {
            GtkWidget *label = gtk_label_new(default_emojis[i]);
            GtkWidget *child_widget = gtk_flow_box_child_new();
            gtk_container_add(GTK_CONTAINER(child_widget), label);
            g_object_set_data_full(G_OBJECT(child_widget), "emoji_name", g_strdup(default_emojis[i]), g_free);
            gtk_container_add(GTK_CONTAINER(flowbox), child_widget);
        }
    }

    free_emojis(emojis);

    g_signal_connect(flowbox, "child-activated", G_CALLBACK(on_emoji_selected), ctx);

    gtk_container_add(GTK_CONTAINER(scrolled), flowbox);
    gtk_box_pack_start(GTK_BOX(content_area), scrolled, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
}

static gboolean
perform_post_tweet(const gchar *content, const gchar *reply_to_id)
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

GtkWidget*
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

    add_attachments_to_box(GTK_BOX(box), tweet->attachments);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(button_box, GTK_ALIGN_START);

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

    gtk_box_pack_start(GTK_BOX(button_box), like_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), retweet_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), reply_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), bookmark_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), reaction_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), button_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), box, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(outer_box), hbox, TRUE, TRUE, 0);
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
