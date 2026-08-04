dsp_include = include_directories('.')

dsp_library = static_library(
    'mr_dsp',
    'dsp.cpp',
    include_directories: dsp_include,
    dependencies: fftw3_dep,
)

dsp_dep = declare_dependency(
    link_with: dsp_library,
    include_directories: dsp_include,
    dependencies: fftw3_dep,
)
