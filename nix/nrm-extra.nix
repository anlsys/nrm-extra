{ stdenv, autoreconfHook, pkgconfig, libnrm, gfortran, mpich2 }:
stdenv.mkDerivation {
  src = ../.;
  name = "nrm-extra";
  nativeBuildInputs = [ autoreconfHook pkgconfig libnrm mpich2 ];
  buildInputs = [ gfortran ];
}
