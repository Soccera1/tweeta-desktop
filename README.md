# tweeta-desktop

A minimal GTK3 X11 client for Tweetapus, built with Zig 0.15.1.

> [!WARNING]
> **Linux Only:** While Tweeta Desktop may compile on non-Linux systems (such as macOS or Windows), it is **horribly broken and completely unsupported**. Official binaries for macOS are provided solely for convenience. Compiling on Windows requires significant manual patching. macOS compilation issues will be fixed (CI/CD is in place for building DMGs), but runtime bugs not present on Linux likely won't be fixed unless they are trivial or a patch is submitted by someone with a Mac, as I cannot test beyond compilation on macOS without access to a Mac. Using them or building from source on non-Linux systems is at your own risk.

(c) 2025 Lily
Licensed under the AGPLv3 license

## Documentation

Detailed documentation is available in the [docs/](docs/index.md) directory.

## Building

To build the client, you will need Zig 0.15.1, GTK3, libcurl, json-glib, and GPGME installed.
To build the Texinfo manual, install `makeinfo` from GNU Texinfo.

### Dependencies

**Required packages:**
- GTK3 (libgtk-3-dev on Debian/Ubuntu)
- libcurl (libcurl4-openssl-dev on Debian/Ubuntu)
- json-glib (libjson-glib-dev on Debian/Ubuntu)
- GPGME (libgpgme-dev on Debian/Ubuntu) - for encrypted messaging support
- Zig 0.15.1
- GNU Texinfo (`makeinfo`) - for generating the Info manual

**Installing dependencies on Debian/Ubuntu:**
```bash
sudo apt-get install libgtk-3-dev libcurl4-openssl-dev libjson-glib-dev libgpgme-dev texinfo pkg-config
```

**Installing dependencies on macOS (Homebrew):**
```bash
brew install gtk+3 json-glib curl gpgme texinfo pkg-config
```

### Using Zig

Run `zig-0.15.1 build` to build the client.

Run `zig-0.15.1 build test` to build and run the test suite.

Run `zig-0.15.1 build -Dfido2=true` to enable native FIDO2/passkey support.

### Using Make

Run `make` to build the client through the Zig build.

Run `make info` to build the Info manual (`tweeta-desktop.info`).

Run `make test` to run `zig-0.15.1 build test`.


## Running

To run the client, simply run `./tweeta-desktop`.

If you need to target a different Tweeta/Tweetapus instance, you can override
the default endpoints at runtime:

```bash
TWEETA_API_BASE_URL="https://your-instance.example/api" \
TWEETA_BASE_DOMAIN="https://your-instance.example" \
./tweeta-desktop
```
