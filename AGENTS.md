# AGENTS.md

This file contains guidelines and commands for agentic coding agents working on the tweeta-desktop codebase.

## Build Commands

### Primary Build System (Make)
```bash
# Build the main application (use parallel compilation)
make -j$(nproc)

# Build static binary
make -j$(nproc) static

# Clean build artifacts
make clean

# Run tests
make -j$(nproc) test

# Install application
make -j$(nproc) install

# Uninstall application
make uninstall
```

**Important:** Always use parallel compilation with `make -j$(nproc)` to significantly speed up build times. If strange errors occur with parallel builds, this should be treated as a **critical bug** and reported immediately.

### Alternative Build System (Meson/Ninja)
```bash
# Setup build directory
meson setup build

# Build application
ninja -C build

# Run tests
ninja -C build test
```

### Test Commands
```bash
# Using Make
make test

# Using Meson
ninja -C build test

# Direct test runner execution
./test_runner
```

### Running Single Tests
The test suite uses GLib testing framework. Tests are defined in `test_main.c`. To run individual tests, you'll need to modify the test file temporarily or run the test runner with specific test functions.

## Code Style Guidelines

### Language and Standards
- **Language**: C (C99 standard)
- **Compiler**: GCC with strict warnings
- **Build Flags**: `-Wall -Wextra -pedantic-errors -std=c99 -O3`

### Naming Conventions
- **Functions**: `snake_case` (e.g., `parse_tweets()`, `create_window()`)
- **Variables**: `snake_case` for local variables, `g_` prefix for globals (e.g., `g_auth_token`, `g_current_username`)
- **Types**: `PascalCase` for structs (e.g., `struct Tweet`, `struct Profile`)
- **Constants**: `UPPER_SNAKE_CASE` with descriptive names (e.g., `API_BASE_URL`, `AVATAR_SIZE`)
- **Headers**: `.h` extension with matching `.c` implementation files

### Import and Include Organization
1. System headers first (angle brackets)
2. GLib/GTK headers
3. External library headers (curl, json-glib)
4. Project headers (double quotes)

Example:
```c
#include <stdlib.h>
#include <curl/curl.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include "types.h"
#include "globals.h"
#include "network.h"
```

### Code Formatting
- **Indentation**: 4 spaces (no tabs)
- **Line Length**: Prefer under 80-100 characters
- **Brace Style**: K&R style - opening brace on same line
- **Function Declarations**: Return type and function name on separate lines for complex signatures
- **Static Functions**: Use `static` for file-local functions

### Memory Management
- Use GLib memory functions (`g_malloc`, `g_free`, `g_new0`, `g_strdup`)
- Always free allocated memory with corresponding free function
- Use `g_object_unref()` for GTK/GLib objects
- Check for NULL pointers after allocations
- Follow reference counting rules for GTK widgets

### Error Handling
- Use GLib logging functions: `g_warning()`, `g_error()`, `g_critical()`, `g_debug()`
- Return `gboolean` for success/failure operations (TRUE = success)
- Use `g_assert_*()` macros in tests for validation
- Handle NULL returns from memory allocations

### GTK/GObject Patterns
- Use `GTK_WIDGET()`, `GTK_IMAGE()` etc. macros for casting
- Check widget types with `GTK_IS_WIDGET()` etc.
- Use `g_object_unref()` when transferring ownership
- Follow GTK naming conventions for callbacks

## Project Structure

### Core Modules
- `main.c` - Application entry point and initialization
- `globals.c/h` - Global state and variables
- `network.c/h` - HTTP API communication using libcurl
- `json_utils.c/h` - JSON parsing and data structure creation
- `session.c/h` - User session management
- `ui_utils.c/h` - Utility functions for UI components
- `ui_components.c/h` - Custom GTK widgets and components
- `views.c/h` - Main application views/screens
- `actions.c/h` - UI action handlers and callbacks
- `challenge.c/h` - CAPTCHA/challenge handling

### Data Types
All data structures are defined in `types.h`:
- `struct Tweet` - Tweet representation
- `struct Profile` - User profile
- `struct Notification` - Notification data
- `struct DirectMessage` - Direct message
- `struct Conversation` - DM conversation
- `struct AdminStats` - Administrative statistics

### Constants
API endpoints and configuration constants are in `constants.h`.

## Dependencies
- GTK3 (GUI toolkit)
- libcurl (HTTP client)
- json-glib-1.0 (JSON parsing)
- pkg-config for dependency management

## Testing
- Tests are in `test_main.c`
- Uses GLib testing framework
- Test runner built as `test_runner`
- Focus on JSON parsing and data structure validation

## Important Rules
- **NEVER** modify the `tweetapus/` reference directory
- The `tweetapus/` directory contains the source code for the tweetapus social media platform used for reference only
- If reference data is needed and no `tweetapus/` directory exists, download it from https://github.com/tweetapus/tweetapus
- Update tests when adding new functionality
- Follow existing patterns and conventions
- Check bounds and validate inputs
- Use GLib functions over standard C library when available
- Handle authentication tokens securely
- Follow AGPLv3 licensing requirements