#include <stdlib.h>
#include "ui_utils.h"
#include "network.h"
#include "constants.h"
#include "globals.h"

static gboolean
set_image_pixbuf(gpointer data)
{
    GdkPixbuf *pixbuf = (GdkPixbuf *)((gpointer *)data)[0];
    GtkWidget *image = (GtkWidget *)((gpointer *)data)[1];

    if (GTK_IS_IMAGE(image)) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
    }

    g_object_unref(pixbuf);
    g_object_unref(image);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static gpointer
fetch_avatar_thread(gpointer data)
{
    struct AvatarData *avatar_data = (struct AvatarData *)data;
    struct MemoryStruct chunk;
    
    gchar *full_url;
    if (g_str_has_prefix(avatar_data->url, "http")) {
        full_url = g_strdup(avatar_data->url);
    } else {
        full_url = g_strdup_printf("%s%s", BASE_DOMAIN, avatar_data->url);
    }

    if (fetch_url(full_url, &chunk, NULL)) {
        GInputStream *stream = g_memory_input_stream_new_from_data(chunk.memory, chunk.size, NULL);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale(stream, avatar_data->size, avatar_data->size, TRUE, NULL, NULL);
        
        if (pixbuf) {
            gpointer *params = g_new(gpointer, 2);
            params[0] = pixbuf; // Already has ref from creation
            params[1] = g_object_ref(avatar_data->image);
            g_idle_add(set_image_pixbuf, params);
        }
        g_object_unref(stream);
        free(chunk.memory);
    }

    g_free(full_url);
    g_free(avatar_data->url);
    g_free(avatar_data);
    return NULL;
}

void
load_avatar(GtkWidget *image, const gchar *url, int size)
{
    if (!url || strlen(url) == 0) return;

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
    show_profile(username);
}
