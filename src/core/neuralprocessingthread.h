#pragma once

#include <QString>
#include <QThread>

#include <stop_token>

// AI 人声提取 (UVR MDX-Net + onnxruntime):
// 输入歌曲, 输出人声与背景 (去背景音) 两个 WAV。无需伴奏参考。
// 模型按目录 id 指定; 本地缺失时自动下载到 models/ 目录 (进度并入总进度 0-15%)。
class NeuralProcessingThread : public QThread {
    Q_OBJECT
public:
    NeuralProcessingThread(QString songPath, QString modelId, QString vocalOutPath,
                           QString backgroundOutPath, QString lang, QString modelsDirOverride = {},
                           QObject* parent = nullptr);

    void cancel();

    QString vocalOutPath() const { return m_vocalOutPath; }
    QString backgroundOutPath() const { return m_backgroundOutPath; }

signals:
    void progressUpdated(int value);
    void statusUpdated(const QString& msg);
    void processingFinished(const QString& outputPath);
    void processingCancelled();
    void errorOccurred(const QString& msg);

protected:
    void run() override;

private:
    // 下载缺失的模型到 modelsDir; 进度经 progressUpdated (0-14) 汇报; 失败置 errorOut。
    bool downloadModel(const QString& url, const QString& fileName, const QString& modelsDir,
                       qint64 expectedBytes, const std::stop_token& stopToken,
                       QString* errorOut);

    QString m_songPath;
    QString m_modelId;
    QString m_vocalOutPath;
    QString m_backgroundOutPath;
    QString m_lang;
    QString m_modelsDirOverride;
    std::stop_source m_stopSource;
};
