AM_COLOR_TESTS = yes

AM_CFLAGS = $(OPENMP_CFLAGS)
AM_LDFLAGS = $(OPENMP_CFLAGS)

TESTS_ENVIRONMENT= LD_PRELOAD=$(top_srcdir)/src/omp/.libs/libnrm-ompt.so \
		   NRM_CMDID=argo \
		   NRM_TRANSMIT=0

OMP_TESTS = omp_basic \
	    omp_stream

check_PROGRAMS = $(OMP_TESTS)
TESTS = $(OMP_TESTS)
