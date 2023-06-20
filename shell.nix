# development shell, includes aml dependencies and dev-related helpers
{ pkgs ? import ./. { } }:
with pkgs;
mkShell.override { stdenv = pkgs.stdenv; } {
  inputsFrom = [ nrm-extra ];
  nativeBuildInputs = [ autoreconfHook pkgconfig ];
  buildInputs = [
    # deps for debug
    gdb
    # style checks
    clang-tools
    python3
    llvmPackages.clang-unwrapped.python
    valgrind
  ];
  VARIORUM_CFLAGS = "-I${variorum}/include";
  VARIORUM_LIBS = "-L${variorum}/lib -lvariorum";
  GEOPM_CFLAGS = "-I${geopmd}/include";
  GEOPM_LIBS = "-L${geopmd}/lib -lgeopmd";
  CFLAGS = "-Wall -Wextra";
}
