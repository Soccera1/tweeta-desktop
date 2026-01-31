#ifndef TYPES_H
#define TYPES_H

#include <gtk/gtk.h>

typedef enum {
    TIMELINE_PUBLIC,
    TIMELINE_FOLLOWING
} TimelineType;

struct Attachment {
    gchar *id;
    gchar *file_url;
    gchar *file_type;
};

struct Tweet {
    gchar *content;
    gchar *author_name;
    gchar *author_username;
    gchar *author_avatar;
    gchar *id;
    gchar *note;
    gchar *note_severity;   /* warning, danger, or info */
    GList *attachments;
    gboolean liked;
    gboolean retweeted;
    gboolean bookmarked;
    int like_count;
    int retweet_count;
    int reply_count;
    struct Tweet *quote_tweet;
    struct Poll *poll;
};

struct Emoji {
    gchar *id;
    gchar *name;
    gchar *file_url;
};

struct Profile {
    gchar *name;
    gchar *username;
    gchar *bio;
    gchar *avatar;
    int follower_count;
    int following_count;
    int post_count;
};

struct Notification {
    gchar *id;
    gchar *type;            /* like, retweet, reply, follow, mention, quote, reaction */
    gchar *content;
    gchar *related_id;      /* ID of related tweet or object */
    gchar *actor_id;
    gchar *actor_username;
    gchar *actor_name;
    gchar *actor_avatar;
    gboolean read;
    gchar *created_at;
};

struct DirectMessage {
    gchar *id;
    gchar *conversation_id;
    gchar *sender_id;
    gchar *content;
    gchar *username;
    gchar *name;
    gchar *avatar;
    gchar *created_at;
    GList *attachments;
};

struct Conversation {
    gchar *id;
    gchar *type;            /* direct or group */
    gchar *title;
    gchar *display_name;
    gchar *display_avatar;
    gchar *last_message_content;
    gchar *last_message_time;
    int unread_count;
    GList *participants;    /* List of Profile* */
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
    gboolean success;
    struct Profile *profile;
    gchar *username;
    gchar *query;
    gchar *conversation_id;
    gchar *community_id;
    guint request_id;       /* Used to cancel stale requests */
    gboolean is_append;     /* TRUE when loading more (infinite scroll) */
    gchar *before_id;       /* Pagination cursor */
};

struct AvatarData {
    GtkWidget *image;
    gchar *url;
    int size;
};

struct ReplyContext {
    GtkWidget *text_view;
    gchar *reply_to_id;
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
    gboolean voted;
};

struct Poll {
    gchar *id;
    gchar *question;
    GList *options;         /* List of PollOption* */
    gboolean is_active;
    gchar *expires_at;
    int total_votes;
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
    gchar *icon_url;
    gchar *banner_url;
    gchar *access_mode;     /* public, private, or restricted */
    int member_count;
    gboolean is_member;
    gboolean is_admin;
    gboolean is_moderator;
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

struct UploadContext {
    GtkWidget *parent_dialog;
    GtkWidget *file_label;
    gchar *file_path;
    gchar *file_type;
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
