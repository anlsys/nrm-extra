{ stdenv, autoreconfHook, pkgconfig, libnrm, gfortran, mpich2, openmp, papi, jansson, variorum }:
stdenv.mkDerivation {
  src = ../.;
  name = "nrm-extra";
  nativeBuildInputs = [ autoreconfHook pkgconfig libnrm mpich2 openmp papi jansson variorum ];
  buildInputs = [ gfortran ] ++ libnrm.buildInputs;
}
