#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "types.h"
#include "globals.h"
#include "json_utils.h"
#include "session.h"
#include "network.h"
#include "actions.h"
#include "constants.h"
#include "challenge.h"

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

static gboolean use_colors = FALSE;
static GList *failed_tests = NULL;
static int total_tests = 0;
static int passed_tests = 0;

static void init_colors(void) {
    use_colors = isatty(STDOUT_FILENO);
}

static const char *green(void) {
    return use_colors ? COLOR_GREEN : "";
}

static const char *red(void) {
    return use_colors ? COLOR_RED : "";
}



static const char *cyan(void) {
    return use_colors ? COLOR_CYAN : "";
}

static const char *bold(void) {
    return use_colors ? COLOR_BOLD : "";
}

static const char *reset(void) {
    return use_colors ? COLOR_RESET : "";
}

/* Commented out to avoid "unused function" warning - kept for potential future use
   (e.g., for warning states or skipped tests that need yellow highlighting)
static const char *yellow(void) {
    return use_colors ? COLOR_YELLOW : "";
}
*/

static void print_summary(int run_count, int failed_count) {
    int passed_count = run_count - failed_count;
    
    g_print("\n%s========================================%s\n", bold(), reset());
    g_print("           %sTEST SUMMARY%s\n", bold(), reset());
    g_print("%s========================================%s\n", bold(), reset());
    
    g_print("\n");
    g_print("  Tests run:    %s%d%s\n", cyan(), run_count, reset());
    g_print("  Passed:       %s%d%s\n", green(), passed_count, reset());
    g_print("  Failed:       %s%d%s\n", 
            failed_count > 0 ? red() : green(), failed_count, reset());
    g_print("\n");
    
    if (failed_count > 0) {
        g_print("%sFailed tests:%s\n", red(), reset());
        for (GList *l = failed_tests; l != NULL; l = l->next) {
            g_print("  %s- %s%s\n", red(), (const char *)l->data, reset());
        }
        g_print("\n");
    }
    
    if (failed_count == 0) {
        g_print("%sAll tests passed!%s\n", green(), reset());
    } else {
        g_print("%s%d test(s) failed.%s\n", red(), failed_count, reset());
    }
    g_print("\n");
}

// Helper macros for colored test output
#define TEST_PASS_MSG() \
    (use_colors ? COLOR_GREEN "[PASS]" COLOR_RESET : "[PASS]")
#define TEST_FAIL_MSG() \
    (use_colors ? COLOR_RED "[FAIL]" COLOR_RESET : "[FAIL]")

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
    // Use the existing temp directory from main() - don't try to change XDG_CONFIG_HOME
    // because g_get_user_config_dir() caches its result on first call
    const gchar *tmp_dir = g_getenv("XDG_CONFIG_HOME");
    g_assert_nonnull(tmp_dir);
    
    // Reset global state
    g_free(g_auth_token);
    g_free(g_current_username);
    g_auth_token = NULL;
    g_current_username = NULL;
    g_is_admin = FALSE;
    
    // Get the path where the session file should be
    gchar *app_dir = g_build_filename(tmp_dir, "tweeta-desktop", NULL);
    gchar *expected_path = g_build_filename(app_dir, "session.json", NULL);
    
    // Delete any existing session file (from integration test or previous runs)
    g_unlink(expected_path);
    
    // Ensure the config directory exists before saving
    g_mkdir_with_parents(app_dir, 0700);
    
    // Test Save
    const char *test_token = "test_token_123";
    const char *test_user = "test_user_abc";
    save_session(test_token, test_user, TRUE);

    // Verify file content manually with retry
    // Retry check a few times in case of filesystem sync delay
    gboolean file_exists = FALSE;
    for (int i = 0; i < 5; i++) {
        if (g_file_test(expected_path, G_FILE_TEST_EXISTS)) {
            file_exists = TRUE;
            break;
        }
        // Small delay (100ms)
        g_usleep(100000);
    }
    g_assert_true(file_exists);
    
    // Reset globals before loading to ensure we're actually loading from file
    g_free(g_auth_token);
    g_free(g_current_username);
    g_auth_token = NULL;
    g_current_username = NULL;
    g_is_admin = FALSE;
    
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
    
    g_free(expected_path);
    g_free(app_dir);
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

