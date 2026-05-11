#include <stdlib.h>
#include <gio/gio.h>
#include "constants.h"
#include "globals.h"
#include "network.h"
#include "ui_utils.h"
#include "types.h"

static gboolean
set_image_pixbuf(gpointer data)
{
    GdkPixbuf *pixbuf = (GdkPixbuf *)((gpointer *)data)[0];
    GtkWidget *image = (GtkWidget *)((gpointer *)data)[1];

    if (GTK_IS_IMAGE(image) && pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
    }

    if (pixbuf) {
        g_object_unref(pixbuf);
    }
    if (G_IS_OBJECT(image)) {
        g_object_unref(image);
    }
    g_free(data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_avatar_thread(gpointer data)
{
    struct AvatarData *avatar_data = (struct AvatarData *)data;
    struct MemoryStruct chunk = {0};
    
    gchar *full_url;
    if (g_str_has_prefix(avatar_data->url, "http")) {
        full_url = g_strdup(avatar_data->url);
    } else {
        full_url = g_strdup_printf("%s%s", BASE_DOMAIN, avatar_data->url);
    }

    long response_code = 0;
    if (fetch_url_internal(full_url, &chunk, NULL, "GET", &response_code)) {
        GInputStream *stream = g_memory_input_stream_new_from_data(chunk.memory, chunk.size, NULL);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale(stream, avatar_data->size, avatar_data->size, TRUE, NULL, NULL);
        
        if (pixbuf) {
            gpointer *params = g_new(gpointer, 2);
            params[0] = pixbuf; // Already has ref from creation
            params[1] = G_IS_OBJECT(avatar_data->image) ? g_object_ref(avatar_data->image) : NULL;
            g_idle_add(set_image_pixbuf, params);
        }
        g_object_unref(stream);
        g_free(chunk.memory);
    }

    g_free(full_url);
    g_free(avatar_data->url);
    g_free(avatar_data);
    return NULL;
}

void
load_avatar(GtkWidget *image, const gchar *url, int size)
{
    if (!image || !url || strlen(url) == 0) return;

    struct AvatarData *data = g_new(struct AvatarData, 1);
    data->image = image;
    data->url = g_strdup(url);
    data->size = size;

    g_thread_new("avatar-loader", fetch_avatar_thread, data);
}

void
on_author_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    const gchar *username = g_object_get_data(G_OBJECT(button), "username");
    if (username && username[0] != '\0') {
        show_profile(username);
    }
}

gchar*
detect_mime_type(const gchar *file_path)
{
    if (!file_path) {
        return g_strdup("application/octet-stream");
    }

    gboolean uncertain = FALSE;
    gchar *content_type = g_content_type_guess(file_path, NULL, 0, &uncertain);
    if (!content_type) {
        return g_strdup("application/octet-stream");
    }

    gchar *mime_type = g_content_type_get_mime_type(content_type);
    g_free(content_type);

    if (!mime_type) {
        return g_strdup("application/octet-stream");
    }

    return mime_type;
}

gchar*
build_reply_banner_text(const gchar *username)
{
    const gchar *display_username = (username && username[0] != '\0') ? username : "unknown";
    return g_strdup_printf("Replying to @%s:", display_username);
}

gchar*
build_account_label_text(const gchar *name, const gchar *username)
{
    const gchar *display_name = (name && name[0] != '\0') ? name : "Unknown";
    const gchar *display_username = (username && username[0] != '\0') ? username : "unknown";
    return g_strdup_printf("%s (@%s)", display_name, display_username);
}

void
free_attachment_payload(gpointer data)
{
    struct Attachment *attach = data;
    if (attach) {
        g_free(attach->id);
        g_free(attach->file_url);
        g_free(attach->file_type);
        g_free(attach);
    }
}

GList*
build_attachment_list(const gchar *file_url, const gchar *file_type)
{
    g_debug("build_attachment_list: file_url=%s, file_type=%s",
            file_url ? file_url : "(null)", file_type ? file_type : "(null)");

    if (!file_url) {
        g_debug("build_attachment_list: returning NULL due to NULL file_url");
        return NULL;
    }

    struct Attachment *attach = g_new0(struct Attachment, 1);
    attach->id = NULL;
    attach->file_url = g_strdup(file_url);
    attach->file_type = g_strdup(file_type ? file_type : "application/octet-stream");

    g_debug("build_attachment_list: created attachment with file_url=%s, file_type=%s",
            attach->file_url, attach->file_type);

    return g_list_append(NULL, attach);
}

void
free_wrapper(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}
