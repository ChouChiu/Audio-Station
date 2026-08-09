# Audio Station

基于 C++20 与 Qt 6 的人声提取器。项目按 DSP、神经网络推理、音频 I/O、国际化、处理管线、UI、应用入口分层，使用 Meson/Ninja 构建，并集成 Clang-Tidy 静态检查。

## 功能

- **AI 人声提取（去背景音）**：集成 UVR 的 MDX-Net 谱域模型（onnxruntime 推理），无需伴奏参考即可从歌曲中提取人声与背景音轨
- 7 种参考对消算法：无损模式、软掩码、谱减法、Wiener 滤波、频率加权、二值掩码、相位敏感
- FFT 粗对齐 + 小数采样/局部延迟轨迹，校正伴奏时间偏移与录制时钟漂移
- 无损模式使用立体声 2×2 自适应参考对消；参考中不存在的观众声与现场环境声不处理
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
- onnxruntime（C++ API，仅 AI 人声提取需要；通过 `tools/fetch_onnxruntime.py` 下载到 `third_party/`）
- Clang-Tidy（仅运行 linter 时需要）

Arch Linux 可安装：

```bash
sudo pacman -S meson ninja clang qt6-base qt6-multimedia qt6-multimedia-ffmpeg qt6-svg fftw libsoxr
```

Qt-Fluent-Widgets 会在首次配置时由 Meson 下载，并固定到已验证的提交。

## 构建与运行

首次构建需要拉取 onnxruntime 运行时（约 22 MB 共享库 + 头文件）。AI 模型
（默认 UVR-MDX-NET 1，约 30 MB）可在首次提取时自动下载，也可预下载：

```bash
python3 tools/fetch_onnxruntime.py      # -> third_party/onnxruntime/{lib,include}
python3 tools/download_models.py        # -> models/UVR_MDXNET_1_9703.onnx + model_data.json

meson setup build --buildtype=release
meson compile -C build
./build/src/app/mr_remover
```

两个脚本均可重复执行（幂等）。模型权重不入库（`.gitignore`），换机器或重克隆后重新执行即可。

安装到指定前缀：

```bash
meson setup build --prefix=/usr/local
meson compile -C build
meson install -C build
```

> 注意：AI 人声提取依赖源码树中的 onnxruntime 共享库（构建期写入 rpath），
> `meson install` 安装的副本暂不包含 AI 功能所需的运行时。

### GUI

左侧导航共四页：**主页**、**MR Remove**、**AI 人声提取**、**设置**。
主页中央为问候语与产品介绍，下方两个按钮分别跳转到 MR Remove 与 AI 人声提取页。

MR Remove 页（参考对消，需要伴奏）：选择歌曲、伴奏（支持按文件名自动查找）与输出文件，
设置算法/强度/混响追踪精度等参数后点击「开始处理」，输出 `<歌曲名>_vocals.wav`。

AI 人声提取页：选择歌曲文件与模型后点击「提取人声（去背景音）」，
在歌曲同目录生成 `<歌曲名>_vocal.wav` 与 `<歌曲名>_background.wav`。无需伴奏文件。

模型选择框按系列收录最强模型（同系列不收录更弱变体）：

| id | 模型 | 系列 | 说明 |
|---|---|---|---|
| `mdxnet_1` | UVR-MDX-NET 1（默认） | MDX-Net (UVR) | 经典系列最强（人声提取） |
| `mdxnet_main` | UVR-MDX-NET Main | MDX-Net (UVR) | 7680 大 FFT 全频带版本（低频分辨率更高） |
| `kim_vocal` | Kim Vocal 1 | Kim Vocal | Kim 系列人声专用模型 |
| `kuielab_b` | kuielab B Vocals | kuielab (MUSDB) | MDX-Net 论文作者 (kuielab) MUSDB 训练 |

模型首次使用时自动下载到 `models/` 目录（下载进度并入总进度）。也可用
`python3 tools/download_models.py [--model <id>|--all]` 预下载。

模型搜索顺序：`--models-dir` / 环境变量 `MR_REMOVER_MODELS` > 程序目录相邻 `models/` > 构建树 `models/` > 当前目录 `models/`。

### 命令行

经典批处理（参考对消，需要伴奏）：

