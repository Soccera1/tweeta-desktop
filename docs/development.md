# Development and Testing

This document provides information for developers who wish to contribute to Tweeta Desktop or run the test suite.

> [!WARNING]
> **Linux Only:** While Tweeta Desktop may compile on non-Linux systems (such as macOS or Windows), it is **horribly broken and completely unsupported**. Official binaries for macOS and Windows are provided solely for convenience as compiling on those systems requires significant manual patching. Using them or building from source on non-Linux systems is at your own risk.

## Running Tests

Tweeta Desktop uses GLib's testing framework. Tests are located in `test_main.c` and are linked against the modular components in the `src/` directory.

To run the tests, use the following command:

```bash
make test
```

If you are using a dedicated build directory, run:

```bash
make -f ../Makefile SRCDIR=.. test
```

This will:
1. Compile the test runner.
2. Execute all defined test cases.
3. Report the results to the console.

### Test Categories

The current test suite covers:
- `parsetweets`: JSON parsing for tweet lists, including attachments, media, and notes.
- `parselogin`: JSON parsing for authentication responses, including admin status.
- `constructpayload`: JSON construction for new posts and DMs.
- `session`: Saving, loading, and clearing user sessions with XDG path overrides.
- `parseprofile`: JSON parsing for user profile data and replies.
- `parseusers`: JSON parsing for user lists in search.
- `parsenotifications`: JSON parsing for various notification types.
- `parseconversations` / `parsemessages`: JSON parsing for DM data.
- `integration`: Basic login flow integration test (requires environment variables).

## Code Style

The project follows a standard C style with GLib/GTK naming conventions:
- Use `gchar`, `gboolean`, `gint`, etc., from GLib.
- Functions use `snake_case`.
- Static global variables are prefixed with `g_` (e.g., `g_auth_token`).
- Use `g_new()`, `g_free()`, and other GLib memory management functions where appropriate.

## Contributing

Contributions are welcome! Please follow these steps:
1. Fork the repository.
2. Create a new branch for your feature or bug fix.
3. Implement your changes.
4. Add tests for new functionality in `test_main.c`.
5. Ensure all tests pass.
6. Submit a pull request.

## Reference Data

For reference, the source code for the Tweetapus social media platform (which this application connects to) may be found in the `tweetapus/` directory if it has been downloaded. **Do not modify the contents of the `tweetapus/` directory**, as it is for reference only.
