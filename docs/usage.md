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
- **Bookmarks Button**: Opens your saved bookmarks (requires login).
- **Refresh Button**: Refreshes the current timeline or view.
- **Login/Logout Button**: Click to log in or log out of your Tweetapus account.
- **User Label**: Displays your login status and username.

### Timeline View

The default view shows a list of recent public tweets. 
- **Infinite Scroll**: As you reach the bottom of the feed, more tweets will automatically load.
- Click on a **Username** to view that user's profile.
- Click the **Reply** button on a tweet to post a reply (requires login).

### Interacting with Tweets

Each tweet has several interaction buttons at the bottom:
- **Like (♡)**: Click to like a tweet. The icon will change to a filled heart (♥) when liked.
- **Retweet (↻)**: Click to open a menu with two options:
    - **Retweet**: Share the tweet directly to your followers.
    - **Quote**: Open a dialog to add your own comments before sharing.
- **Reply (↩)**: Open a dialog to reply to the tweet.
- **Bookmark (☆)**: Save the tweet to your bookmarks for later.
- **React (😀)**: Open an emoji picker to add a reaction to the tweet. You can choose from standard emojis or custom Tweetapus emojis.

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

#### Editing Your Profile

When viewing your own profile (requires login):
- Click the **Edit Profile** button to modify your profile information.
- You can update your **Name**, **Bio**, **Avatar**, and **Banner** image.
- Click **Save** to apply your changes.

### Search View

Displays search results categorized into two tabs:
- **Users**: Matching user accounts.
- **Tweets**: Matching posts.

### Bookmarks View

Access your saved tweets for later reading:
- Click the **Bookmarks** button (bookmark icon) in the header bar to view all your bookmarked tweets (requires login).
- Bookmarked tweets are displayed in a timeline similar to the main timeline.
- Click the **Refresh** button to update your bookmarks list.

### Communities

Join and participate in topic-based communities (requires login):
- Click the **Communities** button in the header bar to browse available communities.
- Communities can be **public** (open to all), **private** (invite-only), or **restricted** (approval required).
- Click **Join** to become a member of a community.
- View community-specific timelines by selecting a community.
- Community members with moderator or admin privileges can manage community settings.

### Polls

Interact with polls on tweets:
- Polls appear inline within tweets with multiple choice options.
- **Voting**: Click on a poll option to cast your vote (requires login).
- **Results**: View real-time results showing vote counts and percentages for each option.
- **Active Polls**: Display the total vote count and remaining time.
- **Closed Polls**: Show final results when the poll has ended.

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
4. **Attach Media** (optional): Click the attachment button to add images or videos to your tweet.
5. **Create a Poll** (optional): Click the poll button to add a poll with multiple choice options.
6. Click **Tweet** to post it.

## Replying to a Tweet

1. Ensure you are logged in.
2. Find the tweet you want to reply to and click its **Reply** button.
3. Enter your reply in the dialog.
4. Click **Reply** to post it.

## Session Persistence

Tweeta Desktop automatically saves your session when you log in. Your credentials (auth token and username) are stored in `~/.config/tweeta-desktop/session.json`. When you start the application again, it will attempt to log you in automatically. Logging out will delete this file. If you are impersonating a user from the admin panel, the desktop client also preserves the original admin session so you can restore it later.

## Admin Features

If you are logged in as a site administrator:
- **Native Admin Tabs**: Use the desktop admin tabs for stats, users, posts, suspensions, reports, moderation logs, DMs, blocks, emojis, badges, shop data, communities, fake notifications, clone-user workflows, impersonation, post-as-user, mass posting, and bulk user edits.
- **Impersonation Restore**: The tools tab can impersonate a user account while keeping the original admin session available for restore.
- **Bulk User Edits**: The tools tab accepts a JSON patch payload and applies it across one or more user IDs or usernames through the upstream admin API.
- **Add Note**: You will see a "Note" button on tweets. Click it to add a public "Note" or fact check to the post. This note will be visible to all users.
