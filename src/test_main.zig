const std = @import("std");

const c = @import("c.zig").c;
const api = @import("api.zig");
const g = @import("globals_import.zig");
const types = @import("types.zig");

const TRUE: c.gboolean = 1;
const FALSE: c.gboolean = 0;
const null_gchar: [*c]const c.gchar = null;
const COLOR_RESET = "\x1b[0m";
const COLOR_GREEN = "\x1b[32m";
const COLOR_RED = "\x1b[31m";
const COLOR_YELLOW = "\x1b[33m";
const COLOR_BLUE = "\x1b[34m";
const COLOR_CYAN = "\x1b[36m";
const COLOR_BOLD = "\x1b[1m";
const TEST_PASS_MSG = "[PASS]";
const TEST_FAIL_MSG = "[FAIL]";

var use_colors: c.gboolean = FALSE;
var failed_tests: [*c]c.GList = null;

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn logMsg(level: c.GLogLevelFlags, comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), level, lit(fmt) } ++ args);
}

fn initColors() void {
    use_colors = if (std.posix.isatty(std.posix.STDOUT_FILENO)) TRUE else FALSE;
}

fn green() [*c]const c.gchar {
    return if (use_colors != FALSE) COLOR_GREEN else "";
}

fn red() [*c]const c.gchar {
    return if (use_colors != FALSE) COLOR_RED else "";
}

fn cyan() [*c]const c.gchar {
    return if (use_colors != FALSE) COLOR_CYAN else "";
}

fn bold() [*c]const c.gchar {
    return if (use_colors != FALSE) COLOR_BOLD else "";
}

fn reset() [*c]const c.gchar {
    return if (use_colors != FALSE) COLOR_RESET else "";
}

fn expect(ok: bool) void {
    std.debug.assert(ok);
}

fn expectTrue(value: c.gboolean) void {
    expect(value != FALSE);
}

fn expectFalse(value: c.gboolean) void {
    expect(value == FALSE);
}

fn expectStr(actual: [*c]const c.gchar, expected: [*c]const c.gchar) void {
    expect(c.g_strcmp0(actual, expected) == 0);
}

fn expectInt(actual: anytype, expected: anytype) void {
    expect(actual == expected);
}

fn listLength(list: [*c]c.GList) c.guint {
    return c.g_list_length(list);
}

fn listData(comptime T: type, list: [*c]c.GList) [*c]T {
    return @ptrCast(@alignCast(list.*.data));
}

fn nthData(comptime T: type, list: [*c]c.GList, n: c.guint) [*c]T {
    return @ptrCast(@alignCast(c.g_list_nth_data(list, n)));
}

fn resetSessionGlobals() void {
    c.g_free(g.g_auth_token);
    c.g_free(g.g_current_username);
    g.g_auth_token = null;
    g.g_current_username = null;
    g.g_is_admin = FALSE;
}

fn testParseTweets() callconv(.c) void {
    const json_input = "{\"posts\": [{\"id\": \"123\", \"content\": \"Hello world\", \"author\": {\"name\": \"Test User\", \"username\": \"testuser\", \"avatar\": \"/api/uploads/avatar.png\"}}]}";
    const tweets = api.parse_tweets(json_input);
    expect(tweets != null);
    expectInt(listLength(tweets), 1);
    const t = listData(types.Tweet, tweets);
    expectStr(t.*.content, "Hello world");
    expectStr(t.*.author_name, "Test User");
    expectStr(t.*.author_username, "testuser");
    expectStr(t.*.author_avatar, "/api/uploads/avatar.png");
    expectStr(t.*.id, "123");
    api.free_tweets(tweets);
}

fn testParseTweetsWithNote() callconv(.c) void {
    const json_input = "{\"posts\": [{\"id\": \"123\", \"content\": \"Fake news\", \"author\": {\"name\": \"User\", \"username\": \"u\"}, \"fact_check\": {\"note\": \"This is false.\", \"severity\": \"warning\"}}]}";
    const tweets = api.parse_tweets(json_input);
    expect(tweets != null);
    const t = listData(types.Tweet, tweets);
    expectStr(t.*.content, "Fake news");
    expect(t.*.note != null);
    expectStr(t.*.note, "This is false.");
    expectStr(t.*.note_severity, "warning");
    api.free_tweets(tweets);
}

fn testParseTweetsWithDangerNote() callconv(.c) void {
    const json_input = "{\"posts\": [{\"id\": \"124\", \"content\": \"Very fake news\", \"author\": {\"name\": \"User\", \"username\": \"u\"}, \"fact_check\": {\"note\": \"Danger!\", \"severity\": \"danger\"}}]}";
    const tweets = api.parse_tweets(json_input);
    expect(tweets != null);
    const t = listData(types.Tweet, tweets);
    expectStr(t.*.note_severity, "danger");
    api.free_tweets(tweets);
}

fn testParseTweetsWithInfoNote() callconv(.c) void {
    const json_input = "{\"posts\": [{\"id\": \"125\", \"content\": \"Context needed\", \"author\": {\"name\": \"User\", \"username\": \"u\"}, \"fact_check\": {\"note\": \"Some info.\", \"severity\": \"info\"}}]}";
    const tweets = api.parse_tweets(json_input);
    expect(tweets != null);
    const t = listData(types.Tweet, tweets);
    expectStr(t.*.note_severity, "info");
    api.free_tweets(tweets);
}

