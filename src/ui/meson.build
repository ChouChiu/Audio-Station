ui_include = include_directories('.')
ui_moc = qt6.compile_moc(
    headers: [
        'aipage.h',
        'fluentcombo.h',
        'fluentswitch.h',
        'homepage.h',
        'mainwindow.h',
        'mrpage.h',
        'settingspage.h',
    ],
    include_directories: ui_include,
    dependencies: [core_dep, i18n_dep, qfw_dep, qt_widgets_dep],
)

ui_library = static_library(
    'mr_ui',
    [
        'aipage.cpp',
        'fluentcombo.cpp',
        'fluentswitch.cpp',
        'homepage.cpp',
        'mainwindow.cpp',
        'mrpage.cpp',
        'settingspage.cpp',
        ui_moc,
    ],
    include_directories: ui_include,
    dependencies: [core_dep, i18n_dep, qfw_dep, qt_widgets_dep],
)

ui_dep = declare_dependency(
    link_with: ui_library,
    include_directories: ui_include,
    dependencies: [core_dep, i18n_dep, qfw_dep, qt_widgets_dep],
)
