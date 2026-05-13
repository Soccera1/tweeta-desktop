.POSIX:

#
# Makefile wrapper for Tweeta Desktop's Zig build.
#
# (c)2025 Lily
# Licensed under the AGPLv3 license
#

ZIG ?= zig-0.15.1
PREFIX ?= /usr/local
DESTDIR ?=
USE_FIDO2 ?= 0
MAKEINFO ?= makeinfo
ZIG_CACHE_ARGS ?= --global-cache-dir build/zig-global-cache --cache-dir build/zig-local-cache
ZIG_OPTIMIZE ?= -Doptimize=ReleaseFast

ZIG_FIDO2 = $(shell [ "$(USE_FIDO2)" = "1" ] && printf '%s' '-Dfido2=true')
ZIG_PREFIX = --prefix $(PREFIX)

all:
	$(ZIG) build $(ZIG_CACHE_ARGS) $(ZIG_OPTIMIZE) $(ZIG_FIDO2)

static:
	@echo "WARNING: Static linking with GTK3 may not work correctly due to dynamic loading"
	-$(ZIG) build $(ZIG_CACHE_ARGS) $(ZIG_OPTIMIZE) $(ZIG_FIDO2) -Dstatic=true
	-cp zig-out/bin/tweeta-desktop-static tweeta-desktop-static

info: tweeta-desktop.info

tweeta-desktop.info: tweeta-desktop.texi
	$(MAKEINFO) -o tweeta-desktop.info tweeta-desktop.texi

install:
	$(ZIG) build install $(ZIG_CACHE_ARGS) $(ZIG_OPTIMIZE) $(ZIG_FIDO2) $(ZIG_PREFIX)
	@mkdir -p $(DESTDIR)$(PREFIX)/share/info
	@$(MAKEINFO) -o $(DESTDIR)$(PREFIX)/share/info/tweeta-desktop.info tweeta-desktop.texi
	@chmod 644 $(DESTDIR)$(PREFIX)/share/info/tweeta-desktop.info

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tweeta-desktop
	rm -f $(DESTDIR)$(PREFIX)/share/applications/tweeta-desktop.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/pixmaps/tweeta-desktop.png
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/tweeta-desktop.1
	rm -f $(DESTDIR)$(PREFIX)/share/info/tweeta-desktop.info

test:
	$(ZIG) build test $(ZIG_CACHE_ARGS) $(ZIG_OPTIMIZE) $(ZIG_FIDO2)

clean:
	rm -rf .zig-cache zig-out build/zig-global-cache build/zig-local-cache tweeta-desktop.info tweeta-desktop-static

.PHONY: all static info install uninstall test clean
