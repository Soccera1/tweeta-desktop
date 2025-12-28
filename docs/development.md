# Development and Testing

This document provides information for developers who wish to contribute to Tweetapus Desktop or run the test suite.

## Running Tests

Tweetapus Desktop uses GLib's testing framework. Tests are located in `test_main.c` and are linked against the modular components in the `src/` directory.

To run the tests, use the following command:

```bash
make test
```

This will:
1. Compile the test runner.
2. Execute all defined test cases.
3. Report the results to the console.

### Test Categories

The current test suite covers:
- `writememorycallback`: Memory buffering for curl responses.
- `parsetweets`: JSON parsing for tweet lists.
- `parselogin`: JSON parsing for authentication responses.
- `constructpayload`: JSON construction for new posts.
- `session`: Saving, loading, and clearing user sessions.
- `parseprofile`: JSON parsing for user profile data.
- `parseusers`: JSON parsing for user lists.

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
