audio_include = include_directories('.')

audio_library = static_library(
    'mr_audio',
    'audio.cpp',
    include_directories: audio_include,
    dependencies: [dsp_dep, qt_multimedia_dep, soxr_dep],
)

audio_dep = declare_dependency(
    link_with: audio_library,
    include_directories: audio_include,
    dependencies: [dsp_dep, qt_multimedia_dep, soxr_dep],
)