fn testTweetsResponseIsEmpty() callconv(.c) void {
    expectTrue(api.tweets_response_is_empty("{\"timeline\": []}"));
    expectFalse(api.tweets_response_is_empty("{\"timeline\": [{\"id\": \"1\", \"content\": \"Hello\", \"author\": {\"name\": \"User\", \"username\": \"u\"}}]}"));
    expectFalse(api.tweets_response_is_empty("{\"error\": \"bad request\"}"));
}

fn testProfileRepliesResponseIsEmpty() callconv(.c) void {
    expectTrue(api.profile_replies_response_is_empty("{\"replies\": []}"));
    expectFalse(api.profile_replies_response_is_empty("{\"replies\": [{\"id\": \"1\", \"content\": \"Reply\", \"author\": {\"name\": \"User\", \"username\": \"u\"}}]}"));
    expectFalse(api.profile_replies_response_is_empty("{\"error\": \"bad request\"}"));
}

fn testParseLoginResponse() callconv(.c) void {
    var token: [*c]c.gchar = null;
    var username: [*c]c.gchar = null;
    var is_admin: c.gboolean = FALSE;

    expectTrue(api.parse_login_response("{\"token\": \"abc123token\", \"user\": {\"id\": \"1\", \"username\": \"validuser\", \"admin\": true}}", &token, &username, &is_admin));
    expectStr(token, "abc123token");
    expectStr(username, "validuser");
    expectTrue(is_admin);
    c.g_free(token);
    c.g_free(username);
    token = null;
    username = null;

    expectTrue(api.parse_login_response("{\"token\": \"abc123token\", \"user\": {\"id\": \"1\", \"username\": \"validuser\", \"admin\": false}}", &token, &username, &is_admin));
    expectFalse(is_admin);
    c.g_free(token);
    c.g_free(username);
    token = null;
    username = null;

    expectTrue(api.parse_login_response("{\"token\": \"abc123token\", \"user\": {\"id\": \"1\", \"username\": \"validuser\"}}", &token, &username, &is_admin));
    expectFalse(is_admin);
    c.g_free(token);
    c.g_free(username);
    token = null;
    username = null;

    expectFalse(api.parse_login_response("{\"user\": {\"username\": \"validuser\"}}", &token, &username, &is_admin));
    expect(token == null);
    expect(username == null);
    expectFalse(api.parse_login_response("{\"token\": \"abc123token\"}", &token, &username, &is_admin));
    expectFalse(api.parse_login_response("{ token: invalid }", &token, &username, &is_admin));
}

fn testConstructTweetPayload() callconv(.c) void {
    var payload = api.construct_tweet_payload("Hello world", null, null);
    expect(payload != null);
    var parser = c.json_parser_new();
    var err: ?*c.GError = null;
    expectTrue(c.json_parser_load_from_data(parser, payload, -1, &err));
    expect(err == null);
    var root = c.json_parser_get_root(parser);
    var obj = c.json_node_get_object(root);
    expectTrue(c.json_object_has_member(obj, "content"));
    expectStr(c.json_object_get_string_member(obj, "content"), "Hello world");
    expectFalse(c.json_object_has_member(obj, "reply_to"));
    c.g_object_unref(parser);
    c.g_free(payload);

    payload = api.construct_tweet_payload("Reply text", "12345", null);
    parser = c.json_parser_new();
    err = null;
    expectTrue(c.json_parser_load_from_data(parser, payload, -1, &err));
    root = c.json_parser_get_root(parser);
    obj = c.json_node_get_object(root);
    expectStr(c.json_object_get_string_member(obj, "content"), "Reply text");
    expectTrue(c.json_object_has_member(obj, "reply_to"));
    expectStr(c.json_object_get_string_member(obj, "reply_to"), "12345");
    c.g_object_unref(parser);
    c.g_free(payload);
}

fn testSessionPersistence() callconv(.c) void {
    const tmp_dir = c.g_getenv("XDG_CONFIG_HOME");
    expect(tmp_dir != null);
    resetSessionGlobals();

    const app_dir = c.g_build_filename(tmp_dir, "tweeta-desktop", null_gchar);
    const expected_path = c.g_build_filename(app_dir, "session.json", null_gchar);
    defer c.g_free(app_dir);
    defer c.g_free(expected_path);
    _ = c.g_unlink(expected_path);
    _ = c.g_mkdir_with_parents(app_dir, 0o700);

    api.save_session("test_token_123", "test_user_abc", TRUE);
    var file_exists = false;
    var i: usize = 0;
    while (i < 5) : (i += 1) {
        if (c.g_file_test(expected_path, c.G_FILE_TEST_EXISTS) != FALSE) {
            file_exists = true;
            break;
        }
        c.g_usleep(100000);
    }
    expect(file_exists);

    resetSessionGlobals();
    api.load_session();
    expectStr(g.g_auth_token, "test_token_123");
    expectStr(g.g_current_username, "test_user_abc");
    expectTrue(g.g_is_admin);

    api.clear_session();
    expectFalse(c.g_file_test(expected_path, c.G_FILE_TEST_EXISTS));
    resetSessionGlobals();
}

