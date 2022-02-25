{ stdenv, autoreconfHook, pkgconfig, libnrm, gfortran, mpich2, openmp, papi }:
stdenv.mkDerivation {
  src = ../.;
  name = "nrm-extra";
  nativeBuildInputs = [ autoreconfHook pkgconfig libnrm mpich2 openmp papi ];
  buildInputs = [ gfortran ] ++ libnrm.buildInputs;
}
