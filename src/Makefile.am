lib_LTLIBRARIES = libnrm-pmpi.la

libnrm_pmpi_la_SOURCES = src/c_mpi_bindings.c \
			 src/f_mpi_bindgings.c \
			 src/mpi_api.c \
			 src/nrm_mpi.h

libnrm_pmpi_la_LDFLAGS = -export-symbols-regex '^(MPI|mpi)_'

