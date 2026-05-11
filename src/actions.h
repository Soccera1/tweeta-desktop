#ifndef ACTIONS_H
#define ACTIONS_H

#include <gtk/gtk.h>
#include "types.h"

void start_loading_tweets(GtkListBox *list_box);
void start_loading_notifications(GtkListBox *list_box);
void start_loading_conversations(GtkListBox *list_box);
void start_loading_messages(GtkListBox *list_box, const gchar *conversation_id);
void on_dm_invite_clicked(GtkWidget *widget, gpointer user_data);
void on_dm_join_invite_clicked(GtkWidget *widget, gpointer user_data);
void on_dm_permissions_clicked(GtkWidget *widget, gpointer user_data);
void on_dm_roles_clicked(GtkWidget *widget, gpointer user_data);
void on_dm_pinned_clicked(GtkWidget *widget, gpointer user_data);
void on_dm_pin_message_clicked(GtkWidget *widget, gpointer user_data);
void start_loading_admin_stats(void);
void start_loading_admin_users(const gchar *search);
void start_loading_admin_posts(const gchar *search);
void start_loading_admin_suspensions(void);
void start_loading_admin_reports(void);
void start_loading_admin_logs(const gchar *search);
void start_loading_admin_blocks(void);
void start_loading_admin_emojis(void);
void start_loading_admin_badges(void);
void start_loading_admin_dms(const gchar *search);
void start_loading_admin_dm_messages(const gchar *conversation_id);
void start_loading_admin_shop(const gchar *search);
void start_loading_admin_communities(void);
void load_more_tweets(GtkListBox *list_box, const gchar *before_id);

void perform_admin_verify(const gchar *username, gboolean verify);
void perform_admin_suspend(const gchar *username, const gchar *reason);
void perform_admin_delete_user(const gchar *username);
void perform_admin_delete_post(const gchar *post_id);

void perform_search(const gchar *query);
void update_login_ui(void);
void perform_logout(void);
gboolean perform_login(const gchar *username, const gchar *password);
void show_profile(const gchar *username);
void show_tweet(const gchar *tweet_id);

void on_search_activated(GtkEntry *entry, gpointer user_data);
void on_back_clicked(GtkWidget *widget, gpointer user_data);
void on_notifications_clicked(GtkWidget *widget, gpointer user_data);
void on_messages_clicked(GtkWidget *widget, gpointer user_data);
void on_settings_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_dm_conversation_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
void on_admin_upload_emoji_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_create_badge_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_send_notification_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_clone_user_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_impersonate_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_restore_admin_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_post_as_user_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_bulk_edit_clicked(GtkWidget *widget, gpointer user_data);
void on_admin_create_community_clicked(GtkWidget *widget, gpointer user_data);
void on_mark_all_read_clicked(GtkWidget *widget, gpointer user_data);
void on_refresh_clicked(GtkWidget *widget, gpointer user_data);
void refresh_notification_badge(void);
gboolean mark_notification_read(const gchar *notification_id);
gboolean perform_post_tweet(const gchar *content, const gchar *reply_to_id, GList *attachments);
void on_compose_clicked(GtkWidget *widget, gpointer window);
void on_note_button_clicked(GtkWidget *widget, gpointer user_data);
void on_login_clicked(GtkWidget *widget, gpointer window);
void on_scroll_edge_reached(GtkScrolledWindow *scrolled_window, GtkPositionType pos, gpointer user_data);

gboolean perform_like(const gchar *tweet_id);
gboolean perform_retweet(const gchar *tweet_id);
gboolean perform_bookmark(const gchar *tweet_id, gboolean add);
gboolean perform_reaction(const gchar *tweet_id, const gchar *emoji);
gboolean perform_edit_tweet(const gchar *tweet_id, const gchar *new_content);
gboolean perform_delete_tweet(const gchar *tweet_id);
gchar* fetch_tweet_edit_history_text(const gchar *tweet_id);
gchar* fetch_tweet_reactions_text(const gchar *tweet_id);
GList* fetch_emojis(void);
void free_emojis(GList *emojis);
gboolean perform_report(const gchar *reported_type,
                        const gchar *reported_id,
                        const gchar *reason,
                        const gchar *additional_info,
                        gchar **error_out);
