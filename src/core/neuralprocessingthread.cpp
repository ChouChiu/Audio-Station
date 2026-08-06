#include "neuralprocessingthread.h"

#include <QDir>
#include <QCryptographicHash>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>

#include "audio.h"
#include "mdxnet.h"
#include "modelcatalog.h"
#include "neuralpaths.h"
#include "strtable.h"

namespace {
constexpr int kModelSampleRate = 44100; // UVR MDX-Net 固定模型采样率

bool readFileBytes(const QString& path, QByteArray* out) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *out = file.readAll();
    return true;
}

// 模型超参: 优先 model_data.json 按 md5 查表, 回退到内置文件名默认表。
std::optional<neural::MdxNetSpec> resolveSpec(const QString& modelPath, QString* errorOut) {
    const QString jsonPath = neuralpaths::modelDataJsonPath(modelPath);
    if (!jsonPath.isEmpty()) {
        QByteArray jsonBytes;
        QByteArray modelBytes;
        if (readFileBytes(jsonPath, &jsonBytes) && readFileBytes(modelPath, &modelBytes)) {
            const QByteArray md5 =
                QCryptographicHash::hash(modelBytes, QCryptographicHash::Md5).toHex();
            if (const auto spec =
                    neural::mdxSpecFromJson(jsonBytes.toStdString(), md5.toStdString())) {
                return spec;
            }
        }
    }
    const QString fileName = QFileInfo(modelPath).fileName();
    if (const auto spec = neural::mdxSpecForModelFile(fileName.toStdString()))
        return spec;
    if (errorOut)
        *errorOut = QStringLiteral("unknown model file: %1").arg(fileName);
    return std::nullopt;
}

} // namespace

NeuralProcessingThread::NeuralProcessingThread(QString songPath, QString modelId,
                                               QString vocalOutPath, QString backgroundOutPath,
                                               QString lang, QString modelsDirOverride,
                                               QObject* parent)
    : QThread(parent),
      m_songPath(std::move(songPath)),
      m_modelId(std::move(modelId)),
      m_vocalOutPath(std::move(vocalOutPath)),
      m_backgroundOutPath(std::move(backgroundOutPath)),
      m_lang(std::move(lang)),
      m_modelsDirOverride(std::move(modelsDirOverride)) {}

void NeuralProcessingThread::cancel() {
    m_stopSource.request_stop();
}

bool NeuralProcessingThread::downloadModel(const QString& url, const QString& fileName,
                                           const QString& modelsDir, qint64 expectedBytes,
                                           const std::stop_token& stopToken, QString* errorOut) {
    if (!QDir().mkpath(modelsDir)) {
        if (errorOut)
            *errorOut = QStringLiteral("无法创建模型目录: %1").arg(modelsDir);
        return false;
    }
    const QString destPath = QDir(modelsDir).filePath(fileName);
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(120000);
    QNetworkReply* reply = manager.get(request);
    QSaveFile file(destPath);
    if (!file.open(QIODevice::WriteOnly)) {
        reply->abort();
        reply->deleteLater();
        if (errorOut)
            *errorOut = QStringLiteral("无法写入模型文件: %1").arg(destPath);
        return false;
    }
    QEventLoop loop;
    QString failure;
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
                     [this](qint64 received, qint64 total) {
                         if (total > 0) {
                             const int value = static_cast<int>(
                                 std::clamp<qint64>(15 + 10 * received / total, 16, 24));
                             emit progressUpdated(value);
                         }
                     });
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&file, reply]() {
        file.write(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
        if (reply->error() != QNetworkReply::NoError)
            failure = reply->errorString();
        loop.quit();
    });
    QTimer cancellationPoll;
    cancellationPoll.setInterval(50);
    QObject::connect(&cancellationPoll, &QTimer::timeout, &loop, [&]() {
        if (stopToken.stop_requested()) {
            reply->abort();
            failure = QStringLiteral("cancelled");
            loop.quit();
        }
    });
    cancellationPoll.start();
    loop.exec();
    cancellationPoll.stop();
    reply->deleteLater();

    if (stopToken.stop_requested()) {
        if (errorOut)
            *errorOut = QStringLiteral("cancelled");
        return false;
    }
    if (!failure.isEmpty()) {
        if (errorOut)
            *errorOut = failure;
        return false;
    }
    if (!file.commit()) {
        if (errorOut)
            *errorOut = QStringLiteral("保存模型文件失败: %1").arg(file.errorString());
        return false;
    }
    if (expectedBytes > 0 && file.size() != expectedBytes) {
        if (errorOut)
            *errorOut = QStringLiteral("模型文件大小不符 (期望 %1 字节, 实际 %2)")
                            .arg(expectedBytes)
                            .arg(file.size());
        return false;
    }
    return true;
}

