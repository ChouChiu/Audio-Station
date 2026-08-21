# 开发、测试与发布

## 开发环境

项目使用 uv 管理 Python、隔离环境和锁文件。安装 uv 后，在仓库根目录同步锁定的运行时与默认开发依赖：

```bash
uv sync --locked
```

构建 Linux 独立程序时再安装部署依赖：

```bash
uv sync --locked --group deploy
```

修改依赖时使用 `uv add <包名>`、`uv add --dev <包名>` 或
`uv add --group deploy <包名>`，并提交同步更新的 `pyproject.toml` 与 `uv.lock`。

不要同时安装其他导出 `qfluentwidgets` 的 PyQt 或 PySide Fluent 组件。项目指定的是
`PySide6-Fluent-Widgets[full]`。

## 代码约定

- 新模块使用 `from __future__ import annotations` 和完整类型标注。
- 可变参数少的任务模型优先使用 `frozen=True, slots=True` 数据类，固定字符串集合使用 `StrEnum`。
- 公共基础设施放在 `shared`；功能专属页面、模型和处理逻辑放在对应 `features/<功能>/`。
- 跨功能编排放在 `app`，不要为了复用而让功能包互相导入。
- 长音频使用 `create_pcm_audio` 与分块循环，不要把整个文件复制到普通内存数组。
- 可取消循环必须定期调用 `CancellationToken.raise_if_cancelled()`，且不得吞掉取消异常。
- 输出使用 `write_wav_atomic`，不要直接覆盖目标文件。
- 日志通过 `logging.getLogger(__name__)` 写入；只允许有明确记录的降级路径忽略局部失败。

Ruff 行宽为 100，启用 E、F、I、UP、B、SIM 和 RUF 规则，E501 由格式化与评审控制。

## 翻译与配置

界面字符串位于：

```text
src/resources/i18n/zh_cn.json
src/resources/i18n/en_us.json
src/resources/i18n/ja_jp.json
src/resources/i18n/ko_kr.json
```

增加、删除或重命名翻译键时必须同步修改四个文件；测试会检查键集合完全一致。调用
`tr(language, key, **values)` 时不得依赖“未知键返回键名”的回退行为。

配置由 `src/shared/config.py` 中的 QConfig 单例管理。修改持久化选项时要同时考虑默认值、验证器、页面重译和测试。

参考提取使用参考掩码对消。任何用户可选路径都只能从现场原混音重建，不得把参考独有内容或其反极性写入结果。DSP 调整不能只看合成指标：先通过自动化门禁，再对固定真实素材导出可直接盲听的对照版本；技术检查不得写成“已经听感确认”。

## 增加源码文件

新增源码目录或文件时检查三个发布清单：

1. `pyproject.toml` 的 wheel 包列表；
2. `pyproject.toml` 的 `[tool.pyside6-project].files`；
3. `pysidedeploy.spec` 的 `include-package` 或相应包含项。

遗漏任一清单都可能造成开发环境正常、wheel 或独立程序缺文件。

## 测试门禁

Qt 测试必须使用离屏平台：

```bash
uv run --locked ruff check src tests
uv run --locked ruff format --check src tests
QT_QPA_PLATFORM=offscreen uv run --locked pytest
QT_QPA_PLATFORM=offscreen uv run --locked audio-station --selftest
uv build
```

慢速基准需要显式开启：

```bash
QT_QPA_PLATFORM=offscreen uv run --locked pytest tests/benchmarks --runslow
```

当前慢速门禁使用 15 分钟、44.1 kHz 双声道音频检查参考对消的输出长度、接缝和峰值常驻内存，内存上限为
1.5 GiB。

测试目录按源码结构镜像，主要覆盖：

- 公共音频读写、重采样、原子写出、短时傅里叶变换和日志；
- 参考对消算法、时间对齐、立体声矩阵、回归场景和端到端任务；
- 完整舞台匹配、时间线与分段渲染；
- MDX-Net 分块重叠相加和模型管线；
- CLI 参数、GUI 导航、设置与统计显示；
- 分层导入边界和三语翻译键一致性。

## Python 包构建

```bash
uv build
```

产物写入 `dist/`。ONNX 权重位于忽略列表，不会进入 wheel 或源码包。构建后应通过 uv 的临时隔离环境安装 wheel 并验证（路径中的版本号按实际产物调整）：

```bash
uvx --from ./dist/audio_station-1.0.0-py3-none-any.whl audio-station --version
QT_QPA_PLATFORM=offscreen uvx --from ./dist/audio_station-1.0.0-py3-none-any.whl \
  audio-station --selftest
```

## Linux 独立程序

项目通过 Qt 官方 `pyside6-deploy` 封装 Nuitka，固定使用 `standalone` 目录模式：

```bash
uv sync --locked --group deploy
uv run --locked --group deploy pyside6-deploy -c pysidedeploy.spec
```

独立产物写入 `dist/`，包含 Python、Qt、Fluent Widgets、SciPy、SoundFile、soxr 和 ONNX Runtime，但不包含模型权重。发布前除自动化测试外，还应实际启动独立程序，检查页面导航、模型查找、音频解码、任务取消和输出试听。

## 持续集成

`.github/workflows/build.yml` 在 `main` 分支、`v*` 标签、Pull Request 和手动触发时运行。
质量门禁通过后，工作流构建 wheel、sdist 与 Linux standalone，再上传：

- pytest JUnit XML；
- 质量门禁与构建命令的独立日志；
- wheel、sdist 及 SHA-256 校验文件；
- 保留可执行权限的 Linux standalone 压缩包及 SHA-256 校验文件。

质量和构建任务都会生成 GitHub Job Summary，列出各门禁结果、缓存命中状态、日志和工件下载链接；
即使前置步骤失败，也会尽可能写入已知结果。

工件保留 14 天。uv 依赖缓存由 `uv.lock` 和 `pyproject.toml` 的内容共同失效，
同时缓存 uv 管理的 Python 3.14；不缓存 `.venv` 本身。Linux standalone 额外使用
ccache，根据锁文件、部署规格和 Python 源码失效，上限为 2 GiB。同一分支有新提交时会取消旧任务，
标签构建不会被取消。

Ubuntu Runner 会显式安装 Qt 加载所需的 `libegl1`。所有命令步骤都使用开启 `pipefail` 的 Bash，
因此通过 `tee` 保存日志时不会掩盖原命令的失败状态。JUnit XML 仅在实际生成后上传。
