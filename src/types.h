#ifndef TYPES_H
#define TYPES_H

#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

typedef enum {
    TIMELINE_PUBLIC,
    TIMELINE_FOLLOWING
} TimelineType;

struct Attachment {
    gchar *id;
    gchar *file_url;
    gchar *file_type;
    gchar *file_hash;
    gchar *file_name;
    gint64 file_size;
    gboolean is_spoiler;
};

struct Tweet {
    gchar *content;
    gchar *author_id;
    gchar *author_name;
    gchar *author_username;
    gchar *author_avatar;
    gboolean author_verified;
    gboolean author_gold;
    gboolean author_gray;
    gchar *id;
    gchar *note;
    gchar *note_severity;
    gchar *edited_at;
    GList *attachments;
    gboolean liked;
    gboolean retweeted;
    gboolean bookmarked;
    int like_count;
    int retweet_count;
    int reply_count;
    int view_count;
    int quote_count;
    int reaction_count;
    gboolean pinned;
    struct Tweet *quote_tweet;
    struct Poll *poll;
    gchar *content_type;
    gchar *retweet_created_at;
    gchar *original_post_id;
    gchar *article_title;
    gchar *article_body_markdown;
    gchar *created_at;
};

struct Emoji {
    gchar *id;
    gchar *name;
    gchar *file_url;
    gchar *file_hash;
    gchar *created_by;
};

struct Profile {
    gchar *id;
    gchar *name;
    gchar *username;
    gchar *bio;
    gchar *avatar;
    gchar *banner;
    gchar *location;
    gchar *website;
    gchar *pronouns;
    gchar *theme;
    gchar *accent_color;
    gchar *label_type;
    int follower_count;
    int following_count;
    int post_count;
    int avatar_radius;
    gboolean is_following;
    gboolean follows_me;
    gboolean is_own_profile;
    gboolean blocked_by_profile;
    gboolean blocked_profile;
    gboolean notify_tweets;
    gboolean label_automated;
    gboolean author_verified;
    gboolean author_gold;
    gboolean author_gray;
};

struct Notification {
    gchar *id;
    gchar *type;
    gchar *content;
    gchar *related_id;
    gchar *actor_id;
    gchar *actor_username;
    gchar *actor_name;
    gchar *actor_avatar;
    gboolean actor_verified;
    gboolean actor_gold;
    gboolean read;
    gchar *created_at;
};

struct DirectMessage {
    gchar *id;
    gchar *conversation_id;
    gchar *sender_id;
    gchar *content;
    gchar *message_type;
    gchar *reply_to;
    gchar *reply_preview;
    gchar *username;
    gchar *name;
    gchar *avatar;
    gboolean verified;
    gboolean is_deleted;
    gchar *created_at;
    gchar *edited_at;
    gchar *reactions_summary;
    gchar *mpi_kind;
    gchar *mpi_status;
    gchar *mpi_net;
    gchar *mpi_gross;
    gchar *mpi_note;
    gchar *mpi_order_id;
    gchar *mpi_payment_link_url;
    GList *attachments;
};

struct Conversation {
    gchar *id;
    gchar *type;
    gchar *title;
    gchar *display_name;
    gchar *display_avatar;
    gchar *last_message_content;
    gchar *last_message_time;
    gchar *last_message_sender;
    gchar *last_message_sender_name;
    int unread_count;
    int participant_count;
    gboolean disappearing_enabled;
    int disappearing_duration;
    GList *participants;
};

struct AdminStats {
    gint64 total_users;
    gint64 suspended_users;
    gint64 restricted_users;
    gint64 verified_users;
    gint64 gold_users;
    gint64 gray_users;
    gint64 total_posts;
    gint64 active_suspensions;
    gint64 active_restricted;
    gint64 active_suspended;
};

struct MemoryStruct {
    char *memory;
    size_t size;
};

