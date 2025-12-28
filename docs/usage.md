# Usage Guide

This guide explains how to use Tweetapus Desktop and its various features.

## Starting the Application

You can start the application from your terminal or your desktop environment's application menu.

From the terminal:
```bash
./tweetapus-gtk-c
```

## Main Interface

The interface consists of a header bar and a main content area that switches between different views.

### Header Bar

The header bar contains the following controls:
- **Search Bar**: Enter a query and press Enter to search for users and posts.
- **Back Button**: Appears when you are in Profile or Search views to return to the previous view.
- **Compose Button**: Opens a dialog to post a new tweet (requires login).
- **Refresh Button**: Refreshes the current timeline or view.
- **Login/Logout Button**: Click to log in or log out of your Tweetapus account.
- **User Label**: Displays your login status and username.

### Timeline View

The default view shows a list of recent public tweets. 
- Click on a **Username** to view that user's profile.
- Click the **Reply** button on a tweet to post a reply (requires login).

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

Tweetapus Desktop automatically saves your session when you log in. Your credentials (auth token and username) are stored in `~/.config/tweetapus-gtk/session.json`. When you start the application again, it will attempt to log you in automatically. Logging out will delete this file.
