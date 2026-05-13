# Installation Guide

This guide covers how to build and install Tweeta Desktop from source.

> [!WARNING]
> **Linux Only:** While Tweeta Desktop may compile on non-Linux systems (such as macOS or Windows), it is **horribly broken and completely unsupported**. Official binaries for macOS are provided solely for convenience. Compiling on Windows requires significant manual patching. macOS compilation issues will be fixed (CI/CD is in place for building DMGs), but runtime bugs not present on Linux likely won't be fixed unless they are trivial or a patch is submitted by someone with a Mac, as I cannot test beyond compilation on macOS without access to a Mac. Using them or building from source on non-Linux systems is at your own risk.

## Prerequisites

To build the client, you will need the following dependencies installed on your system:

- **Zig 0.15.1**: The supported compiler and build system.
- **Make**: Optional compatibility wrapper.
- **GTK+ 3.0**: The GIMP Toolkit for the user interface.
- **libcurl**: Client-side URL transfer library for networking.
- **json-glib-1.0**: A library for parsing and generating JSON using GLib and GObject.
- **GPGME**: GPG integration for encrypted messaging.
- **pkg-config**: A helper tool used when compiling applications and libraries.
- **Texinfo**: Required to generate the GNU Info manual (`makeinfo`).

On Debian-based systems (like Ubuntu), you can install these with:

```bash
sudo apt update
sudo apt install libgtk-3-dev libcurl4-openssl-dev libjson-glib-dev libgpgme-dev texinfo pkg-config
```

## Building

You can build the application using Zig directly or the compatibility Makefile.

### Using Zig

```bash
zig-0.15.1 build
```

This will produce an executable at `zig-out/bin/tweeta-desktop`.

To enable native FIDO2/passkey support:

```bash
zig-0.15.1 build -Dfido2=true
```

### Using Make

Run:

```bash
make
```

To build the GNU Info page, run:

```bash
make info
```

## Installation

### Using Zig

To install the application system-wide, run:

```bash
sudo zig-0.15.1 build install --prefix /usr/local
```

### Using Make

To install the application system-wide, run:

```bash
sudo make install
```

Both methods will perform the following actions:
- Install the binary to `/usr/local/bin/tweeta-desktop`.
- Install the desktop entry to `/usr/local/share/applications/tweeta-desktop.desktop`.
- Install the application icon to `/usr/local/share/pixmaps/tweeta-desktop.png`.
- Install the man page to `/usr/local/share/man/man1/tweeta-desktop.1`.
- Install the GNU Info page to `/usr/local/share/info/tweeta-desktop.info`.

## Uninstallation

### Using Make

To remove the application from your system, run:

```bash
sudo make uninstall
```
