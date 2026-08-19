# Audio Station

Audio Station 是使用 Python、PySide6 与 PySide6-Fluent-Widgets 重写的桌面人声分离工具，提供三种工作流：使用已知伴奏做单曲参考对消的 **MR Remove**、将多首音源自动排入整场录音并分段对消的 **完整舞台**，以及无需伴奏、由 UVR MDX-Net 驱动的 **AI 人声提取**。

## 功能

- Fluent Design 四个主页面；MR 工作区内含“单曲 / 全场”两个子页面，支持浅色、深色和跟随系统主题
- 中文、日本語、한국어即时切换
- 自动匹配参考伴奏、GCC-PHAT 全局对齐和局部时钟漂移跟踪
- 完整舞台多音源时间线：音源无需排序，自动定位完整歌曲和重复片段，Talk、广告与空场原样保留
- 7 种参考对消算法，包括立体声 2×2 MIMO 参考对消 + 中心聚焦模式
- UVR MDX-Net ONNX 推理，模型按需下载并校验 SHA-256
- GUI 和现代子命令 CLI 共用同一处理管线
- WAV、FLAC、OGG、MP3 等 libsndfile 格式，并通过 Qt Multimedia 回退解码 M4A 等格式
- 参考对消 + 中心聚焦模式输出 PCM 24-bit，其余模式输出 PCM 16-bit WAV
- 输出文件名可直接编辑；处理完成后可在应用内试听、拖动定位，并查看时长、采样率、位深、峰值、RMS 与文件大小
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
python -m entrypoints
```

## 命令行

参考伴奏对消：

```bash
audio-station mr <song> <accompaniment> <output.wav> \
  --algorithm reference_center --strength 75 --sigma 8 --align --lang zh_cn \
  [--center-extraction [--weak-vocal-protection]]
```

AI 人声提取：

```bash
audio-station ai <song> \
  [--output-dir <directory>] [--model mdxnet_1] [--models-dir <directory>]
```

GUI 默认只运行 `reference_center` 的简单参考对消：先用音乐起音特征处理现场录音与正式音源波形相关性很低的场景，同时以 GCC-PHAT 和局部漂移跟踪精细校正；随后用 1/3/8/16 秒窗口估计缓慢变化的立体声 2×2 增益矩阵并直接对消参考。需要时可单独开启“中置人声提取”，在约 80 Hz–14 kHz 内聚焦相干幻象中置；其下还可开启“弱人声保护”，用普通 Mid 保护较弱的现场人声而不恢复整层侧声。窗口越短越能跟踪快速音量变化，但也更容易把与参考同唱的内容纳入估计，默认 8 秒偏向稳定保护。旧算法键名 `soft_mask`、`spectral_subtraction`、`wiener_filter`、`frequency_weighted`、`binary_mask`、`phase_sensitive` 仅为 CLI 兼容保留，不再显示在 GUI。

“完整舞台”先为整场录音与每个音源建立多频带起音指纹，每个音源独立寻找最佳完整匹配，再按识别到的舞台时间自动排序；短广告或片头、片尾再次使用同一首歌时，会生成带 `source in/out` 的额外片段实例。自动排布完成后可在时间线中检查舞台位置、音源范围和置信度；每个项目都能单独取消消音，舞台时间与音源截取范围也能双击修改，再执行分段精细对齐与参考对消。参数与单曲 MR Remove 一致，包含对消强度、增益跟踪窗口、自动精细对齐、中置人声提取和弱人声保护。未匹配区间不会补零或裁掉，因此 Talk、观众互动、广告和空场会保持原始长度与内容。

AI 模型输出 `<歌曲名>_vocal.wav` 与 `<歌曲名>_background.wav`。模型搜索顺序为 `--models-dir`、`MR_REMOVER_MODELS`、系统应用数据目录、开发仓库 `models/`。权重缺失时会从 UVR 模型仓库下载到系统应用数据目录；模型不会打入 Python wheel 或 standalone 程序。

## 项目结构

```text
src/
├── app/               应用壳、MR 子页面容器、主窗口和后台任务适配器
├── features/
│   ├── reference_removal/  伴奏匹配、任务模型、处理管线、DSP 与页面
│   ├── full_stage/         多音源指纹匹配、时间线模型与完整舞台页面
│   ├── neural_separation/  模型目录、下载、MDX-Net 管线与页面
│   ├── home/               首页功能
│   └── settings/           设置功能
├── shared/
│   ├── audio/         流式解码、临时 PCM/memmap、重采样和原子 WAV 写出
│   ├── dsp/           两种分离功能共用的 STFT、iSTFT 和频谱工具
│   ├── ui/            Fluent 复合控件
│   └── config.py      跨功能持久化配置
├── resources/         三语翻译和模型元数据
└── entrypoints/       GUI 与 CLI 入口
tests/                 按 app、features、shared、entrypoints 镜像源码结构
deployment/            standalone 专用入口
```

模块依赖保持单向：`shared` 不依赖任何功能，功能包聚合自身的模型、处理、算法和页面，`app` 只负责跨功能编排，`entrypoints` 负责启动应用。新增功能通常只需增加一个 `features/<feature>/` 目录。

## 测试与检查

```bash
.venv/bin/ruff check src tests
.venv/bin/ruff format --check src tests
QT_QPA_PLATFORM=offscreen .venv/bin/pytest
QT_QPA_PLATFORM=offscreen .venv/bin/pytest tests/benchmarks --runslow
QT_QPA_PLATFORM=offscreen .venv/bin/audio-station --selftest
python -m build
```

测试覆盖 STFT/iSTFT、全部参考算法、时间对齐、立体声 MIMO、完整舞台自动排布与分段渲染、音频读写、重采样、伴奏匹配、翻译、MDX-Net 分块重叠相加、GUI 导航及端到端参考处理。`--runslow` 会执行 15 分钟、44.1 kHz、双声道 `reference_center` RSS/长度/接缝门禁，目标峰值为 1.5 GiB。合成 DSP 指标只用于回归，不代表真实音乐数据集表现。

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
