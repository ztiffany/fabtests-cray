
AM_CFLAGS = -g -Wall -D_GNU_SOURCE -I$(top_srcdir)/include
ACLOCAL_AMFLAGS = -I$(top_srcdir)/config

noinst_LTLIBRARIES = libfabtests.la
libfabtests_la_SOURCES = shared.c

if MACOS
libfabtests_la_SOURCES += include/osx/osd.h
libfabtests_la_SOURCES += common/osx/osd.c
endif

EXTRA_DIST = \
	$(top_srcdir)/shared.h
