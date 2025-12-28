#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include "session.h"
#include "globals.h"

gchar*
get_config_path()
{
    const gchar *config_dir = g_get_user_config_dir();
    gchar *app_dir = g_build_filename(config_dir, "tweetapus-gtk", NULL);
    
    if (g_mkdir_with_parents(app_dir, 0700) == -1) {
        g_warning("Failed to create config directory: %s", app_dir);
    }

    gchar *config_path = g_build_filename(app_dir, "session.json", NULL);
    g_free(app_dir);
    return config_path;
}

void
save_session(const gchar *token, const gchar *username)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "token");
    json_builder_add_string_value(builder, token);
    json_builder_set_member_name(builder, "username");
    json_builder_add_string_value(builder, username);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *data = json_generator_to_data(gen, NULL);
    
    gchar *path = get_config_path();
    GError *error = NULL;
    if (!g_file_set_contents(path, data, -1, &error)) {
        g_warning("Failed to save session: %s", error->message);
        g_error_free(error);
    }

    g_free(path);
    g_free(data);
    g_object_unref(gen);
    g_object_unref(builder);
}

void
clear_session()
{
    gchar *path = get_config_path();
    g_unlink(path);
    g_free(path);
}

void
load_session()
{
    gchar *path = get_config_path();
    gchar *data = NULL;
    GError *error = NULL;

    if (g_file_get_contents(path, &data, NULL, &error)) {
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, data, -1, NULL)) {
            JsonNode *root = json_parser_get_root(parser);
            JsonObject *obj = json_node_get_object(root);
            
            if (json_object_has_member(obj, "token") && json_object_has_member(obj, "username")) {
                g_free(g_auth_token);
                g_free(g_current_username);
                g_auth_token = g_strdup(json_object_get_string_member(obj, "token"));
                g_current_username = g_strdup(json_object_get_string_member(obj, "username"));
            }
        }
        g_object_unref(parser);
        g_free(data);
    } else {
        if (error) g_error_free(error);
    }
    g_free(path);
}
