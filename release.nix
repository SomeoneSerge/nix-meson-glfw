{ lib
, stdenv
, libstdcxxClang
, libcxx
, tree
, fetchFromGitHub
, unzip
, pkgconfig
, meson
, ninja
, glfw3
, glew
, xorg
, oiio-pc
, openexr
, msgpack
, clipp
, nlohmann_json
, eigen
, libglvnd
, OpenGL
, Cocoa
, CoreVideo
, IOKit
}:

let
  implot-src =
    let
      name = "implot";
      version = "0.11";
    in
    fetchFromGitHub {
      owner = "epezent";
      repo = name;
      name = "${name}-${version}";
      rev = "51930a5ae64587379c34fa54037e8ef6cbc7190f";
      sha256 = "sha256-V8rY4KkRN8VrZIbCUcMBqXEli27LnCFhQ+5mLw47yHo=";
    };
  imgui-src =
    let
      name = "imgui";
      version = "1.81";
    in
    fetchFromGitHub {
      owner = "ocornut";
      name = "${name}-${version}";
      repo = name;
      rev = "4df57136e9832327c11e48b5bfe00b0326bd5b63";
      sha256 = "sha256-rRkayXk3xz758v6vlMSaUu5fui6NR8Md3njhDB0gJ18=";
    };
in
stdenv.mkDerivation {
  pname = "nix-meson-glfw";
  version = "0.0.1";
  src = ./.;
  nativeBuildInputs = [ ninja meson pkgconfig ];
  buildInputs = [
    glfw3
    glew.dev
    oiio-pc
    openexr.dev
    msgpack
    clipp
    nlohmann_json
    eigen
    libglvnd.dev
    libcxx
  ]
  ++ lib.optionals stdenv.isLinux [ xorg.libX11 ]
  ++ lib.optionals stdenv.isDarwin [ OpenGL Cocoa CoreVideo IOKit ];

  # "auto" is precisely the behaviour we want
  # "enabled" -> meson tries to find garbage like d3d9
  # "diabled" -> meson doesn't try to find glfw3 either
  mesonAutoFeatures = "auto";

  CXXFLAGS = lib.optionalString stdenv.isDarwin "-DTARGET_OS_IOS=0 -DTARGET_OS_TV=0";
  LDFLAGS = lib.optionalString stdenv.isDarwin "-Wl,-L${libstdcxxClang.dev}/lib -Wl,-L${libcxx}/lib";

  # enables -O3 optimization
  mesonBuildType = "release";
  postPatch = ''
    mkdir -p subprojects/implot-0.11 subprojects/imgui-1.81
    cp -r subprojects/packagefiles/* subprojects/
    cp -r ${implot-src}/ subprojects/${implot-src.name}/ --no-target-directory
    cp -r ${imgui-src}/ subprojects/${imgui-src.name}/ --no-target-directory
  '';
}