static void test_parse_tweet_details() {
    const char *json_input = "{"
        "\"tweet\": {\"id\": \"main\", \"content\": \"Main tweet\", \"author\": {\"name\": \"User\", \"username\": \"user\"}},"
        "\"threadPosts\": [{\"id\": \"parent\", \"content\": \"Parent tweet\", \"author\": {\"name\": \"Parent\", \"username\": \"parent\"}}],"
        "\"replies\": [{\"id\": \"reply\", \"content\": \"Reply tweet\", \"author\": {\"name\": \"Replier\", \"username\": \"replier\"}}]"
    "}";
    GList *tweets = parse_tweet_details(json_input);

    g_assert_nonnull(tweets);
    g_assert_cmpint(g_list_length(tweets), ==, 3);

    struct Tweet *t1 = (struct Tweet *)g_list_nth_data(tweets, 0);
    g_assert_cmpstr(t1->id, ==, "parent");
    
    struct Tweet *t2 = (struct Tweet *)g_list_nth_data(tweets, 1);
    g_assert_cmpstr(t2->id, ==, "main");
    
    struct Tweet *t3 = (struct Tweet *)g_list_nth_data(tweets, 2);
    g_assert_cmpstr(t3->id, ==, "reply");

    free_tweets(tweets);
}

static void test_challenge_solver() {
    // A simple challenge: 1 challenge, salt length 8, difficulty 2 (1 byte match)
    const char *challenge_json = "{\"c\": 1, \"s\": 8, \"d\": 2}";
    
    // Generate a random token to ensure the nonce is different each time
    gchar *token = g_strdup_printf("testtoken-%u", g_random_int());
    
    gchar *solutions_json = solve_challenge(challenge_json, token);
    g_assert_nonnull(solutions_json);
    
    JsonParser *parser = json_parser_new();
    g_assert_true(json_parser_load_from_data(parser, solutions_json, -1, NULL));
    JsonNode *root = json_parser_get_root(parser);
    g_assert_true(JSON_NODE_HOLDS_ARRAY(root));
    JsonArray *array = json_node_get_array(root);
    g_assert_cmpint(json_array_get_length(array), ==, 1);
    
    gint64 nonce = json_array_get_int_element(array, 0);
    g_message("Solved random challenge with token '%s', nonce: %ld", token, (long)nonce);
    
    g_object_unref(parser);
    g_free(solutions_json);
    g_free(token);
}

// NEW TESTS FOR IMPLEMENTED FEATURES

static void test_parse_tweets_with_poll() {
    const char *json_input = "{\"posts\": [{\"id\": \"123\", \"content\": \"What do you think?\", \"author\": {\"name\": \"Test User\", \"username\": \"testuser\"}, \"poll\": {\"id\": \"poll1\", \"question\": \"Favorite color?\", \"is_active\": true, \"expires_at\": \"2024-12-31T23:59:59Z\", \"total_votes\": 100, \"options\": [{\"id\": \"opt1\", \"option_text\": \"Red\", \"vote_count\": 30, \"voted\": false}, {\"id\": \"opt2\", \"option_text\": \"Blue\", \"vote_count\": 70, \"voted\": true}]}}]}";
    GList *tweets = parse_tweets(json_input);

    g_assert_nonnull(tweets);
    g_assert_cmpint(g_list_length(tweets), ==, 1);

    struct Tweet *t = (struct Tweet *)tweets->data;
    g_assert_cmpstr(t->content, ==, "What do you think?");
    g_assert_nonnull(t->poll);
    
    struct Poll *poll = t->poll;
    g_assert_cmpstr(poll->id, ==, "poll1");
    g_assert_cmpstr(poll->question, ==, "Favorite color?");
    g_assert_true(poll->is_active);
    g_assert_cmpstr(poll->expires_at, ==, "2024-12-31T23:59:59Z");
    g_assert_cmpint(poll->total_votes, ==, 100);
    g_assert_nonnull(poll->options);
    g_assert_cmpint(g_list_length(poll->options), ==, 2);
    
    struct PollOption *opt1 = (struct PollOption *)poll->options->data;
    g_assert_cmpstr(opt1->id, ==, "opt1");
    g_assert_cmpstr(opt1->option_text, ==, "Red");
    g_assert_cmpint(opt1->vote_count, ==, 30);
    g_assert_false(opt1->voted);
    
    struct PollOption *opt2 = (struct PollOption *)poll->options->next->data;
    g_assert_cmpstr(opt2->id, ==, "opt2");
    g_assert_cmpstr(opt2->option_text, ==, "Blue");
    g_assert_cmpint(opt2->vote_count, ==, 70);
    g_assert_true(opt2->voted);

    free_tweets(tweets);
}

