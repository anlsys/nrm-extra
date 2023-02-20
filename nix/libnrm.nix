{ stdenv, autoreconfHook, pkgconfig, zeromq, czmq, jansson, check, protobufc, git, hwloc }:
stdenv.mkDerivation {
  src = fetchGit {
    url = "https://github.com/anlsys/libnrm.git";
    ref = "feature/scope-creation-api";
    rev = "cf84cb9e030ce9cd7ca1a180efe0ac8f1843d968";
  };
  name = "libnrm";
  prePatch = "echo 0.8.0 > .tarball-version";
  nativeBuildInputs = [ autoreconfHook pkgconfig git ];
  buildInputs = [ czmq jansson check protobufc hwloc ];
  propagatedBuildInputs = [ zeromq ];
}
