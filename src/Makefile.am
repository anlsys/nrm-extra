lib_LTLIBRARIES = libnrm-pmpi.la

libnrm_pmpi_la_SOURCES = c_mpi_bindings.c \
			 f_mpi_bindings.c \
			 mpi_api.c \
			 nrm_mpi.h

libnrm_pmpi_la_CC = @MPICC@
libnrm_pmpi_la_CFLAGS = @LIBNRM_CFLAGS@
libnrm_pmpi_la_LDFLAGS = -export-symbols-regex '^(MPI|mpi)_' @MPILIBS@ @LIBNRM_LIBS@