fn testParseProfile() callconv(.c) void {
    const json_input = "{\"profile\": {\"name\": \"Test User\", \"username\": \"testuser\", \"bio\": \"This is a test bio\", \"avatar\": \"/api/uploads/profile.png\", \"banner\": \"/api/uploads/banner.png\", \"location\": \"Melbourne\", \"website\": \"https://example.com\", \"pronouns\": \"she/her\", \"theme\": \"dark\", \"accent_color\": \"#1d9bf0\", \"label_type\": \"commentary\", \"label_automated\": true, \"notifyTweets\": true, \"avatar_radius\": 12, \"verified\": 1, \"gold\": 0, \"gray\": 1, \"blockedByProfile\": true, \"blockedProfile\": false, \"follower_count\": 100, \"following_count\": 50, \"post_count\": 10}, \"isFollowing\": true, \"followsMe\": true, \"isOwnProfile\": false}";
    const p = api.parse_profile(json_input);
    expect(p != null);
    expectStr(p.*.name, "Test User");
    expectStr(p.*.username, "testuser");
    expectStr(p.*.bio, "This is a test bio");
    expectStr(p.*.avatar, "/api/uploads/profile.png");
    expectStr(p.*.banner, "/api/uploads/banner.png");
    expectStr(p.*.location, "Melbourne");
    expectStr(p.*.website, "https://example.com");
    expectStr(p.*.pronouns, "she/her");
    expectStr(p.*.theme, "dark");
    expectStr(p.*.accent_color, "#1d9bf0");
    expectStr(p.*.label_type, "commentary");
    expectInt(p.*.follower_count, 100);
    expectInt(p.*.following_count, 50);
    expectInt(p.*.post_count, 10);
    expectInt(p.*.avatar_radius, 12);
    expectTrue(p.*.is_following);
    expectTrue(p.*.follows_me);
    expectFalse(p.*.is_own_profile);
    expectTrue(p.*.blocked_by_profile);
    expectFalse(p.*.blocked_profile);
    expectTrue(p.*.notify_tweets);
    expectTrue(p.*.label_automated);
    expectTrue(p.*.author_verified);
    expectFalse(p.*.author_gold);
    expectTrue(p.*.author_gray);
    api.free_user(p);
}

fn testParseProfileReplies() callconv(.c) void {
    const tweets = api.parse_profile_replies("{\"replies\": [{\"id\": \"456\", \"content\": \"Test reply\", \"author\": {\"name\": \"Replier\", \"username\": \"replier\", \"avatar\": \"/api/uploads/reply.png\"}}]}");
    expect(tweets != null);
    expectInt(listLength(tweets), 1);
    const t = listData(types.Tweet, tweets);
    expectStr(t.*.content, "Test reply");
    expectStr(t.*.author_name, "Replier");
    expectStr(t.*.author_username, "replier");
    expectStr(t.*.author_avatar, "/api/uploads/reply.png");
    expectStr(t.*.id, "456");
    api.free_tweets(tweets);
}

fn testParseUsers() callconv(.c) void {
    const users = api.parse_users("{\"users\": [{\"username\": \"testuser\", \"name\": \"Test User\", \"bio\": \"Test Bio\", \"avatar\": \"/api/uploads/user.png\", \"follower_count\": 123}]}");
    expect(users != null);
    expectInt(listLength(users), 1);
    const u = listData(types.Profile, users);
    expectStr(u.*.username, "testuser");
    expectStr(u.*.name, "Test User");
    expectStr(u.*.bio, "Test Bio");
    expectStr(u.*.avatar, "/api/uploads/user.png");
    expectInt(u.*.follower_count, 123);
    api.free_users(users);
}

fn testParseUsersMutuals() callconv(.c) void {
    const users = api.parse_users("{\"mutuals\": [{\"id\": \"u2\", \"username\": \"mutual\", \"name\": \"Mutual User\", \"bio\": \"Known account\"}]}");
    expect(users != null);
    expectInt(listLength(users), 1);
    const u = listData(types.Profile, users);
    expectStr(u.*.id, "u2");
    expectStr(u.*.username, "mutual");
    expectStr(u.*.name, "Mutual User");
    api.free_users(users);
}

fn testParseNotifications() callconv(.c) void {
    const notifications = api.parse_notifications("{\"notifications\": [{\"id\": \"n1\", \"type\": \"like\", \"content\": \"liked your tweet\", \"related_id\": \"t1\", \"actor_id\": \"u1\", \"actor_username\": \"actor\", \"actor_name\": \"Actor Name\", \"actor_avatar\": \"/api/uploads/avatar.png\", \"read\": false, \"created_at\": \"2023-10-27T10:00:00Z\"}]}");
    expect(notifications != null);
    expectInt(listLength(notifications), 1);
    const n = listData(types.Notification, notifications);
    expectStr(n.*.id, "n1");
    expectStr(n.*.type, "like");
    expectStr(n.*.content, "liked your tweet");
    expectStr(n.*.related_id, "t1");
    expectStr(n.*.actor_id, "u1");
    expectStr(n.*.actor_username, "actor");
    expectStr(n.*.actor_name, "Actor Name");
    expectStr(n.*.actor_avatar, "/api/uploads/avatar.png");
    expectFalse(n.*.read);
    expectStr(n.*.created_at, "2023-10-27T10:00:00Z");
    api.free_notifications(notifications);
}

fn testParseTweetsWithAttachments() callconv(.c) void {
    const tweets = api.parse_tweets("{\"posts\": [{\"id\": \"123\", \"content\": \"Hello with media\", \"author\": {\"name\": \"Test User\", \"username\": \"testuser\", \"avatar\": \"/api/uploads/avatar.png\"}, \"attachments\": [{\"id\": \"a1\", \"file_url\": \"/api/uploads/image.jpg\", \"file_type\": \"image/jpeg\"}, {\"id\": \"v1\", \"file_url\": \"/api/uploads/video.mp4\", \"file_type\": \"video/mp4\"}]}]}");
    expect(tweets != null);
    expectInt(listLength(tweets), 1);
    const t = listData(types.Tweet, tweets);
    expectStr(t.*.content, "Hello with media");
    expect(t.*.attachments != null);
    expectInt(listLength(t.*.attachments), 2);
    const a1 = listData(types.Attachment, t.*.attachments);
    expectStr(a1.*.id, "a1");
    expectStr(a1.*.file_url, "/api/uploads/image.jpg");
    expectStr(a1.*.file_type, "image/jpeg");
    const v1 = listData(types.Attachment, t.*.attachments.*.next);
    expectStr(v1.*.id, "v1");
    expectStr(v1.*.file_url, "/api/uploads/video.mp4");
    expectStr(v1.*.file_type, "video/mp4");
    api.free_tweets(tweets);
}

