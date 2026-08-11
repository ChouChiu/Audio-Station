[app]
title = Audio Station
project_dir = .
input_file = deployment/main.py
exec_directory = dist
project_file = pyproject.toml
icon =

[python]
python_path =
packages = Nuitka==4.1.3,ordered-set,zstandard,patchelf
android_packages = 

[qt]
qml_files = 
excluded_qml_plugins = 
modules = Concurrent,Core,DBus,Gui,Multimedia,Network,Widgets
plugins = platforms,multimedia

[android]
wheel_pyside = 
wheel_shiboken = 
plugins = 

[nuitka]
macos.permissions = 
mode = standalone
extra_args = --quiet --noinclude-qt-translations --include-package=app --include-package=entrypoints --include-package=features --include-package=resources --include-package-data=resources --include-package=shared --include-package=qfluentwidgets --include-package=scipy._external.array_api_compat.numpy --include-module=onnxruntime.capi._pybind_state

[buildozer]
mode = debug
recipe_dir = 
jars_dir = 
ndk_path = 
sdk_path = 
local_libs = 
arch = 
