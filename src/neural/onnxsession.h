#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neural {

// onnxruntime C++ API 的 RAII 封装: 加载单输入/单输出 float32 张量模型并推理。
// 线程安全要求: 同一实例的 run() 不可并发调用 (onnxruntime Session 非线程安全)。
class OnnxSession {
public:
    OnnxSession();
    ~OnnxSession();
    OnnxSession(OnnxSession&&) noexcept;
    OnnxSession& operator=(OnnxSession&&) noexcept;
    OnnxSession(const OnnxSession&) = delete;
    OnnxSession& operator=(const OnnxSession&) = delete;

    // 加载模型; 校验为 4D float32 单输入/单输出 (batch=1)。失败返回 false 并置 errorOut。
    [[nodiscard]] bool load(const std::string& modelPath, std::string* errorOut = nullptr);

    [[nodiscard]] bool isLoaded() const noexcept;

    // 模型输入/输出张量形状 (不含 batch 维): {channels, freq, frames}
    [[nodiscard]] const std::vector<int64_t>& inputShape() const noexcept;
    [[nodiscard]] const std::vector<int64_t>& outputShape() const noexcept;

    // 运行推理: inData/outData 均为连续 float32 缓冲, 元素数与对应张量一致。
    // 失败返回 false 并置 errorOut (输入张量仍按原样保留)。
    [[nodiscard]] bool run(const float* inData, std::size_t inElements, float* outData,
                           std::size_t outElements, std::string* errorOut = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace neural
