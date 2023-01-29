#
# Copyright 2023 joshua stein <jcs@jcs.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

PREFIX?=	/usr/local
X11BASE?=	/usr/X11R6

PKGLIBS=	x11 xpm

CC?=		cc
CFLAGS+=	-O2 -Wall -Wunused \
		-Wmissing-prototypes -Wstrict-prototypes -Wpointer-sign \
		`pkg-config --cflags ${PKGLIBS}`
LDFLAGS+=	`pkg-config --libs ${PKGLIBS}`

BINDIR=		$(PREFIX)/bin

SRC=		xbatticon.c

OBJ=		${SRC:.c=.o}
ICONS!=		echo icons/*

BIN=		xbatticon

all: $(BIN)

$(OBJ):	Makefile ${ICONS}

$(BIN): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

install: all
	mkdir -p $(BINDIR) $(MANDIR)
	install -s $(BIN) $(BINDIR)

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all install clean
