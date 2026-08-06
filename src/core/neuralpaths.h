#pragma once

#include <QString>

// AI 模型路径解析: 定位 models/ 目录及其中某个模型的 onnx 权重。
namespace neuralpaths {

// models 目录查找顺序: 显式目录覆盖 (--models-dir / env MR_REMOVER_MODELS) >
//   <appdir>/models > <appdir>/../models > <appdir>/../../../models (in-tree 构建) >
//   cwd/models。目录可能尚不存在 (首次自动下载前)。
[[nodiscard]] QString resolveModelsDir(const QString& overrideDir = {});

// 指定模型文件的完整路径 (文件存在才返回, 否则空)。
[[nodiscard]] QString resolveModelPath(const QString& fileName, const QString& overrideDir = {});

// 与模型同目录的 model_data.json (UVR md5 查表用), 不存在返回空。
[[nodiscard]] QString modelDataJsonPath(const QString& modelPath);

} // namespace neuralpaths
