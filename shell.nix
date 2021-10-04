with (import <nixpkgs> { });

mkShell {
  buildInputs = [ meson clang ninja ];
}
