{ pkgs ? import (builtins.fetchTarball "https://github.com/NixOS/nixpkgs/archive/22.11.tar.gz") {},
}:
pkgs // rec {
  stdenv = pkgs.stdenvAdapters.keepDebugInfo pkgs.llvmPackages_12.stdenv;
  libnrm = pkgs.callPackage ./nix/libnrm.nix { inherit stdenv; };
  variorum = pkgs.callPackage ./nix/variorum.nix { };
  nrm-extra = pkgs.callPackage ./nix/nrm-extra.nix { inherit libnrm; inherit variorum; };
}
