# Installation Guide

This guide covers how to build and install Tweeta Desktop from source.

## Prerequisites

To build the client, you will need the following dependencies installed on your system:

- **GCC**: The GNU Compiler Collection.
- **Make**: Build automation tool.
- **Meson**: A high-performance build system.
- **Ninja**: A small build system with a focus on speed.
- **GTK+ 3.0**: The GIMP Toolkit for the user interface.
- **libcurl**: Client-side URL transfer library for networking.
- **json-glib-1.0**: A library for parsing and generating JSON using GLib and GObject.
- **pkg-config**: A helper tool used when compiling applications and libraries.

On Debian-based systems (like Ubuntu), you can install these with:

```bash
sudo apt update
sudo apt install build-essential meson ninja-build libgtk-3-dev libcurl4-openssl-dev libjson-glib-dev pkg-config
```

## Building

You can build the application using either `make` or `meson`.

### Using Make

1. Run `make` to compile the application:
   ```bash
   make
   ```

This will produce an executable named `tweeta-desktop` in the root directory.

### Using Meson

1. Setup the build directory:
   ```bash
   meson setup build
   ```

2. Compile the application:
   ```bash
   ninja -C build
   ```

This will produce an executable named `tweeta-desktop` in the `build/` directory.

### Static Building (Make only)

If you wish to build a static binary (if your system supports it and you have the static libraries installed), you can run:

```bash
make static
```

This will produce `tweeta-desktop-static`.

## Installation

### Using Make

To install the application system-wide, run:

```bash
sudo make install
```

### Using Meson

To install the application system-wide, run:

```bash
sudo ninja -C build install
```

Both methods will perform the following actions:
- Install the binary to `/usr/local/bin/tweeta-desktop`.
- Install the desktop entry to `/usr/local/share/applications/tweeta-desktop.desktop`.
- Install the man page to `/usr/local/share/man/man1/tweeta-desktop.1`.

## Uninstallation

To remove the application from your system, run:

```bash
sudo make uninstall
```
