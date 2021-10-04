project('nix-meson-glfw', 'cpp',
  version: '0.1',
  default_options: ['cpp_std=c++17'])

glfw3 = dependency('glfw3', version: '>=3.0.0')
executable('nix-meson-glfw', 'app.cpp',
  dependencies: [ glfw3 ])
