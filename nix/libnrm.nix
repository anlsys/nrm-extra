{ stdenv, autoreconfHook, pkgconfig, zeromq, czmq, gfortran, jansson, check, protobufc, git }:
stdenv.mkDerivation {
  src = fetchGit {
    url = "https://github.com/anlsys/libnrm.git";
    ref = "master";
  };
  name = "libnrm";
  nativeBuildInputs = [ autoreconfHook pkgconfig git ];
  buildInputs = [ zeromq gfortran czmq jansson check protobufc ];
}
