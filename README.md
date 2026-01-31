# tweeta-desktop

A minimal GTK3 X11 client in C for Tweetapus.

> [!WARNING]
> **Linux Only:** While Tweeta Desktop may compile on non-Linux systems (such as macOS or Windows), it is **horribly broken and completely unsupported**. Official binaries for macOS are provided solely for convenience. Compiling on Windows requires significant manual patching. macOS compilation issues will be fixed (CI/CD is in place for building DMGs), but runtime bugs not present on Linux likely won't be fixed unless they are trivial or a patch is submitted by someone with a Mac, as I cannot test beyond compilation on macOS without access to a Mac. Using them or building from source on non-Linux systems is at your own risk.

(c) 2025 Lily
Licensed under the AGPLv3 license

## Documentation

Detailed documentation is available in the [docs/](docs/index.md) directory.

## Building

To build the client, you will need to have GTK3, libcurl, json-glib, and GPGME installed.

### Dependencies

**Required packages:**
- GTK3 (libgtk-3-dev on Debian/Ubuntu)
- libcurl (libcurl4-openssl-dev on Debian/Ubuntu)
- json-glib (libjson-glib-dev on Debian/Ubuntu)
- GPGME (libgpgme-dev on Debian/Ubuntu) - for encrypted messaging support

**Installing dependencies on Debian/Ubuntu:**
```bash
sudo apt-get install libgtk-3-dev libcurl4-openssl-dev libjson-glib-dev libgpgme-dev pkg-config
```

**Installing dependencies on macOS (Homebrew):**
```bash
brew install gtk+3 json-glib curl gpgme pkg-config
```

### Using Make

Run `make` to build the client.

### Using Meson/Ninja

Run the following commands:
```bash
meson setup build
ninja -C build
```

## Running

To run the client, simply run `./tweeta-desktop`.