# tweeta-desktop

A minimal GTK3 X11 client in C for Tweetapus.

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