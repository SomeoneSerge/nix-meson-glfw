project('nix-meson-glfw', 'cpp',
  version: '0.1',
  default_options: ['cpp_std=c++20'])

glfw3 = dependency('glfw3', version: '>=3.0.0')
glew = dependency('glew', version: '>=2.2.0')

json = dependency('nlohmann_json')
oiio = dependency('openimageio2')
openexr = dependency('OpenEXR')
msgpack = dependency('msgpack')
clipp = dependency('clipp')
eigen3 = dependency('eigen3')

imgui = subproject('imgui')
imgui_dep = imgui.get_variable('imgui_dep')

implot = subproject('implot')
implot_dep = implot.get_variable('implot_dep')

executable('nix-meson-glfw', 'app.cpp',
  dependencies: [ glfw3, glew, imgui_dep, implot_dep, oiio, openexr, clipp, msgpack, json, eigen3 ],
  install: true)
