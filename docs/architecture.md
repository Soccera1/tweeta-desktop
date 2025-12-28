# Architecture Documentation

Tweetapus Desktop is a single-file C application (`main.c`) using GTK3, libcurl, and json-glib. It follows a procedural style with a focus on asynchronous operations to keep the UI responsive.

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

### 4. Asynchronicity (GLib Threads)

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

- `main.c`: The entire application logic.
- `test_main.c`: Unit tests that include `main.c` (redefining `main`) to test internal functions.
- `Makefile`: Defines the build process and dependencies.
- `tweetapus-gtk-c.desktop`: Desktop integration file.
- `tweetapus-gtk.1`: Man page source.
