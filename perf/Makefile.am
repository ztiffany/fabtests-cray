
AM_CFLAGS = -g -Wall -D_GNU_SOURCE -I$(top_srcdir)/include -lpmi

bin_PROGRAMS = \
	rdm_bw \
	rdm_latency

rdm_bw_SOURCES = \
	rdm_bw.c \
	ft_utils.c
rdm_bw_LDADD = $(top_srcdir)/common/libfabtests.la

rdm_latency_SOURCES = \
	rdm_latency.c \
	ft_utils.c
rdm_latency_LDADD = $(top_srcdir)/common/libfabtests.la


EXTRA_DIST = \
	ft_utils.h \
	$(top_srcdir)/shared.h
