Development environment can be fetched via Nix and direnv

```
CC=clang CXX=clang++ meson setup build/
meson compile -C build/
./build/nix-meson-glfw
```
