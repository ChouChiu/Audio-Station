#include "audio.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QByteArray>
#include <QEventLoop>
#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <soxr.h>

namespace {

void setError(QString* errorOut, const QString& msg) {
    if (errorOut)
        *errorOut = msg;
}

template <typename Sample, typename Converter>
bool appendSamples(const QAudioBuffer& buffer, AudioData& output, Converter converter) {
    const Sample* samples = buffer.constData<Sample>();
    if (samples == nullptr)
        return false;
    const qsizetype frames = buffer.frameCount();
    const int channelCount = buffer.format().channelCount();
    const size_t previousFrameCount = output.channels.front().size();
    const size_t appendedFrameCount = static_cast<size_t>(frames);
    if (appendedFrameCount > std::numeric_limits<size_t>::max() - previousFrameCount)
        return false;
    const size_t newFrameCount = previousFrameCount + appendedFrameCount;
    for (dsp::Vec& channel : output.channels)
        channel.resize(newFrameCount);
    for (qsizetype frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < channelCount; ++channel) {
            const qsizetype index = frame * channelCount + channel;
            output.channels[static_cast<size_t>(channel)]
                           [previousFrameCount + static_cast<size_t>(frame)] =
                converter(samples[index]);
        }
    }
    return true;
}

bool appendBuffer(const QAudioBuffer& buffer, AudioData& output, QAudioFormat& format,
                  QString& errorMessage) {
    if (buffer.frameCount() <= 0)
        return true;
    if (output.channels.empty()) {
        format = buffer.format();
        if (format.channelCount() <= 0 || format.sampleRate() <= 0) {
            errorMessage = QStringLiteral("音频格式无效");
            return false;
        }
        output.sampleRate = format.sampleRate();
        output.channels.resize(static_cast<size_t>(format.channelCount()));
    } else if (buffer.format().channelCount() != format.channelCount() ||
               buffer.format().sampleRate() != format.sampleRate() ||
               buffer.format().sampleFormat() != format.sampleFormat()) {
        errorMessage = QStringLiteral("解码器返回了不一致的音频格式");
        return false;
    }

    bool appended = false;
    switch (format.sampleFormat()) {
    case QAudioFormat::Float:
        appended = appendSamples<float>(buffer, output, [](float value) {
            return static_cast<double>(value);
        });
        break;
    case QAudioFormat::Int16:
        appended = appendSamples<qint16>(buffer, output, [](qint16 value) {
            return static_cast<double>(value) / 32768.0;
        });
        break;
    case QAudioFormat::Int32:
        appended = appendSamples<qint32>(buffer, output, [](qint32 value) {
            return static_cast<double>(value) / 2147483648.0;
        });
        break;
    case QAudioFormat::UInt8:
        appended = appendSamples<quint8>(buffer, output, [](quint8 value) {
            return (static_cast<double>(value) - 128.0) / 128.0;
        });
        break;
    default:
        errorMessage = QStringLiteral("不支持的音频格式");
        return false;
    }
    if (!appended)
        errorMessage = QStringLiteral("音频数据无效");
    return appended;
}

} // namespace

AudioData decodeAudioFile(const QString& path, QString* errorOut,
                          const std::stop_token& stopToken) {
    if (errorOut != nullptr)
        errorOut->clear();
    AudioData out;
    if (!QFile::exists(path)) {
        setError(errorOut, QStringLiteral("无法打开音频文件: %1").arg(path));
        return {};
    }
    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(path));
    QEventLoop loop;
    QAudioFormat format;
    bool failed = false;
    QString errMsg;
    // 看门狗: 解码器静默挂起时避免无限阻塞 (空闲检测: 每次产出 buffer 重置计时)
    QTimer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(60000);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, [&]() {
        failed = true;
        errMsg = QStringLiteral("音频解码超时");
        loop.quit();
    });
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&]() {
        if (stopToken.stop_requested()) {
            decoder.stop();
            loop.quit();
            return;
        }
        if (!appendBuffer(decoder.read(), out, format, errMsg)) {
            failed = true;
            decoder.stop();
            loop.quit();
            return;
        }
        watchdog.start();
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), &loop,
                     [&](QAudioDecoder::Error) {
                         failed = true;
                         errMsg = decoder.errorString();
                         loop.quit();
                     });
    QTimer cancellationPoll;
    cancellationPoll.setInterval(50);
    QObject::connect(&cancellationPoll, &QTimer::timeout, &loop, [&]() {
        if (!stopToken.stop_requested())
            return;
        decoder.stop();
        loop.quit();
    });
    watchdog.start();
    cancellationPoll.start();
    decoder.start();
    loop.exec();
    watchdog.stop();
    cancellationPoll.stop();
    if (stopToken.stop_requested()) {
        setError(errorOut, QStringLiteral("操作已取消"));
        return {};
    }
    if (failed) {
        setError(errorOut, errMsg.isEmpty() ? QStringLiteral("无法解码音频文件") : errMsg);
        return {};
    }
    if (!out.isValid()) {
        setError(errorOut, QStringLiteral("无法解码音频文件"));
        return {};
    }
    return out;
}