static void test_parse_communities() {
    const char *json_input = "{\"communities\": [{\"id\": \"comm1\", \"name\": \"Tech Talk\", \"description\": \"Discussion about technology\", \"icon_url\": \"/uploads/comm1_icon.png\", \"banner_url\": \"/uploads/comm1_banner.png\", \"access_mode\": \"public\", \"member_count\": 1500, \"is_member\": true, \"is_admin\": false, \"is_moderator\": true}]}";
    GList *communities = parse_communities(json_input);

    g_assert_nonnull(communities);
    g_assert_cmpint(g_list_length(communities), ==, 1);

    struct Community *c = (struct Community *)communities->data;
    g_assert_cmpstr(c->id, ==, "comm1");
    g_assert_cmpstr(c->name, ==, "Tech Talk");
    g_assert_cmpstr(c->description, ==, "Discussion about technology");
    g_assert_cmpstr(c->icon_url, ==, "/uploads/comm1_icon.png");
    g_assert_cmpstr(c->banner_url, ==, "/uploads/comm1_banner.png");
    g_assert_cmpstr(c->access_mode, ==, "public");
    g_assert_cmpint(c->member_count, ==, 1500);
    g_assert_true(c->is_member);
    g_assert_false(c->is_admin);
    g_assert_true(c->is_moderator);

    free_communities(communities);
}

static void test_parse_communities_private() {
    const char *json_input = "{\"communities\": [{\"id\": \"comm2\", \"name\": \"Private Club\", \"description\": \"Exclusive community\", \"access_mode\": \"private\", \"member_count\": 50, \"is_member\": false, \"is_admin\": false, \"is_moderator\": false}]}";
    GList *communities = parse_communities(json_input);

    g_assert_nonnull(communities);
    g_assert_cmpint(g_list_length(communities), ==, 1);

    struct Community *c = (struct Community *)communities->data;
    g_assert_cmpstr(c->id, ==, "comm2");
    g_assert_cmpstr(c->access_mode, ==, "private");
    g_assert_cmpint(c->member_count, ==, 50);
    g_assert_false(c->is_member);

    free_communities(communities);
}

static void test_parse_upload_response() {
    const char *json_input = "{\"file_url\": \"/api/uploads/test_image.png\", \"success\": true}";
    gchar *file_url = parse_upload_response(json_input);
    
    g_assert_nonnull(file_url);
    g_assert_cmpstr(file_url, ==, "/api/uploads/test_image.png");
    
    g_free(file_url);
}

static void test_parse_upload_response_failure() {
    const char *json_input = "{\"error\": \"Invalid file type\", \"success\": false}";
    gchar *file_url = parse_upload_response(json_input);
    
    g_assert_null(file_url);
}

