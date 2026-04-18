#ifndef CONSTANTS_H
#define CONSTANTS_H

#define API_BASE_URL "https://tweeta.tiago.zip/api"
#define BASE_DOMAIN "https://tweeta.tiago.zip"

#define AVATAR_SIZE 48
#define MEDIA_SIZE 400

#define PUBLIC_TWEETS_URL API_BASE_URL "/public-tweets"
#define TIMELINE_URL API_BASE_URL "/timeline/"
#define LOGIN_URL API_BASE_URL "/auth/basic-login"
#define AUTH_ME_URL API_BASE_URL "/auth/me"
#define POST_TWEET_URL API_BASE_URL "/tweets/"
#define TWEET_DETAILS_URL API_BASE_URL "/tweets/%s"
#define TWEET_LIKES_URL API_BASE_URL "/tweets/%s/likes"
#define TWEET_RETWEETS_URL API_BASE_URL "/tweets/%s/retweets"
#define TWEET_QUOTES_URL API_BASE_URL "/tweets/%s/quotes"
#define TWEET_REACTIONS_URL API_BASE_URL "/tweets/%s/reactions"
#define TWEET_EDIT_URL API_BASE_URL "/tweets/%s"
#define TWEET_EDIT_HISTORY_URL API_BASE_URL "/tweets/%s/edit-history"
#define TWEET_DELETE_URL API_BASE_URL "/tweets/%s"

#define SEARCH_USERS_URL API_BASE_URL "/search/users"
#define SEARCH_POSTS_URL API_BASE_URL "/search/posts"

#define NOTIFICATIONS_URL API_BASE_URL "/notifications"
#define NOTIFICATIONS_MARK_ALL_READ_URL API_BASE_URL "/notifications/mark-all-read"
#define NOTIFICATIONS_UNREAD_COUNT_URL API_BASE_URL "/notifications/unread-count"
#define NOTIFICATION_READ_URL API_BASE_URL "/notifications/%s/read"

#define LIKE_TWEET_URL API_BASE_URL "/tweets/%s/like"
#define RETWEET_URL API_BASE_URL "/tweets/%s/retweet"
#define REACTION_URL API_BASE_URL "/tweets/%s/reaction"

#define BOOKMARK_ADD_URL API_BASE_URL "/bookmarks/add"
#define BOOKMARK_REMOVE_URL API_BASE_URL "/bookmarks/remove"
#define BOOKMARKS_LIST_URL API_BASE_URL "/bookmarks"

#define EMOJIS_URL API_BASE_URL "/emojis"

#define DM_CONVERSATIONS_URL API_BASE_URL "/dm/conversations"
#define DM_MESSAGES_URL API_BASE_URL "/dm/conversations/%s"
#define DM_SEND_MESSAGE_URL API_BASE_URL "/dm/conversations/%s/messages"
#define DM_MARK_READ_URL API_BASE_URL "/dm/conversations/%s/read"
#define DM_CREATE_CONVERSATION_URL API_BASE_URL "/dm/conversations"
#define DM_ADD_PARTICIPANTS_URL API_BASE_URL "/dm/conversations/%s/participants"
#define DM_REMOVE_PARTICIPANT_URL API_BASE_URL "/dm/conversations/%s/participants/%s"
#define DM_UPDATE_TITLE_URL API_BASE_URL "/dm/conversations/%s/title"
#define DM_TYPING_URL API_BASE_URL "/dm/conversations/%s/typing"
#define DM_TYPING_STOP_URL API_BASE_URL "/dm/conversations/%s/typing-stop"
#define DM_DISAPPEARING_URL API_BASE_URL "/dm/conversations/%s/disappearing"
#define DM_MESSAGE_REACTIONS_URL API_BASE_URL "/dm/messages/%s/reactions"
#define DM_MESSAGE_EDIT_URL API_BASE_URL "/dm/messages/%s"
#define DM_MESSAGE_DELETE_URL API_BASE_URL "/dm/messages/%s"

#define ADMIN_STATS_URL API_BASE_URL "/admin/stats"
#define ADMIN_USERS_URL API_BASE_URL "/admin/users"
#define ADMIN_POSTS_URL API_BASE_URL "/admin/posts"
#define ADMIN_FACT_CHECK_URL API_BASE_URL "/admin/fact-check/%s"

#define CAP_CHALLENGE_URL API_BASE_URL "/auth/cap/challenge"
#define CAP_REDEEM_URL API_BASE_URL "/auth/cap/redeem"
#define CAP_RATE_LIMIT_BYPASS_URL API_BASE_URL "/auth/cap/rate-limit-bypass"

#define PROFILE_URL API_BASE_URL "/profile/%s"
#define PROFILE_FOLLOW_URL API_BASE_URL "/profile/%s/follow"
#define PROFILE_FOLLOWERS_URL API_BASE_URL "/profile/%s/followers"
#define PROFILE_FOLLOWING_URL API_BASE_URL "/profile/%s/following"
#define PROFILE_MUTUALS_URL API_BASE_URL "/profile/%s/mutuals"
#define PROFILE_POSTS_URL API_BASE_URL "/profile/%s/posts"
#define PROFILE_REPLIES_URL API_BASE_URL "/profile/%s/replies"
#define PROFILE_MEDIA_URL API_BASE_URL "/profile/%s/media"
#define PROFILE_NOTIFY_TWEETS_URL API_BASE_URL "/profile/%s/notify-tweets"
#define PROFILE_DELETE_AVATAR_URL API_BASE_URL "/profile/%s/avatar"
#define PROFILE_DELETE_BANNER_URL API_BASE_URL "/profile/%s/banner"
#define PROFILE_PIN_TWEET_URL API_BASE_URL "/profile/%s/pin/%s"
#define PROFILE_PIN_GLOBAL_URL API_BASE_URL "/profile/pin/%s"
#define FOLLOWING_TIMELINE_URL API_BASE_URL "/timeline/following"

#define BLOCK_USER_URL API_BASE_URL "/blocking/block"
#define UNBLOCK_USER_URL API_BASE_URL "/blocking/unblock"
#define CHECK_BLOCK_URL API_BASE_URL "/blocking/check/%s"
#define MUTE_USER_URL API_BASE_URL "/blocking/mute"
#define UNMUTE_USER_URL API_BASE_URL "/blocking/unmute"
#define CHECK_MUTE_URL API_BASE_URL "/blocking/check-mute/%s"

#define POLL_VOTE_URL API_BASE_URL "/tweets/%s/poll/vote"

#define UPDATE_PROFILE_URL API_BASE_URL "/profile/%s"
#define UPDATE_AVATAR_URL API_BASE_URL "/profile/%s/avatar"
#define UPDATE_BANNER_URL API_BASE_URL "/profile/%s/banner"

#define UPLOAD_URL API_BASE_URL "/upload/"

#define COMMUNITIES_LIST_URL API_BASE_URL "/communities"
#define COMMUNITY_DETAILS_URL API_BASE_URL "/communities/%s"
#define COMMUNITY_MEMBERS_URL API_BASE_URL "/communities/%s/members"
#define COMMUNITY_ACCESS_MODE_URL API_BASE_URL "/communities/%s/access-mode"
#define COMMUNITY_JOIN_URL API_BASE_URL "/communities/%s/join"
#define COMMUNITY_LEAVE_URL API_BASE_URL "/communities/%s/leave"
#define COMMUNITY_TWEETS_URL API_BASE_URL "/communities/%s/tweets"

#endif /* CONSTANTS_H */
