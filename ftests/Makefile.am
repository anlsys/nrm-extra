AM_COLOR_TESTS = yes

AM_DEFAULT_SOURCE_EXT = .f90
FC = @MPIFC@

TESTS_ENVIRONMENT= LD_PRELOAD=$(top_srcdir)/src/mpi/.libs/libnrm-pmpi.so

MPI_TESTS = mpi_basic mpi_collectives

clean-local:
	-rm -f *.mod

check_PROGRAMS = $(MPI_TESTS)
#TESTS = $(MPI_TESTS) 
