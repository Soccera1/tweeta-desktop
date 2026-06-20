# tweeta-desktop

A minimal GTK3 X11 client in C for Tweetapus.

> [!WARNING]
> **Linux Only:** While Tweeta Desktop may compile on non-Linux systems (such as macOS or Windows), it is **horribly broken and completely unsupported**. Official binaries for macOS are provided solely for convenience. Compiling on Windows requires significant manual patching. macOS compilation issues will be fixed (CI/CD is in place for building DMGs), but runtime bugs not present on Linux likely won't be fixed unless they are trivial or a patch is submitted by someone with a Mac, as I cannot test beyond compilation on macOS without access to a Mac. Using them or building from source on non-Linux systems is at your own risk.

(c) 2026 Lily
Licensed under the AGPLv3 license

## Documentation

The canonical documentation is the Texinfo manual in
[`tweeta-desktop.texi`](tweeta-desktop.texi). Generate all published formats
with:

```bash
make docs
```

This creates `tweeta-desktop.info`, `build/docs/tweeta-desktop.pdf`,
`build/docs/tweeta-desktop.html`, and split HTML under `build/docs/html/`.

## Building

To build the client, you will need to have GTK3, libcurl, json-glib, and GPGME installed.
To build the Texinfo manual, install `makeinfo` from GNU Texinfo.

### Dependencies

**Required packages:**
- GTK3 (libgtk-3-dev on Debian/Ubuntu)
- libcurl (libcurl4-openssl-dev on Debian/Ubuntu)
- json-glib (libjson-glib-dev on Debian/Ubuntu)
- GPGME (libgpgme-dev on Debian/Ubuntu) - for encrypted messaging support
- GNU Texinfo (`makeinfo`) - for generating the Info manual

**Installing dependencies on Debian/Ubuntu:**
```bash
sudo apt-get install libgtk-3-dev libcurl4-openssl-dev libjson-glib-dev libgpgme-dev texinfo pkg-config
```

**Installing dependencies on macOS (Homebrew):**
```bash
brew install gtk+3 json-glib curl gpgme texinfo pkg-config
```

### Using Make

Run `make` to build the client.

Run `make docs` to build the full Texinfo manual set, or `make info` to build
only the Info manual (`tweeta-desktop.info`).

### Using Meson/Ninja

Run the following commands:
```bash
meson setup build
ninja -C build
```

## Running

To run the client, simply run `./tweeta-desktop`.

If you need to target a different Tweeta/Tweetapus instance, you can override
the default endpoints at runtime:

```bash
TWEETA_API_BASE_URL="https://your-instance.example/api" \
TWEETA_BASE_DOMAIN="https://your-instance.example" \
./tweeta-desktop
```
