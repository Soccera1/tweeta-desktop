#ifndef TYPES_H
#define TYPES_H

#include <gtk/gtk.h>

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
  GList *attachments;
};

// Represents profile data
struct Profile {
    gchar *name;
    gchar *username;
    gchar *bio;
    gchar *avatar;
    int follower_count;
    int following_count;
    int post_count;
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
    gboolean success;
    struct Profile *profile;
    gchar *username;
    gchar *query;
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

#endif // TYPES_H
