# MR Remover

基于 C++20 与 Qt 6 的人声提取器。项目按 DSP、音频 I/O、国际化、处理管线、UI、应用入口分层，使用 Meson/Ninja 构建，并集成 Clang-Tidy 静态检查。

## 功能

- 7 种提取算法：无损模式、软掩码、谱减法、Wiener 滤波、频率加权、二值掩码、相位敏感
- FFT 互相关自动估计并校正伴奏时间偏移
- 根据文件名相似度与关键词自动查找伴奏
- 中文、日本語、한국어界面
- 支持 Qt Multimedia 可解码的 MP3、WAV、FLAC、M4A 等格式
- 无损模式输出 PCM 24-bit，其余模式输出 PCM 16-bit WAV

## 依赖

- Meson 1.4 或更高版本
- Ninja
- 支持 C++20 的编译器
- Qt 6.5 或更高版本：Core、Widgets、Multimedia、Svg
- FFTW3、libsoxr
- Clang-Tidy（仅运行 linter 时需要）

Arch Linux 可安装：

```bash
sudo pacman -S meson ninja clang qt6-base qt6-multimedia qt6-multimedia-ffmpeg qt6-svg fftw libsoxr
```

Qt-Fluent-Widgets 会在首次配置时由 Meson 下载，并固定到已验证的提交。

## 构建与运行

```bash
meson setup build --buildtype=release
meson compile -C build
./build/src/app/mr_remover
```

安装到指定前缀：

```bash
meson setup build --prefix=/usr/local
meson compile -C build
meson install -C build
```

命令行批处理使用 `QCoreApplication`，不需要图形显示服务：

```bash
./build/src/app/mr_remover --process <song> <accompaniment> <output.wav> \
  --algorithm lossless --strength 50 --sigma 1 --align on --lang zh_cn
```

算法键名为 `lossless`、`soft_mask`、`spectral_subtraction`、`wiener_filter`、`frequency_weighted`、`binary_mask`、`phase_sensitive`。

## 测试与静态检查

```bash
meson test -C build --print-errorlogs
meson compile -C build clang-tidy
```

测试包含 DSP 数值/边界、WAV 写出、伴奏匹配、翻译资源一致性，以及无显示服务的 GUI 启动检查。Clang-Tidy 只分析 `src/` 与 `tests/` 中的第一方翻译单元，排除下载的第三方代码，并将诊断视为错误。

## 项目结构

```text
src/
├── dsp/      纯 DSP 算法，仅依赖 FFTW3
├── audio/    音频解码、重采样与 WAV 写出
├── i18n/     内嵌 JSON 字符串资源
├── core/     后台处理管线与伴奏匹配
├── ui/       Qt Fluent 图形界面
└── app/      GUI/CLI 入口
tests/        自动化测试
tools/        数值参考与 Clang-Tidy 驱动脚本
subprojects/  固定版本的 Meson 依赖描述与 overlay
```

模块依赖保持单向：主链为 `dsp <- audio <- core <- ui <- app`，`i18n` 作为共享资源由 `core`、`ui` 与 `app` 使用。

## 许可

AGPL-3.0
