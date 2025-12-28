# Installation Guide

This guide covers how to build and install Tweeta Desktop from source.

## Prerequisites

To build the client, you will need the following dependencies installed on your system:

- **GCC**: The GNU Compiler Collection.
- **Make**: Build automation tool.
- **GTK+ 3.0**: The GIMP Toolkit for the user interface.
- **libcurl**: Client-side URL transfer library for networking.
- **json-glib-1.0**: A library for parsing and generating JSON using GLib and GObject.
- **pkg-config**: A helper tool used when compiling applications and libraries.

On Debian-based systems (like Ubuntu), you can install these with:

```bash
sudo apt update
sudo apt install build-essential libgtk-3-dev libcurl4-openssl-dev libjson-glib-dev pkg-config
```

## Building

Once the dependencies are installed, you can build the application using the provided `Makefile`.

1. Clone the repository (if you haven't already):
   ```bash
   git clone https://github.com/tweetapus/tweeta-desktop.git
   cd tweeta-desktop
   ```

2. Run `make` to compile the application:
   ```bash
   make
   ```

This will produce an executable named `tweeta-desktop` in the root directory.

### Static Building

If you wish to build a static binary (if your system supports it and you have the static libraries installed), you can run:

```bash
make static
```

This will produce `tweeta-desktop-static`.

## Installation

To install the application system-wide, run:

```bash
sudo make install
```

This will perform the following actions:
- Install the binary to `/usr/local/bin/tweeta-desktop`.
- Install the desktop entry to `/usr/local/share/applications/tweeta-desktop.desktop`.
- Install the man page to `/usr/local/share/man/man1/tweeta-desktop.1`.

## Uninstallation

To remove the application from your system, run:

```bash
sudo make uninstall
```
