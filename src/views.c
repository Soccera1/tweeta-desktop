#include "actions.h"
#include "constants.h"
#include "globals.h"
#include "json_utils.h"
#include "network.h"
#include "ui_components.h"
#include "ui_utils.h"
#include "views.h"
#include "p2p_network.h"
#include "p2p_crypto.h"
#include <glib/gstdio.h>

/* Forward declarations */
extern gboolean on_p2p_contact_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
extern gboolean p2p_send_message(const gchar *recipient_username, const gchar *plaintext, const gchar *recipient_fingerprint);
static void on_lists_refresh_clicked(GtkWidget *widget, gpointer user_data);
static void on_filters_refresh_clicked(GtkWidget *widget, gpointer user_data);
static void on_explore_refresh_clicked(GtkWidget *widget, gpointer user_data);
static GtkWidget *create_articles_view(void);

static inline gchar* get_username_safe(void) {
    g_mutex_lock(&g_globals_mutex);
    gchar *username = g_current_username ? g_strdup(g_current_username) : NULL;
    g_mutex_unlock(&g_globals_mutex);
    return username;
}

struct ProfileEditWidgets {
    GtkWidget *name_entry;
    GtkWidget *bio_entry;
    GtkWidget *location_entry;
    GtkWidget *website_entry;
    GtkWidget *pronouns_entry;
    GtkWidget *theme_combo;
    GtkWidget *accent_entry;
    GtkWidget *label_combo;
    GtkWidget *label_automated_check;
    GtkWidget *avatar_radius_spin;
    gboolean include_avatar_radius;
};

static gchar *
color_button_hex_value(GtkColorChooser *chooser)
{
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(chooser, &rgba);
    return g_strdup_printf("#%02x%02x%02x",
                           (guint)(CLAMP(rgba.red, 0.0, 1.0) * 255.0 + 0.5),
                           (guint)(CLAMP(rgba.green, 0.0, 1.0) * 255.0 + 0.5),
                           (guint)(CLAMP(rgba.blue, 0.0, 1.0) * 255.0 + 0.5));
}

static void
on_color_button_set(GtkColorButton *button, gpointer user_data)
{
    GtkEntry *entry = GTK_ENTRY(user_data);
    gchar *hex = color_button_hex_value(GTK_COLOR_CHOOSER(button));

    gtk_entry_set_text(entry, hex);
    g_free(hex);
}

static void
on_color_entry_changed(GtkEntry *entry, gpointer user_data)
{
    GtkColorChooser *chooser = GTK_COLOR_CHOOSER(user_data);
    GdkRGBA rgba;
    const gchar *text = gtk_entry_get_text(entry);

    if (text && gdk_rgba_parse(&rgba, text)) {
        gtk_color_chooser_set_rgba(chooser, &rgba);
    }
}

static GtkWidget *
create_color_entry_row(GtkWidget *entry, const gchar *initial_value)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *button = gtk_color_button_new();
    GdkRGBA rgba;

    gtk_box_pack_start(GTK_BOX(box), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);

    if (initial_value && gdk_rgba_parse(&rgba, initial_value)) {
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(button), &rgba);
    }
    g_signal_connect(button, "color-set", G_CALLBACK(on_color_button_set), entry);
    g_signal_connect(entry, "changed", G_CALLBACK(on_color_entry_changed), button);
    return box;
}

static gint
profile_theme_index(const gchar *theme)
{
    if (g_strcmp0(theme, "light") == 0) {
        return 1;
    }
    if (g_strcmp0(theme, "dark") == 0) {
        return 2;
    }
    return 0;
}

static gint
profile_label_index(const gchar *label_type)
{
    if (g_strcmp0(label_type, "parody") == 0) {
        return 1;
    }
    if (g_strcmp0(label_type, "fan") == 0) {
        return 2;
    }
    if (g_strcmp0(label_type, "commentary") == 0) {
        return 3;
    }
    return 0;
}

static const gchar *
profile_theme_value(GtkComboBox *combo)
{
    switch (gtk_combo_box_get_active(combo)) {
    case 1:
        return "light";
    case 2:
        return "dark";
    default:
        return "auto";
    }
}

static const gchar *
profile_label_value(GtkComboBox *combo)
{
    switch (gtk_combo_box_get_active(combo)) {
    case 1:
        return "parody";
    case 2:
        return "fan";
    case 3:
        return "commentary";
    default:
        return NULL;
    }
}

static void on_profile_edit_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    if (response_id == GTK_RESPONSE_ACCEPT) {
        struct ProfileEditWidgets *widgets = (struct ProfileEditWidgets *)user_data;
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(widgets->name_entry));
        const gchar *bio = gtk_entry_get_text(GTK_ENTRY(widgets->bio_entry));
        const gchar *location = gtk_entry_get_text(GTK_ENTRY(widgets->location_entry));
        const gchar *website = gtk_entry_get_text(GTK_ENTRY(widgets->website_entry));
        const gchar *pronouns = gtk_entry_get_text(GTK_ENTRY(widgets->pronouns_entry));
        const gchar *theme = profile_theme_value(GTK_COMBO_BOX(widgets->theme_combo));
        const gchar *accent_color = gtk_entry_get_text(GTK_ENTRY(widgets->accent_entry));
        const gchar *label_type = profile_label_value(GTK_COMBO_BOX(widgets->label_combo));
        gboolean label_automated = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets->label_automated_check));
        gint avatar_radius = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->avatar_radius_spin));

        gchar *username = get_username_safe();
        if (username && perform_update_profile(username,
                                               name,
                                               bio,
                                               location,
                                               website,
                                               pronouns,
                                               theme,
                                               accent_color,
                                               label_type,
                                               label_automated,
                                               widgets->include_avatar_radius,
                                               avatar_radius)) {
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
        g_free(username);
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
    GtkWidget *location_entry = gtk_entry_new();
    GtkWidget *website_entry = gtk_entry_new();
    GtkWidget *pronouns_entry = gtk_entry_new();
    GtkWidget *theme_combo = gtk_combo_box_text_new();
    GtkWidget *accent_entry = gtk_entry_new();
    GtkWidget *accent_row;
    GtkWidget *label_combo = gtk_combo_box_text_new();
    GtkWidget *label_automated_check = gtk_check_button_new_with_label("Mark label as automated");
    GtkWidget *avatar_radius_spin = gtk_spin_button_new_with_range(0, 1000, 1);

    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "Display name...");
    gtk_entry_set_placeholder_text(GTK_ENTRY(bio_entry), "Bio...");
    gtk_entry_set_placeholder_text(GTK_ENTRY(location_entry), "Location...");
    gtk_entry_set_placeholder_text(GTK_ENTRY(website_entry), "Website...");
    gtk_entry_set_placeholder_text(GTK_ENTRY(pronouns_entry), "Pronouns...");
    gtk_entry_set_placeholder_text(GTK_ENTRY(accent_entry), "Accent color (for example #1d9bf0)");

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "Auto");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "Light");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "Dark");

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(label_combo), "None");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(label_combo), "Parody");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(label_combo), "Fan");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(label_combo), "Commentary");

    if (g_active_profile) {
        gtk_entry_set_text(GTK_ENTRY(name_entry), g_active_profile->name ? g_active_profile->name : "");
        gtk_entry_set_text(GTK_ENTRY(bio_entry), g_active_profile->bio ? g_active_profile->bio : "");
        gtk_entry_set_text(GTK_ENTRY(location_entry), g_active_profile->location ? g_active_profile->location : "");
        gtk_entry_set_text(GTK_ENTRY(website_entry), g_active_profile->website ? g_active_profile->website : "");
        gtk_entry_set_text(GTK_ENTRY(pronouns_entry), g_active_profile->pronouns ? g_active_profile->pronouns : "");
        gtk_entry_set_text(GTK_ENTRY(accent_entry), g_active_profile->accent_color ? g_active_profile->accent_color : "");
        gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo), profile_theme_index(g_active_profile->theme));
        gtk_combo_box_set_active(GTK_COMBO_BOX(label_combo), profile_label_index(g_active_profile->label_type));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(label_automated_check), g_active_profile->label_automated);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(avatar_radius_spin), g_active_profile->avatar_radius);
    } else {
        gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo), 0);
        gtk_combo_box_set_active(GTK_COMBO_BOX(label_combo), 0);
    }

    accent_row = create_color_entry_row(accent_entry, gtk_entry_get_text(GTK_ENTRY(accent_entry)));

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Bio:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), bio_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Location:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), location_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Website:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), website_entry, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Pronouns:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), pronouns_entry, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Profile theme:"), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), theme_combo, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Accent color:"), 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), accent_row, 1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Profile label:"), 0, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_combo, 1, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_automated_check, 1, 8, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Avatar radius:"), 0, 9, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), avatar_radius_spin, 1, 9, 1, 1);

    gboolean can_edit_avatar_radius = g_active_profile &&
                                      (g_active_profile->author_gold || g_active_profile->author_gray);
    gtk_widget_set_sensitive(avatar_radius_spin, can_edit_avatar_radius);

    gtk_widget_show_all(grid);
    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);

    struct ProfileEditWidgets *widgets = g_new0(struct ProfileEditWidgets, 1);
    widgets->name_entry = name_entry;
    widgets->bio_entry = bio_entry;
    widgets->location_entry = location_entry;
    widgets->website_entry = website_entry;
    widgets->pronouns_entry = pronouns_entry;
    widgets->theme_combo = theme_combo;
    widgets->accent_entry = accent_entry;
    widgets->label_combo = label_combo;
    widgets->label_automated_check = label_automated_check;
    widgets->avatar_radius_spin = avatar_radius_spin;
    widgets->include_avatar_radius = can_edit_avatar_radius;

    g_signal_connect(dialog, "response", G_CALLBACK(on_profile_edit_response), widgets);
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

static void
on_profile_notify_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *username;
    gboolean new_state;

    (void)user_data;
    if (!g_active_profile) {
        return;
    }

    username = g_object_get_data(G_OBJECT(widget), "username");
    if (!username) {
        return;
    }

    new_state = !g_active_profile->notify_tweets;
    if (perform_profile_notify_tweets(username, new_state)) {
        g_active_profile->notify_tweets = new_state;
        show_profile(username);
    }
}

static void
on_profile_block_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *user_id;
    const gchar *username;
    gboolean block_state;

    (void)user_data;
    if (!g_active_profile) {
        return;
    }

    user_id = g_object_get_data(G_OBJECT(widget), "user_id");
    username = g_object_get_data(G_OBJECT(widget), "username");
    if (!user_id || !username) {
        return;
    }

    block_state = !g_active_profile->blocked_profile;
    if (perform_block(user_id, block_state)) {
        g_active_profile->blocked_profile = block_state;
        show_profile(username);
    }
}

static void
on_profile_mute_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *user_id;
    const gchar *username;
    gboolean *muted_state;

    (void)user_data;
    user_id = g_object_get_data(G_OBJECT(widget), "user_id");
    username = g_object_get_data(G_OBJECT(widget), "username");
    muted_state = g_object_get_data(G_OBJECT(widget), "muted_state");
    if (!user_id || !username || !muted_state) {
        return;
    }

    if (perform_mute(user_id, !(*muted_state))) {
        *muted_state = !(*muted_state);
        gtk_button_set_label(GTK_BUTTON(widget), *muted_state ? "Unmute" : "Mute");
        show_profile(username);
    }
}

static void
on_profile_delete_avatar_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *username;

    (void)widget;
    (void)user_data;
    username = get_username_safe();
    if (!username) {
        return;
    }

    if (perform_delete_profile_avatar(username)) {
        show_profile(username);
    }
    g_free(username);
}

