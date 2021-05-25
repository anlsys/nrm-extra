# development shell, includes aml dependencies and dev-related helpers
{ pkgs ? import ./. { } }:
with pkgs;
mkShell {
  inputsFrom = [ nrm-extra ] ++ libnrm.buildInputs;
  nativeBuildInputs = [ autoreconfHook pkgconfig ];
  buildInputs = [
    # deps for debug
    gdb
    # style checks
    clang-tools
  ];

  CFLAGS = "-Wall -Wextra";
}
