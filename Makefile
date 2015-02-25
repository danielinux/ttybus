CFLAGS=-Wall
LDFLAGS=-lm
CC=gcc-4.8
BINARIES=tty_bus tty_fake tty_plug tty_attach dpipe

PREFIX?=/usr/local

all: configure.h $(BINARIES) 

install: all
	echo Installing binaries in  $(PREFIX)/bin
	cp $(BINARIES) $(PREFIX)/bin

configure.h: configure.h.in
	cat configure.h.in | sed -e "s/___SVNVERSION___/`svnversion`/g" > configure.h

tty_bus: tty_bus.o




#	gcc -o tty_bus tty_bus.o
tty_bus.o: tty_bus.c
	gcc -c tty_bus.c $(CFLAGS)

tty_plug: tty_plug.o
	gcc -o tty_plug tty_plug.o
tty_plug.o: tty_plug.c
	gcc -c tty_plug.c $(CFLAGS)



tty_fake: tty_fake.o
	gcc -o tty_fake tty_fake.o
tty_fake.o: tty_fake.c
	gcc -c tty_fake.c $(CFLAGS)


tty_attach: tty_attach.o
	gcc -o tty_attach tty_attach.o
tty_attach.o: tty_attach.c
	gcc -c tty_attach.c $(CFLAGS)

dpipe: dpipe.o
	gcc -o dpipe dpipe.o
dpipe.o: dpipe.c
	gcc -c dpipe.c $(CFLAGS)

clean:
	rm -f *.o $(BINARIES) 

distclean: clean
	rm -f configure.h
