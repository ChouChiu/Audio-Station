core_include = include_directories('.')
core_moc = qt6.compile_moc(
    headers: ['neuralprocessingthread.h', 'processingthread.h'],
    include_directories: core_include,
    dependencies: [audio_dep, i18n_dep, neural_dep, qt_core_dep, qt_network_dep],
)

core_library = static_library(
    'mr_core',
    [
        'accompanimentfinder.cpp',
        'filepaths.cpp',
        'neuralpaths.cpp',
        'neuralprocessingthread.cpp',
        'processingthread.cpp',
        core_moc,
    ],
    include_directories: core_include,
    dependencies: [audio_dep, i18n_dep, neural_dep, qt_core_dep, qt_network_dep],
)

core_dep = declare_dependency(
    link_with: core_library,
    include_directories: core_include,
    dependencies: [audio_dep, i18n_dep, neural_dep, qt_core_dep, qt_network_dep],
)
