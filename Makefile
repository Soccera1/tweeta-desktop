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
GTK_CFLAGS = `pkg-config --cflags gtk+-3.0 json-glib-1.0`
GTK_LIBS   = `pkg-config --libs gtk+-3.0 json-glib-1.0 libcurl`

CFLAGS  = -Wall -Wextra -pedantic-errors -std=c99 -O3 -Isrc
# Let pkg-config provide library paths via GTK_LIBS
LDFLAGS =

TARGET      = tweeta-desktop
TEST_TARGET = test_runner

# Define source files (all in src/ directory)
MAIN_SRC = src/main.c
CORE_SRCS = src/globals.c src/network.c src/json_utils.c \
            src/session.c src/ui_utils.c src/ui_components.c \
            src/views.c src/actions.c src/challenge.c

# Object files with src/ prefix
MAIN_OBJ = $(patsubst %.c,%.o,$(MAIN_SRC))
CORE_OBJS = $(patsubst %.c,%.o,$(CORE_SRCS))
OBJS = $(MAIN_OBJ) $(CORE_OBJS)

TEST_SRC = test_main.c
TEST_OBJ = $(patsubst %.c,%.o,$(TEST_SRC))
TEST_OBJS = $(TEST_OBJ) $(CORE_OBJS)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS) $(GTK_LIBS)

static: $(OBJS)
	@echo "WARNING: Static linking with GTK3 may not work correctly due to dynamic loading"
	-$(CC) $(LDFLAGS) -static -o $(TARGET)-static $(OBJS) $(GTK_LIBS)

# Generic rule for .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o *.o $(TARGET) $(TARGET)-static $(TEST_TARGET)

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

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/tweeta-desktop.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/pixmaps/tweeta-desktop.png
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/tweeta-desktop.1

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(GTK_LIBS)

.PHONY: all static clean install uninstall test
