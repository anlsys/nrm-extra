# see semver.org for version info
AC_INIT([nrm-extra],[0.1.0],[swann@anl.gov])

# are we in the right source dir ?
AC_CONFIG_SRCDIR([src/mpi/mpi_api.c])

# build artefacts in separate dir
AC_CONFIG_AUX_DIR([m4])
AC_CONFIG_MACRO_DIR([m4])

# automake should fail on any error
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects 1.12])

# check for programs
AC_LANG([C])
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_CPP
AC_TYPE_SIZE_T
AC_TYPE_INTPTR_T

PKG_CHECK_MODULES([LIBNRM],[libnrm])
PKG_CHECK_MODULES([PAPI], [papi])
PKG_CHECK_MODULES([HWLOC], [hwloc])
PKG_CHECK_MODULES([GEOPM], [geopm_pio, geopm_topo])

AC_ARG_WITH([variorum],
	    [AS_HELP_STRING([--with-variorum],
			    [Use libvariorum @<:@default=no@:>@])],
	    [with_variorum=$withval], [with_variorum=no])
AS_IF([test "x$with_variorum" != "xno"],
      [
       PKG_CHECK_MODULES([VARIORUM],[variorum])
       PKG_CHECK_MODULES([JANSSON], [jansson])
       have_variorum=1
      ],
      [
       have_variorum=0
      ]
)
AM_CONDITIONAL([HAVE_VARIORUM],[$have_variorum])
AC_DEFINE([HAVE_VARIORUM],[$have_variorum], [variorum support])
AC_SUBST([HAVE_VARIORUM])

AC_ARG_WITH([geopm],
	    [AS_HELP_STRING([--with-geopm],
			    [Use libgeopm @<:@default=no@:>@])],
	    [with_geopm=$withval], [with_geopm=no])
AS_IF([test "x$with_geopm != "xno"],
      [
       PKG_CHECK_MODULES([GEOPM], [geopm_pio, geopm_topo])
       have_geopm=1
      ],
      [
       have_geopm=0
      ]
)
AM_CONDITIONAL([HAVE_GEOPM],[$have_geopm])
AC_DEFINE([HAVE_GEOPM],[$have_geopm], [geopm support])
AC_SUBST([HAVE_GEOPM])

AX_MPI([], [AC_MSG_ERROR(MPI compiler needed but couldn't be found)])

AC_OPENMP
save_CFLAGS="$CFLAGS"
CFLAGS="$OPENMP_CFLAGS"
AC_CHECK_HEADER([omp-tools.h])
CFLAGS="$save_CFLAGS"

AC_LANG_PUSH([Fortran])

AC_PROG_FC
AC_FC_WRAPPERS
AX_MPI([], [AC_MSG_ERROR(MPI compiler needed but couldn't be found)])

AC_LANG_POP()

AM_PROG_AR

# check for libtool
LT_INIT
AC_SEARCH_LIBS([dlsym], [dl dld], [], [
  AC_MSG_ERROR([unable to find the dlsym() function])
])
AC_CHECK_LIB(m, ceil,,[AC_MSG_ERROR([missing libmath])])

AC_CONFIG_HEADERS([src/config.h])

AC_CONFIG_FILES([Makefile
		 src/Makefile
		 src/mpi/Makefile
		 ctests/Makefile
		 ctests/mpi/Makefile
		 ctests/omp/Makefile
		 ftests/Makefile])
AC_OUTPUT

# print out what was configured
cat <<EOF
-------------------------------------------------------------------------------
NRM-EXTRA

Version: $PACKAGE_VERSION

FEATURES:
---------

LIBNRM:
=======

CFLAGS:  $LIBNRM_CFLAGS
LDFLAGS: $LIBNRM_LIBS

HWLOC:
=======

CFLAGS:  $HWLOC_CFLAGS
LDFLAGS: $HWLOC_LIBS

PAPI:
=======

CFLAGS:  $PAPI_CFLAGS
LDFLAGS: $PAPI_LIBS

VARIORUM:
=======

Active:  $have_variorum
CFLAGS:  $VARIORUM_CFLAGS $JANSSON_CFLAGS
LDFLAGS: $VARIORUM_LIBS $JANSSON_LIBS

GEOPM:
=======

Active:  $have_geopm
CFLAGS:  $GEOPM_CFLAGS
LDFLAGS: $GEOPM_LIBS

-------------------------------------------------------------------------------
EOF