AudioData resampleTo(const AudioData& in, int targetRate, QString* errorOut,
                     const std::stop_token& stopToken) {
    if (errorOut != nullptr)
        errorOut->clear();
    AudioData out;
    out.sampleRate = targetRate;
    if (targetRate <= 0 || !in.isValid()) {
        setError(errorOut, QStringLiteral("采样率无效"));
        return {};
    }
    if (in.sampleRate == targetRate)
        return in;
    // 注意: 本版 soxr 默认 io spec 为 FLOAT32, 必须显式指定 FLOAT64 (我们传 double*)
    const soxr_io_spec_t iospec = soxr_io_spec(SOXR_FLOAT64, SOXR_FLOAT64);
    const soxr_quality_spec_t qs = soxr_quality_spec(SOXR_HQ, 0);
    for (const dsp::Vec& ch : in.channels) {
        if (stopToken.stop_requested()) {
            setError(errorOut, QStringLiteral("操作已取消"));
            return {};
        }
        const size_t inLen = ch.size();
        const size_t outCap = static_cast<size_t>(
            std::ceil(static_cast<double>(inLen) * targetRate / in.sampleRate));
        dsp::Vec resampled(outCap, 0.0);
        size_t idone = 0;
        size_t odone = 0;
        const soxr_error_t err = soxr_oneshot(
            static_cast<double>(in.sampleRate), static_cast<double>(targetRate), 1,
            ch.data(), inLen, &idone, resampled.data(), outCap, &odone, &iospec, &qs, nullptr);
        if (err != nullptr) {
            setError(errorOut, QString::fromUtf8(soxr_strerror(err)));
            return {};
        }
        if (idone != inLen) {
            setError(errorOut, QStringLiteral("重采样输入未完全消耗"));
            return {};
        }
        resampled.resize(odone);
        out.channels.push_back(std::move(resampled));
    }
    return out;
}

namespace {

bool writeBytes(QIODevice& device, const char* data, qint64 size) {
    return device.write(data, size) == size;
}

bool writeLE16(QIODevice& device, quint16 v) {
    char b[2];
    b[0] = static_cast<char>(v & 0xFF);
    b[1] = static_cast<char>((v >> 8) & 0xFF);
    return writeBytes(device, b, 2);
}

bool writeLE32(QIODevice& device, quint32 v) {
    char b[4];
    b[0] = static_cast<char>(v & 0xFF);
    b[1] = static_cast<char>((v >> 8) & 0xFF);
    b[2] = static_cast<char>((v >> 16) & 0xFF);
    b[3] = static_cast<char>((v >> 24) & 0xFF);
    return writeBytes(device, b, 4);
}

} // namespace

