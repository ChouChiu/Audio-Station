neural_include = include_directories('.')

onnxruntime_dir = meson.project_source_root() / 'third_party' / 'onnxruntime'
onnxruntime_inc = include_directories('../../third_party/onnxruntime/include')
onnxruntime_lib = cpp.find_library('onnxruntime', dirs: [onnxruntime_dir / 'lib'])
onnxruntime_dep = declare_dependency(
    include_directories: onnxruntime_inc,
    dependencies: onnxruntime_lib,
    # 开发构建: 从源码树加载 libonnxruntime.so.1 (不随 meson install 安装)
    link_args: ['-Wl,-rpath,' + onnxruntime_dir / 'lib'],
)

neural_library = static_library(
    'mr_neural',
    ['mdxnet.cpp', 'modelcatalog.cpp', 'onnxsession.cpp'],
    include_directories: neural_include,
    dependencies: [dsp_dep, onnxruntime_dep],
)

neural_dep = declare_dependency(
    link_with: neural_library,
    include_directories: neural_include,
    dependencies: [dsp_dep, onnxruntime_dep],
)
