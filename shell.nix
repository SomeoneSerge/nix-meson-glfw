with (import <nixpkgs> { });

mkShell {
  nativeBuildInputs = [ pkgconfig meson ];
  buildInputs = [ clang ninja glfw3 ];
}