struct AsyncData {
    GtkListBox *list_box;
    GList *tweets;
    GList *users;
    GList *notifications;
    GList *conversations;
    GList *messages;
    GList *communities;
    GList *lists;
    gboolean success;
    gboolean has_more;
    struct Profile *profile;
    struct Conversation *conversation;
    struct TweetaList *list;
    gchar *username;
    gchar *query;
    gchar *conversation_id;
    gchar *community_id;
    gchar *json_data;
    guint request_id;       /* Used to cancel stale requests */
    gboolean is_append;     /* TRUE when loading more (infinite scroll) */
    gchar *before_id;       /* Pagination cursor */
};

struct AvatarData {
    GtkWidget *image;
    gchar *url;
    int size;
};

struct UploadContext {
    GtkWidget *parent_dialog;
    GtkWidget *file_label;
    gchar *file_path;
    gchar *file_type;
    gchar *remote_url;
    gchar *remote_type;
};

struct ReplyContext {
    GtkWidget *text_view;
    gchar *reply_to_id;
    struct UploadContext upload;
};

struct QuoteContext {
    GtkWidget *text_view;
    gchar *quote_id;
};

struct NoteContext {
    GtkWidget *text_view;
    gchar *tweet_id;
    gchar *severity;
};

struct InteractionData {
    gchar *tweet_id;
    GtkWidget *button;
    gboolean *state_ptr;
    int *count_ptr;
};

struct ReactionContext {
    gchar *tweet_id;
    GtkWidget *parent_window;
};

struct PollOption {
    gchar *id;
    gchar *option_text;
    int vote_count;
    int percentage;
    gboolean voted;
    gchar *user_vote;
};

struct Poll {
    gchar *id;
    gchar *question;
    gchar *kind;
    JsonNode *steps;
    GList *options;         /* List of PollOption* */
    gboolean is_active;
    gchar *expires_at;
    int total_votes;
    gboolean has_user_answers;
    int user_score;
    int user_total;
};

struct InteractionState {
    gboolean liked;
    gboolean retweeted;
    gboolean bookmarked;
};

struct Community {
    gchar *id;
    gchar *name;
    gchar *description;
    gchar *rules;
    gchar *icon_url;
    gchar *banner_url;
    gchar *access_mode;
    int member_count;
    gboolean is_member;
    gboolean is_admin;
    gboolean is_moderator;
    gboolean tag_enabled;
    gchar *tag_emoji;
    gchar *tag_text;
};

struct TweetaList {
    gchar *id;
    gchar *user_id;
    gchar *name;
    gchar *description;
    gchar *owner_username;
    gchar *owner_name;
    gint member_count;
    gint follower_count;
    gboolean is_private;
    gboolean is_following;
    gboolean is_owner;
    GList *members;
    GList *followers;
};

struct P2PContact {
    gchar *username;
    gchar *display_name;
    gchar *public_key_fingerprint;
    gchar *public_key_armor;
    gchar *avatar_url;
    gboolean is_online;
    gchar *last_seen;
    gchar *direct_host;         /* For direct P2P connections */
    guint16 direct_port;
};

struct P2PMessage {
    gchar *id;
    gchar *sender_username;
    gchar *recipient_username;
    gchar *encrypted_content;
    gchar *plaintext_content;  /* Decrypted locally */
    gchar *timestamp;
    gboolean is_outgoing;
    gboolean is_verified;
};

struct P2PSession {
    gchar *local_username;
    gchar *local_key_fingerprint;
    GHashTable *contacts;      /* username -> P2PContact* */
    GHashTable *conversations; /* username -> GList* of P2PMessage* */
    GMutex session_mutex;
};

struct ProfileEditContext {
    GtkWidget *name_entry;
    GtkWidget *bio_entry;
    GtkWidget *avatar_btn;
    GtkWidget *banner_btn;
    gchar *new_avatar_path;
    gchar *new_banner_path;
};

#endif /* TYPES_H */