fn testParseTweetsSkipsInvalidPostChildren() callconv(.c) void {
    const json_input =
        "{\"posts\": [null, {\"id\": \"123\",\"content\": \"Hello with media\",\"author\": {\"name\": \"Test User\", \"username\": \"testuser\"},\"attachments\": [null, {\"id\": \"a1\", \"file_url\": \"/api/uploads/image.jpg\", \"file_type\": \"image/jpeg\"}],\"poll\": {\"id\": \"poll1\",\"question\": \"Favorite color?\",\"is_active\": true,\"options\": [null, {\"id\": \"opt1\", \"option_text\": \"Red\", \"vote_count\": 30, \"voted\": false}]}}]}";
    const tweets = api.parse_tweets(json_input);
    expect(tweets != null);
    expectInt(listLength(tweets), 1);
    const t = listData(types.Tweet, tweets);
    expectStr(t.*.id, "123");
    expect(t.*.attachments != null);
    expectInt(listLength(t.*.attachments), 1);
    expect(t.*.poll != null);
    expect(t.*.poll.*.options != null);
    expectInt(listLength(t.*.poll.*.options), 1);
    api.free_tweets(tweets);
}

fn testParseConversations() callconv(.c) void {
    const convs = api.parse_conversations("{\"conversations\": [{\"id\": \"c1\", \"type\": \"direct\", \"displayName\": \"Test User\", \"displayAvatar\": \"/avatar.png\", \"last_message_content\": \"Hello\", \"last_message_time\": \"2023-10-27T10:00:00Z\", \"unread_count\": 1}]}");
    expect(convs != null);
    expectInt(listLength(convs), 1);
    const conv = listData(types.Conversation, convs);
    expectStr(conv.*.id, "c1");
    expectStr(conv.*.type, "direct");
    expectStr(conv.*.display_name, "Test User");
    expectStr(conv.*.display_avatar, "/avatar.png");
    expectStr(conv.*.last_message_content, "Hello");
    expectInt(conv.*.unread_count, 1);
    api.free_conversations(convs);
}

fn testParseMessages() callconv(.c) void {
    const msgs = api.parse_messages("{\"messages\": [{\"id\": \"m1\", \"conversation_id\": \"c1\", \"sender_id\": \"u1\", \"content\": \"Hello\", \"username\": \"testuser\", \"name\": \"Test User\", \"avatar\": \"/avatar.png\", \"created_at\": \"2023-10-27T10:00:00Z\"}]}");
    expect(msgs != null);
    expectInt(listLength(msgs), 1);
    const msg = listData(types.DirectMessage, msgs);
    expectStr(msg.*.id, "m1");
    expectStr(msg.*.content, "Hello");
    expectStr(msg.*.username, "testuser");
    api.free_messages(msgs);
}

fn testParseMessagesExtended() callconv(.c) void {
    const msgs = api.parse_messages(
        "{" ++
            "\"conversation\": {" ++
            "\"messages\": [{" ++
            "\"id\": \"m2\"," ++
            "\"conversation_id\": \"c1\"," ++
            "\"sender_id\": \"u2\"," ++
            "\"content\": \"Updated message\"," ++
            "\"username\": \"editor\"," ++
            "\"name\": \"Editor\"," ++
            "\"created_at\": \"2024-01-01T00:00:00Z\"," ++
            "\"deleted_at\": \"2024-01-01T01:00:00Z\"," ++
            "\"reply_to\": \"m1\"," ++
            "\"reply_to_message\": {\"content\": \"Original\"}," ++
            "\"reactions\": [{\"emoji\": \"😀\", \"count\": 2}]" ++
            "}]}}",
    );
    expect(msgs != null);
    expectInt(listLength(msgs), 1);
    const msg = listData(types.DirectMessage, msgs);
    expectTrue(msg.*.is_deleted);
    expectStr(msg.*.reply_to, "m1");
    expectStr(msg.*.reply_preview, "Original");
    expectStr(msg.*.reactions_summary, "😀 2");
    api.free_messages(msgs);
}

fn testParseTweetDetails() callconv(.c) void {
    const tweets = api.parse_tweet_details("{\"tweet\": {\"id\": \"main\", \"content\": \"Main tweet\", \"author\": {\"name\": \"User\", \"username\": \"user\"}},\"threadPosts\": [{\"id\": \"parent\", \"content\": \"Parent tweet\", \"author\": {\"name\": \"Parent\", \"username\": \"parent\"}}],\"replies\": [{\"id\": \"reply\", \"content\": \"Reply tweet\", \"author\": {\"name\": \"Replier\", \"username\": \"replier\"}}]}");
    expect(tweets != null);
    expectInt(listLength(tweets), 3);
    expectStr(nthData(types.Tweet, tweets, 0).*.id, "parent");
    expectStr(nthData(types.Tweet, tweets, 1).*.id, "main");
    expectStr(nthData(types.Tweet, tweets, 2).*.id, "reply");
    api.free_tweets(tweets);
}

