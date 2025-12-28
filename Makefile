.POSIX:

#
# Makefile for Tweeta Desktop in C
#
# (c) 2025 Lily
# Licensed under the AGPLv3 license
#

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -Isrc `pkg-config --cflags gtk+-3.0 json-glib-1.0`
LDFLAGS = -L/usr/lib
LDLIBS  = `pkg-config --libs gtk+-3.0 json-glib-1.0 libcurl`

TARGET      = tweeta-desktop
TEST_TARGET = test_runner

SRCS = src/main.c src/globals.c src/network.c src/json_utils.c \
       src/session.c src/ui_utils.c src/ui_components.c \
       src/views.c src/actions.c
OBJS = $(SRCS:.c=.o)

TEST_SRCS = test_main.c src/globals.c src/network.c src/json_utils.c \
            src/session.c src/ui_utils.c src/ui_components.c \
            src/views.c src/actions.c
TEST_OBJS = $(TEST_SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LDLIBS)

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o *.o $(TARGET) $(TARGET)-static $(TEST_TARGET)

install: all
	mkdir -p /usr/local/bin
	cp $(TARGET) /usr/local/bin/$(TARGET)
	chmod 755 /usr/local/bin/$(TARGET)
	mkdir -p /usr/local/share/applications
	cp tweeta-desktop.desktop /usr/local/share/applications/tweeta-desktop.desktop
	chmod 644 /usr/local/share/applications/tweeta-desktop.desktop
	mkdir -p /usr/local/share/man/man1
	cp tweeta-desktop.1 /usr/local/share/man/man1/tweeta-desktop.1
	chmod 644 /usr/local/share/man/man1/tweeta-desktop.1

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	rm -f /usr/local/share/applications/tweeta-desktop.desktop
	rm -f /usr/local/share/man/man1/tweeta-desktop.1

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(LDLIBS)