PREFIX ?= /usr/local/bin

CFLAGS += -std=c99 -march=native -O3 -pipe
CFLAGS += -Wall
CFLAGS += -Wconversion
CFLAGS += -Wdouble-promotion
CFLAGS += -Wextra
CFLAGS += -Wmissing-prototypes
CFLAGS += -Wold-style-definition
CFLAGS += -Wpedantic
CFLAGS += -Wshadow

all: xswm

xswm: main.c Makefile
	$(CC) $(CFLAGS) -o $@ $< -lX11

install: all
	install -D xswm $(DESTDIR)$(PREFIX)/xswm

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/xswm

clean:
	rm -f xswm

.PHONY: all install uninstall clean
