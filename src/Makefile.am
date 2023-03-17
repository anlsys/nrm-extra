SUBDIRS = mpi powerwrapper_papi powerwrapper_variorum

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

nrm_omp_SOURCES = omp/entrypoint.c \
                  nrm_omp_callbacks.c \
                  nrm_omp.h
nrm_omp_LDADD = libcommon.la
nrm_omp_CFLAGS = $(COMMON_CFLAGS) $(OPENMP_CFLAGS)
nrm_omp_LDFLAGS = $(COMMON_LDFLAGS) $(OPENMP_CFLAGS)

lib_LTLIBRARIES = libnrm-ompt.la
bin_PROGRAMS = nrm-perfwrapper