static void
on_profile_delete_banner_clicked(GtkWidget *widget, gpointer user_data)
{
    gchar *username;

    (void)widget;
    (void)user_data;
    username = get_username_safe();
    if (!username) {
        return;
    }

    if (perform_delete_profile_banner(username)) {
        show_profile(username);
    }
    g_free(username);
}

GtkWidget*
create_profile_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    g_profile_banner_image = gtk_image_new();
    gtk_widget_set_size_request(g_profile_banner_image, -1, 160);
    gtk_widget_set_no_show_all(g_profile_banner_image, TRUE);
    gtk_widget_hide(g_profile_banner_image);
    gtk_box_pack_start(GTK_BOX(box), g_profile_banner_image, FALSE, FALSE, 0);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    
    g_profile_avatar_image = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DND);
    gtk_widget_set_size_request(g_profile_avatar_image, 80, 80);
    gtk_box_pack_start(GTK_BOX(hbox), g_profile_avatar_image, FALSE, FALSE, 0);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *name_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    g_profile_name_label = gtk_label_new("");
    gtk_widget_set_halign(g_profile_name_label, GTK_ALIGN_START);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_scale_new(1.5));
    gtk_label_set_attributes(GTK_LABEL(g_profile_name_label), attrs);
    pango_attr_list_unref(attrs);

    g_profile_badges_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(name_row), g_profile_name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(name_row), g_profile_badges_box, FALSE, FALSE, 0);

    g_profile_username_label = gtk_label_new("");
    gtk_widget_set_halign(g_profile_username_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(g_profile_username_label, 0.75);

    g_profile_bio_label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(g_profile_bio_label), TRUE);
    gtk_widget_set_halign(g_profile_bio_label, GTK_ALIGN_START);

    g_profile_status_label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(g_profile_status_label), TRUE);
    gtk_widget_set_halign(g_profile_status_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(g_profile_status_label, 0.8);

    g_profile_details_label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(g_profile_details_label), TRUE);
    gtk_widget_set_halign(g_profile_details_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(g_profile_details_label, 0.8);

    g_profile_stats_label = gtk_label_new("");
    gtk_widget_set_halign(g_profile_stats_label, GTK_ALIGN_START);

    g_follow_button = gtk_button_new_with_label("Follow");
    gtk_widget_set_no_show_all(g_follow_button, TRUE);
    gtk_widget_hide(g_follow_button);
    g_signal_connect(g_follow_button, "clicked", G_CALLBACK(on_follow_button_clicked), NULL);

    g_profile_notify_button = gtk_button_new_with_label("Alerts Off");
    gtk_widget_set_no_show_all(g_profile_notify_button, TRUE);
    gtk_widget_hide(g_profile_notify_button);
    g_signal_connect(g_profile_notify_button, "clicked", G_CALLBACK(on_profile_notify_clicked), NULL);

    g_profile_block_button = gtk_button_new_with_label("Block");
    gtk_widget_set_no_show_all(g_profile_block_button, TRUE);
    gtk_widget_hide(g_profile_block_button);
    g_signal_connect(g_profile_block_button, "clicked", G_CALLBACK(on_profile_block_clicked), NULL);

    g_profile_mute_button = gtk_button_new_with_label("Mute");
    gtk_widget_set_no_show_all(g_profile_mute_button, TRUE);
    gtk_widget_hide(g_profile_mute_button);
    g_signal_connect(g_profile_mute_button, "clicked", G_CALLBACK(on_profile_mute_clicked), NULL);

    g_profile_report_button = gtk_button_new_with_label("Report");
    gtk_widget_set_no_show_all(g_profile_report_button, TRUE);
    gtk_widget_hide(g_profile_report_button);
    g_signal_connect(g_profile_report_button, "clicked", G_CALLBACK(on_report_profile_clicked), NULL);

    g_profile_affiliate_button = gtk_button_new_with_label("Request Affiliate");
    gtk_widget_set_no_show_all(g_profile_affiliate_button, TRUE);
    gtk_widget_hide(g_profile_affiliate_button);
    g_signal_connect(g_profile_affiliate_button, "clicked", G_CALLBACK(on_request_affiliate_clicked), NULL);

    g_profile_shop_button = gtk_button_new_with_label("Shop");
    gtk_widget_set_no_show_all(g_profile_shop_button, TRUE);
    gtk_widget_hide(g_profile_shop_button);
    g_signal_connect(g_profile_shop_button, "clicked", G_CALLBACK(on_profile_shop_clicked), NULL);

    g_profile_donate_button = gtk_button_new_with_label("Donate");
    gtk_widget_set_no_show_all(g_profile_donate_button, TRUE);
    gtk_widget_hide(g_profile_donate_button);
    g_signal_connect(g_profile_donate_button, "clicked", G_CALLBACK(on_profile_donate_clicked), NULL);

    g_profile_algorithm_button = gtk_button_new_with_label("Algorithm");
    gtk_widget_set_no_show_all(g_profile_algorithm_button, TRUE);
    gtk_widget_hide(g_profile_algorithm_button);
    g_signal_connect(g_profile_algorithm_button, "clicked", G_CALLBACK(on_profile_algorithm_stats_clicked), NULL);

    g_profile_spam_score_button = gtk_button_new_with_label("Spam Score");
    gtk_widget_set_no_show_all(g_profile_spam_score_button, TRUE);
    gtk_widget_hide(g_profile_spam_score_button);
    g_signal_connect(g_profile_spam_score_button, "clicked", G_CALLBACK(on_profile_spam_score_clicked), NULL);

    g_profile_analytics_button = gtk_button_new_with_label("Analytics");
    gtk_widget_set_no_show_all(g_profile_analytics_button, TRUE);
    gtk_widget_hide(g_profile_analytics_button);
    g_signal_connect(g_profile_analytics_button, "clicked", G_CALLBACK(on_profile_analytics_clicked), NULL);

    g_profile_common_followers_button = gtk_button_new_with_label("Common");
    gtk_widget_set_no_show_all(g_profile_common_followers_button, TRUE);
    gtk_widget_hide(g_profile_common_followers_button);
    g_signal_connect(g_profile_common_followers_button, "clicked", G_CALLBACK(on_profile_common_followers_clicked), NULL);

    g_profile_top_posts_button = gtk_button_new_with_label("Top Posts");
    gtk_widget_set_no_show_all(g_profile_top_posts_button, TRUE);
    gtk_widget_hide(g_profile_top_posts_button);
    g_signal_connect(g_profile_top_posts_button, "clicked", G_CALLBACK(on_profile_top_posts_clicked), NULL);

    g_profile_communities_button = gtk_button_new_with_label("Communities");
    gtk_widget_set_no_show_all(g_profile_communities_button, TRUE);
    gtk_widget_hide(g_profile_communities_button);
    g_signal_connect(g_profile_communities_button, "clicked", G_CALLBACK(on_profile_communities_clicked), NULL);

    g_profile_delete_avatar_button = gtk_button_new_with_label("Remove Avatar");
    gtk_widget_set_no_show_all(g_profile_delete_avatar_button, TRUE);
    gtk_widget_hide(g_profile_delete_avatar_button);
    g_signal_connect(g_profile_delete_avatar_button, "clicked", G_CALLBACK(on_profile_delete_avatar_clicked), NULL);

    g_profile_delete_banner_button = gtk_button_new_with_label("Remove Banner");
    gtk_widget_set_no_show_all(g_profile_delete_banner_button, TRUE);
    gtk_widget_hide(g_profile_delete_banner_button);
    g_signal_connect(g_profile_delete_banner_button, "clicked", G_CALLBACK(on_profile_delete_banner_clicked), NULL);

    g_profile_edit_button = gtk_button_new_with_label("Edit Profile");
    gtk_widget_set_no_show_all(g_profile_edit_button, TRUE);
    gtk_widget_hide(g_profile_edit_button);
    g_signal_connect(g_profile_edit_button, "clicked", G_CALLBACK(on_edit_profile_clicked), NULL);

    GtkWidget *actions_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(actions_row), g_follow_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_notify_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_block_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_mute_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_report_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_affiliate_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_shop_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_donate_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_algorithm_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_spam_score_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_analytics_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_common_followers_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_top_posts_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_communities_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_edit_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_delete_avatar_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_row), g_profile_delete_banner_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), name_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_profile_username_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_profile_bio_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_profile_status_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_profile_details_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_profile_stats_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), actions_row, FALSE, FALSE, 5);

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

    GtkWidget *media_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_media_list = gtk_list_box_new();
    g_object_set_data(G_OBJECT(g_profile_media_list), "feed_type", "profile_media");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_media_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(media_scroll), g_profile_media_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), media_scroll, gtk_label_new("Media"));

    GtkWidget *highlights_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_highlights_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_highlights_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(highlights_scroll), g_profile_highlights_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), highlights_scroll, gtk_label_new("Highlights"));

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

    GtkWidget *mutuals_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_mutuals_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_mutuals_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(mutuals_scroll), g_profile_mutuals_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), mutuals_scroll, gtk_label_new("Mutuals"));

    GtkWidget *followers_you_know_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_followers_you_know_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_followers_you_know_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(followers_you_know_scroll), g_profile_followers_you_know_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), followers_you_know_scroll, gtk_label_new("You Know"));

    GtkWidget *affiliates_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_profile_affiliates_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_profile_affiliates_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(affiliates_scroll), g_profile_affiliates_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), affiliates_scroll, gtk_label_new("Affiliates"));

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
update_dm_compose_status_label(void)
{
    GtkWidget *status_label;
    const gchar *reply_to;
    const gchar *reply_preview;
    const gchar *file_path;
    gchar *basename = NULL;
    GString *status;

    if (!g_dm_messages_list) {
        return;
    }

    status_label = g_object_get_data(G_OBJECT(g_dm_messages_list), "composer_status_label");
    if (!status_label) {
        return;
    }

    reply_to = g_object_get_data(G_OBJECT(g_dm_messages_list), "reply_to_id");
    reply_preview = g_object_get_data(G_OBJECT(g_dm_messages_list), "reply_preview");
    file_path = g_object_get_data(G_OBJECT(g_dm_messages_list), "pending_file_path");

    status = g_string_new(NULL);
    if (reply_to) {
        g_string_append(status, "Replying");
        if (reply_preview && reply_preview[0] != '\0') {
            gchar *trimmed = g_strdup(reply_preview);
            if (strlen(trimmed) > 48) {
                trimmed[48] = '\0';
            }
            g_string_append_printf(status, " to: %s", trimmed);
            g_free(trimmed);
        }
    }
    if (file_path) {
        if (status->len > 0) {
            g_string_append(status, " | ");
        }
        basename = g_path_get_basename(file_path);
        g_string_append_printf(status, "Attachment: %s", basename);
    }

    gtk_label_set_text(GTK_LABEL(status_label), status->str);
    gtk_widget_set_visible(status_label, status->len > 0);

    g_free(basename);
    g_string_free(status, TRUE);
}

static void
clear_dm_compose_context(void)
{
    if (!g_dm_messages_list) {
        return;
    }

    g_object_set_data_full(G_OBJECT(g_dm_messages_list), "reply_to_id", NULL, g_free);
    g_object_set_data_full(G_OBJECT(g_dm_messages_list), "reply_preview", NULL, g_free);
    g_object_set_data_full(G_OBJECT(g_dm_messages_list), "pending_file_path", NULL, g_free);
    g_object_set_data_full(G_OBJECT(g_dm_messages_list), "pending_file_type", NULL, g_free);
    update_dm_compose_status_label();
}

