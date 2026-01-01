#ifndef CHALLENGE_H
#define CHALLENGE_H

#include <glib.h>

/**
 * Solves a Cap PoW challenge.
 * @param challenge_json The JSON object containing "c", "s", "d" or an array of challenges.
 * @param token The challenge token.
 * @return A JSON string representing the solutions array, or NULL on failure.
 */
gchar* solve_challenge(const gchar *challenge_json, const gchar *token);

/**
 * Checks if a response contains a challenge and solves it if necessary.
 * @param response_json The JSON response from the server.
 * @param original_url The URL of the request that triggered the challenge.
 * @return The challenge token if solved, or NULL if no challenge or solving failed.
 */
gchar* check_and_solve_challenge(const gchar *response_json);

#endif // CHALLENGE_H
