{ stdenv
, autoreconfHook
, bats
, check
, czmq
, git
, hwloc
, jansson
, mpich
, openmp
, papi
, geopmd
, pkgconfig
, protobufc
, zeromq
}:
stdenv.mkDerivation {
  src = fetchGit {
    url = "https://github.com/anlsys/libnrm.git";
    ref = "master";
  };
  name = "libnrm";
  prePatch = "echo 0.8.0 > .tarball-version";
  nativeBuildInputs = [ autoreconfHook pkgconfig git ];
  buildInputs = [
    bats
    check
    czmq
    hwloc
    jansson
    mpich
    openmp
    papi
    geopmd
    protobufc
    zeromq
  ];
  propagatedBuildInputs = [ zeromq ];
}
