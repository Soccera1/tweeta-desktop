# Architecture Documentation

Tweeta Desktop is a modular GTK3 application built with Zig 0.15.1, libcurl, and json-glib. It follows a procedural style with a focus on asynchronous operations to keep the UI responsive.

## Project Structure

The codebase is organized into a `src/` directory with the following modules:

- **`main.zig`**: The application entry point and main loop.
- **`actions.zig` / `actions.h`**: Core application logic and event handlers (login, search, posting).
- **`views.zig` / `views.h`**: Definitions for the main window and primary views (Timeline, Profile, Search).
- **`ui_components.zig` / `ui_components.h`**: Specialized widget creation (e.g., tweet and user list items).
- **`json_utils.zig` / `json_utils.h`**: JSON parsing for API responses and payload construction.
- **`network.zig` / `network.h`**: libcurl wrappers and networking utilities.
- **`session.zig` / `session.h`**: User session persistence and configuration management.
- **`globals.zig` / `globals.h`**: Global shared state and widget references.
- **`ui_utils.zig` / `ui_utils.h`**: General UI utilities like asynchronous avatar loading.
- **`types.h`**: Shared data structures.
- **`constants.h`**: API endpoints and configuration constants.

## Core Components

### 1. UI Layer (GTK3)

The UI is built using standard GTK3 widgets:
- `GtkWindow` and `GtkHeaderBar` for the main window structure.
- `GtkStack` for navigating between different views (Timeline, Profile, Search, Notifications, Messages).
- `GtkListBox` for displaying lists of tweets, users, notifications, and conversations.
- `GtkNotebook` for tabs within the Profile and Search views.

### 2. Networking Layer (libcurl)

All API communication is handled via `libcurl`. 
- `fetch_url()`: A utility function that handles initialization, headers (including Bearer tokens), and data transfer.
- `WriteMemoryCallback()`: Handles buffering the response from the server into memory.

### 3. Data Parsing (json-glib)

The application uses `json-glib` to handle API responses and local session storage.
- Parsers exist for tweets, profiles, users, notifications, conversations, and login responses.
- `JsonBuilder` and `JsonGenerator` are used for constructing JSON payloads for POST and PATCH requests.

### 4. Image Handling (GdkPixbuf)

The application handles profile pictures (avatars) and media attachments asynchronously:
- `load_avatar()`: Initiates an asynchronous download and scaling of an image (used for both avatars, media, and custom emojis).
- `fetch_avatar_thread()`: Downloads the image in the background and loads it into a `GdkPixbuf`.
- Placeholders are shown while images are loading or if they fail to load.

### 5. Media and Emoji Support

Tweeta Desktop supports tweets with media attachments and custom reactions:
- **Photos**: Displayed inline within the tweet widget, scaled to a standard width.
- **Videos**: A "Play Video" button opens the video URL in the system's default player.
- **Custom Emojis**: Fetched from the server and displayed in a reaction picker.

### 6. Asynchronicity (GLib Threads)

To prevent the UI from freezing, background tasks use GLib threads:
- `g_thread_new()`: Spawns a background thread for network requests.
- `g_idle_add()`: Schedules a callback to update the UI on the main thread.
- `AsyncData` struct is used to pass context and results between threads.
- Mutexes are used to track and invalidate superseded asynchronous requests (e.g., during rapid refresh).

### 7. Infinite Scrolling

Infinite scrolling is implemented for the main timeline, profile feeds, and notifications:
- `GtkScrolledWindow`'s `edge-reached` signal detects when the user reaches the bottom.
- `on_scroll_edge_reached()`: Signal handler that triggers a "load more" request using the ID of the last item.
- `load_more_tweets()`: Initiates a background thread to fetch older content using the `before` API parameter.

## API Integration

The application communicates with the Tweetapus API at `https://tweeta.tiago.zip/api`.

Key endpoints used:
- `/public-tweets`, `/timeline/following`: Global and following timelines.
- `/auth/basic-login` and `/auth/me`: Authentication and user info.
- `/tweets/`: Posting tweets, replies, and quoting.
- `/tweets/{id}/like`, `/tweets/{id}/retweet`, `/tweets/{id}/reaction`: Interactions.
- `/tweets/{id}/poll/vote`: Poll voting.
- `/bookmarks/add`, `/bookmarks/remove`, `/bookmarks`: Bookmarking.
- `/profile/{username}`, `/profile/{username}/follow`, `/profile/followers/{username}`, `/profile/following/{username}`: User profiles and following.
- `/profile/{username}`, `/profile/{username}/avatar`, `/profile/{username}/banner`: Profile editing.
- `/search/users` and `/search/posts`: Search.
- `/notifications`: User notifications.
- `/dm/conversations`: Direct message conversations and messaging.
- `/communities`, `/communities/{id}/join`, `/communities/{id}/leave`, `/communities/{id}/tweets`: Communities.
- `/upload/`: Media uploads.
- `/blocking/block`, `/blocking/unblock`, `/blocking/mute`, `/blocking/unmute`: Blocking and muting.
- `/emojis`: Custom emoji list.
- `/admin/*`: Admin endpoints. The desktop client renders a native GTK admin surface over these endpoints, including notifications, clone-user tools, impersonation, post-as-user, and bulk user editing.

## File Structure

- `src/`: Directory containing all application source code and headers.
- `src/test_main.zig`: Unit tests that link against the modular application components.
- `build.zig`: Defines the Zig 0.15.1 build process, linked GTK modules, install artifacts, and test runner.
- `Makefile`: Compatibility wrapper around the Zig build.
- `tweeta-desktop.desktop`: Desktop integration file.
- `tweeta-desktop.1`: Man page source.
