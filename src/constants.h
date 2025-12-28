#ifndef CONSTANTS_H
#define CONSTANTS_H

#define API_BASE_URL "https://tweeta.tiago.zip/api"
#define BASE_DOMAIN "https://tweeta.tiago.zip"
#define AVATAR_SIZE 48
#define MEDIA_SIZE 400
#define PUBLIC_TWEETS_URL API_BASE_URL "/public-tweets"
#define LOGIN_URL API_BASE_URL "/auth/basic-login"
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

#endif // CONSTANTS_H
