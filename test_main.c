#include <glib.h>
#include <gtk/gtk.h>

// This is not ideal, but for a single file program, it's the easiest way to get access to static functions.
#define main app_main 
#include "main.c"
#undef main


static void test_writememorycallback() {
    struct MemoryStruct mem;
    mem.memory = malloc(1);
    mem.size = 0;

    char *test_string = "Hello, world!";
    size_t test_string_len = strlen(test_string);

    size_t returned_size = WriteMemoryCallback(test_string, 1, test_string_len, &mem);

    g_assert_cmpint(returned_size, ==, test_string_len);
    g_assert_cmpint(mem.size, ==, test_string_len);
    g_assert_cmpstr(mem.memory, ==, test_string);

    char *test_string2 = " Another string.";
    size_t test_string2_len = strlen(test_string2);

    returned_size = WriteMemoryCallback(test_string2, 1, test_string2_len, &mem);

    g_assert_cmpint(returned_size, ==, test_string2_len);
    g_assert_cmpint(mem.size, ==, test_string_len + test_string2_len);
    g_assert_cmpstr(mem.memory, ==, "Hello, world! Another string.");

    free(mem.memory);
}

static void test_parse_tweets() {
    const char *json_input = "{\"posts\": [{\"id\": \"123\", \"content\": \"Hello world\", \"author\": {\"name\": \"Test User\", \"username\": \"testuser\"}}]}";
    GList *tweets = parse_tweets(json_input);

    g_assert_nonnull(tweets);
    g_assert_cmpint(g_list_length(tweets), ==, 1);

    struct Tweet *t = (struct Tweet *)tweets->data;
    g_assert_cmpstr(t->content, ==, "Hello world");
    g_assert_cmpstr(t->author_name, ==, "Test User");
    g_assert_cmpstr(t->author_username, ==, "testuser");
    g_assert_cmpstr(t->id, ==, "123");

    free_tweets(tweets);
}

static void test_parse_login_response() {
    gchar *token = NULL;
    gchar *username = NULL;

    // Test Valid
    const char *valid_json = "{\"token\": \"abc123token\", \"user\": {\"id\": \"1\", \"username\": \"validuser\"}}";
    gboolean success = parse_login_response(valid_json, &token, &username);
    g_assert_true(success);
    g_assert_cmpstr(token, ==, "abc123token");
    g_assert_cmpstr(username, ==, "validuser");
    g_free(token);
    g_free(username);
    token = NULL;
    username = NULL;

    // Test Missing Token
    const char *missing_token = "{\"user\": {\"username\": \"validuser\"}}";
    success = parse_login_response(missing_token, &token, &username);
    g_assert_false(success);
    g_assert_null(token);
    g_assert_null(username);

    // Test Missing User
    const char *missing_user = "{\"token\": \"abc123token\"}";
    success = parse_login_response(missing_user, &token, &username);
    g_assert_false(success);

    // Test Malformed JSON
    const char *malformed = "{ token: invalid }";
    success = parse_login_response(malformed, &token, &username);
    g_assert_false(success);
}

static void test_construct_tweet_payload() {
    // Test without reply
    gchar *payload = construct_tweet_payload("Hello world", NULL);
    g_assert_nonnull(payload);
    
    // Parse it back to verify
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    json_parser_load_from_data(parser, payload, -1, &error);
    g_assert_no_error(error);
    
    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);
    g_assert_true(json_object_has_member(obj, "content"));
    g_assert_cmpstr(json_object_get_string_member(obj, "content"), ==, "Hello world");
    g_assert_false(json_object_has_member(obj, "reply_to"));
    
    g_object_unref(parser);
    g_free(payload);

    // Test with reply
    payload = construct_tweet_payload("Reply text", "12345");
    parser = json_parser_new();
    json_parser_load_from_data(parser, payload, -1, &error);
    g_assert_no_error(error);
    root = json_parser_get_root(parser);
    obj = json_node_get_object(root);
    g_assert_cmpstr(json_object_get_string_member(obj, "content"), ==, "Reply text");
    g_assert_true(json_object_has_member(obj, "reply_to"));
    g_assert_cmpstr(json_object_get_string_member(obj, "reply_to"), ==, "12345");
    
    g_object_unref(parser);
    g_free(payload);
}

static void test_session_persistence() {
    // Create a temporary directory for config
    gchar *tmp_dir = g_dir_make_tmp("tweetapus_test_XXXXXX", NULL);
    g_assert_nonnull(tmp_dir);

    // Override XDG_CONFIG_HOME to use the temp dir
    g_setenv("XDG_CONFIG_HOME", tmp_dir, TRUE);

    // Reset global state
    g_free(g_auth_token);
    g_free(g_current_username);
    g_auth_token = NULL;
    g_current_username = NULL;

    // Test Save
    const char *test_token = "test_token_123";
    const char *test_user = "test_user_abc";
    save_session(test_token, test_user);

    // Verify file content manually
    gchar *app_dir = g_build_filename(tmp_dir, "tweetapus-gtk", NULL);
    gchar *expected_path = g_build_filename(app_dir, "session.json", NULL);
    g_assert_true(g_file_test(expected_path, G_FILE_TEST_EXISTS));
    
    // Test Load
    load_session();
    
    g_assert_cmpstr(g_auth_token, ==, test_token);
    g_assert_cmpstr(g_current_username, ==, test_user);

    // Test Clear
    clear_session();
    g_assert_false(g_file_test(expected_path, G_FILE_TEST_EXISTS));

    // Cleanup
    g_free(g_auth_token);
    g_free(g_current_username);
    g_auth_token = NULL;
    g_current_username = NULL;
    
    // Clean up temp dir
    gchar *rm_cmd = g_strdup_printf("rm -rf \"%s\"", tmp_dir);
    if (system(rm_cmd) != 0) {
        g_warning("Failed to remove temp dir: %s", tmp_dir);
    }
    g_free(rm_cmd);
    
    g_free(expected_path);
    g_free(app_dir);
    g_free(tmp_dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/writememorycallback/basic", test_writememorycallback);
    g_test_add_func("/parsetweets/basic", test_parse_tweets);
    g_test_add_func("/parselogin/basic", test_parse_login_response);
    g_test_add_func("/constructpayload/basic", test_construct_tweet_payload);
    g_test_add_func("/session/persistence", test_session_persistence);
    return g_test_run();
}
