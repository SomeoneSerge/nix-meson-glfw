project('nix-meson-glfw', 'cpp',
  version: '0.1',
  default_options: ['cpp_std=c++17'])

glfw3 = dependency('glfw3', version: '>=3.0.0')
glew = dependency('glew', version: '>=2.2.0')

imgui = subproject('imgui')
imgui_dep = imgui.get_variable('imgui_dep')

implot = subproject('implot')
implot_dep = implot.get_variable('implot_dep')

clipp = subproject('muellan-clipp')
clipp_dep = clipp.get_variable('clipp_dep')

eigen = subproject('eigen')
eigen_dep = eigen.get_variable('eigen_dep')

oiio = dependency('openimageio2')
openexr = dependency('OpenEXR')

msgpack = dependency('msgpack')

executable('nix-meson-glfw', 'app.cpp',
  dependencies: [ glfw3, glew, imgui_dep, implot_dep, oiio, openexr, clipp_dep, msgpack, eigen_dep ])