void on_report_tweet_clicked(GtkWidget *widget, gpointer user_data);
void on_report_profile_clicked(GtkWidget *widget, gpointer user_data);
void on_translate_tweet_clicked(GtkWidget *widget, gpointer user_data);
void on_request_affiliate_clicked(GtkWidget *widget, gpointer user_data);
void on_profile_shop_clicked(GtkWidget *widget, gpointer user_data);
void on_profile_donate_clicked(GtkWidget *widget, gpointer user_data);
void on_profile_algorithm_stats_clicked(GtkWidget *widget, gpointer user_data);
void on_profile_spam_score_clicked(GtkWidget *widget, gpointer user_data);
void on_profile_analytics_clicked(GtkWidget *widget, gpointer user_data);
void on_profile_common_followers_clicked(GtkWidget *widget, gpointer user_data);
void on_profile_top_posts_clicked(GtkWidget *widget, gpointer user_data);
void on_profile_communities_clicked(GtkWidget *widget, gpointer user_data);

gboolean perform_follow(const gchar *username, gboolean follow);
gboolean perform_profile_notify_tweets(const gchar *username, gboolean notify);
gboolean perform_delete_profile_avatar(const gchar *username);
gboolean perform_delete_profile_banner(const gchar *username);
gboolean perform_toggle_pin_tweet(const gchar *tweet_id, gboolean pin);
void start_loading_followers(const gchar *username);
void start_loading_following(const gchar *username);
void start_loading_profile_media(const gchar *username);
void start_loading_profile_highlights(const gchar *username);
void start_loading_profile_mutuals(const gchar *username);
void start_loading_profile_followers_you_know(const gchar *username);
void start_loading_profile_affiliates(const gchar *username);

void start_loading_bookmarks(GtkListBox *list_box);

void start_loading_lists(void);
void show_list_details(const gchar *list_id);
void on_lists_clicked(GtkWidget *widget, gpointer user_data);
void on_create_list_clicked(GtkWidget *widget, gpointer user_data);
void on_list_follow_clicked(GtkWidget *widget, gpointer user_data);
void on_list_edit_clicked(GtkWidget *widget, gpointer user_data);
void on_list_delete_clicked(GtkWidget *widget, gpointer user_data);
void on_list_add_member_clicked(GtkWidget *widget, gpointer user_data);

gboolean perform_block(const gchar *username, gboolean block);
gboolean perform_mute(const gchar *username, gboolean mute);
gboolean check_user_blocked(const gchar *username);
gboolean check_user_muted(const gchar *username);

void set_timeline_type(TimelineType type);
TimelineType get_current_timeline_type(void);
void start_loading_timeline(GtkListBox *list_box);

gboolean perform_poll_vote(const gchar *tweet_id, const gchar *option_id);
gboolean perform_poll_multi_vote(const gchar *tweet_id, JsonNode *answers, gchar **message_out);
void free_poll(struct Poll *poll);
void free_poll_option(gpointer data);
void on_manage_passkeys_clicked(GtkWidget *widget, gpointer user_data);
void on_push_notifications_clicked(GtkWidget *widget, gpointer user_data);
void on_moderation_history_clicked(GtkWidget *widget, gpointer user_data);
void on_blocking_causes_clicked(GtkWidget *widget, gpointer user_data);
void on_validate_accounts_clicked(GtkWidget *widget, gpointer user_data);
void on_add_account_clicked(GtkWidget *widget, gpointer user_data);
void on_switch_primary_clicked(GtkWidget *widget, gpointer user_data);

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
                                gint avatar_radius);
gboolean perform_upload_avatar(const gchar *username, const gchar *file_path);
gboolean perform_upload_banner(const gchar *username, const gchar *file_path);

gchar* perform_media_upload(const gchar *file_path);

void start_loading_communities(GtkListBox *list_box);
void start_loading_communities_search(GtkListBox *list_box, const gchar *query);
void start_loading_communities_trending(GtkListBox *list_box);
void start_loading_communities_recommended(GtkListBox *list_box);
void start_loading_my_communities(GtkListBox *list_box);
void start_loading_community_tweets(GtkListBox *list_box, const gchar *community_id);
void start_loading_community_members(const gchar *community_id, GtkListBox *list_box);
void start_loading_community_details(const gchar *community_id);
void on_community_create_invite_clicked(GtkWidget *widget, gpointer user_data);
void on_community_accept_invite_clicked(GtkWidget *widget, gpointer user_data);
void on_community_manage_invites_clicked(GtkWidget *widget, gpointer user_data);
void on_community_moderation_clicked(GtkWidget *widget, gpointer user_data);
void on_community_style_clicked(GtkWidget *widget, gpointer user_data);
void on_community_pin_post_clicked(GtkWidget *widget, gpointer user_data);
gboolean perform_join_community(const gchar *community_id);
gboolean perform_leave_community(const gchar *community_id);
gboolean perform_create_community(const gchar *name,
                                  const gchar *description,
                                  const gchar *rules,
                                  const gchar *access_mode);