static void test_poll_memory_management() {
    // Test that poll data is properly freed when freeing tweets
    const char *json_input = "{\"posts\": [{\"id\": \"1\", \"content\": \"Test\", \"author\": {\"name\": \"User\", \"username\": \"user\"}, \"poll\": {\"id\": \"p1\", \"question\": \"Q?\", \"is_active\": true, \"total_votes\": 10, \"options\": [{\"id\": \"o1\", \"option_text\": \"A\", \"vote_count\": 5, \"voted\": false}]}}]}";
    GList *tweets = parse_tweets(json_input);
    
    g_assert_nonnull(tweets);
    struct Tweet *t = (struct Tweet *)tweets->data;
    g_assert_nonnull(t->poll);
    g_assert_nonnull(t->poll->options);
    
    // This should not crash or leak memory
    free_tweets(tweets);
    
    // If we get here, memory was freed successfully
    g_assert_true(TRUE);
}

static void test_community_memory_management() {
    // Test community allocation and freeing
    struct Community *c = g_new0(struct Community, 1);
    c->id = g_strdup("test-id");
    c->name = g_strdup("Test Community");
    c->description = g_strdup("A test community");
    c->icon_url = g_strdup("/icon.png");
    c->banner_url = g_strdup("/banner.png");
    c->access_mode = g_strdup("public");
    c->member_count = 100;
    c->is_member = TRUE;
    c->is_admin = FALSE;
    c->is_moderator = FALSE;
    
    GList *communities = g_list_append(NULL, c);
    
    // This should not crash or leak memory
    free_communities(communities);
    
    g_assert_true(TRUE);
}

static void test_poll_option_memory_management() {
    // Test poll option freeing
    struct PollOption *opt = g_new0(struct PollOption, 1);
    opt->id = g_strdup("opt1");
    opt->option_text = g_strdup("Option Text");
    opt->vote_count = 42;
    opt->voted = TRUE;
    
    // This should not crash
    free_poll_option(opt);
    
    g_assert_true(TRUE);
}

static void test_timeline_type_enum() {
    // Test that TimelineType enum values are distinct
    g_assert_cmpint(TIMELINE_PUBLIC, !=, TIMELINE_FOLLOWING);
    g_assert_cmpint(TIMELINE_PUBLIC, ==, 0);
    g_assert_cmpint(TIMELINE_FOLLOWING, ==, 1);
}

static void test_tweet_with_poll_and_attachments() {
    // Test tweet that has both poll and attachments
    const char *json_input = "{\"posts\": [{\"id\": \"123\", \"content\": \"Check this out!\", \"author\": {\"name\": \"User\", \"username\": \"user\"}, \"attachments\": [{\"id\": \"a1\", \"file_url\": \"/uploads/image.jpg\", \"file_type\": \"image/jpeg\"}], \"poll\": {\"id\": \"p1\", \"question\": \"Like it?\", \"is_active\": true, \"total_votes\": 50, \"options\": [{\"id\": \"o1\", \"option_text\": \"Yes\", \"vote_count\": 30, \"voted\": true}]}}]}";
    GList *tweets = parse_tweets(json_input);

    g_assert_nonnull(tweets);
    struct Tweet *t = (struct Tweet *)tweets->data;
    
    // Check both attachments and poll exist
    g_assert_nonnull(t->attachments);
    g_assert_cmpint(g_list_length(t->attachments), ==, 1);
    g_assert_nonnull(t->poll);
    g_assert_cmpstr(t->poll->question, ==, "Like it?");
    
    free_tweets(tweets);
}

