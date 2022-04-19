{ stdenv, cmake, pkgconfig, jansson, hwloc }:
stdenv.mkDerivation {
  src = fetchGit {
    url = "https://github.com/anlsys/variorum.git";
    ref = "dev";
  };
  name = "variorum";
  cmakeDir = "../src";
  cmakeFlags = [ "-DENABLE_MPI=OFF" "-DENABLE_FORTRAN=OFF" ];
  nativeBuildInputs = [ cmake pkgconfig jansson hwloc ];
  buildInputs = [ ];
}