bool writeWav(const QString& path, const AudioData& data, int bitsPerSample, QString* errorOut,
              const std::stop_token& stopToken) {
    if (errorOut != nullptr)
        errorOut->clear();
    if (bitsPerSample != 16 && bitsPerSample != 24) {
        setError(errorOut, QStringLiteral("仅支持 PCM_16 与 PCM_24"));
        return false;
    }
    if (!data.isValid()) {
        setError(errorOut, QStringLiteral("无可写音频数据"));
        return false;
    }
    const size_t channelCount = data.channels.size();
    const size_t frameCount = data.channels[0].size();
    for (const dsp::Vec& channel : data.channels) {
        if (channel.size() != frameCount) {
            setError(errorOut, QStringLiteral("所有声道的帧数必须一致"));
            return false;
        }
    }
    const int bytesPerSample = bitsPerSample / 8;
    constexpr size_t kMaxWavChannels = 0xFFFFU;
    if (channelCount > kMaxWavChannels / static_cast<size_t>(bytesPerSample)) {
        setError(errorOut, QStringLiteral("声道数量超过 WAV 格式限制"));
        return false;
    }
    const int channels = static_cast<int>(channelCount);
    const int sr = data.sampleRate;
    constexpr quint64 kMaxRiffPayload = 0xFFFFFFFFULL - 36ULL;
    const quint64 bytesPerFrame = static_cast<quint64>(channels) *
                                  static_cast<quint64>(bytesPerSample);
    if (static_cast<quint64>(sr) * bytesPerFrame > 0xFFFFFFFFULL ||
        static_cast<quint64>(frameCount) > kMaxRiffPayload / bytesPerFrame) {
        setError(errorOut, QStringLiteral("音频数据超过 RIFF/WAVE 的 4 GiB 限制"));
        return false;
    }
    const qint64 nFrames = static_cast<qint64>(frameCount);
    const qint64 dataSize = nFrames * channels * bytesPerSample;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        setError(errorOut, QStringLiteral("无法写入文件: %1").arg(path));
        return false;
    }
    const bool headerWritten =
        writeBytes(f, "RIFF", 4) && writeLE32(f, static_cast<quint32>(36 + dataSize)) &&
        writeBytes(f, "WAVE", 4) && writeBytes(f, "fmt ", 4) &&
        writeLE32(f, 16) && // fmt chunk size
        writeLE16(f, 1) &&  // PCM
        writeLE16(f, static_cast<quint16>(channels)) &&
        writeLE32(f, static_cast<quint32>(sr)) &&
        writeLE32(f, static_cast<quint32>(static_cast<qint64>(sr) * channels * bytesPerSample)) &&
        writeLE16(f, static_cast<quint16>(channels * bytesPerSample)) &&
        writeLE16(f, static_cast<quint16>(bitsPerSample)) && writeBytes(f, "data", 4) &&
        writeLE32(f, static_cast<quint32>(dataSize));
    if (!headerWritten) {
        setError(errorOut, QStringLiteral("写入 WAV 文件头失败: %1").arg(f.errorString()));
        return false;
    }
    const double scale = std::ldexp(1.0, bitsPerSample - 1); // 32768 / 8388608
    const qint64 lo = -(static_cast<qint64>(1) << (bitsPerSample - 1));
    const qint64 hi = (static_cast<qint64>(1) << (bitsPerSample - 1)) - 1;
    constexpr qsizetype kWriteBufferSize = 64LL * 1024LL;
    QByteArray writeBuffer;
    writeBuffer.reserve(kWriteBufferSize);
    const auto flushBuffer = [&]() {
        if (writeBuffer.isEmpty())
            return true;
        const bool written = writeBytes(f, writeBuffer.constData(), writeBuffer.size());
        writeBuffer.clear();
        return written;
    };
    for (qint64 i = 0; i < nFrames; ++i) {
        if ((i & 4095) == 0 && stopToken.stop_requested()) {
            setError(errorOut, QStringLiteral("操作已取消"));
            return false;
        }
        for (int c = 0; c < channels; ++c) {
            const double sample = data.channels[static_cast<size_t>(c)][static_cast<size_t>(i)];
            const double v = std::isfinite(sample) ? std::clamp(sample, -1.0, 1.0) : 0.0;
            qint64 s = static_cast<qint64>(std::lrint(v * scale));
            s = std::clamp(s, lo, hi);
            writeBuffer.append(static_cast<char>(s & 0xFF));
            writeBuffer.append(static_cast<char>((s >> 8) & 0xFF));
            if (bitsPerSample == 24)
                writeBuffer.append(static_cast<char>((s >> 16) & 0xFF));
            if (writeBuffer.size() >= kWriteBufferSize && !flushBuffer()) {
                setError(errorOut, QStringLiteral("写入音频数据失败: %1").arg(f.errorString()));
                return false;
            }
        }
    }
    if (!flushBuffer()) {
        setError(errorOut, QStringLiteral("写入音频数据失败: %1").arg(f.errorString()));
        return false;
    }
    if (!f.commit()) {
        setError(errorOut, QStringLiteral("写入音频文件失败: %1").arg(f.errorString()));
        return false;
    }
    return true;
}
