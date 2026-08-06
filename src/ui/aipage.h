#pragma once

#include <QString>
#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;

namespace qfw {
class LineEdit;
class PushButton;
class PrimaryPushButton;
class ProgressBar;
class StrongBodyLabel;
class BodyLabel;
class CaptionLabel;
} // namespace qfw

namespace ui {
class NoAniComboBox;
} // namespace ui

// AI 人声提取页: 歌曲选择 + 模型选择 + 一键提取 (UVR MDX-Net, 去背景音, 无需伴奏)
class AiPage : public QWidget {
    Q_OBJECT
public:
    explicit AiPage(QWidget* parent = nullptr);

    void setLanguage(const QString& lang); // 存储语言并刷新全部文案
    void refreshText();

    // ---- 处理管线反馈 ----
    void setStatus(const QString& text);
    void setProgress(int value);
    void setProcessing(bool running);
    void setModelStatus(const QString& text);

    QString songPath() const;
    void setSongPath(const QString& path);
    QString modelId() const; // 当前选中的模型目录 id
    void setModelId(const QString& id);

signals:
    void extractRequested();
    void cancelRequested();
    void songChanged(const QString& path);
    void modelChanged(const QString& id);

private:
    void selectSong();

    QString m_songPath;
    QString m_lang = QStringLiteral("zh_cn");

    QVBoxLayout* m_mainLayout = nullptr;
    qfw::StrongBodyLabel* m_title = nullptr;

    qfw::BodyLabel* m_songLabel = nullptr;
    qfw::LineEdit* m_songPathEdit = nullptr;
    qfw::PushButton* m_songBtn = nullptr;

    qfw::BodyLabel* m_modelLabel = nullptr;
    ui::NoAniComboBox* m_modelCombo = nullptr;
    qfw::BodyLabel* m_modelStatus = nullptr;

    qfw::CaptionLabel* m_statusLabel = nullptr;
    qfw::ProgressBar* m_progressBar = nullptr;

    qfw::PushButton* m_cancelBtn = nullptr;
    qfw::PrimaryPushButton* m_extractBtn = nullptr;
};
