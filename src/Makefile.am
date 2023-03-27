SUBDIRS = omp powerwrapper_papi powerwrapper_variorum

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

nrm_mpi_CFLAGS = @LIBNRM_CFLAGS@
nrm_mpi_LDFLAGS = -export-symbols-regex '^(MPI|mpi)_' @MPILIBS@ @LIBNRM_LIBS@

CC=@MPICC@

lib_LTLIBRARIES = libnrm-pmpi.la

libnrm_pmpi_la_SOURCES = c_mpi_bindings.c \
			 f_mpi_bindings.c \
			 mpi_api.c \
			 nrm_mpi.h

bin_PROGRAMS = nrm-perfwrapper
