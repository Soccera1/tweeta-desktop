#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include "challenge.h"
#include "constants.h"
#include "globals.h"
#include "network.h"

#define TWEETA_DESKTOP_CLIENT_HEADER "Tweeta Desktop; 1.0.0"
#define TWEETA_DESKTOP_USER_AGENT "TweetaDesktop/1.0.0"

static inline gchar* get_auth_token_safe(void) {
    g_mutex_lock(&g_globals_mutex);
    gchar *token = g_auth_token ? g_strdup(g_auth_token) : NULL;
    g_debug("get_auth_token_safe: token=%s (length=%d)", token ? token : "(null)", token ? (int)strlen(token) : 0);
    g_mutex_unlock(&g_globals_mutex);
    return token;
}

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = g_realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    g_critical("not enough memory (g_realloc returned NULL)");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

static const gchar *
get_runtime_api_base_url(void)
{
    const gchar *env = g_getenv("TWEETA_API_BASE_URL");
    return (env && *env) ? env : API_BASE_URL;
}

static const gchar *
get_runtime_base_domain(void)
{
    const gchar *env = g_getenv("TWEETA_BASE_DOMAIN");
    return (env && *env) ? env : BASE_DOMAIN;
}

static gchar *
rewrite_url_for_runtime_base(const gchar *url)
{
    if (!url) {
        return NULL;
    }

    const gchar *runtime_api_base = get_runtime_api_base_url();
    if (g_str_has_prefix(url, API_BASE_URL) &&
        g_strcmp0(runtime_api_base, API_BASE_URL) != 0) {
        return g_strdup_printf("%s%s", runtime_api_base, url + strlen(API_BASE_URL));
    }

    const gchar *runtime_base_domain = get_runtime_base_domain();
    if (g_str_has_prefix(url, BASE_DOMAIN) &&
        g_strcmp0(runtime_base_domain, BASE_DOMAIN) != 0) {
        return g_strdup_printf("%s%s", runtime_base_domain, url + strlen(BASE_DOMAIN));
    }

    return g_strdup(url);
}

gboolean
fetch_url_internal(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data, const gchar *method, long *response_code)
{
    CURL *curl_handle;
    CURLcode res;
    struct curl_slist *headers = NULL;
    gchar *request_url = NULL;

    if (!url) {
        g_critical("fetch_url_internal: URL is NULL");
        return FALSE;
    }

    if (chunk->memory) {
        g_free(chunk->memory);
    }
    chunk->memory = g_malloc(1);
    chunk->size = 0;
    chunk->memory[0] = '\0';

    curl_handle = curl_easy_init();
    if (!curl_handle) {
        g_critical("curl_easy_init() failed");
        g_free(chunk->memory);
        chunk->memory = NULL;
        return FALSE;
    }

    request_url = rewrite_url_for_runtime_base(url);
    curl_easy_setopt(curl_handle, CURLOPT_URL, request_url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, TWEETA_DESKTOP_USER_AGENT);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "X-Tweetapus-Client: " TWEETA_DESKTOP_CLIENT_HEADER);
    gchar *auth_token = get_auth_token_safe();
    g_debug("fetch_url_internal: url=%s, request_url=%s, method=%s, has_auth_token=%d",
            url,
            request_url,
            method,
            auth_token != NULL);
    if (auth_token) {
        gchar *auth_header = g_strdup_printf("Authorization: Bearer %s", auth_token);
        headers = curl_slist_append(headers, auth_header);
        g_free(auth_header);
    }
    g_free(auth_token);
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
        g_free(chunk->memory);
        chunk->memory = NULL;
        chunk->size = 0;
        curl_easy_cleanup(curl_handle);
        g_free(request_url);
        return FALSE;
    }

    curl_easy_cleanup(curl_handle);
    g_free(request_url);
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

    gchar *cap_token = check_and_solve_challenge(chunk->memory);
    if (cap_token) {
        g_message("Challenge detected and solved. Retrying request with capToken.");
        gchar *new_post_data = NULL;
        if (post_data) {
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
                g_free(challenge_chunk.memory);
                
                if (cap_token) {
                    g_message("Fetched and solved new challenge. Retrying original request.");
                    
                    if (response_code == 429) {
                        struct MemoryStruct bypass_chunk;
                        bypass_chunk.memory = NULL;
                        bypass_chunk.size = 0;
                        gchar *bypass_data = g_strdup_printf("{\"capToken\": \"%s\"}", cap_token);
                        fetch_url_internal(API_BASE_URL "/auth/cap/rate-limit-bypass", &bypass_chunk, bypass_data, "POST", &response_code);
                        g_free(bypass_data);
                        if (bypass_chunk.memory) g_free(bypass_chunk.memory);
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
gboolean
fetch_url_with_file(const gchar *url, struct MemoryStruct *chunk, const gchar *file_path, const gchar *field_name)
{
    CURL *curl_handle;
    CURLcode res;
    struct curl_slist *headers = NULL;
    curl_mime *mime = NULL;
    curl_mimepart *part = NULL;

    g_debug("fetch_url_with_file: url=%s, file_path=%s, field_name=%s", 
            url ? url : "(null)", file_path ? file_path : "(null)", field_name ? field_name : "(null)");

    if (!url || !file_path) {
        g_critical("fetch_url_with_file: URL or file_path is NULL");
        return FALSE;
    }

    if (chunk->memory) {
        g_free(chunk->memory);
    }
    chunk->memory = g_malloc(1);
    chunk->size = 0;
    chunk->memory[0] = '\0';

    curl_handle = curl_easy_init();
    if (!curl_handle) {
        g_critical("curl_easy_init() failed");
        g_free(chunk->memory);
        chunk->memory = NULL;
        return FALSE;
    }

    mime = curl_mime_init(curl_handle);
    if (!mime) {
        g_critical("curl_mime_init() failed");
        g_free(chunk->memory);
        chunk->memory = NULL;
        curl_easy_cleanup(curl_handle);
        return FALSE;
    }

    part = curl_mime_addpart(mime);
    curl_mime_name(part, field_name ? field_name : "file");
    curl_mime_filedata(part, file_path);
    
    g_debug("fetch_url_with_file: added mime part for file=%s", file_path);

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_MIMEPOST, mime);

    gchar *auth_token = get_auth_token_safe();
    if (auth_token) {
        gchar *auth_header = g_strdup_printf("Authorization: Bearer %s", auth_token);
        headers = curl_slist_append(headers, auth_header);
        g_free(auth_header);
    }
    g_free(auth_token);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

    g_debug("fetch_url_with_file: performing curl request");
    res = curl_easy_perform(curl_handle);
    g_debug("fetch_url_with_file: curl perform result=%d (%s)", res, curl_easy_strerror(res));

    curl_slist_free_all(headers);
    curl_mime_free(mime);

    if (res != CURLE_OK) {
        g_critical("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        g_free(chunk->memory);
        chunk->memory = NULL;
        chunk->size = 0;
        curl_easy_cleanup(curl_handle);
        return FALSE;
    }

    g_debug("fetch_url_with_file: request succeeded, response_size=%zu", chunk->size);
    if (chunk->memory && chunk->size > 0) {
        g_debug("fetch_url_with_file: response=%s", chunk->memory);
    }

    curl_easy_cleanup(curl_handle);
    return TRUE;
}
