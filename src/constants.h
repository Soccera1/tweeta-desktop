#ifndef CONSTANTS_H
#define CONSTANTS_H

#define API_BASE_URL "https://tweeta.tiago.zip/api"
#define BASE_DOMAIN "https://tweeta.tiago.zip"
#define AVATAR_SIZE 48
#define MEDIA_SIZE 400
#define PUBLIC_TWEETS_URL API_BASE_URL "/public-tweets"
#define LOGIN_URL API_BASE_URL "/auth/basic-login"
#define AUTH_ME_URL API_BASE_URL "/auth/me"
#define POST_TWEET_URL API_BASE_URL "/tweets/"
#define TWEET_DETAILS_URL API_BASE_URL "/tweets/%s"
#define SEARCH_USERS_URL API_BASE_URL "/search/users"
#define SEARCH_POSTS_URL API_BASE_URL "/search/posts"
#define NOTIFICATIONS_URL API_BASE_URL "/notifications"
#define NOTIFICATIONS_MARK_ALL_READ_URL API_BASE_URL "/notifications/mark-all-read"
#define LIKE_TWEET_URL API_BASE_URL "/tweets/%s/like"
#define RETWEET_URL API_BASE_URL "/tweets/%s/retweet"
#define REACTION_URL API_BASE_URL "/tweets/%s/reaction"
#define BOOKMARK_ADD_URL API_BASE_URL "/bookmarks/add"
#define BOOKMARK_REMOVE_URL API_BASE_URL "/bookmarks/remove"
#define EMOJIS_URL API_BASE_URL "/emojis"
#define DM_CONVERSATIONS_URL API_BASE_URL "/dm/conversations"
#define DM_MESSAGES_URL API_BASE_URL "/dm/conversations/%s"
#define DM_SEND_MESSAGE_URL API_BASE_URL "/dm/conversations/%s/messages"
#define DM_MARK_READ_URL API_BASE_URL "/dm/conversations/%s/read"
#define ADMIN_STATS_URL API_BASE_URL "/admin/stats"
#define ADMIN_USERS_URL API_BASE_URL "/admin/users"
#define ADMIN_POSTS_URL API_BASE_URL "/admin/posts"
#define CAP_CHALLENGE_URL API_BASE_URL "/auth/cap/challenge"
#define CAP_REDEEM_URL API_BASE_URL "/auth/cap/redeem"

// Profile/Follow API
#define PROFILE_FOLLOW_URL API_BASE_URL "/profile/%s/follow"
#define PROFILE_FOLLOWERS_URL API_BASE_URL "/profile/followers/%s"
#define PROFILE_FOLLOWING_URL API_BASE_URL "/profile/following/%s"
#define FOLLOWING_TIMELINE_URL API_BASE_URL "/timeline/following"

// Bookmarks API
#define BOOKMARKS_LIST_URL API_BASE_URL "/bookmarks"

// Blocking API
#define BLOCK_USER_URL API_BASE_URL "/blocking/block"
#define UNBLOCK_USER_URL API_BASE_URL "/blocking/unblock"
#define CHECK_BLOCK_URL API_BASE_URL "/blocking/check/%s"
#define MUTE_USER_URL API_BASE_URL "/blocking/mute"
#define UNMUTE_USER_URL API_BASE_URL "/blocking/unmute"
#define CHECK_MUTE_URL API_BASE_URL "/blocking/mute-check/%s"

// Polls API
#define POLL_VOTE_URL API_BASE_URL "/tweets/%s/poll/vote"

// Profile Edit API
#define UPDATE_PROFILE_URL API_BASE_URL "/profile/%s"
#define UPDATE_AVATAR_URL API_BASE_URL "/profile/%s/avatar"
#define UPDATE_BANNER_URL API_BASE_URL "/profile/%s/banner"

// Upload API
#define UPLOAD_URL API_BASE_URL "/upload/"

// Communities API
#define COMMUNITIES_LIST_URL API_BASE_URL "/communities"
#define COMMUNITY_JOIN_URL API_BASE_URL "/communities/%s/join"
#define COMMUNITY_LEAVE_URL API_BASE_URL "/communities/%s/leave"
#define COMMUNITY_TWEETS_URL API_BASE_URL "/communities/%s/tweets"

#endif // CONSTANTS_H