gboolean perform_update_community(const gchar *community_id,
                                  const gchar *name,
                                  const gchar *description,
                                  const gchar *rules,
                                  const gchar *access_mode);
gboolean perform_delete_community(const gchar *community_id);
gboolean perform_update_community_access_mode(const gchar *community_id, const gchar *access_mode);
void free_community(gpointer data);
void free_communities(GList *communities);

void update_interaction_cache(const gchar *tweet_id, gboolean liked, gboolean retweeted, gboolean bookmarked);
gboolean get_cached_liked(const gchar *tweet_id);
gboolean get_cached_retweeted(const gchar *tweet_id);
gboolean get_cached_bookmarked(const gchar *tweet_id);

void on_theme_changed(GtkComboBox *combo, gpointer user_data);
void on_compact_mode_toggled(GtkSwitch *switch_widget, gboolean state, gpointer user_data);
void on_notifications_enabled_toggled(GtkSwitch *switch_widget, gboolean state, gpointer user_data);
void on_clear_cache_clicked(GtkWidget *widget, gpointer user_data);
void on_clear_history_clicked(GtkWidget *widget, gpointer user_data);
void on_change_password_clicked(GtkWidget *widget, gpointer user_data);
void on_change_username_clicked(GtkWidget *widget, gpointer user_data);
void on_account_private_toggled(GtkSwitch *switch_widget, gboolean state, gpointer user_data);
void on_transparency_location_toggled(GtkSwitch *switch_widget, gboolean state, gpointer user_data);
void on_update_community_tag_clicked(GtkWidget *widget, gpointer user_data);
void on_clear_community_tag_clicked(GtkWidget *widget, gpointer user_data);
void on_update_outlines_clicked(GtkWidget *widget, gpointer user_data);
void on_delete_account_clicked(GtkWidget *widget, gpointer user_data);
void on_bulk_delete_posts_clicked(GtkWidget *widget, gpointer user_data);
void start_loading_account_requests(void);
void on_remove_affiliate_clicked(GtkWidget *widget, gpointer user_data);
void start_loading_my_shop(void);
void on_create_shop_product_clicked(GtkWidget *widget, gpointer user_data);
void start_loading_for_you_interests(void);
void on_clear_for_you_interests_clicked(GtkWidget *widget, gpointer user_data);
void on_logout_clicked(GtkWidget *widget, gpointer user_data);
void start_loading_muted_words(void);
void start_loading_muted_conversations(void);
void on_add_muted_word_clicked(GtkWidget *widget, gpointer user_data);
void on_mute_conversation_clicked(GtkWidget *widget, gpointer user_data);
void start_loading_delegates(void);
void on_invite_delegate_clicked(GtkWidget *widget, gpointer user_data);
void start_loading_scheduled_posts(void);
void on_schedule_post_clicked(GtkWidget *widget, gpointer user_data);
void on_explore_clicked(GtkWidget *widget, gpointer user_data);
void on_explore_category_changed(GtkComboBox *combo, gpointer user_data);
void start_loading_explore(void);
void on_articles_clicked(GtkWidget *widget, gpointer user_data);
void on_compose_article_clicked(GtkWidget *widget, gpointer user_data);
void start_loading_articles(void);

void update_settings_username_display(void);
void refresh_cache_size_display(void);

/* P2P Encrypted Messaging actions */
void on_p2p_send_clicked(GtkWidget *widget, gpointer user_data);
void on_p2p_setup_clicked(GtkWidget *widget, gpointer user_data);
void on_p2p_contact_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
void on_p2p_generate_keys_clicked(GtkWidget *widget, gpointer user_data);
void on_p2p_import_contact_clicked(GtkWidget *widget, gpointer user_data);
void on_p2p_add_contact_clicked(GtkWidget *widget, gpointer user_data);
void on_p2p_transport_changed(GtkComboBox *combo, gpointer user_data);
void on_p2p_start_listener_clicked(GtkWidget *widget, gpointer user_data);
void on_p2p_connect_clicked(GtkWidget *widget, gpointer user_data);
gboolean on_p2p_contact_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
gboolean p2p_init_session(const gchar *username);
void p2p_send_encrypted_message(const gchar *recipient, const gchar *plaintext);
void p2p_refresh_contacts_list(void);
void p2p_refresh_messages_list(const gchar *contact_username);
void p2p_free_contact(gpointer data);
void p2p_free_message(gpointer data);
void p2p_free_session(struct P2PSession *session);

#endif /* ACTIONS_H */
