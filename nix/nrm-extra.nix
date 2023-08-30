{ stdenv, autoreconfHook, pkgconfig, libnrm, papi, jansson, variorum, hwloc, git  }:
stdenv.mkDerivation {
  src = ../.;
  name = "nrm-extra";
  nativeBuildInputs = [ autoreconfHook pkgconfig git ];
  buildInputs = [ libnrm papi jansson variorum hwloc ];
  configureFlags = "--with-variorum";
  VARIORUM_CFLAGS = "-I${variorum}/include";
  VARIORUM_LIBS = "-L${variorum}/lib -lvariorum";
  CFLAGS = "-Wall -Wextra";
}
