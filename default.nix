{ pkgs ? import (builtins.fetchTarball "https://github.com/NixOS/nixpkgs/archive/23.05.tar.gz") {},
}:
pkgs // rec {
  stdenv = pkgs.stdenvAdapters.keepDebugInfo pkgs.llvmPackages_12.stdenv;
  variorum = pkgs.callPackage ./nix/variorum.nix { };
  geopmd = pkgs.callPackage ./nix/geopmd.nix { openmp = pkgs.llvmPackages_12.openmp; };
  libnrm = pkgs.callPackage ./nix/libnrm.nix { openmp = pkgs.llvmPackages_15.openmp; inherit geopmd; };
  nrm-extra = pkgs.callPackage ./nix/nrm-extra.nix { inherit libnrm; inherit variorum;};
}
