SUBDIRS = mpi omp powerwrapper_variorum

COMMON_CFLAGS = @LIBNRM_CFLAGS@ -I$(top_srcdir)/src/common
COMMON_LDFLAGS = @LIBNRM_LIBS@

# default flags if none are specified
AM_CFLAGS = $(COMMON_CFLAGS)
AM_LDFLAGS = $(COMMON_LDFLAGS)

noinst_LTLIBRARIES = libcommon.la
noinst_HEADERS = common/extra.h
libcommon_la_SOURCES = common/extra.c

nrm_perfwrapper_SOURCES = perfwrapper/perfwrapper.c
nrm_perfwrapper_LDADD = libcommon.la
nrm_perfwrapper_CFLAGS = $(COMMON_CFLAGS) @PAPI_CFLAGS@
nrm_perfwrapper_LDFLAGS = $(COMMON_LDFLAGS) @PAPI_LIBS@

nrm_powerwrapper_papi_SOURCES = powerwrapper_papi/nrmpower_papi.c
nrm_powerwrapper_papi_LDADD = libcommon.la
nrm_powerwrapper_papi_CFLAGS = $(COMMON_CFLAGS) @PAPI_CFLAGS@
nrm_powerwrapper_papi_LDFLAGS = $(COMMON_LDFLAGS) @PAPI_LIBS@

bin_PROGRAMS = nrm-perfwrapper nrm-powerwrapper-papi
