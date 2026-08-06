#include "onnxsession.h"

#include <onnxruntime_cxx_api.h>

#include <cstring>
#include <stdexcept>
#include <utility>

namespace neural {

struct OnnxSession::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "mr_remover"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
    std::vector<int64_t> inputShape;  // {channels, freq, frames}
    std::vector<int64_t> outputShape; // {channels, freq, frames}
    std::string inputName;
    std::string outputName;

    Impl() { options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL); }
};

OnnxSession::OnnxSession() = default;
OnnxSession::~OnnxSession() = default;

OnnxSession::OnnxSession(OnnxSession&&) noexcept = default;
OnnxSession& OnnxSession::operator=(OnnxSession&&) noexcept = default;

bool OnnxSession::load(const std::string& modelPath, std::string* errorOut) {
    m_impl = std::make_unique<Impl>();
    try {
        m_impl->session = std::make_unique<Ort::Session>(m_impl->env, modelPath.c_str(),
                                                         m_impl->options);
        const auto allocator = Ort::AllocatorWithDefaultOptions();
        Ort::AllocatedStringPtr inputName = m_impl->session->GetInputNameAllocated(0, allocator);
        Ort::AllocatedStringPtr outputName = m_impl->session->GetOutputNameAllocated(0, allocator);
        m_impl->inputName = inputName.get();
        m_impl->outputName = outputName.get();

        const auto checkShape = [](const Ort::TypeInfo& info, std::vector<int64_t>& out) {
            auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
            // batch 维允许动态 (-1): UVR 导出的模型 batch 为 symbolic
            if (shape.size() != 4 || (shape[0] != 1 && shape[0] != -1))
                return false;
            out = {shape[1], shape[2], shape[3]};
            return true;
        };
        if (!checkShape(m_impl->session->GetInputTypeInfo(0), m_impl->inputShape) ||
            !checkShape(m_impl->session->GetOutputTypeInfo(0), m_impl->outputShape)) {
            if (errorOut)
                *errorOut = "model is not a 4D batch=1 float32 tensor";
            m_impl->session.reset();
            return false;
        }
        if (m_impl->inputShape != m_impl->outputShape) {
            if (errorOut)
                *errorOut = "model input/output shapes differ";
            m_impl->session.reset();
            return false;
        }
        return true;
    } catch (const Ort::Exception& e) {
        if (errorOut)
            *errorOut = e.what();
        m_impl->session.reset();
        return false;
    } catch (const std::exception& e) {
        if (errorOut)
            *errorOut = e.what();
        m_impl->session.reset();
        return false;
    }
}

bool OnnxSession::isLoaded() const noexcept {
    return m_impl && m_impl->session;
}

const std::vector<int64_t>& OnnxSession::inputShape() const noexcept {
    return m_impl->inputShape;
}

const std::vector<int64_t>& OnnxSession::outputShape() const noexcept {
    return m_impl->outputShape;
}

bool OnnxSession::run(const float* inData, std::size_t inElements, float* outData,
                      std::size_t outElements, std::string* errorOut) {
    if (!isLoaded())
        return false;
    std::size_t expectedIn = 1;
    for (int64_t dim : m_impl->inputShape)
        expectedIn *= static_cast<std::size_t>(dim);
    std::size_t expectedOut = 1;
    for (int64_t dim : m_impl->outputShape)
        expectedOut *= static_cast<std::size_t>(dim);
    if (inElements != expectedIn || outElements != expectedOut) {
        if (errorOut) {
            *errorOut = "buffer size mismatch: expected " + std::to_string(expectedIn) + " in / " +
                        std::to_string(expectedOut) + " out";
        }
        return false;
    }
    try {
        const std::vector<int64_t> batchShape{1, m_impl->inputShape[0], m_impl->inputShape[1],
                                              m_impl->inputShape[2]};
        // 输入张量由 onnxruntime 分配器持有 (memcpy 自用户缓冲):
        // 用户内存 + OrtArenaAllocator 声明会让会话把输入缓冲当作 arena 内存复用为中间结果,
        // 推理期间输入被覆写 -> 输出偶发 NaN/垃圾。
        Ort::AllocatorWithDefaultOptions allocator;
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            allocator, batchShape.data(), batchShape.size());
        float* inputData = inputTensor.GetTensorMutableData<float>();
        std::memcpy(inputData, inData, inElements * sizeof(float));
        const char* inputNames[] = {m_impl->inputName.c_str()};
        const char* outputNames[] = {m_impl->outputName.c_str()};
        std::vector<Ort::Value> outputs =
            m_impl->session->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1,
                                 outputNames, 1);
        if (outputs.empty() || !outputs[0].IsTensor()) {
            if (errorOut)
                *errorOut = "session run returned no tensor output";
            return false;
        }
        const float* src = outputs[0].GetTensorData<float>();
        const std::size_t actualElements =
            outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
        if (actualElements != outElements) {
            if (errorOut)
                *errorOut = "session output element count mismatch";
            return false;
        }
        for (std::size_t i = 0; i < outElements; ++i)
            outData[i] = src[i];
        return true;
    } catch (const Ort::Exception& e) {
        if (errorOut)
            *errorOut = e.what();
        return false;
    } catch (const std::exception& e) {
        if (errorOut)
            *errorOut = e.what();
        return false;
    }
}

} // namespace neural