fn testChallengeSolver() callconv(.c) void {
    const token = c.g_strdup_printf("testtoken-%u", c.g_random_int());
    defer c.g_free(token);
    const solutions_json = api.solve_challenge("{\"c\": 1, \"s\": 8, \"d\": 2}", token);
    defer c.g_free(solutions_json);
    expect(solutions_json != null);
    const parser = c.json_parser_new();
    defer c.g_object_unref(parser);
    expectTrue(c.json_parser_load_from_data(parser, solutions_json, -1, null));
    const root = c.json_parser_get_root(parser);
    expect(c.JSON_NODE_HOLDS_ARRAY(root));
    const array = c.json_node_get_array(root);
    expectInt(c.json_array_get_length(array), 1);
    const nonce = c.json_array_get_int_element(array, 0);
    logMsg(c.G_LOG_LEVEL_MESSAGE, "Solved random challenge with token '%s', nonce: %ld", .{ token, @as(c_long, @intCast(nonce)) });
}

fn testParseTweetsWithPoll() callconv(.c) void {
    const tweets = api.parse_tweets("{\"posts\": [{\"id\": \"123\", \"content\": \"What do you think?\", \"author\": {\"name\": \"Test User\", \"username\": \"testuser\"}, \"poll\": {\"id\": \"poll1\", \"question\": \"Favorite color?\", \"is_active\": true, \"expires_at\": \"2024-12-31T23:59:59Z\", \"total_votes\": 100, \"options\": [{\"id\": \"opt1\", \"option_text\": \"Red\", \"vote_count\": 30, \"voted\": false}, {\"id\": \"opt2\", \"option_text\": \"Blue\", \"vote_count\": 70, \"voted\": true}]}}]}");
    expect(tweets != null);
    expectInt(listLength(tweets), 1);
    const t = listData(types.Tweet, tweets);
    expectStr(t.*.content, "What do you think?");
    expect(t.*.poll != null);
    const poll = t.*.poll;
    expectStr(poll.*.id, "poll1");
    expectStr(poll.*.question, "Favorite color?");
    expectTrue(poll.*.is_active);
    expectStr(poll.*.expires_at, "2024-12-31T23:59:59Z");
    expectInt(poll.*.total_votes, 100);
    expect(poll.*.options != null);
    expectInt(listLength(poll.*.options), 2);
    const opt1 = listData(types.PollOption, poll.*.options);
    expectStr(opt1.*.id, "opt1");
    expectStr(opt1.*.option_text, "Red");
    expectInt(opt1.*.vote_count, 30);
    expectFalse(opt1.*.voted);
    const opt2 = listData(types.PollOption, poll.*.options.*.next);
    expectStr(opt2.*.id, "opt2");
    expectStr(opt2.*.option_text, "Blue");
    expectInt(opt2.*.vote_count, 70);
    expectTrue(opt2.*.voted);
    api.free_tweets(tweets);
}

fn testParseCommunities() callconv(.c) void {
    const communities = api.parse_communities("{\"communities\": [{\"id\": \"comm1\", \"name\": \"Tech Talk\", \"description\": \"Discussion about technology\", \"icon_url\": \"/uploads/comm1_icon.png\", \"banner_url\": \"/uploads/comm1_banner.png\", \"access_mode\": \"public\", \"member_count\": 1500, \"is_member\": true, \"is_admin\": false, \"is_moderator\": true}]}");
    expect(communities != null);
    expectInt(listLength(communities), 1);
    const community = listData(types.Community, communities);
    expectStr(community.*.id, "comm1");
    expectStr(community.*.name, "Tech Talk");
    expectStr(community.*.description, "Discussion about technology");
    expectStr(community.*.icon_url, "/uploads/comm1_icon.png");
    expectStr(community.*.banner_url, "/uploads/comm1_banner.png");
    expectStr(community.*.access_mode, "public");
    expectInt(community.*.member_count, 1500);
    expectTrue(community.*.is_member);
    expectFalse(community.*.is_admin);
    expectTrue(community.*.is_moderator);
    api.free_communities(communities);
}

fn testParseCommunitiesPrivate() callconv(.c) void {
    const communities = api.parse_communities("{\"communities\": [{\"id\": \"comm2\", \"name\": \"Private Club\", \"description\": \"Exclusive community\", \"access_mode\": \"private\", \"member_count\": 50, \"is_member\": false, \"is_admin\": false, \"is_moderator\": false}]}");
    expect(communities != null);
    const community = listData(types.Community, communities);
    expectStr(community.*.id, "comm2");
    expectStr(community.*.access_mode, "private");
    expectInt(community.*.member_count, 50);
    expectFalse(community.*.is_member);
    api.free_communities(communities);
}

fn testParseCommunityDetails() callconv(.c) void {
    const community = api.parse_community_details("{\"community\": {\"id\": \"comm3\", \"name\": \"Builders\", \"description\": \"Ship things\", \"rules\": \"Be useful\", \"access_mode\": \"locked\", \"member_count\": 12}}");
    expect(community != null);
    expectStr(community.*.id, "comm3");
    expectStr(community.*.name, "Builders");
    expectStr(community.*.rules, "Be useful");
    expectStr(community.*.access_mode, "locked");
    expectInt(community.*.member_count, 12);
    api.free_community(community);
}

