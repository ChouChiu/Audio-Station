#pragma once

#include <QString>
#include <QThread>

#include <stop_token>

#include "dsp.h"

class ProcessingThread : public QThread {
    Q_OBJECT
public:
    struct Params {
        int strength = 75;
        bool autoAlign = true;
        dsp::Algorithm algorithm = dsp::Algorithm::Lossless;
        int sigmaTime = 1; // 默认 σ=1, 镜像参考 combo 首次填充停在索引 0
        QString lang = QStringLiteral("zh_cn");
    };

    ProcessingThread(QString songPath, QString accPath, QString outPath, Params params,
                     QObject* parent = nullptr);

    void cancel();

signals:
    void progressUpdated(int value);
    void statusUpdated(const QString& msg);
    void processingFinished(const QString& outputPath);
    void processingCancelled();
    void errorOccurred(const QString& msg);

protected:
    void run() override;

private:
    QString m_songPath;
    QString m_accPath;
    QString m_outPath;
    Params m_params;
    std::stop_source m_stopSource;
};
