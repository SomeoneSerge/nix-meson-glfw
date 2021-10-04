with (import <nixpkgs> {});

mkShell {
  nativeBuildInputs = [ pkgconfig meson ];
  buildInputs = [ clang ninja glfw3 glew x11 ];
}
