dsp_test = executable(
    'dsp_test',
    'dsp_test.cpp',
    dependencies: dsp_dep,
)

test('dsp', dsp_test, timeout: 120)

audio_test = executable(
    'audio_test',
    'audio_test.cpp',
    dependencies: audio_dep,
)
test('audio', audio_test, timeout: 30)

accompanimentfinder_test = executable(
    'accompanimentfinder_test',
    'accompanimentfinder_test.cpp',
    dependencies: core_dep,
)
test('accompaniment-finder', accompanimentfinder_test, timeout: 30)

test(
    'translations',
    python,
    args: [
        meson.project_source_root() / 'tools/check_translations.py',
        meson.project_source_root() / 'src/i18n',
    ],
    timeout: 30,
)

gui_test_environment = environment()
gui_test_environment.set('QT_QPA_PLATFORM', 'offscreen')
test(
    'gui-smoke',
    mr_remover,
    args: ['--selftest'],
    env: gui_test_environment,
    timeout: 10,
)
