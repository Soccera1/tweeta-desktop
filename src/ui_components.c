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
            json_builder_set_member_name(builder, "source");
            json_builder_add_string_value(builder, "Tweeta Desktop");
            json_builder_set_member_name(builder, "quote_tweet_id");
            json_builder_add_string_value(builder, ctx->quote_id);
            json_builder_end_object(builder);

            JsonGenerator *gen = json_generator_new();
            json_generator_set_root(gen, json_builder_get_root(builder));
            gchar *post_data = json_generator_to_data(gen, NULL);

            struct MemoryStruct chunk;
            if (fetch_url(POST_TWEET_URL, &chunk, post_data, "POST")) {
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

    if (emoji_name && ctx && ctx->tweet_id) {
        perform_reaction(ctx->tweet_id, emoji_name);
    }

    gtk_widget_destroy(dialog);
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

static gboolean
perform_post_tweet(const gchar *content, const gchar *reply_to_id)
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
                const gchar *current_view = gtk_stack_get_visible_child_name(GTK_STACK(g_stack));
                if (g_strcmp0(current_view, "conversation") == 0 && ctx->reply_to_id) {
                    show_tweet(ctx->reply_to_id);
                } else {
                    start_loading_tweets(GTK_LIST_BOX(g_main_list_box));
                }
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
        g_signal_connect(delete_item, "activate", G_CALLBACK(on_admin_delete_post_activated), g_strdup(post_id));
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
        g_signal_connect(verify_item, "activate", G_CALLBACK(on_admin_verify_user_activated), g_strdup(username));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), verify_item);

        GtkWidget *delete_item = gtk_menu_item_new_with_label("Admin: Delete User");
        g_signal_connect(delete_item, "activate", G_CALLBACK(on_admin_delete_user_activated), g_strdup(username));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), delete_item);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
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

    gtk_box_pack_start(GTK_BOX(author_hbox), author_btn, FALSE, FALSE, 0);

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

    gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), box, TRUE, TRUE, 0);
    
    gtk_container_add(GTK_CONTAINER(event_box), hbox);
    g_object_set_data_full(G_OBJECT(event_box), "tweet_id", g_strdup(tweet->id), g_free);

    if (g_is_admin) {
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

    gtk_box_pack_start(GTK_BOX(button_box), like_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), retweet_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), reply_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), bookmark_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), reaction_btn, FALSE, FALSE, 0);

    if (g_is_admin) {
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

    GtkWidget *event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(event_box), hbox);
    g_object_set_data_full(G_OBJECT(event_box), "username", g_strdup(user->username), g_free);
    
    if (g_is_admin) {
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
    if (username) {
        show_profile(username);
    }
}

static gboolean
on_notification_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)user_data;
    if (event->button != 1) return FALSE;

    const gchar *tweet_id = g_object_get_data(G_OBJECT(widget), "related_id");
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
    if (g_strcmp0(notif->type, "like") == 0) {
        notif_text = g_strdup_printf("<b>%s</b> liked your tweet", notif->actor_name ? notif->actor_name : notif->actor_username);
    } else if (g_strcmp0(notif->type, "retweet") == 0) {
        notif_text = g_strdup_printf("<b>%s</b> retweeted your tweet", notif->actor_name ? notif->actor_name : notif->actor_username);
    } else if (g_strcmp0(notif->type, "reply") == 0) {
        notif_text = g_strdup_printf("<b>%s</b> replied to your tweet", notif->actor_name ? notif->actor_name : notif->actor_username);
    } else if (g_strcmp0(notif->type, "follow") == 0) {
        notif_text = g_strdup_printf("<b>%s</b> followed you", notif->actor_name ? notif->actor_name : notif->actor_username);
    } else if (g_strcmp0(notif->type, "mention") == 0) {
        notif_text = g_strdup_printf("<b>%s</b> mentioned you", notif->actor_name ? notif->actor_name : notif->actor_username);
    } else if (g_strcmp0(notif->type, "quote") == 0) {
        notif_text = g_strdup_printf("<b>%s</b> quoted your tweet", notif->actor_name ? notif->actor_name : notif->actor_username);
    } else if (g_strcmp0(notif->type, "reaction") == 0) {
        notif_text = g_strdup_printf("<b>%s</b> reacted to your tweet", notif->actor_name ? notif->actor_name : notif->actor_username);
    } else {
        notif_text = g_strdup_printf("<b>%s</b>: %s", notif->actor_name ? notif->actor_name : notif->actor_username, notif->content);
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
    if (notif->related_id) {
        g_object_set_data_full(G_OBJECT(content_event_box), "related_id", g_strdup(notif->related_id), g_free);
        g_signal_connect(content_event_box, "button-press-event", G_CALLBACK(on_notification_clicked), NULL);
    }
    
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

static void
on_conversation_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    const gchar *conv_id = g_object_get_data(G_OBJECT(widget), "conversation_id");
    const gchar *display_name = g_object_get_data(G_OBJECT(widget), "display_name");
    
    if (conv_id) {
        gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "dm_messages");
        gtk_label_set_text(GTK_LABEL(g_dm_title_label), display_name ? display_name : "Messages");
        g_object_set_data_full(G_OBJECT(g_dm_messages_list), "conversation_id", g_strdup(conv_id), g_free);
        start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);

        // Mark as read
        gchar *url = g_strdup_printf(DM_MARK_READ_URL, conv_id);
        struct MemoryStruct chunk;
        if (fetch_url(url, &chunk, "", "PATCH")) {
            free(chunk.memory);
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

    GtkWidget *last_msg_label = gtk_label_new(conv->last_message_content);
    gtk_label_set_xalign(GTK_LABEL(last_msg_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(last_msg_label), PANGO_ELLIPSIZE_END);
    GtkStyleContext *context = gtk_widget_get_style_context(last_msg_label);
    gtk_style_context_add_class(context, "dim-label");

    gtk_box_pack_start(GTK_BOX(vbox), name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), last_msg_label, FALSE, FALSE, 0);

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

    gchar *header_text = g_strdup_printf("<b>%s</b> (@%s) · %s", msg->name, msg->username, msg->created_at);
    GtkWidget *header_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_label), header_text);
    gtk_label_set_xalign(GTK_LABEL(header_label), 0.0);
    g_free(header_text);

    GtkWidget *content_label = gtk_label_new(msg->content);
    gtk_label_set_xalign(GTK_LABEL(content_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(content_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(content_label), TRUE);

    gtk_box_pack_start(GTK_BOX(vbox), header_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), content_label, FALSE, FALSE, 0);
    
    add_attachments_to_box(GTK_BOX(vbox), msg->attachments);

    gtk_box_pack_start(GTK_BOX(hbox), avatar_image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

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
