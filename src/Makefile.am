lib_LTLIBRARIES = libnrm-pmpi.la libnrm-ompt.la

libnrm_pmpi_la_SOURCES = mpi/c_mpi_bindings.c \
			 mpi/f_mpi_bindings.c \
			 mpi/mpi_api.c \
			 mpi/nrm_mpi.h

libnrm_pmpi_la_CC = @MPICC@
libnrm_pmpi_la_CFLAGS = @LIBNRM_CFLAGS@
libnrm_pmpi_la_LDFLAGS = -export-symbols-regex '^(MPI|mpi)_' @MPILIBS@ @LIBNRM_LIBS@

libnrm_ompt_la_SOURCES = omp/entrypoint.c \
			 omp/nrm_omp_callbacks.c \
			 omp/nrm_omp.h

libnrm_ompt_la_CFLAGS = $(OPENMP_CFLAGS) @LIBNRM_CFLAGS@
libnrm_ompt_la_LDFLAGS = $(OPENMP_CFLAGS) @LIBNRM_LIBS@
