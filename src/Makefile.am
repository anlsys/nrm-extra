SUBDIRS = mpi omp powerwrapper_papi powerwrapper_variorum

COMMON_CFLAGS = @LIBNRM_CFLAGS@ -I$(top_srcdir)/src/common
COMMON_LDFLAGS = @LIBNRM_LIBS@

# default flags if none are specified
AM_CFLAGS = $(COMMON_CFLAGS)
AM_LDFLAGS = $(COMMON_LDFLAGS)

noinst_LTLIBRARIES = libcommon.la
libcommon_la_SOURCES = common/extra.c

nrm_perfwrapper_SOURCES = perfwrapper/perfwrapper.c
nrm_perfwrapper_LDADD = libcommon.la
nrm_perfwrapper_CFLAGS = $(COMMON_CFLAGS) @PAPI_CFLAGS@
nrm_perfwrapper_LDFLAGS = $(COMMON_LDFLAGS) @PAPI_LIBS@

bin_PROGRAMS = nrm-perfwrapper
