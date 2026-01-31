#ifndef TYPES_H
#define TYPES_H

#include <gtk/gtk.h>

// Timeline type enum
typedef enum {
    TIMELINE_PUBLIC,
    TIMELINE_FOLLOWING
} TimelineType;

// Represents a media attachment
struct Attachment {
    gchar *id;
    gchar *file_url;
    gchar *file_type;
};

// Represents a single tweet
struct Tweet {
  gchar *content;
  gchar *author_name;
  gchar *author_username;
  gchar *author_avatar;
  gchar *id;
  gchar *note;
  gchar *note_severity;
  GList *attachments;
  gboolean liked;
  gboolean retweeted;
  gboolean bookmarked;
  int like_count;
  int retweet_count;
  int reply_count;
  struct Tweet *quote_tweet;
  struct Poll *poll;  // Optional poll attached to the tweet
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
    gchar *type;
    gchar *content;
    gchar *related_id;
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
    gchar *type;
    gchar *title;
    gchar *display_name;
    gchar *display_avatar;
    gchar *last_message_content;
    gchar *last_message_time;
    int unread_count;
    GList *participants; // List of Profile*
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
    GList *notifications;
    GList *conversations;
    GList *messages;
    GList *communities;  // List of Community*
    gboolean success;
    struct Profile *profile;
    gchar *username;
    gchar *query;
    gchar *conversation_id;
    gchar *community_id;
    guint request_id;  // Track which request instance this is
    gboolean is_append;
    gchar *before_id;
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

// Poll structures
struct PollOption {
    gchar *id;
    gchar *option_text;
    int vote_count;
    gboolean voted;
};

struct Poll {
    gchar *id;
    gchar *question;
    GList *options;  // List of PollOption*
    gboolean is_active;
    gchar *expires_at;
    int total_votes;
};

// Community structures
struct Community {
    gchar *id;
    gchar *name;
    gchar *description;
    gchar *icon_url;
    gchar *banner_url;
    gchar *access_mode;  // "public", "private", "restricted"
    int member_count;
    gboolean is_member;
    gboolean is_admin;
    gboolean is_moderator;
};

// Upload/Attachment context
struct UploadContext {
    GtkWidget *parent_dialog;
    GtkWidget *file_label;
    gchar *file_path;
    gchar *file_type;
};

// Profile edit context
struct ProfileEditContext {
    GtkWidget *name_entry;
    GtkWidget *bio_entry;
    GtkWidget *avatar_btn;
    GtkWidget *banner_btn;
    gchar *new_avatar_path;
    gchar *new_banner_path;
};

#endif // TYPES_H
