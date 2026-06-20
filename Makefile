.POSIX:

#
# Makefile for Tweeta Desktop in C
#
# (c)2025 Lily
# Licensed under the AGPLv3 license
#

CC      ?= gcc
PREFIX  ?= /usr/local
SRCDIR  = .

# Dependencies (backticks are evaluated by shell in rules)
GTK_CFLAGS = `pkg-config --cflags gtk+-3.0 json-glib-1.0 gpgme`
GTK_LIBS   = `pkg-config --libs gtk+-3.0 json-glib-1.0 libcurl gpgme`
USE_FIDO2 ?= 0
BUILD_CONFIG = .build-use-fido2-$(USE_FIDO2)

CFLAGS  = -Wall -Wextra -pedantic-errors -std=c99 -O3 -Isrc

ifeq ($(USE_FIDO2),1)
GTK_CFLAGS += `pkg-config --cflags libfido2`
GTK_LIBS   += `pkg-config --libs libfido2`
CFLAGS     += -DUSE_FIDO2
endif

# Let pkg-config provide library paths via GTK_LIBS
LDFLAGS =

TARGET      = tweeta-desktop
TEST_TARGET = test_runner
INFO_SOURCE = tweeta-desktop.texi
INFO_TARGET = tweeta-desktop.info
DOC_DIR     = build/docs
PDF_TARGET  = $(DOC_DIR)/tweeta-desktop.pdf
HTML_TARGET = $(DOC_DIR)/tweeta-desktop.html
HTML_SPLIT_DIR = $(DOC_DIR)/html
MAKEINFO   ?= makeinfo
TEXI2PDF   ?= texi2pdf

# Define source files (all in src/ directory)
MAIN_SRC = src/main.c
CORE_SRCS = src/globals.c src/network.c src/json_utils.c \
            src/session.c src/ui_utils.c src/ui_components.c \
            src/views.c src/actions.c src/challenge.c src/p2p_crypto.c \
            src/p2p_network.c src/actions_p2p_network.c src/webauthn_fido2.c

# Object files with src/ prefix
MAIN_OBJ = $(patsubst %.c,%.o,$(MAIN_SRC))
CORE_OBJS = $(patsubst %.c,%.o,$(CORE_SRCS))
OBJS = $(MAIN_OBJ) $(CORE_OBJS)

TEST_SRC = test_main.c
TEST_OBJ = $(patsubst %.c,%.o,$(TEST_SRC))
TEST_OBJS = $(TEST_OBJ) $(CORE_OBJS)

all: $(TARGET)

$(BUILD_CONFIG):
	rm -f .build-use-fido2-*
	touch $(BUILD_CONFIG)

$(OBJS) $(TEST_OBJS): $(BUILD_CONFIG)

info: $(INFO_TARGET)

docs: info pdf html html-split

pdf: $(PDF_TARGET)

html: $(HTML_TARGET)

html-split: $(HTML_SPLIT_DIR)/index.html

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS) $(GTK_LIBS)

static: $(OBJS)
	@echo "WARNING: Static linking with GTK3 may not work correctly due to dynamic loading"
	-$(CC) $(LDFLAGS) -static -o $(TARGET)-static $(OBJS) $(GTK_LIBS)

# Generic rule for .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o *.o $(TARGET) $(TARGET)-static $(TEST_TARGET) $(INFO_TARGET) .build-use-fido2-*
	rm -rf $(DOC_DIR)

$(INFO_TARGET): $(INFO_SOURCE)
	$(MAKEINFO) -o $(INFO_TARGET) $(INFO_SOURCE)

$(DOC_DIR):
	mkdir -p $(DOC_DIR)

$(HTML_SPLIT_DIR):
	mkdir -p $(HTML_SPLIT_DIR)

$(PDF_TARGET): $(INFO_SOURCE) | $(DOC_DIR)
	$(TEXI2PDF) --clean -o $(PDF_TARGET) $(INFO_SOURCE)

$(HTML_TARGET): $(INFO_SOURCE) | $(DOC_DIR)
	$(MAKEINFO) --html --no-split -o $(HTML_TARGET) $(INFO_SOURCE)

$(HTML_SPLIT_DIR)/index.html: $(INFO_SOURCE) | $(HTML_SPLIT_DIR)
	$(MAKEINFO) --html --split=node -o $(HTML_SPLIT_DIR) $(INFO_SOURCE)

install: all
	@if [ ! -d "$(DESTDIR)$(PREFIX)" ]; then \
		echo "Creating $(DESTDIR)$(PREFIX)"; \
		mkdir -p "$(DESTDIR)$(PREFIX)"; \
	fi
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@mkdir -p $(DESTDIR)$(PREFIX)/share/applications
	@cp $(SRCDIR)/tweeta-desktop.desktop $(DESTDIR)$(PREFIX)/share/applications/tweeta-desktop.desktop
	@chmod 644 $(DESTDIR)$(PREFIX)/share/applications/tweeta-desktop.desktop
	@mkdir -p $(DESTDIR)$(PREFIX)/share/pixmaps
	@cp $(SRCDIR)/logo.png $(DESTDIR)$(PREFIX)/share/pixmaps/tweeta-desktop.png
	@chmod 644 $(DESTDIR)$(PREFIX)/share/pixmaps/tweeta-desktop.png
	@mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	@cp $(SRCDIR)/tweeta-desktop.1 $(DESTDIR)$(PREFIX)/share/man/man1/tweeta-desktop.1
	@chmod 644 $(DESTDIR)$(PREFIX)/share/man/man1/tweeta-desktop.1
	@mkdir -p $(DESTDIR)$(PREFIX)/share/info
	@$(MAKEINFO) -o $(DESTDIR)$(PREFIX)/share/info/$(INFO_TARGET) $(SRCDIR)/$(INFO_SOURCE)
	@chmod 644 $(DESTDIR)$(PREFIX)/share/info/$(INFO_TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/tweeta-desktop.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/pixmaps/tweeta-desktop.png
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/tweeta-desktop.1
	rm -f $(DESTDIR)$(PREFIX)/share/info/$(INFO_TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(GTK_LIBS)

.PHONY: all info docs pdf html html-split static clean install uninstall test