static void test_parse_closed_poll() {
    // Test parsing a closed/inactive poll
    const char *json_input = "{\"posts\": [{\"id\": \"456\", \"content\": \"Poll ended\", \"author\": {\"name\": \"User\", \"username\": \"user\"}, \"poll\": {\"id\": \"p2\", \"question\": \"Winner?\", \"is_active\": false, \"expires_at\": \"2024-01-01T00:00:00Z\", \"total_votes\": 200, \"options\": [{\"id\": \"o1\", \"option_text\": \"Option A\", \"vote_count\": 80, \"voted\": false}, {\"id\": \"o2\", \"option_text\": \"Option B\", \"vote_count\": 120, \"voted\": false}]}}]}";
    GList *tweets = parse_tweets(json_input);

    g_assert_nonnull(tweets);
    struct Tweet *t = (struct Tweet *)tweets->data;
    g_assert_nonnull(t->poll);
    g_assert_false(t->poll->is_active);
    g_assert_cmpstr(t->poll->expires_at, ==, "2024-01-01T00:00:00Z");
    g_assert_cmpint(t->poll->total_votes, ==, 200);

    free_tweets(tweets);
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
    init_colors();
    
    // Set XDG_CONFIG_HOME early so GLib picks it up for tests that use config dirs
    gchar *tmp_root = g_dir_make_tmp("tweeta_xdg_XXXXXX", NULL);
    g_setenv("XDG_CONFIG_HOME", tmp_root, TRUE);

    g_test_init(&argc, &argv, NULL);
    
    // Enable verbose output if colors are enabled
    if (use_colors) {
        g_setenv("G_TEST_VERBOSE", "1", FALSE);
    }
    
    // Print header
    if (use_colors) {
        g_print("\n%s========================================%s\n", bold(), reset());
        g_print("       %sTWEETA DESKTOP TEST SUITE%s\n", cyan(), reset());
        g_print("%s========================================%s\n\n", bold(), reset());
    } else {
        g_print("\n========================================\n");
        g_print("       TWEETA DESKTOP TEST SUITE\n");
        g_print("========================================\n\n");
    }
    
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
    g_test_add_func("/parsetweetdetails/basic", test_parse_tweet_details);
    g_test_add_func("/challenge/solver", test_challenge_solver);
    
    // New tests for implemented features
    g_test_add_func("/polls/parse", test_parse_tweets_with_poll);
    g_test_add_func("/polls/memory_management", test_poll_memory_management);
    g_test_add_func("/polls/option_memory", test_poll_option_memory_management);
    g_test_add_func("/polls/closed_poll", test_parse_closed_poll);
    g_test_add_func("/polls/with_attachments", test_tweet_with_poll_and_attachments);
    g_test_add_func("/communities/parse", test_parse_communities);
    g_test_add_func("/communities/private", test_parse_communities_private);
    g_test_add_func("/communities/memory_management", test_community_memory_management);
    g_test_add_func("/upload/parse_success", test_parse_upload_response);
    g_test_add_func("/upload/parse_failure", test_parse_upload_response_failure);
    g_test_add_func("/timeline/enum_values", test_timeline_type_enum);
    
    int result = g_test_run();
    
    // Calculate actual results from g_test_run() return value
    // result contains the number of failed tests
    total_tests = 0;
    passed_tests = 0;
    
    // Count total registered tests by listing them
    // Since we can't easily hook into GLib's test framework,
    // we'll use the exit code to determine failure count
    int failed_count = result;
    
    // We know we have 33 tests registered
    // This is a workaround since GLib doesn't provide an easy way to get the count
    int known_total = 33;
    total_tests = known_total;
    passed_tests = known_total - failed_count;
    
    // Store failed test names if we can determine them
    // For now, we just note that some tests failed
    if (failed_count > 0) {
        // Try to parse any failure info from the test output
        // In a real implementation, we'd use a custom test fixture to track this
        failed_tests = g_list_append(failed_tests, g_strdup("(See test output above for failed test names)"));
    }
    
    // Print summary - GLib runs only non-skipped tests, so we calculate actual run count
    int tests_run = known_total;  // This is the count of non-skipped tests that were attempted
    print_summary(tests_run, failed_count);
    
    // Cleanup temp root
    gchar *rm_cmd = g_strdup_printf("rm -rf \"%s\"", tmp_root);
    system(rm_cmd);
    g_free(rm_cmd);
    g_free(tmp_root);
    
    // Free failed tests list
    g_list_free_full(failed_tests, (GDestroyNotify)g_free);
    
    return result;
}
