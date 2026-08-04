#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <limits>
#include <stop_token>

#include "audio.h"

namespace {

quint32 readLe32(const QByteArray& bytes, qsizetype offset) {
    const auto byte = [&](qsizetype index) {
        return static_cast<quint32>(static_cast<unsigned char>(bytes[index]));
    };
    return byte(offset) | (byte(offset + 1) << 8U) | (byte(offset + 2) << 16U) |
           (byte(offset + 3) << 24U);
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            passed = false;
        }
    };

    QTemporaryDir temporaryDirectory;
    expect(temporaryDirectory.isValid(), "temporary directory creation failed");

    const QString outputPath = temporaryDirectory.filePath(QStringLiteral("test.wav"));
    AudioData audio;
    audio.sampleRate = 48000;
    audio.channels = {{-1.0, 0.0, 1.0}, {0.5, -0.5, 0.25}};
    QString error;
    expect(writeWav(outputPath, audio, 16, &error), "valid PCM 16 WAV was rejected");

    QFile output(outputPath);
    expect(output.open(QIODevice::ReadOnly), "written WAV cannot be opened");
    const QByteArray bytes = output.readAll();
    expect(bytes.size() == 56, "WAV size does not match its frame count");
    expect(bytes.startsWith("RIFF") && bytes.mid(8, 4) == QByteArrayLiteral("WAVE"),
           "WAV container signature is invalid");
    expect(bytes.size() >= 44 && readLe32(bytes, 40) == 12,
           "WAV data chunk size is invalid");

    const AudioData decoded = decodeAudioFile(outputPath, &error);
    expect(decoded.isValid(), "written WAV could not be decoded");
    expect(decoded.sampleRate == audio.sampleRate, "decoded sample rate changed");
    expect(decoded.channels.size() == audio.channels.size(), "decoded channel count changed");
    expect(decoded.channels.front().size() == audio.channels.front().size(),
           "decoded frame count changed");

    AudioData mismatched = audio;
    mismatched.channels[1].pop_back();
    const QString invalidPath = temporaryDirectory.filePath(QStringLiteral("invalid.wav"));
    expect(!writeWav(invalidPath, mismatched, 16, &error),
           "mismatched channel lengths were accepted");
    expect(!QFile::exists(invalidPath), "invalid WAV left a partial output file");
    expect(!writeWav(invalidPath, audio, 8, &error), "unsupported PCM depth was accepted");

    AudioData nonFinite = audio;
    nonFinite.channels[0][0] = std::numeric_limits<double>::quiet_NaN();
    const QString sanitizedPath = temporaryDirectory.filePath(QStringLiteral("sanitized.wav"));
    expect(writeWav(sanitizedPath, nonFinite, 24, &error),
           "non-finite samples were not sanitized during WAV output");

    std::stop_source stopSource;
    stopSource.request_stop();
    const QString cancelledPath = temporaryDirectory.filePath(QStringLiteral("cancelled.wav"));
    expect(!writeWav(cancelledPath, audio, 16, &error, stopSource.get_token()),
           "cancelled WAV output was reported as successful");
    expect(!QFile::exists(cancelledPath), "cancelled WAV output left a partial file");

    return passed ? 0 : 1;
}