static void
send_dm_typing_state(gboolean typing)
{
    const gchar *conv_id;
    gchar *url;
    struct MemoryStruct chunk = {0};

    if (!g_dm_messages_list || !g_auth_token) {
        return;
    }

    conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
    if (!conv_id) {
        return;
    }

    url = g_strdup_printf(typing ? DM_TYPING_URL : DM_TYPING_STOP_URL, conv_id);
    if (fetch_url(url, &chunk, "{}", "POST")) {
        g_free(chunk.memory);
    }
    g_free(url);
}

static void
send_dm_draft_update(const gchar *content)
{
    const gchar *conv_id;
    gchar *url;
    JsonBuilder *builder;
    JsonGenerator *gen;
    gchar *payload;
    struct MemoryStruct chunk = {0};

    if (!g_dm_messages_list || !g_auth_token) {
        return;
    }
    conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
    if (!conv_id) {
        return;
    }

    builder = json_builder_new();
    gen = json_generator_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, content ? content : "");
    json_builder_end_object(builder);
    json_generator_set_root(gen, json_builder_get_root(builder));
    payload = json_generator_to_data(gen, NULL);
    url = g_strdup_printf(DM_DRAFT_URL, conv_id);
    if (fetch_url(url, &chunk, payload, "POST")) {
        g_free(chunk.memory);
    }
    g_free(url);
    g_free(payload);
    g_object_unref(gen);
    g_object_unref(builder);
}

static void
on_dm_entry_changed(GtkEditable *editable, gpointer user_data)
{
    const gchar *text;
    gboolean was_typing;
    gboolean is_typing;

    (void)user_data;
    text = gtk_entry_get_text(GTK_ENTRY(editable));
    was_typing = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(editable), "typing_active"));
    is_typing = (text && text[0] != '\0');

    if (is_typing != was_typing) {
        send_dm_typing_state(is_typing);
        g_object_set_data(G_OBJECT(editable), "typing_active", GINT_TO_POINTER(is_typing));
    }
    send_dm_draft_update(text);
}

static void
on_dm_attach_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *toplevel;
    gchar *filename;

    (void)user_data;
    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_file_chooser_dialog_new("Attach File",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Attach", GTK_RESPONSE_ACCEPT,
                                         NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            gchar *mime_type = detect_mime_type(filename);
            g_object_set_data_full(G_OBJECT(g_dm_messages_list), "pending_file_path", filename, g_free);
            g_object_set_data_full(G_OBJECT(g_dm_messages_list), "pending_file_type", mime_type, g_free);
            update_dm_compose_status_label();
        }
    }

    gtk_widget_destroy(dialog);
}

static void
on_dm_clear_context_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    clear_dm_compose_context();
}

static gboolean
perform_dm_simple_request(const gchar *url, const gchar *payload, const gchar *method)
{
    struct MemoryStruct chunk = {0};
    gboolean success = FALSE;

    if (fetch_url(url, &chunk, payload, method)) {
        success = (chunk.memory && strstr(chunk.memory, "\"error\"") == NULL);
        g_free(chunk.memory);
    }

    return success;
}

static void
on_dm_title_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *entry;
    GtkWidget *content_area;
    const gchar *conv_id;
    struct Conversation *conversation;
    GtkWidget *toplevel;

    (void)user_data;
    if (!g_dm_messages_list) {
        return;
    }

    conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
    conversation = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_detail");
    if (!conv_id || !conversation) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Rename Conversation",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), conversation->title ? conversation->title : "");
    gtk_box_pack_start(GTK_BOX(content_area), entry, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *url = g_strdup_printf(DM_UPDATE_TITLE_URL, conv_id);
        JsonBuilder *builder = json_builder_new();
        JsonGenerator *gen = json_generator_new();
        gchar *payload;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "title");
        json_builder_add_string_value(builder, gtk_entry_get_text(GTK_ENTRY(entry)));
        json_builder_end_object(builder);
        json_generator_set_root(gen, json_builder_get_root(builder));
        payload = json_generator_to_data(gen, NULL);
        if (perform_dm_simple_request(url, payload, "PATCH")) {
            start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);
        }
        g_free(payload);
        g_object_unref(gen);
        g_object_unref(builder);
        g_free(url);
    }

    gtk_widget_destroy(dialog);
}

static void
on_dm_add_people_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *entry;
    GtkWidget *content_area;
    const gchar *conv_id;
    GtkWidget *toplevel;

    (void)user_data;
    conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
    if (!conv_id) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Add Participants",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Add", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "username1, username2");
    gtk_box_pack_start(GTK_BOX(content_area), entry, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *raw = gtk_entry_get_text(GTK_ENTRY(entry));
        if (raw && raw[0] != '\0') {
            gchar **parts = g_strsplit(raw, ",", -1);
            gchar *url = g_strdup_printf(DM_ADD_PARTICIPANTS_URL, conv_id);
            JsonBuilder *builder = json_builder_new();
            JsonGenerator *gen = json_generator_new();
            gchar *payload;
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "usernames");
            json_builder_begin_array(builder);
            for (gint i = 0; parts[i] != NULL; i++) {
                gchar *trimmed = g_strdup(parts[i]);
                g_strstrip(trimmed);
                if (trimmed[0] != '\0') {
                    json_builder_add_string_value(builder, trimmed);
                }
                g_free(trimmed);
            }
            json_builder_end_array(builder);
            json_builder_end_object(builder);
            json_generator_set_root(gen, json_builder_get_root(builder));
            payload = json_generator_to_data(gen, NULL);
            if (perform_dm_simple_request(url, payload, "POST")) {
                start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);
            }
            g_free(payload);
            g_object_unref(gen);
            g_object_unref(builder);
            g_free(url);
            g_strfreev(parts);
        }
    }

    gtk_widget_destroy(dialog);
}

static void
on_dm_leave_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *conv_id;
    struct Conversation *conversation;
    gchar *current_username;

    (void)widget;
    (void)user_data;
    conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
    conversation = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_detail");
    current_username = get_username_safe();
    if (conv_id && conversation && current_username) {
        for (GList *l = conversation->participants; l != NULL; l = l->next) {
            struct Profile *participant = l->data;
            if (participant->username && g_strcmp0(participant->username, current_username) == 0 && participant->id) {
                gchar *url = g_strdup_printf(DM_REMOVE_PARTICIPANT_URL, conv_id, participant->id);
                if (perform_dm_simple_request(url, NULL, "DELETE")) {
                    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "messages");
                    start_loading_conversations(GTK_LIST_BOX(g_conversations_list));
                }
                g_free(url);
                break;
            }
        }
    }
    g_free(current_username);
}

static void
on_dm_disappearing_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *enabled_check;
    GtkWidget *duration_spin;
    const gchar *conv_id;
    struct Conversation *conversation;
    GtkWidget *toplevel;

    (void)user_data;
    conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
    conversation = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_detail");
    if (!conv_id) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Disappearing Messages",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    enabled_check = gtk_check_button_new_with_label("Enable disappearing messages");
    duration_spin = gtk_spin_button_new_with_range(5, 86400, 5);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled_check),
                                 conversation ? conversation->disappearing_enabled : FALSE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_spin),
                              conversation && conversation->disappearing_duration > 0 ?
                              conversation->disappearing_duration : 60);
    gtk_box_pack_start(GTK_BOX(content_area), enabled_check, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(content_area), duration_spin, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *url = g_strdup_printf(DM_DISAPPEARING_URL, conv_id);
        JsonBuilder *builder = json_builder_new();
        JsonGenerator *gen = json_generator_new();
        gchar *payload;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "enabled");
        json_builder_add_boolean_value(builder, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled_check)));
        json_builder_set_member_name(builder, "duration");
        json_builder_add_int_value(builder, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(duration_spin)));
        json_builder_end_object(builder);
        json_generator_set_root(gen, json_builder_get_root(builder));
        payload = json_generator_to_data(gen, NULL);
        if (perform_dm_simple_request(url, payload, "PATCH")) {
            start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);
        }
        g_free(payload);
        g_object_unref(gen);
        g_object_unref(builder);
        g_free(url);
    }

    gtk_widget_destroy(dialog);
}

static void
show_dm_request_message(GtkWindow *parent, GtkMessageType type, const gchar *title, const gchar *message)
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

static gchar*
extract_request_error(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    gchar *message = NULL;
    if (json_data && json_parser_load_from_data(parser, json_data, -1, NULL)) {
        JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
        if (obj && json_object_has_member(obj, "error") &&
            !json_node_is_null(json_object_get_member(obj, "error")))
            message = g_strdup(json_object_get_string_member(obj, "error"));
    }
    g_object_unref(parser);
    return message ? message : g_strdup("The request could not be created.");
}

static void
on_dm_request_payment_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    const gchar *conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GtkWindow *window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    if (!conv_id) return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Request payment",
                                                    window,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Request", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    GtkWidget *amount = gtk_spin_button_new_with_range(1, 100000, 1);
    GtkWidget *note = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(note), "Optional note");
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Amount (₹)"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), amount, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Note"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), note, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        JsonBuilder *builder = json_builder_new();
        JsonGenerator *gen = json_generator_new();
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "conversationId");
        json_builder_add_string_value(builder, conv_id);
        json_builder_set_member_name(builder, "amount");
        json_builder_add_double_value(builder, gtk_spin_button_get_value(GTK_SPIN_BUTTON(amount)));
        const gchar *note_text = gtk_entry_get_text(GTK_ENTRY(note));
        if (note_text && *note_text) {
            json_builder_set_member_name(builder, "note");
            json_builder_add_string_value(builder, note_text);
        }
        json_builder_end_object(builder);
        JsonNode *root = json_builder_get_root(builder);
        json_generator_set_root(gen, root);
        gchar *payload = json_generator_to_data(gen, NULL);
        struct MemoryStruct chunk = {0};
        if (fetch_url(MPI_REQUEST_URL, &chunk, payload, "POST")) {
            JsonParser *parser = json_parser_new();
            gboolean success = FALSE;
            if (chunk.memory && json_parser_load_from_data(parser, chunk.memory, -1, NULL)) {
                JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
                success = obj && json_object_has_member(obj, "success") &&
                    json_object_get_boolean_member(obj, "success");
            }
            g_object_unref(parser);
            if (success) {
                start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);
            } else {
                gchar *error = extract_request_error(chunk.memory);
                show_dm_request_message(window, GTK_MESSAGE_ERROR, "Payment request failed.", error);
                g_free(error);
            }
            g_free(chunk.memory);
        } else {
            show_dm_request_message(window, GTK_MESSAGE_ERROR, "Payment request failed.", NULL);
        }
        g_free(payload);
        json_node_free(root);
        g_object_unref(gen);
        g_object_unref(builder);
    }
    gtk_widget_destroy(dialog);
}