```bash
./build/src/app/mr_remover --process <song> <accompaniment> <output.wav> \
  --algorithm lossless --strength 75 --sigma 1 --align on --lang zh_cn
```

AI 人声提取（去背景音，无需伴奏）：

```bash
./build/src/app/mr_remover --extract-vocal <song> [--out-dir <dir>] \
  [--model <id>] [--models-dir <dir>]
```

算法键名为 `lossless`、`soft_mask`、`spectral_subtraction`、`wiener_filter`、`frequency_weighted`、`binary_mask`、`phase_sensitive`；模型 id 见上表。

## 测试与静态检查

```bash
meson test -C build --print-errorlogs
meson compile -C build clang-tidy
```

测试包含 DSP 数值/边界、MDX-Net 分块重叠相加骨架（恒等模型回环）、WAV 写出、伴奏匹配、翻译资源一致性，以及无显示服务的 GUI 启动检查。Clang-Tidy 只分析 `src/` 与 `tests/` 中的第一方翻译单元，排除下载的第三方代码，并将诊断视为错误。

## 项目结构

```text
src/
├── dsp/      纯 DSP 算法，仅依赖 FFTW3
├── neural/   onnxruntime 封装 + UVR MDX-Net 人声分离（STFT 交织/分块推理/重叠相加）
├── audio/    音频解码、重采样与 WAV 写出
├── i18n/     内嵌 JSON 字符串资源
├── core/     后台处理管线（参考对消 + AI 提取）与伴奏匹配
├── ui/       Qt Fluent 图形界面
└── app/      GUI/CLI 入口
tests/        自动化测试
tools/        onnxruntime/模型拉取脚本、翻译检查、Clang-Tidy 驱动
third_party/  onnxruntime 头文件（fetch_onnxruntime.py 生成，不入库）
models/       UVR 模型权重与超参表（download_models.py 生成，不入库）
subprojects/  固定版本的 Meson 依赖描述与 overlay
```

模块依赖保持单向：主链为 `dsp <- neural <- core <- ui <- app`，`i18n` 作为共享资源由 `core`、`ui` 与 `app` 使用。

## AI 提取实现说明

- 管线为 UVR（ultimatevocalremovergui）MDX-Net 引擎的 C++ 移植：每 chunk 做 torch 语义 STFT
  （hann periodic、center=True reflect 填充、hop=1024），模型输入为 [1,4,3072,256] 频主序
  张量（两声道实部/虚部交织），输出人声频谱后经 iSTFT 还原，chunk 间用对称 Hann 窗重叠相加
  （UVR overlap=Default 语义：step = chunk − n_fft），两端裁剪 trim 后按输入长度截断，
  最后乘模型补偿增益（compensate）。背景音轨 = 混音 − 人声。
- 模型超参（n_fft/dim_f/dim_t/compensate）按 UVR `model_data.json` 以 onnx 文件 md5 查表，
  查不到时回退到内置文件名默认表（UVR-MDX-NET 1/Main：6144/2048 与 7680/3072；Kim 同 Main
  架构；kuielab 同 1 架构）。
- 推理在 CPU 执行（onnxruntime CPUExecutionProvider）；GPU 推理（CUDA EP）未启用。

## 致谢与许可

- 本项目的 AI 提取管线以 [Ultimate Vocal Remover GUI](https://github.com/Anjok07/ultimatevocalremovergui)
  （MIT 许可）的 MDX-Net 处理流程为参考移植（STFT/iSTFT 语义、SeperateMDX 分块与重叠相加、
  model_data.json 超参表）。
- 模型权重 `UVR_MDXNET_Main.onnx` 来自 [TRvlvr/model_repo](https://github.com/TRvlvr/model_repo)
  （UVR 官方模型仓库）。按 UVR README 的要求，第三方开发者使用其模型时需致谢 UVR 及其开发者：
  模型非正式开源许可，属「MIT 精神」的致谢要求。
- onnxruntime 来自 [microsoft/onnxruntime](https://github.com/microsoft/onnxruntime)（MIT），
  版权与第三方声明见 `third_party/onnxruntime/LICENSE` 与 `ThirdPartyNotices.txt`。

## 许可

AGPL-3.0
