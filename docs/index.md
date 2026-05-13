# Tweeta Desktop Documentation

Welcome to the documentation for **Tweeta Desktop**, a minimal GTK3 X11 client built with Zig 0.15.1 for the Tweetapus social media platform.

## Overview

Tweeta Desktop provides a lightweight, native experience for interacting with Tweetapus. It is designed to be fast, simple, and resource-efficient.

> [!WARNING]
> **Linux Only:** While Tweeta Desktop may compile on non-Linux systems (such as macOS or Windows), it is **horribly broken and completely unsupported**. Official binaries for macOS are provided solely for convenience. Compiling on Windows requires significant manual patching. macOS compilation issues will be fixed (CI/CD is in place for building DMGs), but runtime bugs not present on Linux likely won't be fixed unless they are trivial or a patch is submitted by someone with a Mac, as I cannot test beyond compilation on macOS without access to a Mac. Using them or building from source on non-Linux systems is at your own risk.

## Documentation Sections

- [**Installation**](installation.md): How to build and install Tweeta Desktop on your system.
- [**Usage**](usage.md): A guide on how to use the application and its features.
- [**Architecture**](architecture.md): Information about the internal structure and technical implementation.
- [**Development**](development.md): Information for developers, including testing and contributing.

Additional local manuals:
- Man page: `tweeta-desktop(1)`
- GNU Info manual: `info tweeta-desktop`

## Features

- **Timeline View**: View public tweets from the Tweetapus community.
- **User Profiles**: View detailed profiles of users, including their bio, stats, and tweets.
- **Profile Editing**: Edit your profile name, bio, avatar, and banner (requires login).
- **Search**: Search for users and posts across the platform.
- **Authentication**: Secure login and session persistence.
- **Tweeting**: Post new tweets and reply to others.
- **Media Uploads**: Attach images and videos to your tweets.
- **Polls**: Create and vote in polls on tweets.
- **Direct Messages**: Send and receive private messages with other users.
- **Bookmarks**: Save tweets for later reading and access your bookmarked posts.
- **Communities**: Join and participate in topic-based communities.
- **Native Experience**: A GTK3 application that integrates well with Linux desktop environments.

## License

Tweeta Desktop is licensed under the AGPLv3 license.
