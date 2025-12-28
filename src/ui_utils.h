#ifndef UI_UTILS_H
#define UI_UTILS_H

#include <gtk/gtk.h>

void load_avatar(GtkWidget *image, const gchar *url, int size);
void on_author_clicked(GtkButton *button, gpointer user_data);
void show_profile(const gchar *username);

#endif // UI_UTILS_H
