#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include "types.h"
#include "globals.h"
#include "json_utils.h"
#include "session.h"
#include "network.h"
#include "actions.h"
#include "constants.h"

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

static void test_parse_tweets_with_note() {
    const char *json_input = "{\"posts\": [{\"id\": \"123\", \"content\": \"Fake news\", \"author\": {\"name\": \"User\", \"username\": \"u\"}, \"fact_check\": {\"note\": \"This is false.\", \"severity\": \"warning\"}}]}";
    GList *tweets = parse_tweets(json_input);

    g_assert_nonnull(tweets);
    struct Tweet *t = (struct Tweet *)tweets->data;
    g_assert_cmpstr(t->content, ==, "Fake news");
    g_assert_nonnull(t->note);
    g_assert_cmpstr(t->note, ==, "This is false.");
    g_assert_cmpstr(t->note_severity, ==, "warning");

    free_tweets(tweets);
}

static void test_parse_tweets_with_danger_note() {
    const char *json_input = "{\"posts\": [{\"id\": \"124\", \"content\": \"Very fake news\", \"author\": {\"name\": \"User\", \"username\": \"u\"}, \"fact_check\": {\"note\": \"Danger!\", \"severity\": \"danger\"}}]}";
    GList *tweets = parse_tweets(json_input);

    g_assert_nonnull(tweets);
    struct Tweet *t = (struct Tweet *)tweets->data;
    g_assert_cmpstr(t->note_severity, ==, "danger");

    free_tweets(tweets);
}

static void test_parse_tweets_with_info_note() {
    const char *json_input = "{\"posts\": [{\"id\": \"125\", \"content\": \"Context needed\", \"author\": {\"name\": \"User\", \"username\": \"u\"}, \"fact_check\": {\"note\": \"Some info.\", \"severity\": \"info\"}}]}";
    GList *tweets = parse_tweets(json_input);

    g_assert_nonnull(tweets);
    struct Tweet *t = (struct Tweet *)tweets->data;
    g_assert_cmpstr(t->note_severity, ==, "info");

    free_tweets(tweets);
}

