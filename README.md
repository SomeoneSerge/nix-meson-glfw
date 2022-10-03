Development environment can be fetched via Nix and direnv

```
CC=clang CXX=clang++ meson setup build/
meson compile -C build/
./build/nix-meson-glfw
```

## Without nix/direnv

The project can be built via meson.
Dependencies are discovered via pkg-config.

## Leaks

Either glfw or libX11 or nvidia (likely the latter) is causing memory leaks even in simplest setups:

```
==284505== LEAK SUMMARY:
==284505==    definitely lost: 7,176 bytes in 4 blocks
==284505==    indirectly lost: 136,665 bytes in 752 blocks
==284505==      possibly lost: 0 bytes in 0 blocks
==284505==    still reachable: 56,155 bytes in 841 blocks
==284505==         suppressed: 0 bytes in 0 blocks
```
