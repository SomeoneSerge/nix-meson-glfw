project('nix-meson-glfw', 'cpp',
  version: '0.1',
  default_options: ['cpp_std=c++20'])

cpp_args = []
link_args = []

if target_machine.system() == 'darwin'
  cpp_args += [ '-mmacosx-version-min=10.15' ]
endif

glfw3 = dependency('glfw3', version: '>=3.0.0')
glew = dependency('glew', version: '>=2.2.0')

json = dependency('nlohmann_json')
oiio = dependency('openimageio2')
openexr = dependency('OpenEXR')
msgpack = dependency('msgpack')
clipp = dependency('clipp')

# torch_modules = [
#   'Threads::Threads', 'protobuf::libprotobuf', 'caffe2::cuda',
#   'torch::cudart', 'torch::nvtoolsext', 'caffe2::cudnn', 'caffe2::curand',
#   'caffe2::cufft', 'caffe2::cublas', 'caffe2::nvrtc', 'caffe2::mkl',
#   'caffe2::mkldnn', 'c10_cuda', 'c10', 'torch_cpu',
#   'torch_cuda', 'torch']
torch_modules = [
  'torch',
  'c10',
  'torch_cpu',
  'Threads::Threads',
  ]
torch_cuda_modules = [
  'torch_cuda',
  ]

# libtorch is a wrapper that selects cpu or cuda at runtime?
link_args += ['-Wl,--no-as-needed,-ltorch', '-Wl,--as-needed']

torch_cuda = dependency('Torch',
  method: 'cmake',
  modules: torch_modules + torch_cuda_modules,
  required: false,
  )

if torch_cuda.found()
  torch = torch_cuda
else
  message('Falling back to torch_cpu')
  torch = dependency('Torch',
    method: 'cmake',
    modules: torch_modules,
    required: true)
endif

imgui = subproject('imgui')
imgui_dep = imgui.get_variable('imgui_dep')

implot = subproject('implot')
implot_dep = implot.get_variable('implot_dep')

executable('nix-meson-glfw', ['app.cpp', 'raii.cpp', 'utils.cpp'],
  include_directories: ['./include'],
  dependencies: [ glfw3, glew, imgui_dep, implot_dep, oiio, openexr, clipp, msgpack, json, torch ],
  cpp_args: cpp_args,
  objcpp_args: cpp_args,
  link_args: link_args,
  install: true)
