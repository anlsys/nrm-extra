{ stdenv, autoreconfHook, pkgconfig, libnrm, gfortran, mpich2, openmp }:
stdenv.mkDerivation {
  src = ../.;
  name = "nrm-extra";
  nativeBuildInputs = [ autoreconfHook pkgconfig libnrm mpich2 openmp ];
  buildInputs = [ gfortran ] ++ libnrm.buildInputs;
}
