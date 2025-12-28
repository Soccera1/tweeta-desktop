#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include "types.h"
#include "globals.h"
#include "json_utils.h"
#include "session.h"
#include "network.h"

// We need to declare internal functions if they are not in headers but needed for tests.
// Actually most of them ARE in headers now.
// WriteMemoryCallback is static in network.c, let's see if we can test it differently 
// or if we should move it to a header/utility.
// In my refactoring I kept it static in network.c.

// For now, I'll assume I can test the public API.
// If I need to test WriteMemoryCallback, I might need to make it non-static.

static void test_parse_tweets() {
    const char *json_input = "{\"posts\": [{\"id\": \"123\", \"content\": \"Hello world\", \"author\": {\"name\": \"Test User\", \"username\": \"testuser\", \"avatar\": \"/api/uploads/avatar.png\"}}]}";
    GList *tweets = parse_tweets(json_input);

    g_assert_nonnull(tweets);
    g_assert_cmpint(g_list_length(tweets), ==, 1);

    struct Tweet *t = (struct Tweet *)tweets->data;
    g_assert_cmpstr(t->content, ==, "Hello world");
    g_assert_cmpstr(t->author_name, ==, "Test User");
    g_assert_cmpstr(t->author_username, ==, "testuser");
    g_assert_cmpstr(t->author_avatar, ==, "/api/uploads/avatar.png");
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

static void test_parse_profile() {
    const char *json_input = "{\"profile\": {\"name\": \"Test User\", \"username\": \"testuser\", \"bio\": \"This is a test bio\", \"avatar\": \"/api/uploads/profile.png\", \"follower_count\": 100, \"following_count\": 50, \"post_count\": 10}}";
    struct Profile *p = parse_profile(json_input);

    g_assert_nonnull(p);
    g_assert_cmpstr(p->name, ==, "Test User");
    g_assert_cmpstr(p->username, ==, "testuser");
    g_assert_cmpstr(p->bio, ==, "This is a test bio");
    g_assert_cmpstr(p->avatar, ==, "/api/uploads/profile.png");
    g_assert_cmpint(p->follower_count, ==, 100);
    g_assert_cmpint(p->following_count, ==, 50);
    g_assert_cmpint(p->post_count, ==, 10);

    g_free(p->name);
    g_free(p->username);
    g_free(p->bio);
    g_free(p->avatar);
    g_free(p);
}

static void test_parse_profile_replies() {
    const char *json_input = "{\"replies\": [{\"id\": \"456\", \"content\": \"Test reply\", \"author\": {\"name\": \"Replier\", \"username\": \"replier\", \"avatar\": \"/api/uploads/reply.png\"}}]}";
    GList *tweets = parse_profile_replies(json_input);

    g_assert_nonnull(tweets);
    g_assert_cmpint(g_list_length(tweets), ==, 1);

    struct Tweet *t = (struct Tweet *)tweets->data;
    g_assert_cmpstr(t->content, ==, "Test reply");
    g_assert_cmpstr(t->author_name, ==, "Replier");
    g_assert_cmpstr(t->author_username, ==, "replier");
    g_assert_cmpstr(t->author_avatar, ==, "/api/uploads/reply.png");
    g_assert_cmpstr(t->id, ==, "456");

    free_tweets(tweets);
}

static void test_parse_users() {
    const char *json_input = "{\"users\": [{\"username\": \"testuser\", \"name\": \"Test User\", \"bio\": \"Test Bio\", \"avatar\": \"/api/uploads/user.png\", \"follower_count\": 123}]}";
    GList *users = parse_users(json_input);

    g_assert_nonnull(users);
    g_assert_cmpint(g_list_length(users), ==, 1);

    struct Profile *u = (struct Profile *)users->data;
    g_assert_cmpstr(u->username, ==, "testuser");
    g_assert_cmpstr(u->name, ==, "Test User");
    g_assert_cmpstr(u->bio, ==, "Test Bio");
    g_assert_cmpstr(u->avatar, ==, "/api/uploads/user.png");
    g_assert_cmpint(u->follower_count, ==, 123);

    free_users(users);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/parsetweets/basic", test_parse_tweets);
    g_test_add_func("/parselogin/basic", test_parse_login_response);
    g_test_add_func("/constructpayload/basic", test_construct_tweet_payload);
    g_test_add_func("/session/persistence", test_session_persistence);
    g_test_add_func("/parseprofile/basic", test_parse_profile);
    g_test_add_func("/parseprofile/replies", test_parse_profile_replies);
    g_test_add_func("/parseusers/basic", test_parse_users);
    return g_test_run();
}