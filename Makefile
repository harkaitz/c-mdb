## Configuration
DESTDIR    =
PREFIX     =/usr/local
VARDIR     =/var/lib
AR         =ar
CC         =gcc
CFLAGS     =-Wall -g -DRELEASE
CPPFLAGS   =
LIBS       ="-l:libhiredis.a" "-l:libuuid.a"
## Sources and targets
PROGRAMS   =mdb
LIBRARIES  =libmdb.a
HEADERS    =mdb.h
MARKDOWNS  =README.md mdb.3.md
SOURCES    =mdb-hiredis.c
## AUXILIARY
CFLAGS_ALL =$(LDFLAGS) $(CFLAGS) $(CPPFLAGS)

## STANDARD TARGETS
all: $(PROGRAMS) $(LIBRARIES)
help:
	@echo "all        : Build everything."
	@echo "clean      : Clean files."
	@echo "install    : Install all produced files."
	@echo "install-db : Create database."
install: all
	install -d                  $(DESTDIR)$(PREFIX)/bin
	install -m755 $(PROGRAMS)   $(DESTDIR)$(PREFIX)/bin
	install -d                  $(DESTDIR)$(PREFIX)/include
	install -m644 $(HEADERS)    $(DESTDIR)$(PREFIX)/include
	install -d                  $(DESTDIR)$(PREFIX)/lib
	install -m644 $(LIBRARIES)  $(DESTDIR)$(PREFIX)/lib
install-db:
	install -d                  $(DESTDIR)$(VARDIR)/mdb
	chmod a+rwx                 $(DESTDIR)$(VARDIR)/mdb
clean:
	rm -f $(PROGRAMS) $(LIBRARIES)

## LIBRARY
libmdb.a: $(SOURCES) $(HEADERS)
	mkdir -p .b
	cd .b && $(CC) -c $(SOURCES:%=../%) $(CFLAGS_ALL)
	$(AR) -crs $@ .b/*.o
	rm -f .b/*.o
mdb: main.c libmdb.a
	$(CC) -o $@ main.c libmdb.a $(CFLAGS_ALL) $(LIBS)
## -- manpages --
MAN_3=./mdb.3 
install: install-man3
install-man3: $(MAN_3)
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man3
	cp $(MAN_3) $(DESTDIR)$(PREFIX)/share/man/man3
## -- manpages --
## -- license --
install: install-license
install-license: LICENSE
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/c-mdb
	cp LICENSE $(DESTDIR)$(PREFIX)/share/doc/c-mdb
## -- license --
