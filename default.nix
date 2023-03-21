{ pkgs ? import (builtins.fetchTarball "https://github.com/NixOS/nixpkgs/archive/22.05.tar.gz") {},
}:
pkgs // rec {
  stdenv = pkgs.llvmPackages_12.stdenv;
  libnrm = pkgs.callPackage ./nix/libnrm.nix { };
  variorum = pkgs.callPackage ./nix/variorum.nix { };
  nrm-extra = pkgs.callPackage ./nix/nrm-extra.nix { inherit libnrm; inherit variorum; openmp = pkgs.llvmPackages_12.openmp; };
}
