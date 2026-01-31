# tweeta-desktop

A minimal GTK3 X11 client in C for Tweetapus.

> [!WARNING]
> **Linux Only:** While Tweeta Desktop may compile on non-Linux systems (such as macOS or Windows), it is **horribly broken and completely unsupported**. Official binaries for macOS are provided solely for convenience. Compiling on Windows requires significant manual patching. macOS compilation issues will be fixed (CI/CD is in place for building DMGs), but runtime bugs not present on Linux likely won't be fixed unless they are trivial or a patch is submitted by someone with a Mac, as I cannot test beyond compilation on macOS without access to a Mac. Using them or building from source on non-Linux systems is at your own risk.

(c) 2025 Lily
Licensed under the AGPLv3 license

## Documentation

Detailed documentation is available in the [docs/](docs/index.md) directory.

## Building

To build the client, you will need to have GTK3, libcurl, and json-glib installed.

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