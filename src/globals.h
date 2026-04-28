#ifndef GLOBALS_H
#define GLOBALS_H

#include <gtk/gtk.h>
#include "types.h"

extern gchar *g_auth_token;
extern gchar *g_current_username;
extern gboolean g_is_admin;
extern gboolean g_is_impersonating;
extern gchar *g_impersonation_admin_token;
extern gchar *g_impersonation_admin_username;
extern gboolean g_impersonation_admin_is_admin;
extern GMutex g_globals_mutex;
extern GHashTable *g_interaction_cache;
extern GtkWidget *g_compose_button;
extern GtkWidget *g_header_auth_button;
extern GtkWidget *g_notifications_button;
extern GtkWidget *g_user_label;
extern GtkWidget *g_main_list_box;
extern GtkWidget *g_notifications_list;
extern GtkWidget *g_conversations_list;
extern GtkWidget *g_dm_messages_list;
extern GtkWidget *g_dm_title_label;
extern GtkWidget *g_dm_info_label;
extern GtkWidget *g_dm_entry;
extern GtkWidget *g_conversation_list;
extern GtkWidget *g_stack;
extern GtkWidget *g_back_button;
extern GtkWidget *g_admin_button;

extern GtkWidget *g_admin_stats_label;
extern GtkWidget *g_admin_users_list;
extern GtkWidget *g_admin_posts_list;
extern GtkWidget *g_admin_suspensions_list;
extern GtkWidget *g_admin_reports_list;
extern GtkWidget *g_admin_logs_list;
extern GtkWidget *g_admin_blocks_list;
extern GtkWidget *g_admin_emojis_list;
extern GtkWidget *g_admin_badges_list;
extern GtkWidget *g_admin_dms_list;
extern GtkWidget *g_admin_dm_admin_messages_list;
extern GtkWidget *g_admin_shop_products_list;
extern GtkWidget *g_admin_shop_purchases_list;
extern GtkWidget *g_admin_communities_list;
extern GtkWidget *g_admin_users_search;
extern GtkWidget *g_admin_posts_search;
extern GtkWidget *g_admin_logs_search;
extern GtkWidget *g_admin_dms_search;
extern GtkWidget *g_admin_shop_search;
extern GtkWidget *g_admin_notifications_target_entry;
extern GtkWidget *g_admin_notifications_type_entry;
extern GtkWidget *g_admin_notifications_title_entry;
extern GtkWidget *g_admin_notifications_subtitle_entry;
extern GtkWidget *g_admin_notifications_url_entry;
extern GtkWidget *g_admin_notifications_message_view;
extern GtkWidget *g_admin_notifications_result_label;
extern GtkWidget *g_admin_impersonation_entry;
extern GtkWidget *g_admin_impersonation_status_label;
extern GtkWidget *g_admin_clone_source_entry;
extern GtkWidget *g_admin_clone_username_entry;
extern GtkWidget *g_admin_clone_name_entry;
extern GtkWidget *g_admin_clone_relations_check;
extern GtkWidget *g_admin_clone_ghosts_check;
extern GtkWidget *g_admin_clone_tweets_check;
extern GtkWidget *g_admin_clone_replies_check;
extern GtkWidget *g_admin_clone_retweets_check;
extern GtkWidget *g_admin_clone_reactions_check;
extern GtkWidget *g_admin_clone_communities_check;
extern GtkWidget *g_admin_clone_media_check;
extern GtkWidget *g_admin_clone_affiliate_check;
extern GtkWidget *g_admin_clone_result_label;
extern GtkWidget *g_admin_tools_post_targets_entry;
extern GtkWidget *g_admin_tools_post_reply_to_entry;
extern GtkWidget *g_admin_tools_post_source_entry;
extern GtkWidget *g_admin_tools_post_created_at_entry;
extern GtkWidget *g_admin_tools_post_no_char_limit_check;
extern GtkWidget *g_admin_tools_post_content_view;
extern GtkWidget *g_admin_tools_bulk_targets_entry;
extern GtkWidget *g_admin_tools_bulk_payload_view;

extern GtkWidget *g_search_entry;
extern GtkWidget *g_search_users_list;
extern GtkWidget *g_search_tweets_list;

extern GtkWidget *g_profile_name_label;
extern GtkWidget *g_profile_username_label;
extern GtkWidget *g_profile_bio_label;
extern GtkWidget *g_profile_status_label;
extern GtkWidget *g_profile_details_label;
extern GtkWidget *g_profile_stats_label;
extern GtkWidget *g_profile_avatar_image;
extern GtkWidget *g_profile_banner_image;
extern GtkWidget *g_profile_badges_box;
extern GtkWidget *g_profile_tweets_list;
extern GtkWidget *g_profile_replies_list;
extern GtkWidget *g_profile_media_list;
extern GtkWidget *g_profile_mutuals_list;
extern GtkWidget *g_follow_button;
extern GtkWidget *g_profile_edit_button;
extern GtkWidget *g_profile_notify_button;
extern GtkWidget *g_profile_block_button;
extern GtkWidget *g_profile_mute_button;
extern GtkWidget *g_profile_delete_avatar_button;
extern GtkWidget *g_profile_delete_banner_button;
extern GtkWidget *g_followers_list;
extern GtkWidget *g_following_list;
extern struct Profile *g_active_profile;

extern GtkWidget *g_bookmarks_list;

extern TimelineType g_current_timeline_type;

extern GtkWidget *g_communities_list;
extern GtkWidget *g_community_tweets_list;
extern GtkWidget *g_community_title_label;
extern GtkWidget *g_community_details_label;
extern gchar *g_community_id;

extern GtkWidget *g_theme_combo;
extern GtkWidget *g_compact_mode_switch;
extern GtkWidget *g_enable_notifications_switch;
extern GtkWidget *g_sound_notifications_switch;
extern GtkWidget *g_dm_notifications_switch;
extern GtkWidget *g_cache_size_label;
extern GtkWidget *g_settings_username_label;
extern GtkWidget *g_settings_auth_button;
extern GtkWidget *g_change_password_button;

extern gboolean g_notifications_enabled;
extern gboolean g_sound_enabled;
extern gboolean g_dm_notifications_enabled;
extern gboolean g_compact_mode_enabled;
extern int g_theme_preference;

/* P2P Encrypted Messaging globals */
extern GtkWidget *g_p2p_contacts_list;
extern GtkWidget *g_p2p_messages_list;
extern GtkWidget *g_p2p_entry;
extern GtkWidget *g_p2p_title_label;
extern GtkWidget *g_p2p_status_label;
extern struct P2PSession *g_p2p_session;

#endif /* GLOBALS_H */
