project('implot', 'cpp',
    version: '0.11',
    license: 'MIT',
)

include_dirs = include_directories('.')
sources = ['implot.cpp', 'implot_items.cpp']

imgui = subproject('imgui')
imgui_dep = imgui.get_variable('imgui_dep')

# Would be very nice to delete all this stuff and take dependencies as "inputs" from the downstream,
# but heck...

# Or maybe implot doesn't touch those explicitly?
# Let's try

implot = library('implot',
    sources,
    include_directories: include_dirs,
    dependencies: [ imgui_dep ]
)

implot_dep = declare_dependency(include_directories: include_dirs, link_with: implot)
