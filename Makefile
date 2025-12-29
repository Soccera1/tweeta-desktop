.POSIX:

#
# Makefile for Tweeta Desktop in C
#
# (c) 2025 Lily
# Licensed under the AGPLv3 license
#

CC      = gcc
PREFIX  = /usr/local
SRCDIR  = .

# Dependencies (backticks are evaluated by the shell in rules)
GTK_CFLAGS = `pkg-config --cflags gtk+-3.0 json-glib-1.0`
GTK_LIBS   = `pkg-config --libs gtk+-3.0 json-glib-1.0 libcurl`

CFLAGS  = -Wall -Wextra -std=c99 -I$(SRCDIR)/src
LDFLAGS = -L/usr/lib

TARGET      = tweeta-desktop
TEST_TARGET = test_runner

# Define objects
CORE_OBJS = globals.o network.o json_utils.o \
            session.o ui_utils.o ui_components.o \
            views.o actions.o

OBJS = main.o $(CORE_OBJS)

TEST_OBJS = test_main.o $(CORE_OBJS)

# VPATH allows finding source files in different directories
# Supported by most modern make implementations including GNU and BSD
VPATH = $(SRCDIR)/src:$(SRCDIR)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS) $(GTK_LIBS)

static: $(OBJS)
	$(CC) $(LDFLAGS) -static -o $(TARGET)-static $(OBJS) $(GTK_LIBS)

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

clean:
	rm -f $(SRCDIR)/src/*.o *.o $(TARGET) $(TARGET)-static $(TEST_TARGET)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/share/applications
	cp $(SRCDIR)/tweeta-desktop.desktop $(DESTDIR)$(PREFIX)/share/applications/tweeta-desktop.desktop
	chmod 644 $(DESTDIR)$(PREFIX)/share/applications/tweeta-desktop.desktop
	mkdir -p $(DESTDIR)$(PREFIX)/share/pixmaps
	cp $(SRCDIR)/logo.png $(DESTDIR)$(PREFIX)/share/pixmaps/tweeta-desktop.png
	chmod 644 $(DESTDIR)$(PREFIX)/share/pixmaps/tweeta-desktop.png
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	cp $(SRCDIR)/tweeta-desktop.1 $(DESTDIR)$(PREFIX)/share/man/man1/tweeta-desktop.1
	chmod 644 $(DESTDIR)$(PREFIX)/share/man/man1/tweeta-desktop.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/tweeta-desktop.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/pixmaps/tweeta-desktop.png
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/tweeta-desktop.1

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(GTK_LIBS)