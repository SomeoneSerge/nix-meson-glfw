with (import <nixpkgs> { });

mkShell {
  nativeBuildInputs = [ pkgconfig meson valgrind ];
  buildInputs = [ clang ninja glfw3 glew x11 ];
}