static void
on_dm_send_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    const gchar *content = gtk_entry_get_text(GTK_ENTRY(g_dm_entry));
    const gchar *conv_id = g_object_get_data(G_OBJECT(g_dm_messages_list), "conversation_id");
    const gchar *p2p_recipient = g_object_get_data(G_OBJECT(g_dm_messages_list), "p2p_recipient");
    const gchar *reply_to = g_object_get_data(G_OBJECT(g_dm_messages_list), "reply_to_id");
    const gchar *pending_file_path = g_object_get_data(G_OBJECT(g_dm_messages_list), "pending_file_path");
    const gchar *pending_file_type = g_object_get_data(G_OBJECT(g_dm_messages_list), "pending_file_type");

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
    if (((content && strlen(content) > 0) || pending_file_path) && conv_id) {
        gchar *url = g_strdup_printf(DM_SEND_MESSAGE_URL, conv_id);
        JsonBuilder *builder = json_builder_new();
        JsonGenerator *gen = json_generator_new();
        gchar *post_data;
        struct MemoryStruct chunk = {0};
        gchar *uploaded_url = NULL;

        json_builder_begin_object(builder);
        if (content) {
            json_builder_set_member_name(builder, "content");
            json_builder_add_string_value(builder, content);
        }
        if (reply_to) {
            json_builder_set_member_name(builder, "replyTo");
            json_builder_add_string_value(builder, reply_to);
        }
        if (pending_file_path) {
            GStatBuf stat_buf;
            gchar *basename = g_path_get_basename(pending_file_path);
            uploaded_url = perform_media_upload(pending_file_path);
            if (uploaded_url) {
                json_builder_set_member_name(builder, "files");
                json_builder_begin_array(builder);
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "hash");
                json_builder_add_null_value(builder);
                json_builder_set_member_name(builder, "name");
                json_builder_add_string_value(builder, basename ? basename : "attachment");
                json_builder_set_member_name(builder, "type");
                json_builder_add_string_value(builder, pending_file_type ? pending_file_type : "application/octet-stream");
                json_builder_set_member_name(builder, "size");
                if (g_stat(pending_file_path, &stat_buf) == 0) {
                    json_builder_add_int_value(builder, stat_buf.st_size);
                } else {
                    json_builder_add_int_value(builder, 0);
                }
                json_builder_set_member_name(builder, "url");
                json_builder_add_string_value(builder, uploaded_url);
                json_builder_end_object(builder);
                json_builder_end_array(builder);
            }
            g_free(basename);
        }
        json_builder_end_object(builder);
        json_generator_set_root(gen, json_builder_get_root(builder));
        post_data = json_generator_to_data(gen, NULL);

        if (fetch_url(url, &chunk, post_data, "POST")) {
            gtk_entry_set_text(GTK_ENTRY(g_dm_entry), "");
            g_object_set_data(G_OBJECT(g_dm_entry), "typing_active", GINT_TO_POINTER(FALSE));
            send_dm_typing_state(FALSE);
            clear_dm_compose_context();
            start_loading_messages(GTK_LIST_BOX(g_dm_messages_list), conv_id);
            g_free(chunk.memory);
        }
        
        g_free(uploaded_url);
        g_free(post_data);
        g_object_unref(gen);
        g_object_unref(builder);
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
    g_signal_connect(g_p2p_contacts_list, "row-selected", G_CALLBACK(on_p2p_contact_row_selected), NULL);
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
    GtkWidget *header_text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *header_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    g_dm_title_label = gtk_label_new("Messages");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(g_dm_title_label), attrs);
    pango_attr_list_unref(attrs);

    g_dm_info_label = gtk_label_new("");
    gtk_widget_set_opacity(g_dm_info_label, 0.75);
    gtk_widget_set_halign(g_dm_info_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(g_dm_info_label), TRUE);

    gtk_box_pack_start(GTK_BOX(header_text_box), g_dm_title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_text_box), g_dm_info_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_box), header_text_box, TRUE, TRUE, 0);

    GtkWidget *rename_btn = gtk_button_new_with_label("Rename");
    g_signal_connect(rename_btn, "clicked", G_CALLBACK(on_dm_title_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header_actions), rename_btn, FALSE, FALSE, 0);

    GtkWidget *add_btn = gtk_button_new_with_label("Add");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_dm_add_people_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header_actions), add_btn, FALSE, FALSE, 0);

    GtkWidget *disappearing_btn = gtk_button_new_with_label("Disappear");
    g_signal_connect(disappearing_btn, "clicked", G_CALLBACK(on_dm_disappearing_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header_actions), disappearing_btn, FALSE, FALSE, 0);

    GtkWidget *invite_btn = gtk_button_new_with_label("Invite");
    g_signal_connect(invite_btn, "clicked", G_CALLBACK(on_dm_invite_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header_actions), invite_btn, FALSE, FALSE, 0);

    GtkWidget *join_btn = gtk_button_new_with_label("Join");
    g_signal_connect(join_btn, "clicked", G_CALLBACK(on_dm_join_invite_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header_actions), join_btn, FALSE, FALSE, 0);

    GtkWidget *permissions_btn = gtk_button_new_with_label("Permissions");
    g_signal_connect(permissions_btn, "clicked", G_CALLBACK(on_dm_permissions_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header_actions), permissions_btn, FALSE, FALSE, 0);

    GtkWidget *roles_btn = gtk_button_new_with_label("Roles");
    g_signal_connect(roles_btn, "clicked", G_CALLBACK(on_dm_roles_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header_actions), roles_btn, FALSE, FALSE, 0);

    GtkWidget *pinned_btn = gtk_button_new_with_label("Pinned");
    g_signal_connect(pinned_btn, "clicked", G_CALLBACK(on_dm_pinned_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header_actions), pinned_btn, FALSE, FALSE, 0);

    GtkWidget *leave_btn = gtk_button_new_with_label("Leave");
    g_signal_connect(leave_btn, "clicked", G_CALLBACK(on_dm_leave_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(header_actions), leave_btn, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(header_box), header_actions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), header_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    g_dm_messages_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_dm_messages_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_dm_messages_list);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    
    GtkWidget *input_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(input_hbox), 5);
    GtkWidget *attach_btn = gtk_button_new_with_label("Attach");
    GtkWidget *request_btn = gtk_button_new_with_label("Request");
    g_dm_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_dm_entry), "Type a message...");
    g_signal_connect(g_dm_entry, "activate", G_CALLBACK(on_dm_send_clicked), NULL);
    g_signal_connect(g_dm_entry, "changed", G_CALLBACK(on_dm_entry_changed), NULL);
    
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_dm_clear_context_clicked), NULL);

    GtkWidget *send_btn = gtk_button_new_with_label("Send");
    g_signal_connect(send_btn, "clicked", G_CALLBACK(on_dm_send_clicked), NULL);

    GtkWidget *status_label = gtk_label_new("");
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(status_label, 0.75);
    gtk_widget_set_no_show_all(status_label, TRUE);

    g_signal_connect(attach_btn, "clicked", G_CALLBACK(on_dm_attach_clicked), NULL);
    g_signal_connect(request_btn, "clicked", G_CALLBACK(on_dm_request_payment_clicked), NULL);
    
    gtk_box_pack_start(GTK_BOX(input_hbox), attach_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(input_hbox), request_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(input_hbox), g_dm_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(input_hbox), clear_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(input_hbox), send_btn, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(g_dm_messages_list), "composer_status_label", status_label);
    gtk_box_pack_start(GTK_BOX(box), status_label, FALSE, FALSE, 5);
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

    GtkWidget *push_btn = gtk_button_new_with_label("Push Notifications");
    g_signal_connect(push_btn, "clicked", G_CALLBACK(on_push_notifications_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(notifications_box), push_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(notifications_frame), notifications_box);
    gtk_box_pack_start(GTK_BOX(content_box), notifications_frame, FALSE, FALSE, 0);

    // Content Filters Section
    GtkWidget *filters_frame = gtk_frame_new("Content Filters");
    GtkWidget *filters_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *muted_word_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *muted_words_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *muted_conversations_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *add_muted_word_btn = gtk_button_new_with_label("Add");
    GtkWidget *refresh_filters_btn = gtk_button_new_with_label("Refresh Filters");
    gtk_container_set_border_width(GTK_CONTAINER(filters_box), 10);
    g_muted_word_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_muted_word_entry), "Muted word or phrase");
    g_signal_connect(g_muted_word_entry, "activate", G_CALLBACK(on_add_muted_word_clicked), NULL);
    g_signal_connect(add_muted_word_btn, "clicked", G_CALLBACK(on_add_muted_word_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(muted_word_row), g_muted_word_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(muted_word_row), add_muted_word_btn, FALSE, FALSE, 0);
    g_muted_words_list = gtk_list_box_new();
    g_muted_conversations_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_muted_words_list), GTK_SELECTION_NONE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_muted_conversations_list), GTK_SELECTION_NONE);
    gtk_widget_set_size_request(muted_words_scroll, -1, 150);
    gtk_widget_set_size_request(muted_conversations_scroll, -1, 120);
    gtk_container_add(GTK_CONTAINER(muted_words_scroll), g_muted_words_list);
    gtk_container_add(GTK_CONTAINER(muted_conversations_scroll), g_muted_conversations_list);
    g_signal_connect(refresh_filters_btn, "clicked", G_CALLBACK(on_filters_refresh_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(filters_box), muted_word_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filters_box), gtk_label_new("Muted words"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filters_box), muted_words_scroll, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filters_box), gtk_label_new("Muted conversations"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filters_box), muted_conversations_scroll, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filters_box), refresh_filters_btn, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(filters_frame), filters_box);
    gtk_box_pack_start(GTK_BOX(content_box), filters_frame, FALSE, FALSE, 0);

    GtkWidget *interests_frame = gtk_frame_new("For You Interests");
    GtkWidget *interests_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *interests_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *interests_refresh_btn = gtk_button_new_with_label("Refresh Interests");
    GtkWidget *interests_clear_btn = gtk_button_new_with_label("Reset Interests");
    GtkWidget *interests_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(interests_box), 10);
    g_for_you_interests_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_for_you_interests_list), GTK_SELECTION_NONE);
    gtk_widget_set_size_request(interests_scroll, -1, 160);
    gtk_container_add(GTK_CONTAINER(interests_scroll), g_for_you_interests_list);
    g_signal_connect(interests_refresh_btn, "clicked", G_CALLBACK(on_filters_refresh_clicked), NULL);
    g_signal_connect(interests_clear_btn, "clicked", G_CALLBACK(on_clear_for_you_interests_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(interests_actions), interests_refresh_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(interests_actions), interests_clear_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(interests_box), interests_actions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(interests_box), interests_scroll, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(interests_frame), interests_box);
    gtk_box_pack_start(GTK_BOX(content_box), interests_frame, FALSE, FALSE, 0);

    GtkWidget *schedule_frame = gtk_frame_new("Scheduled Posts");
    GtkWidget *schedule_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *schedule_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *schedule_new_btn = gtk_button_new_with_label("Schedule Post");
    GtkWidget *schedule_refresh_btn = gtk_button_new_with_label("Refresh");
    GtkWidget *schedule_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(schedule_box), 10);
    g_scheduled_posts_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_scheduled_posts_list), GTK_SELECTION_NONE);
    gtk_widget_set_size_request(schedule_scroll, -1, 180);
    gtk_container_add(GTK_CONTAINER(schedule_scroll), g_scheduled_posts_list);
    g_signal_connect(schedule_new_btn, "clicked", G_CALLBACK(on_schedule_post_clicked), NULL);
    g_signal_connect(schedule_refresh_btn, "clicked", G_CALLBACK(on_filters_refresh_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(schedule_actions), schedule_new_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(schedule_actions), schedule_refresh_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(schedule_box), schedule_actions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(schedule_box), schedule_scroll, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(schedule_frame), schedule_box);
    gtk_box_pack_start(GTK_BOX(content_box), schedule_frame, FALSE, FALSE, 0);

    GtkWidget *shop_frame = gtk_frame_new("Shop");
    GtkWidget *shop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *shop_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *shop_new_btn = gtk_button_new_with_label("New Product");
    GtkWidget *shop_refresh_btn = gtk_button_new_with_label("Refresh Shop");
    GtkWidget *shop_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(shop_box), 10);
    g_shop_products_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_shop_products_list), GTK_SELECTION_NONE);
    gtk_widget_set_size_request(shop_scroll, -1, 220);
    gtk_container_add(GTK_CONTAINER(shop_scroll), g_shop_products_list);
    g_signal_connect(shop_new_btn, "clicked", G_CALLBACK(on_create_shop_product_clicked), NULL);
    g_signal_connect(shop_refresh_btn, "clicked", G_CALLBACK(on_filters_refresh_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(shop_actions), shop_new_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(shop_actions), shop_refresh_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(shop_box), shop_actions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(shop_box), shop_scroll, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(shop_frame), shop_box);
    gtk_box_pack_start(GTK_BOX(content_box), shop_frame, FALSE, FALSE, 0);

    GtkWidget *delegates_frame = gtk_frame_new("Delegates");
    GtkWidget *delegates_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *delegate_invite_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *delegate_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *invite_delegate_btn = gtk_button_new_with_label("Invite");
    GtkWidget *switch_primary_btn = gtk_button_new_with_label("Switch to Primary");
    GtkWidget *add_account_btn = gtk_button_new_with_label("Add Account");
    GtkWidget *validate_accounts_btn = gtk_button_new_with_label("Validate Accounts");
    GtkWidget *delegates_notebook = gtk_notebook_new();
    GtkWidget *delegates_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *delegations_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *invitations_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *sent_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *refresh_delegates_btn = gtk_button_new_with_label("Refresh Delegates");
    gtk_container_set_border_width(GTK_CONTAINER(delegates_box), 10);
    g_delegate_username_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_delegate_username_entry), "Username to invite");
    g_signal_connect(g_delegate_username_entry, "activate", G_CALLBACK(on_invite_delegate_clicked), NULL);
    g_signal_connect(invite_delegate_btn, "clicked", G_CALLBACK(on_invite_delegate_clicked), NULL);
    g_signal_connect(refresh_delegates_btn, "clicked", G_CALLBACK(on_filters_refresh_clicked), NULL);
    g_signal_connect(switch_primary_btn, "clicked", G_CALLBACK(on_switch_primary_clicked), NULL);
    g_signal_connect(add_account_btn, "clicked", G_CALLBACK(on_add_account_clicked), NULL);
    g_signal_connect(validate_accounts_btn, "clicked", G_CALLBACK(on_validate_accounts_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(delegate_invite_row), g_delegate_username_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(delegate_invite_row), invite_delegate_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(delegate_actions), switch_primary_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(delegate_actions), add_account_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(delegate_actions), validate_accounts_btn, FALSE, FALSE, 0);
    g_delegates_list = gtk_list_box_new();
    g_delegations_list = gtk_list_box_new();
    g_delegate_invitations_list = gtk_list_box_new();
    g_delegate_sent_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_delegates_list), GTK_SELECTION_NONE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_delegations_list), GTK_SELECTION_NONE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_delegate_invitations_list), GTK_SELECTION_NONE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_delegate_sent_list), GTK_SELECTION_NONE);
    gtk_widget_set_size_request(delegates_notebook, -1, 260);
    gtk_container_add(GTK_CONTAINER(delegates_scroll), g_delegates_list);
    gtk_container_add(GTK_CONTAINER(delegations_scroll), g_delegations_list);
    gtk_container_add(GTK_CONTAINER(invitations_scroll), g_delegate_invitations_list);
    gtk_container_add(GTK_CONTAINER(sent_scroll), g_delegate_sent_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(delegates_notebook), delegates_scroll, gtk_label_new("Your Delegates"));
    gtk_notebook_append_page(GTK_NOTEBOOK(delegates_notebook), delegations_scroll, gtk_label_new("You Delegate"));
    gtk_notebook_append_page(GTK_NOTEBOOK(delegates_notebook), invitations_scroll, gtk_label_new("Invites"));
    gtk_notebook_append_page(GTK_NOTEBOOK(delegates_notebook), sent_scroll, gtk_label_new("Sent"));
    gtk_box_pack_start(GTK_BOX(delegates_box), delegate_invite_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(delegates_box), delegate_actions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(delegates_box), delegates_notebook, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(delegates_box), refresh_delegates_btn, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(delegates_frame), delegates_box);
    gtk_box_pack_start(GTK_BOX(content_box), delegates_frame, FALSE, FALSE, 0);

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

    GtkWidget *username_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *change_username_btn = gtk_button_new_with_label("Change Username");
    g_settings_new_username_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_settings_new_username_entry), "New username");
    g_signal_connect(g_settings_new_username_entry, "activate", G_CALLBACK(on_change_username_clicked), NULL);
    g_signal_connect(change_username_btn, "clicked", G_CALLBACK(on_change_username_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(username_row), g_settings_new_username_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(username_row), change_username_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(account_box), username_row, FALSE, FALSE, 0);

    GtkWidget *private_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *private_label = gtk_label_new("Private account");
    g_settings_private_switch = gtk_switch_new();
    gtk_box_pack_start(GTK_BOX(private_row), private_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(private_row), g_settings_private_switch, FALSE, FALSE, 0);
    g_signal_connect(g_settings_private_switch, "state-set", G_CALLBACK(on_account_private_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), private_row, FALSE, FALSE, 0);

    GtkWidget *transparency_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *transparency_label = gtk_label_new("Show continent in transparency info");
    g_settings_transparency_switch = gtk_switch_new();
    gtk_box_pack_start(GTK_BOX(transparency_row), transparency_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(transparency_row), g_settings_transparency_switch, FALSE, FALSE, 0);
    g_signal_connect(g_settings_transparency_switch, "state-set", G_CALLBACK(on_transparency_location_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), transparency_row, FALSE, FALSE, 0);

    GtkWidget *community_tag_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *set_community_tag_btn = gtk_button_new_with_label("Set Tag");
    GtkWidget *clear_community_tag_btn = gtk_button_new_with_label("Clear");
    g_settings_community_tag_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_settings_community_tag_entry), "Community ID for profile tag");
    g_signal_connect(g_settings_community_tag_entry, "activate", G_CALLBACK(on_update_community_tag_clicked), NULL);
    g_signal_connect(set_community_tag_btn, "clicked", G_CALLBACK(on_update_community_tag_clicked), NULL);
    g_signal_connect(clear_community_tag_btn, "clicked", G_CALLBACK(on_clear_community_tag_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(community_tag_row), g_settings_community_tag_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(community_tag_row), set_community_tag_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(community_tag_row), clear_community_tag_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(account_box), community_tag_row, FALSE, FALSE, 0);

    GtkWidget *outline_grid = gtk_grid_new();
    GtkWidget *outline_btn = gtk_button_new_with_label("Update Outlines");
    gtk_grid_set_row_spacing(GTK_GRID(outline_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(outline_grid), 6);
    g_settings_checkmark_outline_entry = gtk_entry_new();
    g_settings_avatar_outline_entry = gtk_entry_new();
    GtkWidget *checkmark_outline_row = create_color_entry_row(g_settings_checkmark_outline_entry, NULL);
    GtkWidget *avatar_outline_row = create_color_entry_row(g_settings_avatar_outline_entry, NULL);
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_settings_checkmark_outline_entry), "Checkmark outline color");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_settings_avatar_outline_entry), "Avatar outline color");
    gtk_grid_attach(GTK_GRID(outline_grid), gtk_label_new("Checkmark:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(outline_grid), checkmark_outline_row, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(outline_grid), gtk_label_new("Avatar:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(outline_grid), avatar_outline_row, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(outline_grid), outline_btn, 1, 2, 1, 1);
    g_signal_connect(outline_btn, "clicked", G_CALLBACK(on_update_outlines_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), outline_grid, FALSE, FALSE, 0);

    g_change_password_button = gtk_button_new_with_label("Change Password");
    g_signal_connect(g_change_password_button, "clicked", G_CALLBACK(on_change_password_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), g_change_password_button, FALSE, FALSE, 0);

    GtkWidget *passkeys_btn = gtk_button_new_with_label("Manage Passkeys");
    g_signal_connect(passkeys_btn, "clicked", G_CALLBACK(on_manage_passkeys_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), passkeys_btn, FALSE, FALSE, 0);

    GtkWidget *delete_account_btn = gtk_button_new_with_label("Delete Account");
    g_signal_connect(delete_account_btn, "clicked", G_CALLBACK(on_delete_account_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), delete_account_btn, FALSE, FALSE, 0);

    GtkWidget *bulk_delete_posts_btn = gtk_button_new_with_label("Bulk Delete Posts");
    g_signal_connect(bulk_delete_posts_btn, "clicked", G_CALLBACK(on_bulk_delete_posts_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), bulk_delete_posts_btn, FALSE, FALSE, 0);

    GtkWidget *moderation_history_btn = gtk_button_new_with_label("Moderation History");
    GtkWidget *blocking_causes_btn = gtk_button_new_with_label("Block Causes");
    g_signal_connect(moderation_history_btn, "clicked", G_CALLBACK(on_moderation_history_clicked), NULL);
    g_signal_connect(blocking_causes_btn, "clicked", G_CALLBACK(on_blocking_causes_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), moderation_history_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(account_box), blocking_causes_btn, FALSE, FALSE, 0);

    GtkWidget *requests_notebook = gtk_notebook_new();
    GtkWidget *follow_requests_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *affiliate_requests_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *requests_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *requests_refresh_btn = gtk_button_new_with_label("Refresh Requests");
    GtkWidget *remove_affiliate_btn = gtk_button_new_with_label("Remove Affiliate");
    g_follow_requests_list = gtk_list_box_new();
    g_affiliate_requests_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_follow_requests_list), GTK_SELECTION_NONE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_affiliate_requests_list), GTK_SELECTION_NONE);
    gtk_widget_set_size_request(requests_notebook, -1, 220);
    gtk_container_add(GTK_CONTAINER(follow_requests_scroll), g_follow_requests_list);
    gtk_container_add(GTK_CONTAINER(affiliate_requests_scroll), g_affiliate_requests_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(requests_notebook), follow_requests_scroll, gtk_label_new("Follow Requests"));
    gtk_notebook_append_page(GTK_NOTEBOOK(requests_notebook), affiliate_requests_scroll, gtk_label_new("Affiliate Requests"));
    g_signal_connect(requests_refresh_btn, "clicked", G_CALLBACK(on_filters_refresh_clicked), NULL);
    g_signal_connect(remove_affiliate_btn, "clicked", G_CALLBACK(on_remove_affiliate_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(requests_row), requests_refresh_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(requests_row), remove_affiliate_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(account_box), requests_notebook, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(account_box), requests_row, FALSE, FALSE, 0);

    g_settings_auth_button = gtk_button_new_with_label("Login");
    gtk_widget_set_name(g_settings_auth_button, "auth_button");
    g_signal_connect(g_settings_auth_button, "clicked", G_CALLBACK(on_login_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(account_box), g_settings_auth_button, FALSE, FALSE, 0);

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

static void
on_admin_logs_search_activated(GtkEntry *entry, gpointer user_data)
{
    (void)user_data;
    start_loading_admin_logs(gtk_entry_get_text(entry));
}

static void
on_admin_dms_search_activated(GtkEntry *entry, gpointer user_data)
{
    (void)user_data;
    start_loading_admin_dms(gtk_entry_get_text(entry));
}

static void
on_admin_shop_search_activated(GtkEntry *entry, gpointer user_data)
{
    (void)user_data;
    start_loading_admin_shop(gtk_entry_get_text(entry));
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

static void
on_lists_refresh_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    start_loading_lists();
}

static void
on_filters_refresh_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    start_loading_muted_words();
    start_loading_muted_conversations();
    start_loading_for_you_interests();
    start_loading_scheduled_posts();
    start_loading_my_shop();
    start_loading_delegates();
    start_loading_account_requests();
}

static void
on_explore_refresh_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    start_loading_explore();
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

static void
on_articles_refresh_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    start_loading_articles();
}

