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


struct MemoryStruct {
  char *memory;
  size_t size;
};

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

static void
fetch_tweets(GtkWidget *list_box)
{
  CURL *curl_handle;
  CURLcode res;

  struct MemoryStruct chunk;

  chunk.memory = malloc(1);
  chunk.size = 0;

  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_URL, "https://tweeta.tiago.zip/api/public-tweets");
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
  res = curl_easy_perform(curl_handle);

  if(res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
  }
  else {
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    json_parser_load_from_data(parser, chunk.memory, -1, &error);
    if (error) {
        g_warning("Unable to parse json: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return;
    }

    JsonNode *root_node = json_parser_get_root(parser);
    JsonObject *root_object = json_node_get_object(root_node);
    JsonArray *posts = json_object_get_array_member(root_object, "posts");

    for (guint i = 0; i < json_array_get_length(posts); i++) {
      JsonNode *post_node = json_array_get_element(posts, i);
      JsonObject *post_object = json_node_get_object(post_node);
      const gchar *content = json_object_get_string_member(post_object, "content");

      JsonObject *author_object = json_object_get_object_member(post_object, "author");
      const gchar *author_name = json_object_get_string_member(author_object, "name");
      const gchar *author_username = json_object_get_string_member(author_object, "username");
      gchar *author_str = g_strdup_printf("%s (@%s)", author_name, author_username);


      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
      GtkWidget *author_label = gtk_label_new(author_str);
      GtkWidget *content_label = gtk_label_new(content);

      gtk_label_set_xalign(GTK_LABEL(author_label), 0.0);
      gtk_label_set_xalign(GTK_LABEL(content_label), 0.0);
      gtk_label_set_line_wrap(GTK_LABEL(content_label), TRUE);

      gtk_box_pack_start(GTK_BOX(box), author_label, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(box), content_label, FALSE, FALSE, 0);

      gtk_list_box_insert(GTK_LIST_BOX(list_box), box, -1);
      g_free(author_str);
    }

    g_object_unref(parser);
  }

  curl_easy_cleanup(curl_handle);
  free(chunk.memory);
  curl_global_cleanup();
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *list_box;
    GtkWidget *scrolled_window;

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Tweetapus");
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(window), scrolled_window);

    list_box = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scrolled_window), list_box);

    fetch_tweets(list_box);

    gtk_widget_show_all(window);

    gtk_main();

    return 0;
}