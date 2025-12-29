#ifndef SESSION_H
#define SESSION_H

#include <glib.h>

gchar* get_config_path();
void save_session(const gchar *token, const gchar *username, gboolean is_admin);
void clear_session();
void load_session();

#endif // SESSION_H
