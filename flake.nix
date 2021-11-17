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
        };
      });
      pkgs = import nixpkgs { inherit system; overlays = [ overlay ]; };
    in
    {
      packages = {
        inherit (pkgs) viscor oiio-pc;
        _pkgs = pkgs;
      };
      defaultPackage = pkgs.viscor;
    });
}
