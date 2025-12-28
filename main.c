/*
 * Tweetapus GTK Client
 *
 * A minimal GTK3 X11 client in C for tweetapus.
 *
 * (c) 2025 Lily
 * Licensed under the AGPLv3 license
 */

#include <gtk/gtk.h>
#include <curl/curl.h>
#include <json-glib/json-glib.h>

#define API_URL "https://tweeta.tiago.zip/api/public-tweets"

// Represents a single tweet
struct Tweet {
  gchar *content;
  gchar *author_name;
  gchar *author_username;
};

// Memory buffer for curl
struct MemoryStruct {
  char *memory;
  size_t size;
};

// Data to pass between threads
struct AsyncData {
    GtkListBox *list_box;
    GList *tweets;
    gboolean success;
};

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

static gboolean
fetch_url(const gchar *url, struct MemoryStruct *chunk)
{
    CURL *curl_handle;
    CURLcode res;

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

    res = curl_easy_perform(curl_handle);

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

static GList*
parse_tweets(const gchar *json_data)
{
    JsonParser *parser;
    GError *error = NULL;
    GList *tweets = NULL;

    parser = json_parser_new();
    json_parser_load_from_data(parser, json_data, -1, &error);
    if (error) {
        g_warning("Unable to parse json: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root_node = json_parser_get_root(parser);
    JsonObject *root_object = json_node_get_object(root_node);
    JsonArray *posts = json_object_get_array_member(root_object, "posts");

    for (guint i = 0; i < json_array_get_length(posts); i++) {
        JsonNode *post_node = json_array_get_element(posts, i);
        JsonObject *post_object = json_node_get_object(post_node);
        JsonObject *author_object = json_object_get_object_member(post_object, "author");

        struct Tweet *tweet = g_new(struct Tweet, 1);
        tweet->content = g_strdup(json_object_get_string_member(post_object, "content"));
        tweet->author_name = g_strdup(json_object_get_string_member(author_object, "name"));
        tweet->author_username = g_strdup(json_object_get_string_member(author_object, "username"));

        tweets = g_list_append(tweets, tweet);
    }

    g_object_unref(parser);
    return tweets;
}

static void
free_tweet(gpointer data)
{
    struct Tweet *tweet = data;
    g_free(tweet->content);
    g_free(tweet->author_name);
    g_free(tweet->author_username);
    g_free(tweet);
}

static void
free_tweets(GList *tweets)
{
    g_list_free_full(tweets, free_tweet);
}

static GtkWidget*
create_tweet_widget(struct Tweet *tweet)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gchar *author_str = g_strdup_printf("%s (@%s)", tweet->author_name, tweet->author_username);

    GtkWidget *author_label = gtk_label_new(author_str);
    gtk_label_set_xalign(GTK_LABEL(author_label), 0.0);

    GtkWidget *content_label = gtk_label_new(tweet->content);
    gtk_label_set_xalign(GTK_LABEL(content_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(content_label), TRUE);

    gtk_box_pack_start(GTK_BOX(box), author_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), content_label, FALSE, FALSE, 0);

    g_free(author_str);

    return box;
}

static void
populate_tweet_list(GtkListBox *list_box, GList *tweets)
{
    // Clear existing items (e.g. Loading...)
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    // Add new items
    for (GList *l = tweets; l != NULL; l = l->next) {
        GtkWidget *tweet_widget = create_tweet_widget(l->data);
        gtk_widget_show_all(tweet_widget);
        gtk_list_box_insert(list_box, tweet_widget, -1);
    }
}

// Callback executed on the main thread
static gboolean
on_tweets_loaded(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    
    if (async_data->success && async_data->tweets) {
        populate_tweet_list(async_data->list_box, async_data->tweets);
        free_tweets(async_data->tweets);
    } else {
        // Clear "Loading..." and show error
        GList *children = gtk_container_get_children(GTK_CONTAINER(async_data->list_box));
        for(GList *iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);

        GtkWidget *error_label = gtk_label_new("Failed to load tweets.");
        gtk_widget_show(error_label);
        gtk_list_box_insert(async_data->list_box, error_label, -1);
    }

    g_free(async_data);
    return G_SOURCE_REMOVE;
}

// Thread function
static gpointer
fetch_tweets_thread(gpointer data)
{
    struct AsyncData *async_data = (struct AsyncData *)data;
    struct MemoryStruct chunk;

    if (fetch_url(API_URL, &chunk)) {
        async_data->tweets = parse_tweets(chunk.memory);
        async_data->success = (async_data->tweets != NULL);
        free(chunk.memory);
    } else {
        async_data->success = FALSE;
    }

    // Schedule UI update on main thread
    g_idle_add(on_tweets_loaded, async_data);
    
    return NULL;
}

static void
start_loading_tweets(GtkListBox *list_box)
{
    // Add "Loading..." label
    GtkWidget *loading_label = gtk_label_new("Loading tweets...");
    gtk_widget_show(loading_label);
    gtk_list_box_insert(list_box, loading_label, -1);

    // Prepare async data
    struct AsyncData *data = g_new0(struct AsyncData, 1);
    data->list_box = list_box;
    
    // Spawn thread
    g_thread_new("tweet-loader", fetch_tweets_thread, data);
}

static GtkWidget*
create_window()
{
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Tweetapus Desktop");
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    return window;
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *list_box;
    GtkWidget *scrolled_window;

    gtk_init(&argc, &argv);
    curl_global_init(CURL_GLOBAL_ALL);

    window = create_window();

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(window), scrolled_window);

    list_box = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scrolled_window), list_box);

    // Initial Show
    gtk_widget_show_all(window);

    // Start loading
    start_loading_tweets(GTK_LIST_BOX(list_box));

    gtk_main();

    curl_global_cleanup();

    return 0;
}