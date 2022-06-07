{ stdenv, autoreconfHook, pkgconfig, libnrm, gfortran, mpich2, openmp, papi, jansson, variorum, hwloc }:
stdenv.mkDerivation {
  src = ../.;
  name = "nrm-extra";
  nativeBuildInputs = [ autoreconfHook pkgconfig libnrm mpich2 openmp papi jansson variorum hwloc ];
  buildInputs = [ gfortran ] ++ libnrm.buildInputs;
  VARIORUM_CFLAGS = "-I${variorum}/include";
  VARIORUM_LIBS = "-L${variorum}/lib -lvariorum";
}
