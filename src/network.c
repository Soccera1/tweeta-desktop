#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <json-glib/json-glib.h>
#include "network.h"
#include "globals.h"
#include "challenge.h"
#include "constants.h"

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    g_critical("not enough memory (realloc returned NULL)");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

gboolean
fetch_url_internal(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data, const gchar *method, long *response_code)
{
    CURL *curl_handle;
    CURLcode res;
    struct curl_slist *headers = NULL;

    if (chunk->memory) {
        free(chunk->memory);
    }
    chunk->memory = malloc(1);
    chunk->size = 0;
    chunk->memory[0] = '\0';

    curl_handle = curl_easy_init();
    if (!curl_handle) {
        g_critical("curl_easy_init() failed");
        free(chunk->memory);
        chunk->memory = NULL;
        return FALSE;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (g_auth_token) {
        gchar *auth_header = g_strdup_printf("Authorization: Bearer %s", g_auth_token);
        headers = curl_slist_append(headers, auth_header);
        g_free(auth_header);
    }
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

    if (method) {
        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method);
    }

    if (post_data) {
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_data);
    }

    res = curl_easy_perform(curl_handle);

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, response_code);
    }

    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        g_critical("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        free(chunk->memory);
        chunk->memory = NULL;
        chunk->size = 0;
        curl_easy_cleanup(curl_handle);
        return FALSE;
    }

    curl_easy_cleanup(curl_handle);
    return TRUE;
}

gboolean
fetch_url(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data, const gchar *method)
{
    long response_code = 0;
    chunk->memory = NULL;
    chunk->size = 0;

    if (!fetch_url_internal(url, chunk, post_data, method, &response_code)) {
        return FALSE;
    }

    // Check if the response contains a challenge
    gchar *cap_token = check_and_solve_challenge(chunk->memory);
    if (cap_token) {
        g_message("Challenge detected and solved. Retrying request with capToken.");
        gchar *new_post_data = NULL;
        if (post_data) {
            // Add capToken to existing JSON post_data
            JsonParser *parser = json_parser_new();
            if (json_parser_load_from_data(parser, post_data, -1, NULL)) {
                JsonNode *root = json_parser_get_root(parser);
                if (JSON_NODE_HOLDS_OBJECT(root)) {
                    JsonObject *obj = json_node_get_object(root);
                    json_object_set_string_member(obj, "capToken", cap_token);
                    JsonGenerator *gen = json_generator_new();
                    json_generator_set_root(gen, root);
                    new_post_data = json_generator_to_data(gen, NULL);
                    g_object_unref(gen);
                }
            }
            g_object_unref(parser);
        } else {
            // Create new JSON with capToken
            JsonBuilder *builder = json_builder_new();
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "capToken");
            json_builder_add_string_value(builder, cap_token);
            json_builder_end_object(builder);
            JsonGenerator *gen = json_generator_new();
            json_generator_set_root(gen, json_builder_get_root(builder));
            new_post_data = json_generator_to_data(gen, NULL);
            g_object_unref(gen);
            g_object_unref(builder);
        }

        gboolean success = fetch_url_internal(url, chunk, new_post_data, method ? method : "POST", &response_code);
        g_free(new_post_data);
        g_free(cap_token);
        return success;
    }

    // If it was a 429 or 403 with "Challenge token is required", we should explicitly get a challenge
    if (response_code == 429 || response_code == 403 || response_code == 400) {
        gboolean needs_cap = FALSE;
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, chunk->memory, -1, NULL)) {
            JsonNode *root = json_parser_get_root(parser);
            if (JSON_NODE_HOLDS_OBJECT(root)) {
                JsonObject *obj = json_node_get_object(root);
                if (json_object_has_member(obj, "error")) {
                    const gchar *error_msg = json_object_get_string_member(obj, "error");
                    if (g_str_has_prefix(error_msg, "Challenge token is required") || 
                        g_str_has_prefix(error_msg, "Rate limit exceeded") ||
                        response_code == 429) {
                        needs_cap = TRUE;
                    }
                }
            }
        }
        g_object_unref(parser);

        if (needs_cap) {
            g_message("Challenge token required. Fetching new challenge.");
            struct MemoryStruct challenge_chunk;
            challenge_chunk.memory = NULL;
            challenge_chunk.size = 0;
            if (fetch_url_internal(CAP_CHALLENGE_URL, &challenge_chunk, "{}", "POST", &response_code)) {
                cap_token = check_and_solve_challenge(challenge_chunk.memory);
                free(challenge_chunk.memory);
                
                if (cap_token) {
                    g_message("Fetched and solved new challenge. Retrying original request.");
                    
                    // If it was a ratelimit, we might need to call rate-limit-bypass first
                    if (response_code == 429) {
                        struct MemoryStruct bypass_chunk;
                        bypass_chunk.memory = NULL;
                        bypass_chunk.size = 0;
                        gchar *bypass_data = g_strdup_printf("{\"capToken\": \"%s\"}", cap_token);
                        fetch_url_internal(API_BASE_URL "/auth/cap/rate-limit-bypass", &bypass_chunk, bypass_data, "POST", &response_code);
                        g_free(bypass_data);
                        if (bypass_chunk.memory) free(bypass_chunk.memory);
                    }

                    gchar *new_post_data = NULL;
                    if (post_data) {
                        JsonParser *parser2 = json_parser_new();
                        if (json_parser_load_from_data(parser2, post_data, -1, NULL)) {
                            JsonNode *root = json_parser_get_root(parser2);
                            if (JSON_NODE_HOLDS_OBJECT(root)) {
                                JsonObject *obj = json_node_get_object(root);
                                json_object_set_string_member(obj, "capToken", cap_token);
                                JsonGenerator *gen = json_generator_new();
                                json_generator_set_root(gen, root);
                                new_post_data = json_generator_to_data(gen, NULL);
                                g_object_unref(gen);
                            }
                        }
                        g_object_unref(parser2);
                    } else {
                         // Even for GET, if it's ratelimited, we might need to pass capToken
                         // but standard practice is POST for these.
                         // For now, let's just try adding it if we can.
                    }

                    gboolean success = fetch_url_internal(url, chunk, new_post_data, method, &response_code);
                    g_free(new_post_data);
                    g_free(cap_token);
                    return success;
                }
            }
        }
    }

    return TRUE;
}
