project(
    'mr_remover',
    'cpp',
    version: '1.0.0',
    meson_version: '>=1.4.0',
    default_options: [
        'cpp_std=c++20',
        'warning_level=3',
        'buildtype=debugoptimized',
    ],
)

qt6 = import('qt6')
python = import('python').find_installation()
cpp = meson.get_compiler('cpp')
if cpp.has_argument('-Wno-sfinae-incomplete')
    # GCC 16 can emit this from Qt 6 system headers; it is not actionable here.
    add_project_arguments('-Wno-sfinae-incomplete', language: 'cpp')
endif

project_config = configuration_data()
project_config.set_quoted('MR_REMOVER_VERSION', meson.project_version())
configure_file(output: 'config.h', configuration: project_config)
project_config_include = include_directories('.')

qt_core_dep = dependency('qt6', modules: ['Core'], version: '>=6.5')
qt_multimedia_dep = dependency('qt6', modules: ['Multimedia'], version: '>=6.5')
qt_widgets_dep = dependency('qt6', modules: ['Widgets'], version: '>=6.5')
qt_network_dep = dependency('qt6', modules: ['Network'], version: '>=6.5')
fftw3_dep = dependency('fftw3')
soxr_dep = dependency('soxr')

qfw_project = subproject(
    'qt-fluent-widgets',
    default_options: ['warning_level=0', 'werror=false'],
)
qfw_dep = qfw_project.get_variable('qtfluentwidgets_dep')

# Layers depend in one direction only:
# dsp <- audio + i18n <- core <- ui <- app
subdir('src')
subdir('tests')

clang_tidy = find_program('clang-tidy', required: false)
if clang_tidy.found()
    run_target(
        'clang-tidy',
        command: [
            python,
            files('tools/run_clang_tidy.py'),
            '--clang-tidy',
            clang_tidy,
            '--source-dir',
            meson.project_source_root(),
            '--build-dir',
            meson.project_build_root(),
        ],
    )
endif

summary(
    {
        'Version': meson.project_version(),
        'C++ standard': get_option('cpp_std'),
        'Build type': get_option('buildtype'),
        'Clang-Tidy target': clang_tidy.found(),
    },
    section: 'Audio Station',
)
