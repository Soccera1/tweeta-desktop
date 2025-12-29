# Usage Guide

This guide explains how to use Tweeta Desktop and its various features.

## Starting the Application

You can start the application from your terminal or your desktop environment's application menu.

From the terminal:
```bash
./tweeta-desktop
```

## Main Interface

The interface consists of a header bar and a main content area that switches between different views.

### Header Bar

The header bar contains the following controls:
- **Search Bar**: Enter a query and press Enter to search for users and posts.
- **Back Button**: Appears when you are in Profile or Search views to return to the previous view.
- **Compose Button**: Opens a dialog to post a new tweet (requires login).
- **Notifications Button**: Opens your notification inbox (requires login).
- **Messages Button**: Opens your direct message conversations (requires login).
- **Refresh Button**: Refreshes the current timeline or view.
- **Login/Logout Button**: Click to log in or log out of your Tweetapus account.
- **User Label**: Displays your login status and username.

### Timeline View

The default view shows a list of recent public tweets. 
- **Infinite Scroll**: As you reach the bottom of the feed, more tweets will automatically load.
- Click on a **Username** to view that user's profile.
- Click the **Reply** button on a tweet to post a reply (requires login).

### Notifications View

Displays your recent interactions:
- **Likes, Retweets, Replies, Follows, Mentions, Quotes, and Reactions** are all shown here.
- **Unread Notifications** are highlighted with a light blue background.
- **Mark all as read**: Click this button in the action bar to clear all unread notification markers.
- Click the **Refresh** button in the header while in this view to check for new notifications.

### Profile View

Displays information about a specific user:
- **Name and Username**
- **Bio**
- **Stats**: Follower count, following count, and post count.
- **Tabs**: Switch between the user's **Tweets** and **Replies**.

### Search View

Displays search results categorized into two tabs:
- **Users**: Matching user accounts.
- **Tweets**: Matching posts.

### Direct Messages

View and participate in private conversations:
- **Conversations List**: Displays a list of all your active conversations with last message previews and unread counts.
- **Message View**: Click on a conversation to view the full message history.
- **Sending Messages**: Type your message in the entry at the bottom of a conversation and click **Send** or press **Enter**.
- **Unread Status**: New messages are automatically marked as read when you open the conversation.

## Posting a Tweet

1. Ensure you are logged in.
2. Click the **Compose** button in the header bar.
3. Enter your message in the dialog.
4. Click **Tweet** to post it.

## Replying to a Tweet

1. Ensure you are logged in.
2. Find the tweet you want to reply to and click its **Reply** button.
3. Enter your reply in the dialog.
4. Click **Reply** to post it.

## Session Persistence

Tweeta Desktop automatically saves your session when you log in. Your credentials (auth token and username) are stored in `~/.config/tweeta-desktop/session.json`. When you start the application again, it will attempt to log you in automatically. Logging out will delete this file.

## Admin Features

If you are logged in as a site administrator:
- **Add Note**: You will see a "Note" button on tweets. Click it to add a public "Note" or fact check to the post. This note will be visible to all users.
