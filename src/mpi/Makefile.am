AM_CFLAGS = @LIBNRM_CFLAGS@
AM_LDFLAGS = -export-symbols-regex '^(MPI|mpi)_' @MPILIBS@ @LIBNRM_LIBS@

CC=@MPICC@

lib_LTLIBRARIES = libnrm-pmpi.la

libnrm_pmpi_la_SOURCES = c_mpi_bindings.c \
			 f_mpi_bindings.c \
			 mpi_api.c \
			 nrm_mpi.h
