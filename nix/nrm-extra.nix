{ stdenv, autoreconfHook, pkgconfig, libnrm }:
stdenv.mkDerivation {
  src = ../.;
  name = "nrm-extra";
  nativeBuildInputs = [ autoreconfHook pkgconfig libnrm ];
}
