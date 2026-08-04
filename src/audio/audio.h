#pragma once

#include <QString>
#include <stop_token>
#include <vector>

#include "dsp.h"

struct AudioData {
    int sampleRate = 0;
    std::vector<dsp::Vec> channels; // 平面非交织, 每声道一个 Vec, 值域 [-1,1]

    [[nodiscard]] bool isValid() const noexcept {
        if (sampleRate <= 0 || channels.empty() || channels.front().empty())
            return false;
        const size_t frames = channels.front().size();
        for (const dsp::Vec& channel : channels) {
            if (channel.size() != frames)
                return false;
        }
        return true;
    }
};

// 解码 mp3/wav/flac/m4a -> float64 平面声道 (QAudioDecoder + FFmpeg 后端)
// 失败返回空 AudioData 并置 errorOut
AudioData decodeAudioFile(const QString& path, QString* errorOut = nullptr,
                          const std::stop_token& stopToken = {});

// 每声道重采样到 targetRate (libsoxr SOXR_HQ, 等价 librosa res_type='soxr_hq')
// 失败返回空 AudioData 并置 errorOut
AudioData resampleTo(const AudioData& in, int targetRate, QString* errorOut = nullptr,
                     const std::stop_token& stopToken = {});

// 写 RIFF/WAVE PCM_16 或 PCM_24 交织文件
// 整型换算镜像 libsndfile: clip(v,-1,1) * 2^(bits-1) 后 round-to-nearest 并钳位
bool writeWav(const QString& path, const AudioData& data, int bitsPerSample,
              QString* errorOut = nullptr, const std::stop_token& stopToken = {});
