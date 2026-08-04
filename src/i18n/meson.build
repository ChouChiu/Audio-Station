i18n_include = include_directories('.')

i18n_library = static_library(
    'mr_i18n',
    'strtable.cpp',
    include_directories: i18n_include,
    dependencies: qt_core_dep,
)

i18n_resources = qt6.compile_resources(
    sources: 'i18n.qrc',
)

i18n_dep = declare_dependency(
    link_with: i18n_library,
    include_directories: i18n_include,
    dependencies: qt_core_dep,
)
