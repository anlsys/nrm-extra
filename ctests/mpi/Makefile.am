AM_COLOR_TESTS = yes

AM_LDFLAGS = @MPILIBS@
CC= @MPICC@

TESTS_ENVIRONMENT= LD_PRELOAD=$(top_srcdir)/src/mpi/.libs/libnrm-pmpi.so \
		   NRM_CMDID=argo \
		   NRM_TRANSMIT=0
MPI_TESTS = mpi_basic mpi_collectives

check_PROGRAMS = $(MPI_TESTS)
TESTS = $(MPI_TESTS)
