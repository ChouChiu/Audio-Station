# Audio Station

Audio Station 是使用 Python、PySide6 与 PySide6-Fluent-Widgets 重写的桌面人声分离工具，提供两条处理链：使用已知伴奏做参考对消的 **MR Remove**，以及无需伴奏、由 UVR MDX-Net 驱动的 **AI 人声提取**。

## 功能

- Fluent Design 四页桌面界面，支持浅色、深色和跟随系统主题
- 中文、日本語、한국어即时切换
- 自动匹配参考伴奏、GCC-PHAT 全局对齐和局部时钟漂移跟踪
- 7 种参考对消算法，包括立体声 2×2 MIMO 无损模式
- UVR MDX-Net ONNX 推理，模型按需下载并校验 SHA-256
- GUI 和现代子命令 CLI 共用同一处理管线
- WAV、FLAC、OGG、MP3 等 libsndfile 格式，并通过 Qt Multimedia 回退解码 M4A 等格式
- PCM 24-bit 无损模式输出，其余模式输出 PCM 16-bit WAV
- 设置页可即时切换并持久化 `DEBUG`、`INFO`、`WARNING`、`ERROR`、`CRITICAL` 日志等级

Python、后台任务和 Qt 消息统一使用单行日志格式：

```text
YYYY-MM-DD HH:MM:SS.mm [TYPE] MODULE: MESSAGE
```

## 安装

需要 Python 3.11 或更新版本。务必使用独立虚拟环境；不要混装 PyQt/PySide 的其他 Fluent Widgets 包，它们都导出 `qfluentwidgets`。

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e '.[dev]' -i https://pypi.org/simple/
```

项目依赖完整版 Fluent Widgets：

```bash
pip install "PySide6-Fluent-Widgets[full]" -i https://pypi.org/simple/
```

运行 GUI：

```bash
audio-station
# 或
python -m audio_station
```

## 命令行

参考伴奏对消：

```bash
audio-station mr <song> <accompaniment> <output.wav> \
  --algorithm lossless --strength 75 --sigma 8 --align --lang zh_cn
```

AI 人声提取：

```bash
audio-station ai <song> \
  [--output-dir <directory>] [--model mdxnet_1] [--models-dir <directory>]
```

算法键名为 `lossless`、`soft_mask`、`spectral_subtraction`、`wiener_filter`、`frequency_weighted`、`binary_mask`、`phase_sensitive`。

AI 模型输出 `<歌曲名>_vocal.wav` 与 `<歌曲名>_background.wav`。模型搜索顺序为 `--models-dir`、`MR_REMOVER_MODELS`、系统应用数据目录、开发仓库 `models/`。权重缺失时会从 UVR 模型仓库下载到系统应用数据目录；模型不会打入 Python wheel 或 standalone 程序。

## 项目结构

```text
src/audio_station/
├── application/       公共任务、处理编排、取消、进度、翻译与伴奏匹配
├── audio/             流式解码、临时 PCM/memmap、重采样和原子 WAV 写出
├── dsp/
│   ├── transforms/    STFT、iSTFT 和频谱工具
│   ├── alignment/     全局与局部时间对齐
│   └── algorithms/    参考伴奏对消算法
├── neural/            模型目录、下载缓存和 MDX-Net ONNX 推理
├── presentation/
│   ├── pages/         主页、MR、AI 与设置页
│   └── widgets/       Fluent 复合控件
├── resources/         三语翻译和模型元数据
└── entrypoints/       GUI 与 CLI 入口
tests/                 unit、integration、gui、benchmarks
deployment/            standalone 专用入口
```

模块依赖保持单向：底层音频/DSP/神经网络不依赖界面；GUI 与 CLI 仅通过 `application` 的任务类型和处理服务调用它们。

## 测试与检查

```bash
.venv/bin/ruff check src/audio_station tests
.venv/bin/ruff format --check src/audio_station tests
QT_QPA_PLATFORM=offscreen .venv/bin/pytest
QT_QPA_PLATFORM=offscreen .venv/bin/pytest tests/benchmarks --runslow
QT_QPA_PLATFORM=offscreen .venv/bin/audio-station --selftest
python -m build
```

测试覆盖 STFT/iSTFT、全部参考算法、时间对齐、立体声 MIMO、音频读写、重采样、伴奏匹配、翻译、MDX-Net 分块重叠相加、GUI 导航及端到端参考处理。`--runslow` 会执行 15 分钟、44.1 kHz、双声道 lossless RSS/长度/接缝门禁，目标峰值为 1.5 GiB。合成 DSP 指标只用于回归，不代表真实音乐数据集表现。

## Linux standalone 发布

Qt 官方的 `pyside6-deploy` 是 Nuitka 的封装。本项目固定使用 `standalone` 目录模式：

```bash
source .venv/bin/activate
python -m pip install -e '.[deploy]'
pyside6-deploy -c pysidedeploy.spec
```

产物写入 `dist/`，包含 Python、Qt、Fluent Widgets、SciPy、SoundFile、soxr 和 ONNX Runtime，但不包含 ONNX 权重。构建产物和 Nuitka 中间生成的 C 文件不会纳入版本控制。

## 模型、致谢与许可

- AI 处理流程参考 [Ultimate Vocal Remover GUI](https://github.com/Anjok07/ultimatevocalremovergui) 的 MDX-Net 管线。
- 模型来自 [TRvlvr/model_repo](https://github.com/TRvlvr/model_repo)，模型许可与致谢要求以其发布页为准。
- ONNX Runtime 使用 MIT 许可。
- PySide6-Fluent-Widgets 开源版本使用 GPLv3；商业用途请确认上游许可。
- 本项目使用 AGPL-3.0-or-later。