static void
on_communities_search_activated(GtkWidget *widget, gpointer user_data)
{
    const gchar *query;

    (void)user_data;
    query = gtk_entry_get_text(GTK_ENTRY(widget));
    start_loading_communities_search(GTK_LIST_BOX(g_communities_list), query);
}

static void
on_communities_all_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    if (g_communities_search_entry) {
        gtk_entry_set_text(GTK_ENTRY(g_communities_search_entry), "");
    }
    start_loading_communities(GTK_LIST_BOX(g_communities_list));
}

static void
on_communities_trending_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    start_loading_communities_trending(GTK_LIST_BOX(g_communities_list));
}

static void
on_communities_recommended_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    start_loading_communities_recommended(GTK_LIST_BOX(g_communities_list));
}

static void
on_communities_mine_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    start_loading_my_communities(GTK_LIST_BOX(g_communities_list));
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

static GtkWidget *
create_articles_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *action_bar = gtk_action_bar_new();
    GtkWidget *new_button = gtk_button_new_with_label("New Article");
    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);

    g_articles_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_articles_list), GTK_SELECTION_NONE);

    g_signal_connect(new_button, "clicked", G_CALLBACK(on_compose_article_clicked), NULL);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_articles_refresh_clicked), NULL);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), new_button);
    gtk_action_bar_pack_end(GTK_ACTION_BAR(action_bar), refresh_button);

    gtk_container_add(GTK_CONTAINER(scroll), g_articles_list);
    gtk_box_pack_start(GTK_BOX(box), action_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    return box;
}

