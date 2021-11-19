{
  description = "Visualizer for soft image correspondences";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-21.05";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }: flake-utils.lib.eachDefaultSystem (system:
    let
      overlay = (final: prev: {
        oiio-pc = final.callPackage ./oiio-pc.nix { };
        viscor = final.callPackage ./release.nix {
          inherit (final.darwin.apple_sdk.frameworks) Cocoa OpenGL CoreVideo IOKit;
          # pytorch is broken on Darwin
          libtorch = if final.stdenv.isDarwin then final.libtorch-bin else
          let p = final.python3Packages.pytorch; in
          final.symlinkJoin {
            name = "libtorch-unsplit";
            paths = [ p.lib p.dev ];
          };
        };
        viscor-cuda = final.viscor.override {
          libtorch = final.libtorch-bin.override { cudaSupport = true; };
          cudaSupport = true;
        };
      });
      pkgs = import nixpkgs { inherit system; overlays = [ overlay ]; };
      pkgsUnfree = import nixpkgs {
        inherit system;
        config.allowUnfreePredicate = pkg: builtins.elem (pkgs.lib.getName pkg) [
          "libtorch"
          "cudatoolkit"
          "libtorch-bin"
        ];
        overlays = [ overlay ];
      };
    in
    {
      packages = {
        inherit (pkgs) viscor oiio-pc;
        inherit (pkgsUnfree) viscor-cuda;
        _pkgs = pkgs;
        _pkgsUnfree = pkgsUnfree;
      };
      defaultPackage = pkgsUnfree.viscor;
    });
}
