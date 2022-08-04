{ stdenv, autoreconfHook, pkgconfig, zeromq, czmq, jansson, check, protobufc, git }:
stdenv.mkDerivation {
  src = fetchGit {
    url = "https://github.com/anlsys/libnrm.git";
    ref = "master";
  };
  name = "libnrm";
  nativeBuildInputs = [ autoreconfHook pkgconfig git ];
  buildInputs = [ zeromq czmq jansson check protobufc ];
}