GtkWidget*
create_lists_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *action_bar = gtk_action_bar_new();
    GtkWidget *create_button = gtk_button_new_with_label("Create List");
    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");
    GtkWidget *notebook = gtk_notebook_new();
    GtkWidget *owned_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *followed_scroll = gtk_scrolled_window_new(NULL, NULL);

    g_signal_connect(create_button, "clicked", G_CALLBACK(on_create_list_clicked), NULL);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_lists_refresh_clicked), NULL);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), create_button);
    gtk_action_bar_pack_end(GTK_ACTION_BAR(action_bar), refresh_button);
    gtk_box_pack_start(GTK_BOX(box), action_bar, FALSE, FALSE, 0);

    g_lists_owned_list = gtk_list_box_new();
    g_lists_followed_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_lists_owned_list), GTK_SELECTION_NONE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_lists_followed_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(owned_scroll), g_lists_owned_list);
    gtk_container_add(GTK_CONTAINER(followed_scroll), g_lists_followed_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), owned_scroll, gtk_label_new("Owned"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), followed_scroll, gtk_label_new("Followed"));
    gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE, 0);

    return box;
}

GtkWidget*
create_list_details_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *notebook = gtk_notebook_new();
    GtkWidget *tweets_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *members_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *followers_scroll = gtk_scrolled_window_new(NULL, NULL);

    gtk_container_set_border_width(GTK_CONTAINER(header), 10);
    g_list_title_label = gtk_label_new("List");
    g_list_details_label = gtk_label_new("");
    gtk_widget_set_halign(g_list_title_label, GTK_ALIGN_START);
    gtk_widget_set_halign(g_list_details_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(g_list_details_label), TRUE);
    gtk_widget_set_opacity(g_list_details_label, 0.75);
    gtk_box_pack_start(GTK_BOX(header_text), g_list_title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_text), g_list_details_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), header_text, TRUE, TRUE, 0);

    g_list_follow_button = gtk_button_new_with_label("Follow");
    g_list_edit_button = gtk_button_new_with_label("Edit");
    g_list_delete_button = gtk_button_new_with_label("Delete");
    g_list_add_member_button = gtk_button_new_with_label("Add Member");
    g_signal_connect(g_list_follow_button, "clicked", G_CALLBACK(on_list_follow_clicked), GINT_TO_POINTER(3));
    g_signal_connect(g_list_edit_button, "clicked", G_CALLBACK(on_list_edit_clicked), NULL);
    g_signal_connect(g_list_delete_button, "clicked", G_CALLBACK(on_list_delete_clicked), NULL);
    g_signal_connect(g_list_add_member_button, "clicked", G_CALLBACK(on_list_add_member_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(actions), g_list_follow_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), g_list_edit_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), g_list_delete_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), g_list_add_member_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), actions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    g_list_tweets_list = gtk_list_box_new();
    g_list_members_list = gtk_list_box_new();
    g_list_followers_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_list_tweets_list), GTK_SELECTION_NONE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_list_members_list), GTK_SELECTION_NONE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_list_followers_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(tweets_scroll), g_list_tweets_list);
    gtk_container_add(GTK_CONTAINER(members_scroll), g_list_members_list);
    gtk_container_add(GTK_CONTAINER(followers_scroll), g_list_followers_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tweets_scroll, gtk_label_new("Tweets"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), members_scroll, gtk_label_new("Members"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), followers_scroll, gtk_label_new("Followers"));
    gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE, 0);

    return box;
}

GtkWidget*
create_explore_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *action_bar = gtk_action_bar_new();
    GtkWidget *refresh = gtk_button_new_with_label("Refresh");
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);

    g_explore_category_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Trends");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Best of Week");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Most Bookmarked");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Most Discussed");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Longest Threads");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "With Media");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "With Polls");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Trending Users");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Suggested Users");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "User Directory");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Top Hashtags");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Digest");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Leaderboard");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_explore_category_combo), "Stats");
    gtk_combo_box_set_active(GTK_COMBO_BOX(g_explore_category_combo), 0);
    g_signal_connect(g_explore_category_combo, "changed", G_CALLBACK(on_explore_category_changed), NULL);
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_explore_refresh_clicked), NULL);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), g_explore_category_combo);
    gtk_action_bar_pack_end(GTK_ACTION_BAR(action_bar), refresh);
    gtk_box_pack_start(GTK_BOX(box), action_bar, FALSE, FALSE, 0);

    g_explore_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_explore_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), g_explore_list);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    return box;
}

static gint
community_access_index(const gchar *access_mode)
{
    if (g_strcmp0(access_mode, "locked") == 0) {
        return 1;
    }
    return 0;
}

static const gchar *
community_access_value(GtkComboBox *combo)
{
    return gtk_combo_box_get_active(combo) == 1 ? "locked" : "open";
}

static void
on_create_community_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *grid;
    GtkWidget *name_entry;
    GtkWidget *description_entry;
    GtkWidget *rules_entry;
    GtkWidget *access_combo;
    GtkWidget *toplevel;

    (void)user_data;
    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Create Community",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
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

    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        if (perform_create_community(gtk_entry_get_text(GTK_ENTRY(name_entry)),
                                     gtk_entry_get_text(GTK_ENTRY(description_entry)),
                                     gtk_entry_get_text(GTK_ENTRY(rules_entry)),
                                     community_access_value(GTK_COMBO_BOX(access_combo)))) {
            start_loading_communities(GTK_LIST_BOX(g_communities_list));
        }
    }

    gtk_widget_destroy(dialog);
}

static void
on_edit_community_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *grid;
    GtkWidget *name_entry;
    GtkWidget *description_entry;
    GtkWidget *rules_entry;
    GtkWidget *access_combo;
    GtkWidget *toplevel;
    const gchar *community_id;
    const gchar *name;
    const gchar *description;
    const gchar *rules;
    const gchar *access_mode;

    (void)user_data;
    if (!g_community_tweets_list) {
        return;
    }

    community_id = g_object_get_data(G_OBJECT(g_community_tweets_list), "community_id");
    name = g_object_get_data(G_OBJECT(g_community_tweets_list), "community_name");
    description = g_object_get_data(G_OBJECT(g_community_tweets_list), "community_description");
    rules = g_object_get_data(G_OBJECT(g_community_tweets_list), "community_rules");
    access_mode = g_object_get_data(G_OBJECT(g_community_tweets_list), "community_access_mode");
    if (!community_id) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Edit Community",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    name_entry = gtk_entry_new();
    description_entry = gtk_entry_new();
    rules_entry = gtk_entry_new();
    access_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(access_combo), "Open");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(access_combo), "Locked");
    gtk_combo_box_set_active(GTK_COMBO_BOX(access_combo), community_access_index(access_mode));

    gtk_entry_set_text(GTK_ENTRY(name_entry), name ? name : "");
    gtk_entry_set_text(GTK_ENTRY(description_entry), description ? description : "");
    gtk_entry_set_text(GTK_ENTRY(rules_entry), rules ? rules : "");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Description:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), description_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Rules:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), rules_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Access:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), access_combo, 1, 3, 1, 1);

    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        if (perform_update_community(community_id,
                                     gtk_entry_get_text(GTK_ENTRY(name_entry)),
                                     gtk_entry_get_text(GTK_ENTRY(description_entry)),
                                     gtk_entry_get_text(GTK_ENTRY(rules_entry)),
                                     community_access_value(GTK_COMBO_BOX(access_combo)))) {
            start_loading_community_tweets(GTK_LIST_BOX(g_community_tweets_list), community_id);
            start_loading_communities(GTK_LIST_BOX(g_communities_list));
        }
    }

    gtk_widget_destroy(dialog);
}

static void
on_delete_community_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *toplevel;
    const gchar *community_id;

    (void)user_data;
    community_id = g_community_tweets_list
        ? g_object_get_data(G_OBJECT(g_community_tweets_list), "community_id") : NULL;
    if (!community_id) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_OK_CANCEL,
                                    "Delete this community?");
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        if (perform_delete_community(community_id)) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_stack), "communities");
            start_loading_communities(GTK_LIST_BOX(g_communities_list));
        }
    }
    gtk_widget_destroy(dialog);
}

