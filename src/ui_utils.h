#ifndef UI_UTILS_H
#define UI_UTILS_H

#include <gtk/gtk.h>

void load_avatar(GtkWidget *image, const gchar *url, int size);
void on_author_clicked(GtkButton *button, gpointer user_data);
void show_profile(const gchar *username);
gchar* detect_mime_type(const gchar *file_path);

void free_attachment_payload(gpointer data);
GList* build_attachment_list(const gchar *file_url, const gchar *file_type);

#endif /* UI_UTILS_H */
