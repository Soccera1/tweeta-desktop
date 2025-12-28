# Architecture Documentation

Tweetapus Desktop is a modular C application using GTK3, libcurl, and json-glib. It follows a procedural style with a focus on asynchronous operations to keep the UI responsive.

## Project Structure

The codebase is organized into a `src/` directory with the following modules:

- **`main.c`**: The application entry point and main loop.
- **`actions.c` / `actions.h`**: Core application logic and event handlers (login, search, posting).
- **`views.c` / `views.h`**: Definitions for the main window and primary views (Timeline, Profile, Search).
- **`ui_components.c` / `ui_components.h`**: Specialized widget creation (e.g., tweet and user list items).
- **`json_utils.c` / `json_utils.h`**: JSON parsing for API responses and payload construction.
- **`network.c` / `network.h`**: libcurl wrappers and networking utilities.
- **`session.c` / `session.h`**: User session persistence and configuration management.
- **`globals.c` / `globals.h`**: Global shared state and widget references.
- **`ui_utils.c` / `ui_utils.h`**: General UI utilities like asynchronous avatar loading.
- **`types.h`**: Shared data structures.
- **`constants.h`**: API endpoints and configuration constants.

## Core Components

### 1. UI Layer (GTK3)

The UI is built using standard GTK3 widgets:
- `GtkWindow` and `GtkHeaderBar` for the main window structure.
- `GtkStack` for navigating between different views (Timeline, Profile, Search).
- `GtkListBox` for displaying lists of tweets and users.
- `GtkNotebook` for tabs within the Profile and Search views.

### 2. Networking Layer (libcurl)

All API communication is handled via `libcurl`. 
- `fetch_url()`: A utility function that handles initialization, headers (including Bearer tokens), and data transfer.
- `WriteMemoryCallback()`: Handles buffering the response from the server into memory.

### 3. Data Parsing (json-glib)

The application uses `json-glib` to handle API responses and local session storage.
- Parsers exist for tweets, profiles, users, and login responses.
- `JsonBuilder` and `JsonGenerator` are used for constructing JSON payloads for POST requests.

### 4. Image Handling (GdkPixbuf)

The application handles profile pictures (avatars) and media attachments asynchronously:
- `load_avatar()`: Initiates an asynchronous download and scaling of an image (used for both avatars and media).
- `fetch_avatar_thread()`: Downloads the image in the background and loads it into a `GdkPixbuf`.
- Placeholders are shown while images are loading or if they fail to load.

### 5. Media Support

Tweetapus Desktop supports tweets with media attachments (photos and videos):
- **Photos**: Displayed inline within the tweet widget, scaled to a standard width while preserving aspect ratio.
- **Videos**: A "Play Video" button is shown, which opens the video URL in the system's default media player or browser via `gtk_show_uri_on_window`.
- **Other Attachments**: Shown as links with their detected file type.

### 6. Asynchronicity (GLib Threads)

To prevent the UI from freezing during network requests, Tweetapus Desktop uses GLib threads:
- `g_thread_new()`: Spawns a background thread for fetching data.
- `g_idle_add()`: Schedules a callback to update the UI on the main thread once the data is ready.

`AsyncData` struct is used to pass context and results between the background threads and the main UI thread.

## API Integration

The application communicates with the Tweetapus API at `https://tweeta.tiago.zip/api`.

Key endpoints used:
- `/public-tweets`: Fetches the global timeline.
- `/auth/basic-login`: Authenticates a user.
- `/tweets/`: Posts new tweets or replies.
- `/profile/{username}`: Fetches user profile data.
- `/search/users` and `/search/posts`: Performs search queries.

## File Structure

- `src/`: Directory containing all application source code and headers.
- `test_main.c`: Unit tests that link against the modular application components.
- `Makefile`: Defines the modular build process and dependencies.
- `tweetapus-gtk-c.desktop`: Desktop integration file.
- `tweetapus-gtk.1`: Man page source.
