AM_CFLAGS = @LIBNRM_CFLAGS@ -I$(top_srcdir)/src/common
AM_LDFLAGS = -export-symbols-regex '^(MPI|mpi)_' @MPILIBS@ @LIBNRM_LIBS@
CC=@MPICC@

noinst_LTLIBRARIES = libcommon.la
noinst_HEADERS = $(srcdir)/../common/extra.h
libcommon_la_SOURCES = $(srcdir)/../common/extra.c

lib_LTLIBRARIES = libnrm-pmpi.la
libnrm_pmpi_la_LIBADD = libcommon.la
libnrm_pmpi_la_SOURCES = c_mpi_bindings.c \
			 f_mpi_bindings.c \
			 mpi_api.c \
			 nrm_mpi.h
