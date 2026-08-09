#include "processingthread.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <exception>

#include "audio.h"
#include "filepaths.h"
#include "strtable.h"

ProcessingThread::ProcessingThread(QString songPath, QString accPath, QString outPath, Params params,
                                   QObject* parent)
    : QThread(parent),
      m_songPath(std::move(songPath)),
      m_accPath(std::move(accPath)),
      m_outPath(std::move(outPath)),
      m_params(std::move(params)) {}

void ProcessingThread::cancel() {
    m_stopSource.request_stop();
}

void ProcessingThread::run() {
    const std::stop_token stopToken = m_stopSource.get_token();
    const QString lang = m_params.lang;
    auto t = [&](const QString& key, const QHash<QString, QString>& kwargs = {}) {
        QString s = i18n::t(lang, key);
        for (auto it = kwargs.cbegin(); it != kwargs.cend(); ++it)
            s.replace(it.key(), it.value());
        return s;
    };
    const auto cancelled = [&]() {
        if (!stopToken.stop_requested())
            return false;
        emit processingCancelled();
        return true;
    };
    try {
        if (cancelled())
            return;
        if (filepaths::equal(m_outPath, m_songPath) ||
            filepaths::equal(m_outPath, m_accPath)) {
            emit errorOccurred(t(QStringLiteral("proc_error"),
                                 {{QStringLiteral("{msg}"),
                                   t(QStringLiteral("warn_output_conflict"))}}));
            return;
        }
        emit statusUpdated(t(QStringLiteral("loading_song")));
        QString err;
        AudioData song = decodeAudioFile(m_songPath, &err, stopToken);
        if (cancelled())
            return;
        if (!song.isValid()) {
            emit errorOccurred(t(QStringLiteral("proc_error"),
                                 {{QStringLiteral("{msg}"), err.isEmpty() ? QStringLiteral("decode failed") : err}}));
            return;
        }
        emit progressUpdated(10);

        emit statusUpdated(t(QStringLiteral("loading_acc")));
        AudioData acc = decodeAudioFile(m_accPath, &err, stopToken);
        if (cancelled())
            return;
        if (!acc.isValid()) {
            emit errorOccurred(t(QStringLiteral("proc_error"),
                                 {{QStringLiteral("{msg}"), err.isEmpty() ? QStringLiteral("decode failed") : err}}));
            return;
        }
        if (song.sampleRate != acc.sampleRate) {
            emit statusUpdated(t(QStringLiteral("resampling")));
            acc = resampleTo(acc, song.sampleRate, &err, stopToken);
            if (cancelled())
                return;
            if (!acc.isValid()) {
                emit errorOccurred(t(QStringLiteral("proc_error"),
                                     {{QStringLiteral("{msg}"), err.isEmpty() ? QStringLiteral("resample failed") : err}}));
                return;
            }
        }
        if (song.channels.size() == 1)
            song.channels.push_back(song.channels[0]);
        if (acc.channels.size() == 1)
            acc.channels.push_back(acc.channels[0]);

        if (m_params.autoAlign) {
            emit statusUpdated(t(QStringLiteral("aligning")));
            try {
                acc.channels = dsp::alignAudio(song.channels, acc.channels, song.sampleRate,
                                               stopToken);
            } catch (const std::exception& e) {
                qWarning() << "Align failed:" << e.what();
                emit statusUpdated(t(QStringLiteral("align_fail")));
            }
            if (cancelled())
                return;
        }

        const size_t minLen = std::min(song.channels[0].size(), acc.channels[0].size());
        for (dsp::Vec& ch : song.channels)
            ch.resize(minLen);
        for (dsp::Vec& ch : acc.channels)
            ch.resize(minLen);
        emit progressUpdated(30);

        emit statusUpdated(t(QStringLiteral("processing")));
        const int nFft = 2048;
        const int hop = 512;
        const double strength = m_params.strength / 100.0;
        const dsp::Algorithm algo = m_params.algorithm;

        emit statusUpdated(t(QStringLiteral("proc_left")));
        std::vector<dsp::Vec> processed = dsp::processStereo(
            song.channels, acc.channels, song.sampleRate, nFft, hop, strength, algo,
            m_params.sigmaTime, stopToken);
        if (cancelled())
            return;
        if (processed.size() < 2 || processed[0].empty() || processed[1].empty()) {
            emit errorOccurred(t(QStringLiteral("proc_error"),
                                 {{QStringLiteral("{msg}"), QStringLiteral("DSP processing failed")}}));
            return;
        }
        dsp::Vec left = std::move(processed[0]);
        dsp::Vec right = std::move(processed[1]);
        emit progressUpdated(80);

        // 镜像 nan_to_num: 非有限值 -> 0
        auto clean = [](dsp::Vec& v) {
            for (double& x : v)
                if (!std::isfinite(x))
                    x = 0.0;
        };
        clean(left);
        clean(right);
        if (algo != dsp::Algorithm::Lossless) {
            for (double& x : left)
                x = std::clamp(x, -1.0, 1.0);
            for (double& x : right)
                x = std::clamp(x, -1.0, 1.0);
        } else {
            // 双声道联动峰值保护。24-bit 只降低量化误差，不能阻止 WAV 写出阶段硬削波。
            double peak = 0.0;
            for (double x : left)
                peak = std::max(peak, std::abs(x));
            for (double x : right)
                peak = std::max(peak, std::abs(x));
            if (peak > 0.999) {
                const double scale = 0.999 / peak;
                for (double& x : left)
                    x *= scale;
                for (double& x : right)
                    x *= scale;
            }
        }
        emit progressUpdated(90);

        emit statusUpdated(t(QStringLiteral("saving")));
        AudioData out;
        out.sampleRate = song.sampleRate;
        out.channels = {std::move(left), std::move(right)};
        const int bits = (algo == dsp::Algorithm::Lossless) ? 24 : 16;
        if (!writeWav(m_outPath, out, bits, &err, stopToken)) {
            if (cancelled())
                return;
            emit errorOccurred(t(QStringLiteral("proc_error"),
                                 {{QStringLiteral("{msg}"), err.isEmpty() ? QStringLiteral("write failed") : err}}));
            return;
        }
        emit progressUpdated(100);
        emit processingFinished(m_outPath);
    } catch (const std::exception& e) {
        if (cancelled())
            return;
        qWarning() << "Processing failed:" << e.what();
        emit errorOccurred(t(QStringLiteral("proc_error"), {{QStringLiteral("{msg}"), QString::fromUtf8(e.what())}}));
    } catch (...) {
        if (cancelled())
            return;
        qWarning() << "Processing failed: unknown exception";
        emit errorOccurred(t(QStringLiteral("proc_error"), {{QStringLiteral("{msg}"), QStringLiteral("unknown error")}}));
    }
}
