#
# Makefile for the Tweetapus GTK Client in C
#
# (c) 2025 Lily
# Licensed under the AGPLv3 license
#

CC=gcc
CFLAGS:=-Wall -Wextra -std=c99 $(shell pkg-config --cflags gtk+-3.0 json-glib-1.0)

LDFLAGS:=-L/usr/lib
LDLIBS:=$(shell pkg-config --libs gtk+-3.0 json-glib-1.0 libcurl)
TARGET=tweetapus-gtk-c

all: $(TARGET)

$(TARGET): main.o
	$(CC) $(LDFLAGS) -o $(TARGET) main.o $(LDLIBS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f *.o $(TARGET)

install:
	install -Dm755 $(TARGET) /usr/local/bin/$(TARGET)
	install -Dm644 tweetapus-gtk-c.desktop /usr/local/share/applications/tweetapus-gtk-c.desktop

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	rm -f /usr/local/share/applications/tweetapus-gtk-c.desktop
