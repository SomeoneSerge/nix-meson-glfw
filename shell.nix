with (import <nixpkgs> { });

let
  oiio-pc = writeTextDir "lib/pkgconfig/openimageio2.pc" ''
    includedir=${openimageio2.dev}/include
    libdir=${openimageio2.out}/lib

    Name: openimageio2
    Description: ${openimageio2.meta.description}
    Version: ${openimageio2.version}
    Cflags: -I''${includedir}
    Libs: -L''${libdir} -lOpenImageIO -lOpenImageIO_Util
  '';
in
mkShell {
  nativeBuildInputs = [ pkgconfig meson valgrind ];
  buildInputs = [ clang ninja glfw3 glew xorg.libX11 oiio-pc openexr.dev ];
}
