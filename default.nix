{ pkgs ? import (builtins.fetchTarball "https://github.com/NixOS/nixpkgs/archive/21.05.tar.gz") {}
}:
pkgs // rec {
  libnrm = pkgs.callPackage ./nix/libnrm.nix {};
  nrm-extra = pkgs.callPackage ./nix/nrm-extra.nix { inherit libnrm; };
}
