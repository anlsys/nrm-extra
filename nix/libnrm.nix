{ stdenv, autoreconfHook, pkgconfig, zeromq, czmq, jansson, check, protobufc, git, hwloc, bats }:
stdenv.mkDerivation {
  src = fetchGit {
    url = "https://github.com/anlsys/libnrm.git";
    ref = "master";
  };
  name = "libnrm";
  prePatch = "echo 0.8.0 > .tarball-version";
  nativeBuildInputs = [ autoreconfHook pkgconfig git ];
  buildInputs = [ czmq jansson check protobufc hwloc bats ];
  propagatedBuildInputs = [ zeromq ];
}
