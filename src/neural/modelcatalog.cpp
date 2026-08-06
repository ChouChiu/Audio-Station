#include "modelcatalog.h"

#include <algorithm>

namespace neural {
namespace {

constexpr const char* kReleaseBase =
    "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/";

std::string releaseUrl(const char* fileName) {
    return std::string(kReleaseBase) + fileName;
}

} // namespace

const std::vector<ModelEntry>& modelCatalog() {
    static const std::vector<ModelEntry> catalog = {
        {
            .id = "mdxnet_1",
            .displayName = "UVR-MDX-NET 1",
            .series = "MDX-Net (UVR)",
            .fileName = "UVR_MDXNET_1_9703.onnx",
            .url = releaseUrl("UVR_MDXNET_1_9703.onnx"),
            .sizeBytes = 29704436,
            .bundled = false,
            .defaultPick = true, // 系列最强 (训练步数最多)
        },
        {
            .id = "mdxnet_main",
            .displayName = "UVR-MDX-NET Main",
            .series = "MDX-Net (UVR)",
            .fileName = "UVR_MDXNET_Main.onnx",
            .url = releaseUrl("UVR_MDXNET_Main.onnx"),
            .sizeBytes = 66759214,
            .bundled = false,
            .defaultPick = false, // 7680 大 FFT 版本 (低频分辨率更细), 与 1 属不同权衡
        },
        {
            .id = "kim_vocal",
            .displayName = "Kim Vocal 1",
            .series = "Kim Vocal",
            .fileName = "Kim_Vocal_1.onnx",
            .url = releaseUrl("Kim_Vocal_1.onnx"),
            .sizeBytes = 66759214,
            .bundled = false,
            .defaultPick = true,
        },
        {
            .id = "kuielab_b",
            .displayName = "kuielab B Vocals",
            .series = "kuielab (MUSDB)",
            .fileName = "kuielab_b_vocals.onnx",
            .url = releaseUrl("kuielab_b_vocals.onnx"),
            .sizeBytes = 29703204,
            .bundled = false,
            .defaultPick = true,
        },
    };
    return catalog;
}

const ModelEntry* modelById(std::string_view id) {
    const auto& catalog = modelCatalog();
    const auto it = std::find_if(catalog.cbegin(), catalog.cend(),
                                 [id](const ModelEntry& entry) { return entry.id == id; });
    return it == catalog.cend() ? nullptr : &*it;
}

const ModelEntry* modelByFileName(std::string_view fileName) {
    const auto& catalog = modelCatalog();
    const auto it = std::find_if(catalog.cbegin(), catalog.cend(), [fileName](const ModelEntry& entry) {
        return entry.fileName == fileName;
    });
    return it == catalog.cend() ? nullptr : &*it;
}

const ModelEntry* defaultModel() {
    const auto& catalog = modelCatalog();
    for (const ModelEntry& entry : catalog) {
        if (entry.defaultPick)
            return &entry;
    }
    return catalog.empty() ? nullptr : &catalog.front();
}

} // namespace neural