fn testParseListsResponse() callconv(.c) void {
    var owned: [*c]c.GList = null;
    var followed: [*c]c.GList = null;
    expectTrue(api.parse_lists_response("{\"ownedLists\":[{\"id\":\"l1\",\"name\":\"Core\",\"description\":\"Main list\",\"owner_username\":\"alice\",\"member_count\":3,\"follower_count\":7,\"is_private\":true}],\"followedLists\":[{\"id\":\"l2\",\"name\":\"Followed\",\"member_count\":1,\"follower_count\":2}]}", &owned, &followed));
    expect(owned != null);
    expect(followed != null);
    expectInt(listLength(owned), 1);
    expectInt(listLength(followed), 1);
    const first = listData(types.TweetaList, owned);
    expectStr(first.*.id, "l1");
    expectStr(first.*.name, "Core");
    expectStr(first.*.description, "Main list");
    expectStr(first.*.owner_username, "alice");
    expectInt(first.*.member_count, 3);
    expectInt(first.*.follower_count, 7);
    expectTrue(first.*.is_private);
    expectStr(listData(types.TweetaList, followed).*.id, "l2");
    api.free_tweeta_lists(owned);
    api.free_tweeta_lists(followed);
}

fn testParseListDetails() callconv(.c) void {
    const list = api.parse_list_details_response("{\"list\":{\"id\":\"l3\",\"name\":\"Details\",\"description\":\"Expanded\",\"member_count\":2,\"follower_count\":5,\"owner\":{\"username\":\"owner\",\"name\":\"Owner Name\"}},\"isFollowing\":true,\"isOwner\":true,\"members\":[{\"id\":\"u1\",\"username\":\"member\",\"name\":\"Member Name\",\"verified\":true}]}");
    expect(list != null);
    expectStr(list.*.id, "l3");
    expectStr(list.*.owner_username, "owner");
    expectStr(list.*.owner_name, "Owner Name");
    expectTrue(list.*.is_following);
    expectTrue(list.*.is_owner);
    expect(list.*.members != null);
    const member = listData(types.Profile, list.*.members);
    expectStr(member.*.id, "u1");
    expectStr(member.*.username, "member");
    expectTrue(member.*.author_verified);
    api.free_tweeta_list(list);
}

fn testParseListFollowers() callconv(.c) void {
    const followers = api.parse_list_followers_response("{\"followers\":[{\"id\":\"u2\",\"username\":\"follower\",\"name\":\"Follower Name\",\"gold\":true}]}");
    expect(followers != null);
    expectInt(listLength(followers), 1);
    const follower = listData(types.Profile, followers);
    expectStr(follower.*.id, "u2");
    expectStr(follower.*.username, "follower");
    expectTrue(follower.*.author_gold);
    api.free_users(followers);
}

fn testParseUploadResponse() callconv(.c) void {
    const file_url = api.parse_upload_response("{\"file\": {\"url\": \"/api/uploads/test_image.webp\"}, \"success\": true}");
    defer c.g_free(file_url);
    expect(file_url != null);
    expectStr(file_url, "/api/uploads/test_image.webp");
}

fn testParseUploadResponseFailure() callconv(.c) void {
    const file_url = api.parse_upload_response("{\"error\": \"Invalid file type\", \"success\": false}");
    expect(file_url == null);
}

fn testBuildAttachmentList() callconv(.c) void {
    const attachments = api.build_attachment_list("/api/uploads/test.jpg", "image/jpeg");
    expect(attachments != null);
    expectInt(listLength(attachments), 1);
    const attach = listData(types.Attachment, attachments);
    expectStr(attach.*.file_url, "/api/uploads/test.jpg");
    expectStr(attach.*.file_type, "image/jpeg");
    expect(attach.*.id == null);
    c.g_list_free_full(attachments, api.free_attachment_payload);
}

fn testBuildAttachmentListNullUrl() callconv(.c) void {
    expect(api.build_attachment_list(null, "image/jpeg") == null);
}

fn testBuildAttachmentListDefaultType() callconv(.c) void {
    const attachments = api.build_attachment_list("/api/uploads/test.mp4", null);
    expect(attachments != null);
    const attach = listData(types.Attachment, attachments);
    expectStr(attach.*.file_type, "application/octet-stream");
    c.g_list_free_full(attachments, api.free_attachment_payload);
}

fn testFreeAttachmentPayload() callconv(.c) void {
    const attach: [*c]types.Attachment = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.Attachment))));
    attach.*.id = c.g_strdup("att123");
    attach.*.file_url = c.g_strdup("/api/uploads/image.jpg");
    attach.*.file_type = c.g_strdup("image/jpeg");
    api.free_attachment_payload(attach);
}

fn testBuildReplyBannerText() callconv(.c) void {
    const with_username = api.build_reply_banner_text("alice");
    const with_empty_username = api.build_reply_banner_text("");
    const with_null_username = api.build_reply_banner_text(null);
    defer c.g_free(with_username);
    defer c.g_free(with_empty_username);
    defer c.g_free(with_null_username);
    expectStr(with_username, "Replying to @alice:");
    expectStr(with_empty_username, "Replying to @unknown:");
    expectStr(with_null_username, "Replying to @unknown:");
}

fn testBuildAccountLabelText() callconv(.c) void {
    const with_both = api.build_account_label_text("Alice", "alice");
    const with_missing_name = api.build_account_label_text(null, "alice");
    const with_missing_username = api.build_account_label_text("Alice", null);
    const with_missing_both = api.build_account_label_text("", "");
    defer c.g_free(with_both);
    defer c.g_free(with_missing_name);
    defer c.g_free(with_missing_username);
    defer c.g_free(with_missing_both);
    expectStr(with_both, "Alice (@alice)");
    expectStr(with_missing_name, "Unknown (@alice)");
    expectStr(with_missing_username, "Alice (@unknown)");
    expectStr(with_missing_both, "Unknown (@unknown)");
}