void NeuralProcessingThread::run() {
    const std::stop_token stopToken = m_stopSource.get_token();
    const QString lang = m_lang;
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

        emit statusUpdated(t(QStringLiteral("loading_song")));
        QString err;
        AudioData song = decodeAudioFile(m_songPath, &err, stopToken);
        if (cancelled())
            return;
        if (!song.isValid()) {
            emit errorOccurred(t(QStringLiteral("proc_error"),
                                 {{QStringLiteral("{msg}"),
                                   err.isEmpty() ? QStringLiteral("decode failed") : err}}));
            return;
        }
        emit progressUpdated(10);

        if (song.sampleRate != kModelSampleRate) {
            emit statusUpdated(t(QStringLiteral("ai_resampling")));
            song = resampleTo(song, kModelSampleRate, &err, stopToken);
            if (cancelled())
                return;
            if (!song.isValid()) {
                emit errorOccurred(t(QStringLiteral("proc_error"),
                                     {{QStringLiteral("{msg}"),
                                       err.isEmpty() ? QStringLiteral("resample failed") : err}}));
                return;
            }
        }
        if (song.channels.size() == 1)
            song.channels.push_back(song.channels[0]);
        if (song.channels.size() < 2) {
            emit errorOccurred(t(QStringLiteral("proc_error"),
                                 {{QStringLiteral("{msg}"), QStringLiteral("no audio channels")}}));
            return;
        }
        emit progressUpdated(15);

        // ---- 模型: 目录 id -> 文件; 缺失则自动下载 ----
        const neural::ModelEntry* entry = neural::modelById(m_modelId.toStdString());
        if (entry == nullptr)
            entry = neural::defaultModel();
        const QString modelFile = QString::fromStdString(entry->fileName);
        const QString modelName = QString::fromStdString(entry->displayName);
        const QString modelsDir = m_modelsDirOverride.isEmpty()
                                      ? neuralpaths::resolveModelsDir()
                                      : m_modelsDirOverride;
        QString modelPath = neuralpaths::resolveModelPath(modelFile, m_modelsDirOverride);
        if (modelPath.isEmpty()) {
            emit statusUpdated(
                t(QStringLiteral("ai_downloading"), {{QStringLiteral("{name}"), modelName}}));
            if (!downloadModel(QString::fromStdString(entry->url), modelFile, modelsDir,
                               entry->sizeBytes, stopToken, &err)) {
                if (cancelled())
                    return;
                emit errorOccurred(t(QStringLiteral("ai_download_failed"),
                                     {{QStringLiteral("{msg}"),
                                       err.isEmpty() ? QStringLiteral("download failed") : err}}));
                return;
            }
            if (cancelled())
                return;
            modelPath = neuralpaths::resolveModelPath(modelFile, m_modelsDirOverride);
            if (modelPath.isEmpty()) {
                emit errorOccurred(t(QStringLiteral("ai_download_failed"),
                                     {{QStringLiteral("{msg}"), QStringLiteral("download failed")}}));
                return;
            }
        }

        QString specError;
        const std::optional<neural::MdxNetSpec> spec = resolveSpec(modelPath, &specError);
        if (!spec) {
            emit errorOccurred(t(QStringLiteral("ai_err_model_load"),
                                 {{QStringLiteral("{msg}"), specError}}));
            return;
        }

        emit statusUpdated(t(QStringLiteral("ai_loading_model")));
        neural::MdxNet net;
        std::string loadError;
        if (!net.load(modelPath.toStdString(), *spec, &loadError)) {
            emit errorOccurred(t(QStringLiteral("ai_err_model_load"),
                                 {{QStringLiteral("{msg}"), QString::fromUtf8(loadError.c_str())}}));
            return;
        }
        emit progressUpdated(25);
        if (cancelled())
            return;

        emit statusUpdated(t(QStringLiteral("ai_inferring")));
        std::vector<dsp::Vec> vocal;
        const auto progress = [this](int current, int total) {
            if (total <= 0)
                return;
            const int value = 25 + static_cast<int>(60LL * current / total);
            emit progressUpdated(std::clamp(value, 25, 85));
        };
        if (!net.separate(song.channels, vocal, progress, stopToken, &loadError)) {
            if (cancelled())
                return;
            emit errorOccurred(t(QStringLiteral("ai_err_infer"),
                                 {{QStringLiteral("{msg}"), QString::fromUtf8(loadError.c_str())}}));
            return;
        }
        if (cancelled())
            return;
        emit progressUpdated(85);

        // 背景音轨 = 混音 - 人声 (UVR is_invert_spec=False 语义)
        std::vector<dsp::Vec> background(2);
        for (int c = 0; c < 2; ++c) {
            background[static_cast<std::size_t>(c)].resize(vocal[static_cast<std::size_t>(c)].size());
            for (std::size_t k = 0; k < vocal[static_cast<std::size_t>(c)].size(); ++k) {
                background[static_cast<std::size_t>(c)][k] =
                    song.channels[static_cast<std::size_t>(c)][k] -
                    vocal[static_cast<std::size_t>(c)][k];
            }
        }

        emit statusUpdated(t(QStringLiteral("ai_saving")));
        AudioData vocalData;
        vocalData.sampleRate = kModelSampleRate;
        vocalData.channels = std::move(vocal);
        AudioData backgroundData;
        backgroundData.sampleRate = kModelSampleRate;
        backgroundData.channels = std::move(background);

        constexpr int kBits = 16;
        if (!writeWav(m_vocalOutPath, vocalData, kBits, &err, stopToken)) {
            if (cancelled())
                return;
            emit errorOccurred(t(QStringLiteral("proc_error"),
                                 {{QStringLiteral("{msg}"),
                                   err.isEmpty() ? QStringLiteral("write failed") : err}}));
            return;
        }
        emit progressUpdated(93);
        if (!writeWav(m_backgroundOutPath, backgroundData, kBits, &err, stopToken)) {
            if (cancelled())
                return;
            emit errorOccurred(t(QStringLiteral("proc_error"),
                                 {{QStringLiteral("{msg}"),
                                   err.isEmpty() ? QStringLiteral("write failed") : err}}));
            return;
        }
        emit progressUpdated(100);
        emit statusUpdated(t(QStringLiteral("ai_done"),
                             {{QStringLiteral("{vocal}"), m_vocalOutPath},
                              {QStringLiteral("{background}"), m_backgroundOutPath}}));
        emit processingFinished(m_vocalOutPath);
    } catch (const std::exception& e) {
        if (cancelled())
            return;
        qWarning() << "Neural processing failed:" << e.what();
        emit errorOccurred(t(QStringLiteral("proc_error"),
                             {{QStringLiteral("{msg}"), QString::fromUtf8(e.what())}}));
    } catch (...) {
        if (cancelled())
            return;
        qWarning() << "Neural processing failed: unknown exception";
        emit errorOccurred(t(QStringLiteral("proc_error"),
                             {{QStringLiteral("{msg}"), QStringLiteral("unknown error")}}));
    }
}
