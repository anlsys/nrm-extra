COMMON_CFLAGS = @LIBNRM_CFLAGS@ -I$(top_srcdir)/src/common
COMMON_LDFLAGS = @LIBNRM_LIBS@

# default flags if none are specified
AM_CFLAGS = $(COMMON_CFLAGS)
AM_LDFLAGS = $(COMMON_LDFLAGS)

noinst_LTLIBRARIES = libcommon.la
noinst_HEADERS = common/extra.h
libcommon_la_SOURCES = common/extra.c

nrm_power_papi_SOURCES = power_papi/nrmpower_papi.c
nrm_power_papi_LDADD = libcommon.la
nrm_power_papi_CFLAGS = $(COMMON_CFLAGS) @PAPI_CFLAGS@ @HWLOC_CFLAGS@
nrm_power_papi_LDFLAGS = $(COMMON_LDFLAGS) @PAPI_LIBS@ @HWLOC_LIBS@

bin_PROGRAMS = nrm-power-papi

if HAVE_VARIORUM
nrm_power_variorum_SOURCES = power_variorum/nrmpower_variorum.c
nrm_power_variorum_LDADD = libcommon.la
nrm_power_variorum_CFLAGS = $(COMMON_CFLAGS) @VARIORUM_CFLAGS@ @HWLOC_CFLAGS@ @JANSSON_CFLAGS@
nrm_power_variorum_LDFLAGS = $(COMMON_LDFLAGS) @VARIORUM_LIBS@ @HWLOC_LIBS@ @JANSSON_LIBS@
bin_PROGRAMS += nrm-power-variorum
endif
