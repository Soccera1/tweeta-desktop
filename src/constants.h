#ifndef CONSTANTS_H
#define CONSTANTS_H

#define API_BASE_URL "https://tweeta.tiago.zip/api"
#define BASE_DOMAIN "https://tweeta.tiago.zip"
#define AVATAR_SIZE 48
#define MEDIA_SIZE 400
#define PUBLIC_TWEETS_URL API_BASE_URL "/public-tweets"
#define LOGIN_URL API_BASE_URL "/auth/basic-login"
#define POST_TWEET_URL API_BASE_URL "/tweets/"
#define SEARCH_USERS_URL API_BASE_URL "/search/users"
#define SEARCH_POSTS_URL API_BASE_URL "/search/posts"
#define LIKE_TWEET_URL API_BASE_URL "/tweets/%s/like"
#define RETWEET_URL API_BASE_URL "/tweets/%s/retweet"
#define REACTION_URL API_BASE_URL "/tweets/%s/reaction"
#define BOOKMARK_ADD_URL API_BASE_URL "/bookmarks/add"
#define BOOKMARK_REMOVE_URL API_BASE_URL "/bookmarks/remove"
#define EMOJIS_URL API_BASE_URL "/emojis"

#endif // CONSTANTS_H
