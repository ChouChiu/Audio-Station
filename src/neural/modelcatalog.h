#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neural {

// 模型目录条目: 每个系列只收录最强模型 (见 modelCatalog 注释)。
struct ModelEntry {
    std::string id;          // 稳定标识 (CLI --model / 设置持久化)
    std::string displayName; // 界面显示名 (模型为专有名词, 不随语言翻译)
    std::string series;      // 模型系列
    std::string fileName;    // models/ 目录下的 onnx 文件名
    std::string url;         // 下载地址 (bundled=false 时首次使用自动下载)
    int64_t sizeBytes = 0;
    bool bundled = false;    // true = 随软件分发 (models/ 入库); false = 自动下载
    bool defaultPick = false; // 该系列最强, 界面默认选中
};

// 模型目录。系列内"最强"依据:
//   - MDX-Net (UVR 经典): UVR 1/2/3 是改进版重训系列, 文件名编码训练步数
//     (UVR_MDXNET_1_9703 > 2_9682 > 3_9662), 1 为最强; Main 是 7680 大 FFT 版本
//     (全频带覆盖更广), 作为不同权衡保留。2/3/Karaoke/9482 等更弱或用途不同, 不收录。
//   - Kim Vocal: KimberleyJSN 训练的 vocal 专用模型。
//   - kuielab (MUSDB): MDX-Net 论文作者 (kuielab) 的 MUSDB18 训练模型, b 为改进版。
[[nodiscard]] const std::vector<ModelEntry>& modelCatalog();
[[nodiscard]] const ModelEntry* modelById(std::string_view id);
[[nodiscard]] const ModelEntry* modelByFileName(std::string_view fileName);
[[nodiscard]] const ModelEntry* defaultModel(); // 系列最强, 界面默认选中

} // namespace neural