fn testPollMemoryManagement() callconv(.c) void {
    const tweets = api.parse_tweets("{\"posts\": [{\"id\": \"1\", \"content\": \"Test\", \"author\": {\"name\": \"User\", \"username\": \"user\"}, \"poll\": {\"id\": \"p1\", \"question\": \"Q?\", \"is_active\": true, \"total_votes\": 10, \"options\": [{\"id\": \"o1\", \"option_text\": \"A\", \"vote_count\": 5, \"voted\": false}]}}]}");
    expect(tweets != null);
    const t = listData(types.Tweet, tweets);
    expect(t.*.poll != null);
    expect(t.*.poll.*.options != null);
    api.free_tweets(tweets);
}

fn testCommunityMemoryManagement() callconv(.c) void {
    const community: [*c]types.Community = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.Community))));
    community.*.id = c.g_strdup("test-id");
    community.*.name = c.g_strdup("Test Community");
    community.*.description = c.g_strdup("A test community");
    community.*.icon_url = c.g_strdup("/icon.png");
    community.*.banner_url = c.g_strdup("/banner.png");
    community.*.access_mode = c.g_strdup("public");
    community.*.member_count = 100;
    community.*.is_member = TRUE;
    community.*.is_admin = FALSE;
    community.*.is_moderator = FALSE;
    const communities = c.g_list_append(null, community);
    api.free_communities(communities);
}

fn testPollOptionMemoryManagement() callconv(.c) void {
    const opt: [*c]types.PollOption = @ptrCast(@alignCast(c.g_malloc0(@sizeOf(types.PollOption))));
    opt.*.id = c.g_strdup("opt1");
    opt.*.option_text = c.g_strdup("Option Text");
    opt.*.vote_count = 42;
    opt.*.voted = TRUE;
    api.free_poll_option(opt);
}

fn testTimelineTypeEnum() callconv(.c) void {
    expect(types.TimelineType.TIMELINE_PUBLIC != types.TimelineType.TIMELINE_FOLLOWING);
    expectInt(@intFromEnum(types.TimelineType.TIMELINE_PUBLIC), 0);
    expectInt(@intFromEnum(types.TimelineType.TIMELINE_FOLLOWING), 1);
}

fn testTweetWithPollAndAttachments() callconv(.c) void {
    const tweets = api.parse_tweets("{\"posts\": [{\"id\": \"123\", \"content\": \"Check this out!\", \"author\": {\"name\": \"User\", \"username\": \"user\"}, \"attachments\": [{\"id\": \"a1\", \"file_url\": \"/uploads/image.jpg\", \"file_type\": \"image/jpeg\"}], \"poll\": {\"id\": \"p1\", \"question\": \"Like it?\", \"is_active\": true, \"total_votes\": 50, \"options\": [{\"id\": \"o1\", \"option_text\": \"Yes\", \"vote_count\": 30, \"voted\": true}]}}]}");
    expect(tweets != null);
    const t = listData(types.Tweet, tweets);
    expect(t.*.attachments != null);
    expectInt(listLength(t.*.attachments), 1);
    expect(t.*.poll != null);
    expectStr(t.*.poll.*.question, "Like it?");
    api.free_tweets(tweets);
}

fn testParseTweetPinned() callconv(.c) void {
    const tweets = api.parse_tweets("{\"posts\": [{\"id\": \"789\", \"content\": \"Pinned post\", \"pinned\": 1, \"author\": {\"name\": \"User\", \"username\": \"user\"}}]}");
    expect(tweets != null);
    const t = listData(types.Tweet, tweets);
    expectTrue(t.*.pinned);
    api.free_tweets(tweets);
}

fn testParseClosedPoll() callconv(.c) void {
    const tweets = api.parse_tweets("{\"posts\": [{\"id\": \"456\", \"content\": \"Poll ended\", \"author\": {\"name\": \"User\", \"username\": \"user\"}, \"poll\": {\"id\": \"p2\", \"question\": \"Winner?\", \"is_active\": false, \"expires_at\": \"2024-01-01T00:00:00Z\", \"total_votes\": 200, \"options\": [{\"id\": \"o1\", \"option_text\": \"Option A\", \"vote_count\": 80, \"voted\": false}, {\"id\": \"o2\", \"option_text\": \"Option B\", \"vote_count\": 120, \"voted\": false}]}}]}");
    expect(tweets != null);
    const t = listData(types.Tweet, tweets);
    expect(t.*.poll != null);
    expectFalse(t.*.poll.*.is_active);
    expectStr(t.*.poll.*.expires_at, "2024-01-01T00:00:00Z");
    expectInt(t.*.poll.*.total_votes, 200);
    api.free_tweets(tweets);
}

fn testIntegrationLogin() callconv(.c) void {
    const username = c.g_getenv("USERNAME");
    const password = c.g_getenv("PASSWORD");
    if (username == null or password == null) {
        c.g_test_skip("Skipping integration test: USERNAME and PASSWORD not set");
        return;
    }
    resetSessionGlobals();
    expectTrue(api.perform_login(username, password));
    expect(g.g_auth_token != null);
    expectStr(g.g_current_username, username);
    c.g_print("\n[Integration] Logged in as %s. Admin status: %s\n", g.g_current_username, if (g.g_is_admin != FALSE) lit("TRUE") else lit("FALSE"));
}

fn addTest(path: [*c]const u8, func: c.GTestFunc) void {
    c.g_test_add_func(path, func);
}

fn printHeader() void {
    if (use_colors != FALSE) {
        c.g_print("\n%s========================================%s\n", bold(), reset());
        c.g_print("       %sTWEETA DESKTOP TEST SUITE%s\n", cyan(), reset());
        c.g_print("%s========================================%s\n\n", bold(), reset());
    } else {
        c.g_print("\n========================================\n");
        c.g_print("       TWEETA DESKTOP TEST SUITE\n");
        c.g_print("========================================\n\n");
    }
}

