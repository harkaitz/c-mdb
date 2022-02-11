## Configuration
DESTDIR    =
PREFIX     =/usr/local
VARDIR     =/var/lib
AR         =ar
CC         =gcc
CFLAGS     =-Wall -g
CPPFLAGS   =
LIBS       ="-l:libgdbm.a" "-l:libuuid.a"
## Sources and targets
PROGRAMS   =mdb
LIBRARIES  =libmdb.a
HEADERS    =mdb.h
MARKDOWNS  =README.md mdb.3.md
MANPAGES_3 =mdb.3
SOURCES    =mdb.c
## AUXILIARY
CFLAGS_ALL =$(LDFLAGS) $(CFLAGS) $(CPPFLAGS)

## STANDARD TARGETS
all: $(PROGRAMS) $(LIBRARIES)
help:
	@echo "all     : Build everything."
	@echo "clean   : Clean files."
	@echo "install : Install all produced files."
install: all
	install -d                  $(DESTDIR)$(PREFIX)/bin
	install -m755 $(PROGRAMS)   $(DESTDIR)$(PREFIX)/bin
	install -d                  $(DESTDIR)$(PREFIX)/include
	install -m644 $(HEADERS)    $(DESTDIR)$(PREFIX)/include
	install -d                  $(DESTDIR)$(PREFIX)/lib
	install -m644 $(LIBRARIES)  $(DESTDIR)$(PREFIX)/lib
	install -d                  $(DESTDIR)$(PREFIX)/share/man/man3	
	install -m644 $(MANPAGES_3) $(DESTDIR)$(PREFIX)/share/man/man3
	install -d                  $(DESTDIR)$(VARDIR)/mdb
	chmod a+rwx                 $(DESTDIR)$(VARDIR)/mdb
clean:
	rm -f $(PROGRAMS) $(LIBRARIES)
ssnip:
	ssnip LICENSE $(MARKDOWNS) $(HEADERS) $(SOURCES) $(MANPAGES_3)

## LIBRARY
libmdb.a: $(SOURCES) $(HEADERS)
	mkdir -p .b
	cd .b && $(CC) -c $(SOURCES:%=../%) $(CFLAGS_ALL)
	$(AR) -crs $@ .b/*.o
	rm -f .b/*.o
mdb: main.c libmdb.a
	$(CC) -o $@ main.c libmdb.a $(CFLAGS_ALL) $(LIBS)
