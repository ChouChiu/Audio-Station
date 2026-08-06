#pragma once

#include <functional>
#include <optional>
#include <string>
#include <stop_token>
#include <vector>

#include "dsp.h"
#include "onnxsession.h"

namespace neural {

// UVR MDX-Net 模型超参。取值来源: UVR 仓库 models/MDX_Net_Models/model_data/model_data.json
// (键 = onnx 文件 md5), 以及 UVR 固定引擎参数 (hop=1024, segment=256, overlap=Default)。
struct MdxNetSpec {
    int nFft = 7680;
    int dimF = 3072; // 模型输入频率维 (STFT bins 裁剪)
    int dimT = 256;  // 模型输入时间维 (帧数)
    int hop = 1024;
    int segmentSize = 256; // chunk = hop * (segmentSize - 1) 样本
    double compensate = 1.0;
    int sampleRate = 44100;
    std::string primaryStem = "Vocals";
};

// 从 UVR model_data.json 文本按 md5 查表, 解析 MdxNetSpec。
// 查不到返回 std::nullopt (不抛异常)。
[[nodiscard]] std::optional<MdxNetSpec> mdxSpecFromJson(const std::string& jsonText,
                                                        const std::string& modelMd5);

// 按 onnx 文件名查内置默认表 (UVR 固定引擎参数 + 经典模型超参)。
// 用于 model_data.json 缺失或 md5 不匹配时的回退。
[[nodiscard]] std::optional<MdxNetSpec> mdxSpecForModelFile(const std::string& fileName);

// 单 chunk 波形 [2][chunk] 的模型推断回调 (UVR run_model):
// in/out 均为 [2][chunkSamples] 平面 float32, out = 人声波形。
// 失败返回 false 并置 errorOut。
using ChunkInferFn = std::function<bool(const float* in, float* out, int chunkSamples,
                                        std::string* errorOut)>;

// UVR SeperateMDX.demix 的纯 C++ 移植 (不含模型):
// 按 chunk 分块 -> Hann 交叉淡化重叠相加 -> 裁 trim -> 截断到输入长度。
// mix/vocal 为 [2][T] 平面 double。progress(current, total) 每 chunk 回调。
// 返回 false 表示取消 (stop 请求) 或模型回调失败。
[[nodiscard]] bool demixChunks(const std::vector<dsp::Vec>& mix, int nFft, int hop,
                               int segmentSize, const ChunkInferFn& modelFn,
                               std::vector<dsp::Vec>& vocal,
                               const std::function<void(int current, int total)>& progress,
                               const std::stop_token& stopToken, std::string* errorOut);

// UVR MDX-Net 人声分离器: 输入立体声混音, 输出人声; 背景 = 混音 - 人声 (调用方)。
class MdxNet {
public:
    [[nodiscard]] bool load(const std::string& onnxPath, const MdxNetSpec& spec,
                            std::string* errorOut = nullptr);
    [[nodiscard]] bool isLoaded() const noexcept;

    const MdxNetSpec& spec() const noexcept { return m_spec; }

    // 输入 mix 为 [2][T] @ spec.sampleRate; 输出 vocal 为 [2][T]。
    // progress 每 chunk 回调 (current, total); stop 请求时尽快返回 false。
    [[nodiscard]] bool separate(const std::vector<dsp::Vec>& mix, std::vector<dsp::Vec>& vocal,
                                const std::function<void(int current, int total)>& progress,
                                const std::stop_token& stopToken, std::string* errorOut = nullptr);

private:
    [[nodiscard]] bool inferChunk(const float* in, float* out, int chunkSamples,
                                  std::string* errorOut);

    MdxNetSpec m_spec;
    OnnxSession m_session;
    int m_chunkSize = 0;
    int m_trim = 0;
    int m_nBins = 0;
};

} // namespace neural
