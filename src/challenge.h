#ifndef CHALLENGE_H
#define CHALLENGE_H

#include <glib.h>

gchar* solve_challenge(const gchar *challenge_json, const gchar *token);

gchar* check_and_solve_challenge(const gchar *response_json);

#endif // CHALLENGE_H
