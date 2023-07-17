{ stdenv, autoreconfHook, pkgconfig, libnrm, gfortran, mpich, openmp, papi, jansson, variorum, hwloc, git, geopmd }:
stdenv.mkDerivation {
  src = ../.;
  name = "nrm-extra";
  nativeBuildInputs = [ autoreconfHook pkgconfig git ];
  buildInputs = [ gfortran libnrm mpich openmp papi jansson variorum hwloc geopmd ];
  configureFlags = "--with-geopm --with-variorum";
  VARIORUM_CFLAGS = "-I${variorum}/include";
  VARIORUM_LIBS = "-L${variorum}/lib -lvariorum";
  GEOPM_CFLAGS = "-I${geopmd}/include";
  GEOPM_LIBS = "-L${geopmd}/lib -lgeopmd";
  CFLAGS = "-Wall -Wextra";
}
