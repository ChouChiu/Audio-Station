# Audio Station

Audio Station 是一款桌面人声与伴奏分离工具，使用 Python、PySide6 和
PySide6-Fluent-Widgets 构建。项目面向演出录音、现场视频音轨和普通歌曲，提供三种工作流：

- **单曲参考对消**：已有与现场演出对应的正式伴奏时，对齐两段音频并从现场混音中消除伴奏。
- **完整舞台**：向一段整场录音提供多首音源，自动识别歌曲及重复片段的位置，再按时间线分段处理。
- **AI 人声提取**：没有参考伴奏时，使用 UVR MDX-Net ONNX 模型直接分离人声与背景。

参考对消只消除能够由参考音源解释的内容，因此适合保留参考中不存在的现场人声、讲话、欢呼和环境声。
它不是万能的人声分离：录音削波、严重混响、不同编曲或错误音源都会影响结果。没有准确参考音源时，建议使用
AI 人声提取。

## 主要功能

- Fluent Design 桌面界面，支持浅色、深色和跟随系统主题
- 中文、日语和韩语界面即时切换
- 全局时间对齐、局部时钟漂移跟踪和默认参考感知频域掩码
- 可显式选择旧立体声直接消除，使用同一素材进行 A/B
- 完整舞台自动排歌、重复片段识别和可编辑时间线
- 4 个可选 MDX-Net 模型，缺失时自动下载并校验 SHA-256
- 长音频分块处理、任务取消和原子写出
- 处理完成后可在应用内试听并查看音频统计信息
- GUI 与命令行共用同一套单曲参考对消和 AI 分离管线

## 安装

需要 [uv](https://docs.astral.sh/uv/)；项目会按 `.python-version` 自动选择 Python，
并在仓库内管理隔离环境。不要向该环境混装其他导出 `qfluentwidgets` 的 Qt Fluent 组件。

```bash
uv sync --locked
```

启动图形界面：

```bash
uv run --locked audio-station
```

也可以从源码入口启动：

```bash
uv run --locked python -m entrypoints
```

## 图形界面使用方法

### 单曲参考对消

1. 打开“MR Remove”，选择“单曲”。
2. 选择待处理的现场录音或混音，以及与其内容对应的参考伴奏。
3. 保持“参考感知频域掩码”和自动对齐，先使用默认的 75% 强度和 8 秒统计窗口处理。
4. 试听结果后再调整强度。声音出现明显抽吸或人声变薄时，应降低强度或确认参考音源是否正确。
5. “中置人声提取”和“弱人声保护”是可选的后处理；弱人声保护只能在中置人声提取开启时使用。

参考伴奏应尽量与现场播放版本一致。不同母带、剪辑、变速、升降调或带有额外垫音的版本都会降低抵消效果。

### 完整舞台

1. 在“MR Remove”中选择“全场”，载入完整舞台录音。
2. 添加可能在整场录音中出现的歌曲音源；文件顺序不影响识别。
3. 执行自动排布并检查时间线中的完整歌曲、短片段、未匹配区间和置信度。
4. 必要时双击修改舞台时间或音源截取范围，也可关闭不应处理的片段。
5. 确认输出路径和处理参数后开始渲染。

未匹配区间会保留原始内容和长度，因此串场讲话、观众互动、广告及空场不会被自动裁除。完整舞台工作流目前仅在图形界面中提供。

### AI 人声提取

1. 打开“AI 人声提取”，选择歌曲和模型。
2. 选择输出目录并开始处理。
3. 首次使用某个模型时，程序会自动下载权重；请保持网络连接并等待校验完成。

处理完成后会生成：

- `<歌曲名>_vocal.wav`：模型预测的人声
- `<歌曲名>_background.wav`：原始混音减去预测人声得到的背景

## 命令行使用方法

查看版本和帮助：

```bash
uv run --locked audio-station --version
uv run --locked audio-station --help
uv run --locked audio-station mr --help
uv run --locked audio-station ai --help
```

使用默认参数进行参考对消：

```bash
uv run --locked audio-station mr "现场录音.wav" "参考伴奏.wav" "现场人声.wav"
```

指定强度、增益窗口并开启中置处理：

```bash
uv run --locked audio-station mr "现场录音.wav" "参考伴奏.wav" "现场人声.wav" \
  --strength 75 --sigma 8 --align \
  --center-extraction --weak-vocal-protection
```

与旧直接消除算法进行 A/B：

```bash
uv run --locked audio-station mr "现场录音.wav" "参考伴奏.wav" "旧算法对照.wav" \
  --algorithm direct --strength 75 --sigma 8 --align
```

常用参考对消参数：

| 参数 | 取值 | 说明 |
|---|---:|---|
| `--algorithm` | `spectral_mask`、`direct` | 默认使用不直接相减参考波形的频域掩码；`direct` 为旧算法对照 |
| `--strength` | `0`～`100` | 参考信号的消除强度，默认 `75` |
| `--sigma` | `1`、`3`、`8`、`16` | 增益跟踪窗口秒数，默认 `8` |
| `--align` / `--no-align` | 开 / 关 | 是否自动对齐，默认开启 |
| `--center-extraction` | 开关 | 对消后进一步聚焦幻象中置内容 |
| `--weak-vocal-protection` | 开关 | 保护较弱的中置人声，依赖中置人声提取 |
| `--lang` | `zh_cn`、`ja_jp`、`ko_kr` | 进度信息语言 |

AI 分离示例：

```bash
uv run --locked audio-station ai "歌曲.wav" --output-dir "输出目录" --model mdxnet_1
```

可用模型为 `mdxnet_1`、`mdxnet_main`、`kim_vocal` 和 `kuielab_b`。可用
`--models-dir` 指定权重目录，也可通过 `MR_REMOVER_MODELS` 环境变量设置公共模型目录。

## 输入、输出与注意事项

- libsndfile 原生支持 WAV、FLAC、OGG 等格式；其他系统可解码格式会尝试通过 Qt Multimedia 读取。
- 参考对消以待处理音频的采样率输出；AI 分离固定输出 44.1 kHz、16-bit 双声道 WAV。
- 参考对消和完整舞台输出 24-bit WAV。
- 输出路径不能覆盖任何输入文件。按 `Ctrl+C` 可取消命令行任务。
- 模型权重不会包含在 Python 包或独立程序中。
- 合成测试只能说明实现没有明显回归，最终效果应以同一素材的试听对比为准。

## 技术文档

算法、架构、测试和发布说明已移至 [docs/](docs/README.md)：

- [架构与数据流](docs/architecture.md)
- [参考对消实现](docs/reference-removal.md)
- [完整舞台实现](docs/full-stage.md)
- [AI 人声分离实现](docs/neural-separation.md)
- [开发、测试与发布](docs/development.md)

## 致谢与许可

AI 处理流程参考
[Ultimate Vocal Remover GUI](https://github.com/Anjok07/ultimatevocalremovergui) 的 MDX-Net 管线，模型来自
[TRvlvr/model_repo](https://github.com/TRvlvr/model_repo)。模型许可与致谢要求以各自发布页为准。

本项目以 [AGPL-3.0-or-later](LICENSE) 发布。PySide6-Fluent-Widgets 开源版本使用 GPLv3，商业使用前请确认其上游许可。
