AM_CFLAGS = $(OPENMP_CFLAGS) @LIBNRM_CFLAGS@
AM_LDFLAGS = $(OPENMP_CFLAGS) @LIBNRM_LIBS@

lib_LTLIBRARIES = libnrm-ompt.la

libnrm_ompt_la_SOURCES = entrypoint.c \
			 nrm_omp_callbacks.c \
			 nrm_omp.h