fn printSummary(result: c_int) void {
    const known_total: c_int = 41;
    const passed_count = known_total - result;
    if (result > 0) {
        failed_tests = c.g_list_append(failed_tests, c.g_strdup("(See test output above for failed test names)"));
    }

    c.g_print("\n%s========================================%s\n", bold(), reset());
    c.g_print("           %sTEST SUMMARY%s\n", bold(), reset());
    c.g_print("%s========================================%s\n", bold(), reset());
    c.g_print("\n");
    c.g_print("  Tests run:    %s%d%s\n", cyan(), known_total, reset());
    c.g_print("  Passed:       %s%d%s\n", green(), passed_count, reset());
    c.g_print("  Failed:       %s%d%s\n", if (result > 0) red() else green(), result, reset());
    c.g_print("\n");
    if (result > 0) {
        c.g_print("%sFailed tests:%s\n", red(), reset());
        var l = failed_tests;
        while (l != null) : (l = l.*.next) {
            c.g_print("  %s- %s%s\n", red(), @as([*c]const c.gchar, @ptrCast(l.*.data)), reset());
        }
        c.g_print("\n");
    }
    if (result == 0) {
        c.g_print("%sAll tests passed!%s\n", green(), reset());
    } else {
        c.g_print("%s%d test(s) failed.%s\n", red(), result, reset());
    }
    c.g_print("\n");
}

pub fn main() u8 {
    initColors();
    const tmp_root = c.g_dir_make_tmp("tweeta_xdg_XXXXXX", null);
    defer {
        if (tmp_root != null) {
            const rm_cmd = c.g_strdup_printf("rm -rf \"%s\"", tmp_root);
            if (c.system(rm_cmd) != 0) {
                logMsg(c.G_LOG_LEVEL_WARNING, "Failed to remove temporary directory: %s", .{tmp_root});
            }
            c.g_free(rm_cmd);
            c.g_free(tmp_root);
        }
        c.g_list_free_full(failed_tests, c.g_free);
    }
    _ = c.g_setenv("XDG_CONFIG_HOME", tmp_root, TRUE);

    var argc: c_int = @intCast(std.os.argv.len);
    var argv: [*c][*c]u8 = @ptrCast(std.os.argv.ptr);
    c.g_test_init(&argc, &argv, @as(?*anyopaque, null));
    if (use_colors != FALSE) {
        _ = c.g_setenv("G_TEST_VERBOSE", "1", FALSE);
    }
    printHeader();

    addTest("/integration/login", testIntegrationLogin);
    addTest("/parsetweets/basic", testParseTweets);
    addTest("/parsetweets/note", testParseTweetsWithNote);
    addTest("/parsetweets/note_danger", testParseTweetsWithDangerNote);
    addTest("/parsetweets/note_info", testParseTweetsWithInfoNote);
    addTest("/parsetweets/empty_response", testTweetsResponseIsEmpty);
    addTest("/parsetweets/attachments", testParseTweetsWithAttachments);
    addTest("/parsetweets/invalid_children", testParseTweetsSkipsInvalidPostChildren);
    addTest("/parselogin/basic", testParseLoginResponse);
    addTest("/constructpayload/basic", testConstructTweetPayload);
    addTest("/session/persistence", testSessionPersistence);
    addTest("/parseprofile/basic", testParseProfile);
    addTest("/parseprofile/replies", testParseProfileReplies);
    addTest("/parseprofile/replies_empty_response", testProfileRepliesResponseIsEmpty);
    addTest("/parseusers/basic", testParseUsers);
    addTest("/parseusers/mutuals", testParseUsersMutuals);
    addTest("/parsenotifications/basic", testParseNotifications);
    addTest("/parseconversations/basic", testParseConversations);
    addTest("/parsemessages/basic", testParseMessages);
    addTest("/parsemessages/extended", testParseMessagesExtended);
    addTest("/parsetweetdetails/basic", testParseTweetDetails);
    addTest("/challenge/solver", testChallengeSolver);
    addTest("/polls/parse", testParseTweetsWithPoll);
    addTest("/polls/memory_management", testPollMemoryManagement);
    addTest("/polls/option_memory", testPollOptionMemoryManagement);
    addTest("/polls/closed_poll", testParseClosedPoll);
    addTest("/polls/with_attachments", testTweetWithPollAndAttachments);
    addTest("/parsetweets/pinned", testParseTweetPinned);
    addTest("/communities/parse", testParseCommunities);
    addTest("/communities/private", testParseCommunitiesPrivate);
    addTest("/communities/details", testParseCommunityDetails);
    addTest("/communities/memory_management", testCommunityMemoryManagement);
    addTest("/upload/parse_success", testParseUploadResponse);
    addTest("/upload/parse_failure", testParseUploadResponseFailure);
    addTest("/upload/build_attachment_list", testBuildAttachmentList);
    addTest("/upload/build_attachment_list_null_url", testBuildAttachmentListNullUrl);
    addTest("/upload/build_attachment_list_default_type", testBuildAttachmentListDefaultType);
    addTest("/upload/free_attachment_payload", testFreeAttachmentPayload);
    addTest("/ui/reply_banner_text", testBuildReplyBannerText);
    addTest("/ui/account_label_text", testBuildAccountLabelText);
    addTest("/timeline/enum_values", testTimelineTypeEnum);

    const result = c.g_test_run();
    printSummary(result);
    return @intCast(result);
}