static void test_parse_login_response() {
    gchar *token = NULL;
    gchar *username = NULL;
    gboolean is_admin = FALSE;

    // Test Valid
    const char *valid_json = "{\"token\": \"abc123token\", \"user\": {\"id\": \"1\", \"username\": \"validuser\", \"admin\": true}}";
    gboolean success = parse_login_response(valid_json, &token, &username, &is_admin);
    g_assert_true(success);
    g_assert_cmpstr(token, ==, "abc123token");
    g_assert_cmpstr(username, ==, "validuser");
    g_assert_true(is_admin);
    g_free(token);
    g_free(username);
    token = NULL;
    username = NULL;

    // Test Valid Non-Admin
    const char *valid_user = "{\"token\": \"abc123token\", \"user\": {\"id\": \"1\", \"username\": \"validuser\", \"admin\": false}}";
    success = parse_login_response(valid_user, &token, &username, &is_admin);
    g_assert_true(success);
    g_assert_false(is_admin);
    g_free(token);
    g_free(username);
    token = NULL;
    username = NULL;

    // Test Valid Default Admin (missing field)
    const char *default_user = "{\"token\": \"abc123token\", \"user\": {\"id\": \"1\", \"username\": \"validuser\"}}";
    success = parse_login_response(default_user, &token, &username, &is_admin);
    g_assert_true(success);
    g_assert_false(is_admin);
    g_free(token);
    g_free(username);
    token = NULL;
    username = NULL;

    // Test Missing Token
    const char *missing_token = "{\"user\": {\"username\": \"validuser\"}}";
    success = parse_login_response(missing_token, &token, &username, &is_admin);
    g_assert_false(success);
    g_assert_null(token);
    g_assert_null(username);

    // Test Missing User
    const char *missing_user = "{\"token\": \"abc123token\"}";
    success = parse_login_response(missing_user, &token, &username, &is_admin);
    g_assert_false(success);

    // Test Malformed JSON
    const char *malformed = "{ token: invalid }";
    success = parse_login_response(malformed, &token, &username, &is_admin);
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
    gchar *tmp_dir = g_dir_make_tmp("tweeta_test_XXXXXX", NULL);
    g_assert_nonnull(tmp_dir);

    // Override XDG_CONFIG_HOME to use the temp dir
    g_setenv("XDG_CONFIG_HOME", tmp_dir, TRUE);

    // Reset global state
    g_free(g_auth_token);
    g_free(g_current_username);
    g_auth_token = NULL;
    g_current_username = NULL;
    g_is_admin = FALSE;

    // Test Save
    const char *test_token = "test_token_123";
    const char *test_user = "test_user_abc";
    save_session(test_token, test_user, TRUE);

    // Verify file content manually
    gchar *app_dir = g_build_filename(tmp_dir, "tweeta-desktop", NULL);
    gchar *expected_path = g_build_filename(app_dir, "session.json", NULL);
    g_assert_true(g_file_test(expected_path, G_FILE_TEST_EXISTS));
    
    // Test Load
    load_session();
    
    g_assert_cmpstr(g_auth_token, ==, test_token);
    g_assert_cmpstr(g_current_username, ==, test_user);
    g_assert_true(g_is_admin);

    // Test Clear
    clear_session();
    g_assert_false(g_file_test(expected_path, G_FILE_TEST_EXISTS));

    // Cleanup
    g_free(g_auth_token);
    g_free(g_current_username);
    g_auth_token = NULL;
    g_current_username = NULL;
    g_is_admin = FALSE;
    
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

static void test_parse_notifications() {
    const char *json_input = "{\"notifications\": [{\"id\": \"n1\", \"type\": \"like\", \"content\": \"liked your tweet\", \"related_id\": \"t1\", \"actor_id\": \"u1\", \"actor_username\": \"actor\", \"actor_name\": \"Actor Name\", \"actor_avatar\": \"/api/uploads/avatar.png\", \"read\": false, \"created_at\": \"2023-10-27T10:00:00Z\"}]}";
    GList *notifications = parse_notifications(json_input);

    g_assert_nonnull(notifications);
    g_assert_cmpint(g_list_length(notifications), ==, 1);

    struct Notification *n = (struct Notification *)notifications->data;
    g_assert_cmpstr(n->id, ==, "n1");
    g_assert_cmpstr(n->type, ==, "like");
    g_assert_cmpstr(n->content, ==, "liked your tweet");
    g_assert_cmpstr(n->related_id, ==, "t1");
    g_assert_cmpstr(n->actor_id, ==, "u1");
    g_assert_cmpstr(n->actor_username, ==, "actor");
    g_assert_cmpstr(n->actor_name, ==, "Actor Name");
    g_assert_cmpstr(n->actor_avatar, ==, "/api/uploads/avatar.png");
    g_assert_false(n->read);
    g_assert_cmpstr(n->created_at, ==, "2023-10-27T10:00:00Z");

    free_notifications(notifications);
}

static void test_parse_tweets_with_attachments() {
    const char *json_input = "{\"posts\": [{\"id\": \"123\", \"content\": \"Hello with media\", \"author\": {\"name\": \"Test User\", \"username\": \"testuser\", \"avatar\": \"/api/uploads/avatar.png\"}, \"attachments\": [{\"id\": \"a1\", \"file_url\": \"/api/uploads/image.jpg\", \"file_type\": \"image/jpeg\"}, {\"id\": \"v1\", \"file_url\": \"/api/uploads/video.mp4\", \"file_type\": \"video/mp4\"}]}]}";
    GList *tweets = parse_tweets(json_input);

    g_assert_nonnull(tweets);
    g_assert_cmpint(g_list_length(tweets), ==, 1);

    struct Tweet *t = (struct Tweet *)tweets->data;
    g_assert_cmpstr(t->content, ==, "Hello with media");
    
    g_assert_nonnull(t->attachments);
    g_assert_cmpint(g_list_length(t->attachments), ==, 2);

    struct Attachment *a1 = (struct Attachment *)t->attachments->data;
    g_assert_cmpstr(a1->id, ==, "a1");
    g_assert_cmpstr(a1->file_url, ==, "/api/uploads/image.jpg");
    g_assert_cmpstr(a1->file_type, ==, "image/jpeg");

    struct Attachment *v1 = (struct Attachment *)t->attachments->next->data;
    g_assert_cmpstr(v1->id, ==, "v1");
    g_assert_cmpstr(v1->file_url, ==, "/api/uploads/video.mp4");
    g_assert_cmpstr(v1->file_type, ==, "video/mp4");

    free_tweets(tweets);
}

static void test_parse_conversations() {
    const char *json_input = "{\"conversations\": [{\"id\": \"c1\", \"type\": \"direct\", \"displayName\": \"Test User\", \"displayAvatar\": \"/avatar.png\", \"last_message_content\": \"Hello\", \"last_message_time\": \"2023-10-27T10:00:00Z\", \"unread_count\": 1}]}";
    GList *convs = parse_conversations(json_input);

    g_assert_nonnull(convs);
    g_assert_cmpint(g_list_length(convs), ==, 1);

    struct Conversation *c = (struct Conversation *)convs->data;
    g_assert_cmpstr(c->id, ==, "c1");
    g_assert_cmpstr(c->type, ==, "direct");
    g_assert_cmpstr(c->display_name, ==, "Test User");
    g_assert_cmpstr(c->display_avatar, ==, "/avatar.png");
    g_assert_cmpstr(c->last_message_content, ==, "Hello");
    g_assert_cmpint(c->unread_count, ==, 1);

    free_conversations(convs);
}

static void test_parse_messages() {
    const char *json_input = "{\"messages\": [{\"id\": \"m1\", \"conversation_id\": \"c1\", \"sender_id\": \"u1\", \"content\": \"Hello\", \"username\": \"testuser\", \"name\": \"Test User\", \"avatar\": \"/avatar.png\", \"created_at\": \"2023-10-27T10:00:00Z\"}]}";
    GList *msgs = parse_messages(json_input);

    g_assert_nonnull(msgs);
    g_assert_cmpint(g_list_length(msgs), ==, 1);

    struct DirectMessage *m = (struct DirectMessage *)msgs->data;
    g_assert_cmpstr(m->id, ==, "m1");
    g_assert_cmpstr(m->content, ==, "Hello");
    g_assert_cmpstr(m->username, ==, "testuser");

    free_messages(msgs);
}

static void test_integration_login() {
    const gchar *username = g_getenv("USERNAME");
    const gchar *password = g_getenv("PASSWORD");

    if (!username || !password) {
        g_test_skip("Skipping integration test: USERNAME and PASSWORD not set");
        return;
    }

    // Reset session state
    g_free(g_auth_token);
    g_free(g_current_username);
    g_auth_token = NULL;
    g_current_username = NULL;
    g_is_admin = FALSE;

    gboolean success = perform_login(username, password);
    g_assert_true(success);
    g_assert_nonnull(g_auth_token);
    g_assert_cmpstr(g_current_username, ==, username);
    
    // Check if admin status was correctly fetched
    g_print("\n[Integration] Logged in as %s. Admin status: %s\n", g_current_username, g_is_admin ? "TRUE" : "FALSE");
}

int main(int argc, char** argv) {
    // Set XDG_CONFIG_HOME early so GLib picks it up for tests that use config dirs
    gchar *tmp_root = g_dir_make_tmp("tweeta_xdg_XXXXXX", NULL);
    g_setenv("XDG_CONFIG_HOME", tmp_root, TRUE);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/integration/login", test_integration_login);
    g_test_add_func("/parsetweets/basic", test_parse_tweets);
    g_test_add_func("/parsetweets/note", test_parse_tweets_with_note);
    g_test_add_func("/parsetweets/note_danger", test_parse_tweets_with_danger_note);
    g_test_add_func("/parsetweets/note_info", test_parse_tweets_with_info_note);
    g_test_add_func("/parsetweets/attachments", test_parse_tweets_with_attachments);
    g_test_add_func("/parselogin/basic", test_parse_login_response);
    g_test_add_func("/constructpayload/basic", test_construct_tweet_payload);
    g_test_add_func("/session/persistence", test_session_persistence);
    g_test_add_func("/parseprofile/basic", test_parse_profile);
    g_test_add_func("/parseprofile/replies", test_parse_profile_replies);
    g_test_add_func("/parseusers/basic", test_parse_users);
    g_test_add_func("/parsenotifications/basic", test_parse_notifications);
    g_test_add_func("/parseconversations/basic", test_parse_conversations);
    g_test_add_func("/parsemessages/basic", test_parse_messages);
    
    int result = g_test_run();
    
    // Cleanup temp root
    gchar *rm_cmd = g_strdup_printf("rm -rf \"%s\"", tmp_root);
    system(rm_cmd);
    g_free(rm_cmd);
    g_free(tmp_root);
    
    return result;
}
    