static void
on_community_members_clicked(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *scroll;
    GtkWidget *list;
    GtkWidget *toplevel;
    const gchar *community_id;

    (void)user_data;
    community_id = g_community_tweets_list
        ? g_object_get_data(G_OBJECT(g_community_tweets_list), "community_id") : NULL;
    if (!community_id) {
        return;
    }

    toplevel = gtk_widget_get_toplevel(widget);
    dialog = gtk_dialog_new_with_buttons("Community Members",
                                         GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close", GTK_RESPONSE_CLOSE,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, 420, 420);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(content_area), scroll, TRUE, TRUE, 0);

    start_loading_community_members(community_id, GTK_LIST_BOX(list));
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
on_community_access_clicked(GtkWidget *widget, gpointer user_data)
{
    const gchar *community_id;
    const gchar *access_mode;
    const gchar *new_mode;

    (void)widget;
    (void)user_data;
    if (!g_community_tweets_list) {
        return;
    }

    community_id = g_object_get_data(G_OBJECT(g_community_tweets_list), "community_id");
    access_mode = g_object_get_data(G_OBJECT(g_community_tweets_list), "community_access_mode");
    if (!community_id) {
        return;
    }

    new_mode = g_strcmp0(access_mode, "locked") == 0 ? "open" : "locked";
    if (perform_update_community_access_mode(community_id, new_mode)) {
        start_loading_community_tweets(GTK_LIST_BOX(g_community_tweets_list), community_id);
        start_loading_communities(GTK_LIST_BOX(g_communities_list));
    }
}

GtkWidget*
create_communities_view(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *action_bar = gtk_action_bar_new();
    GtkWidget *create_button = gtk_button_new_with_label("Create");
    GtkWidget *join_invite_button = gtk_button_new_with_label("Join Invite");
    GtkWidget *all_button = gtk_button_new_with_label("All");
    GtkWidget *mine_button = gtk_button_new_with_label("Mine");
    GtkWidget *trending_button = gtk_button_new_with_label("Trending");
    GtkWidget *recommended_button = gtk_button_new_with_label("Recommended");
    g_communities_search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_communities_search_entry), "Search communities");
    g_signal_connect(g_communities_search_entry, "activate", G_CALLBACK(on_communities_search_activated), NULL);
    g_signal_connect(all_button, "clicked", G_CALLBACK(on_communities_all_clicked), NULL);
    g_signal_connect(mine_button, "clicked", G_CALLBACK(on_communities_mine_clicked), NULL);
    g_signal_connect(trending_button, "clicked", G_CALLBACK(on_communities_trending_clicked), NULL);
    g_signal_connect(recommended_button, "clicked", G_CALLBACK(on_communities_recommended_clicked), NULL);
    g_signal_connect(create_button, "clicked", G_CALLBACK(on_create_community_clicked), NULL);
    g_signal_connect(join_invite_button, "clicked", G_CALLBACK(on_community_accept_invite_clicked), NULL);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), all_button);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), mine_button);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), trending_button);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), recommended_button);
    gtk_action_bar_pack_start(GTK_ACTION_BAR(action_bar), g_communities_search_entry);
    gtk_action_bar_pack_end(GTK_ACTION_BAR(action_bar), join_invite_button);
    gtk_action_bar_pack_end(GTK_ACTION_BAR(action_bar), create_button);
    gtk_box_pack_start(GTK_BOX(box), action_bar, FALSE, FALSE, 0);

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
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *header_text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *actions_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    gtk_container_set_border_width(GTK_CONTAINER(header_box), 10);
    g_community_title_label = gtk_label_new("Community");
    g_community_details_label = gtk_label_new("");
    gtk_widget_set_halign(g_community_title_label, GTK_ALIGN_START);
    gtk_widget_set_halign(g_community_details_label, GTK_ALIGN_START);
    gtk_widget_set_opacity(g_community_details_label, 0.75);
    gtk_label_set_line_wrap(GTK_LABEL(g_community_details_label), TRUE);
    gtk_box_pack_start(GTK_BOX(header_text_box), g_community_title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_text_box), g_community_details_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_box), header_text_box, TRUE, TRUE, 0);

    GtkWidget *members_btn = gtk_button_new_with_label("Members");
    GtkWidget *invite_btn = gtk_button_new_with_label("Invite");
    GtkWidget *manage_invites_btn = gtk_button_new_with_label("Invites");
    GtkWidget *moderation_btn = gtk_button_new_with_label("Moderation");
    GtkWidget *style_btn = gtk_button_new_with_label("Style");
    GtkWidget *pin_post_btn = gtk_button_new_with_label("Pin Post");
    GtkWidget *edit_btn = gtk_button_new_with_label("Edit");
    GtkWidget *access_btn = gtk_button_new_with_label("Access");
    GtkWidget *delete_btn = gtk_button_new_with_label("Delete");
    g_signal_connect(members_btn, "clicked", G_CALLBACK(on_community_members_clicked), NULL);
    g_signal_connect(invite_btn, "clicked", G_CALLBACK(on_community_create_invite_clicked), NULL);
    g_signal_connect(manage_invites_btn, "clicked", G_CALLBACK(on_community_manage_invites_clicked), NULL);
    g_signal_connect(moderation_btn, "clicked", G_CALLBACK(on_community_moderation_clicked), NULL);
    g_signal_connect(style_btn, "clicked", G_CALLBACK(on_community_style_clicked), NULL);
    g_signal_connect(pin_post_btn, "clicked", G_CALLBACK(on_community_pin_post_clicked), NULL);
    g_signal_connect(edit_btn, "clicked", G_CALLBACK(on_edit_community_clicked), NULL);
    g_signal_connect(access_btn, "clicked", G_CALLBACK(on_community_access_clicked), NULL);
    g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_delete_community_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(actions_box), members_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_box), invite_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_box), manage_invites_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_box), moderation_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_box), style_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_box), pin_post_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_box), edit_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_box), access_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions_box), delete_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header_box), actions_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), header_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

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

    /* Stats Tab */
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

    /* Users Tab */
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

    /* Posts Tab */
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

    /* Suspensions Tab */
    GtkWidget *suspensions_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *suspensions_refresh = gtk_button_new_with_label("Refresh Suspensions");
    GtkWidget *suspensions_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_suspensions_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_suspensions_list), GTK_SELECTION_NONE);
    g_signal_connect(suspensions_refresh, "clicked", G_CALLBACK(start_loading_admin_suspensions), NULL);
    gtk_box_pack_start(GTK_BOX(suspensions_vbox), suspensions_refresh, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(suspensions_scroll), g_admin_suspensions_list);
    gtk_box_pack_start(GTK_BOX(suspensions_vbox), suspensions_scroll, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), suspensions_vbox, gtk_label_new("Suspensions"));

    /* Reports Tab */
    GtkWidget *reports_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *reports_refresh = gtk_button_new_with_label("Refresh Reports");
    GtkWidget *reports_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_reports_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_reports_list), GTK_SELECTION_NONE);
    g_signal_connect(reports_refresh, "clicked", G_CALLBACK(start_loading_admin_reports), NULL);
    gtk_box_pack_start(GTK_BOX(reports_vbox), reports_refresh, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(reports_scroll), g_admin_reports_list);
    gtk_box_pack_start(GTK_BOX(reports_vbox), reports_scroll, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), reports_vbox, gtk_label_new("Reports"));

    /* Logs Tab */
    GtkWidget *logs_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_admin_logs_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_logs_search), "Search moderation logs...");
    g_signal_connect(g_admin_logs_search, "activate", G_CALLBACK(on_admin_logs_search_activated), NULL);
    GtkWidget *logs_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_logs_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_logs_list), GTK_SELECTION_NONE);
    gtk_box_pack_start(GTK_BOX(logs_vbox), g_admin_logs_search, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(logs_scroll), g_admin_logs_list);
    gtk_box_pack_start(GTK_BOX(logs_vbox), logs_scroll, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), logs_vbox, gtk_label_new("Logs"));

    /* DMs Tab */
    GtkWidget *dms_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *dms_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *dms_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *dms_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *dms_left_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *dms_right_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_dms_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_dms_search), "Search DM conversations by username...");
    g_signal_connect(g_admin_dms_search, "activate", G_CALLBACK(on_admin_dms_search_activated), NULL);
    g_admin_dms_list = gtk_list_box_new();
    g_admin_dm_admin_messages_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_dms_list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_dm_admin_messages_list), GTK_SELECTION_NONE);
    g_signal_connect(g_admin_dms_list, "row-selected", G_CALLBACK(on_admin_dm_conversation_selected), NULL);
    gtk_box_pack_start(GTK_BOX(dms_vbox), g_admin_dms_search, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(dms_left_scroll), g_admin_dms_list);
    gtk_container_add(GTK_CONTAINER(dms_right_scroll), g_admin_dm_admin_messages_list);
    gtk_box_pack_start(GTK_BOX(dms_left), dms_left_scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(dms_right), dms_right_scroll, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(dms_paned), dms_left, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(dms_paned), dms_right, TRUE, FALSE);
    gtk_box_pack_start(GTK_BOX(dms_vbox), dms_paned, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), dms_vbox, gtk_label_new("DMs"));

    /* Blocks Tab */
    GtkWidget *blocks_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *blocks_refresh = gtk_button_new_with_label("Refresh Blocks");
    GtkWidget *blocks_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_blocks_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_blocks_list), GTK_SELECTION_NONE);
    g_signal_connect(blocks_refresh, "clicked", G_CALLBACK(start_loading_admin_blocks), NULL);
    gtk_box_pack_start(GTK_BOX(blocks_vbox), blocks_refresh, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(blocks_scroll), g_admin_blocks_list);
    gtk_box_pack_start(GTK_BOX(blocks_vbox), blocks_scroll, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), blocks_vbox, gtk_label_new("Blocks"));

    /* Emojis Tab */
    GtkWidget *emojis_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *emojis_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *emojis_refresh = gtk_button_new_with_label("Refresh Emojis");
    GtkWidget *emojis_upload = gtk_button_new_with_label("Upload Emoji");
    GtkWidget *emojis_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_emojis_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_emojis_list), GTK_SELECTION_NONE);
    g_signal_connect(emojis_refresh, "clicked", G_CALLBACK(start_loading_admin_emojis), NULL);
    g_signal_connect(emojis_upload, "clicked", G_CALLBACK(on_admin_upload_emoji_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(emojis_actions), emojis_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(emojis_actions), emojis_upload, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(emojis_vbox), emojis_actions, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(emojis_scroll), g_admin_emojis_list);
    gtk_box_pack_start(GTK_BOX(emojis_vbox), emojis_scroll, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), emojis_vbox, gtk_label_new("Emojis"));

    /* Badges Tab */
    GtkWidget *badges_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *badges_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *badges_refresh = gtk_button_new_with_label("Refresh Badges");
    GtkWidget *badges_create = gtk_button_new_with_label("Create Badge");
    GtkWidget *badges_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_badges_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_badges_list), GTK_SELECTION_NONE);
    g_signal_connect(badges_refresh, "clicked", G_CALLBACK(start_loading_admin_badges), NULL);
    g_signal_connect(badges_create, "clicked", G_CALLBACK(on_admin_create_badge_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(badges_actions), badges_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(badges_actions), badges_create, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(badges_vbox), badges_actions, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(badges_scroll), g_admin_badges_list);
    gtk_box_pack_start(GTK_BOX(badges_vbox), badges_scroll, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), badges_vbox, gtk_label_new("Badges"));

    /* Shop Tab */
    GtkWidget *shop_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *shop_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    GtkWidget *shop_products_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *shop_purchases_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *shop_products_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *shop_purchases_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_shop_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_shop_search), "Search shop products...");
    g_signal_connect(g_admin_shop_search, "activate", G_CALLBACK(on_admin_shop_search_activated), NULL);
    g_admin_shop_products_list = gtk_list_box_new();
    g_admin_shop_purchases_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_shop_products_list), GTK_SELECTION_NONE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_shop_purchases_list), GTK_SELECTION_NONE);
    gtk_box_pack_start(GTK_BOX(shop_vbox), g_admin_shop_search, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(shop_products_scroll), g_admin_shop_products_list);
    gtk_container_add(GTK_CONTAINER(shop_purchases_scroll), g_admin_shop_purchases_list);
    gtk_box_pack_start(GTK_BOX(shop_products_box), gtk_label_new("Products"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(shop_products_box), shop_products_scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(shop_purchases_box), gtk_label_new("Purchases"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(shop_purchases_box), shop_purchases_scroll, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(shop_paned), shop_products_box, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(shop_paned), shop_purchases_box, TRUE, FALSE);
    gtk_box_pack_start(GTK_BOX(shop_vbox), shop_paned, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), shop_vbox, gtk_label_new("Shop"));

    /* Communities Tab */
    GtkWidget *communities_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *communities_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *communities_refresh = gtk_button_new_with_label("Refresh");
    GtkWidget *communities_create = gtk_button_new_with_label("Create Community");
    GtkWidget *communities_scroll = gtk_scrolled_window_new(NULL, NULL);
    g_admin_communities_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_admin_communities_list), GTK_SELECTION_NONE);
    g_signal_connect(communities_refresh, "clicked", G_CALLBACK(start_loading_admin_communities), NULL);
    g_signal_connect(communities_create, "clicked", G_CALLBACK(on_admin_create_community_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(communities_actions), communities_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(communities_actions), communities_create, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(communities_vbox), communities_actions, FALSE, FALSE, 5);
    gtk_container_add(GTK_CONTAINER(communities_scroll), g_admin_communities_list);
    gtk_box_pack_start(GTK_BOX(communities_vbox), communities_scroll, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), communities_vbox, gtk_label_new("Communities"));

    /* Notifications Tab */
    GtkWidget *notifications_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *notifications_grid = gtk_grid_new();
    GtkWidget *notifications_message_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *notifications_send = gtk_button_new_with_label("Send Notification");
    gtk_container_set_border_width(GTK_CONTAINER(notifications_vbox), 12);
    gtk_grid_set_row_spacing(GTK_GRID(notifications_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(notifications_grid), 6);
    g_admin_notifications_target_entry = gtk_entry_new();
    g_admin_notifications_type_entry = gtk_entry_new();
    g_admin_notifications_title_entry = gtk_entry_new();
    g_admin_notifications_subtitle_entry = gtk_entry_new();
    g_admin_notifications_url_entry = gtk_entry_new();
    g_admin_notifications_message_view = gtk_text_view_new();
    g_admin_notifications_result_label = gtk_label_new("");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_notifications_target_entry), "username, username2, or all");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_notifications_type_entry), "default");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_notifications_title_entry), "Title");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_notifications_subtitle_entry), "Subtitle");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_notifications_url_entry), "https://...");
    gtk_widget_set_size_request(notifications_message_scroll, -1, 180);
    gtk_container_add(GTK_CONTAINER(notifications_message_scroll), g_admin_notifications_message_view);
    gtk_label_set_xalign(GTK_LABEL(g_admin_notifications_result_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(g_admin_notifications_result_label), TRUE);
    gtk_grid_attach(GTK_GRID(notifications_grid), gtk_label_new("Targets"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), g_admin_notifications_target_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), gtk_label_new("Type"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), g_admin_notifications_type_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), gtk_label_new("Title"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), g_admin_notifications_title_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), gtk_label_new("Subtitle"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), g_admin_notifications_subtitle_entry, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), gtk_label_new("URL"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), g_admin_notifications_url_entry, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), gtk_label_new("Message"), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(notifications_grid), notifications_message_scroll, 1, 5, 1, 1);
    g_signal_connect(notifications_send, "clicked", G_CALLBACK(on_admin_send_notification_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(notifications_vbox), notifications_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(notifications_vbox), notifications_send, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(notifications_vbox), g_admin_notifications_result_label, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), notifications_vbox, gtk_label_new("Notifications"));

    /* Clone Tab */
    GtkWidget *clone_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *clone_grid = gtk_grid_new();
    GtkWidget *clone_checks_a = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *clone_checks_b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *clone_checks_c = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *clone_submit = gtk_button_new_with_label("Clone User");
    gtk_container_set_border_width(GTK_CONTAINER(clone_vbox), 12);
    gtk_grid_set_row_spacing(GTK_GRID(clone_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(clone_grid), 6);
    g_admin_clone_source_entry = gtk_entry_new();
    g_admin_clone_username_entry = gtk_entry_new();
    g_admin_clone_name_entry = gtk_entry_new();
    g_admin_clone_relations_check = gtk_check_button_new_with_label("Relations");
    g_admin_clone_ghosts_check = gtk_check_button_new_with_label("Ghosts");
    g_admin_clone_tweets_check = gtk_check_button_new_with_label("Tweets");
    g_admin_clone_replies_check = gtk_check_button_new_with_label("Replies");
    g_admin_clone_retweets_check = gtk_check_button_new_with_label("Retweets");
    g_admin_clone_reactions_check = gtk_check_button_new_with_label("Reactions");
    g_admin_clone_communities_check = gtk_check_button_new_with_label("Communities");
    g_admin_clone_media_check = gtk_check_button_new_with_label("Media");
    g_admin_clone_affiliate_check = gtk_check_button_new_with_label("Affiliate");
    g_admin_clone_result_label = gtk_label_new("");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_admin_clone_relations_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_admin_clone_ghosts_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_admin_clone_tweets_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_admin_clone_replies_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_admin_clone_retweets_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_admin_clone_reactions_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_admin_clone_communities_check), TRUE);
    gtk_label_set_xalign(GTK_LABEL(g_admin_clone_result_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(g_admin_clone_result_label), TRUE);
    gtk_grid_attach(GTK_GRID(clone_grid), gtk_label_new("Source"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(clone_grid), g_admin_clone_source_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(clone_grid), gtk_label_new("New Username"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(clone_grid), g_admin_clone_username_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(clone_grid), gtk_label_new("Display Name"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(clone_grid), g_admin_clone_name_entry, 1, 2, 1, 1);
    gtk_box_pack_start(GTK_BOX(clone_checks_a), g_admin_clone_relations_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_checks_a), g_admin_clone_ghosts_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_checks_a), g_admin_clone_tweets_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_checks_b), g_admin_clone_replies_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_checks_b), g_admin_clone_retweets_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_checks_b), g_admin_clone_reactions_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_checks_c), g_admin_clone_communities_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_checks_c), g_admin_clone_media_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_checks_c), g_admin_clone_affiliate_check, FALSE, FALSE, 0);
    g_signal_connect(clone_submit, "clicked", G_CALLBACK(on_admin_clone_user_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(clone_vbox), clone_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_vbox), clone_checks_a, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_vbox), clone_checks_b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_vbox), clone_checks_c, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_vbox), clone_submit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clone_vbox), g_admin_clone_result_label, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), clone_vbox, gtk_label_new("Clone"));

    /* Tools Tab */
    GtkWidget *tools_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *tools_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *impersonation_grid = gtk_grid_new();
    GtkWidget *impersonation_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *impersonate_button = gtk_button_new_with_label("Impersonate");
    GtkWidget *restore_button = gtk_button_new_with_label("Restore Admin");
    GtkWidget *post_grid = gtk_grid_new();
    GtkWidget *post_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *post_button = gtk_button_new_with_label("Create Post");
    GtkWidget *bulk_grid = gtk_grid_new();
    GtkWidget *bulk_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *bulk_button = gtk_button_new_with_label("Apply Bulk Edit");
    gtk_container_set_border_width(GTK_CONTAINER(tools_vbox), 12);
    gtk_grid_set_row_spacing(GTK_GRID(impersonation_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(impersonation_grid), 6);
    gtk_grid_set_row_spacing(GTK_GRID(post_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(post_grid), 6);
    gtk_grid_set_row_spacing(GTK_GRID(bulk_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(bulk_grid), 6);
    g_admin_impersonation_entry = gtk_entry_new();
    g_admin_impersonation_status_label = gtk_label_new("Admin session active.");
    g_admin_tools_post_targets_entry = gtk_entry_new();
    g_admin_tools_post_reply_to_entry = gtk_entry_new();
    g_admin_tools_post_source_entry = gtk_entry_new();
    g_admin_tools_post_created_at_entry = gtk_entry_new();
    g_admin_tools_post_no_char_limit_check = gtk_check_button_new_with_label("No Character Limit");
    g_admin_tools_post_content_view = gtk_text_view_new();
    g_admin_tools_bulk_targets_entry = gtk_entry_new();
    g_admin_tools_bulk_payload_view = gtk_text_view_new();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_admin_tools_post_no_char_limit_check), TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_tools_post_targets_entry), "username, username2");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_tools_bulk_targets_entry), "username, username2");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_impersonation_entry), "username or user id");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_tools_post_reply_to_entry), "Optional post ID");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_tools_post_source_entry), "Tweeta Desktop");
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_admin_tools_post_created_at_entry), "2026-04-28T12:00:00.000Z");
    gtk_widget_set_size_request(post_scroll, -1, 180);
    gtk_widget_set_size_request(bulk_scroll, -1, 220);
    gtk_container_add(GTK_CONTAINER(post_scroll), g_admin_tools_post_content_view);
    gtk_container_add(GTK_CONTAINER(bulk_scroll), g_admin_tools_bulk_payload_view);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_admin_tools_bulk_payload_view)),
                             "{\n  \"verified\": true\n}",
                             -1);
    gtk_label_set_xalign(GTK_LABEL(g_admin_impersonation_status_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(g_admin_impersonation_status_label), TRUE);
    gtk_grid_attach(GTK_GRID(impersonation_grid), gtk_label_new("Impersonation"), 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(impersonation_grid), gtk_label_new("Target"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(impersonation_grid), g_admin_impersonation_entry, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(impersonation_actions), impersonate_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(impersonation_actions), restore_button, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(impersonation_grid), impersonation_actions, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(impersonation_grid), g_admin_impersonation_status_label, 0, 3, 2, 1);
    gtk_grid_attach(GTK_GRID(post_grid), gtk_label_new("Posting"), 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(post_grid), gtk_label_new("Targets"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), g_admin_tools_post_targets_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), gtk_label_new("Reply To"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), g_admin_tools_post_reply_to_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), gtk_label_new("Source"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), g_admin_tools_post_source_entry, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), gtk_label_new("Created At"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), g_admin_tools_post_created_at_entry, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), gtk_label_new("Content"), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), post_scroll, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), g_admin_tools_post_no_char_limit_check, 1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(post_grid), post_button, 1, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(bulk_grid), gtk_label_new("Bulk Edit"), 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(bulk_grid), gtk_label_new("Targets"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(bulk_grid), g_admin_tools_bulk_targets_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(bulk_grid), gtk_label_new("Payload"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(bulk_grid), bulk_scroll, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(bulk_grid), bulk_button, 1, 3, 1, 1);
    g_signal_connect(impersonate_button, "clicked", G_CALLBACK(on_admin_impersonate_clicked), NULL);
    g_signal_connect(restore_button, "clicked", G_CALLBACK(on_admin_restore_admin_clicked), NULL);
    g_signal_connect(post_button, "clicked", G_CALLBACK(on_admin_post_as_user_clicked), NULL);
    g_signal_connect(bulk_button, "clicked", G_CALLBACK(on_admin_bulk_edit_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(tools_vbox), impersonation_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tools_vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tools_vbox), post_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tools_vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tools_vbox), bulk_grid, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(tools_scroll), tools_vbox);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tools_scroll, gtk_label_new("Tools"));

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
    g_notifications_button = gtk_button_new_with_label("Alerts");
    g_signal_connect(g_notifications_button, "clicked", G_CALLBACK(on_notifications_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), g_notifications_button);

    // Messages Button (Left)
    GtkWidget *messages_button = gtk_button_new_from_icon_name("mail-unread-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(messages_button, "clicked", G_CALLBACK(on_messages_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), messages_button);

    // Bookmarks Button (Left)
    GtkWidget *bookmarks_button = gtk_button_new_from_icon_name("bookmark-new-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(bookmarks_button, "clicked", G_CALLBACK(on_bookmarks_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), bookmarks_button);

    // Lists Button (Left)
    GtkWidget *lists_button = gtk_button_new_from_icon_name("view-list-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(lists_button, "clicked", G_CALLBACK(on_lists_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), lists_button);

    // Explore Button (Left)
    GtkWidget *explore_button = gtk_button_new_from_icon_name("edit-find-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(explore_button, "clicked", G_CALLBACK(on_explore_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), explore_button);

    GtkWidget *articles_button = gtk_button_new_with_label("Articles");
    g_signal_connect(articles_button, "clicked", G_CALLBACK(on_articles_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), articles_button);

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

    g_header_auth_button = gtk_button_new_with_label("Login");
    g_signal_connect(g_header_auth_button, "clicked", G_CALLBACK(on_login_clicked), window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), g_header_auth_button);

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

    GtkWidget *lists_view = create_lists_view();
    gtk_stack_add_named(GTK_STACK(g_stack), lists_view, "lists");

    GtkWidget *list_details_view = create_list_details_view();
    gtk_stack_add_named(GTK_STACK(g_stack), list_details_view, "list_details");

    GtkWidget *explore_view = create_explore_view();
    gtk_stack_add_named(GTK_STACK(g_stack), explore_view, "explore");

    GtkWidget *articles_view = create_articles_view();
    gtk_stack_add_named(GTK_STACK(g_stack), articles_view, "articles");

    GtkWidget *communities_view = create_communities_view();
    gtk_stack_add_named(GTK_STACK(g_stack), communities_view, "communities");

    GtkWidget *community_tweets_view = create_community_tweets_view();
    gtk_stack_add_named(GTK_STACK(g_stack), community_tweets_view, "community_tweets");

    return window;
}
