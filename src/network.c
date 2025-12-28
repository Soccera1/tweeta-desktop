#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include "network.h"
#include "globals.h"

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
fetch_url(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data, const gchar *method)
{
    CURL *curl_handle;
    CURLcode res;
    struct curl_slist *headers = NULL;

    chunk->memory = malloc(1);
    chunk->size = 0;

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
