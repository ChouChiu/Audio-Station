#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QHBoxLayout;
class QVBoxLayout;

namespace qfw {
class LineEdit;
class PushButton;
class PrimaryPushButton;
class Slider;
class SwitchButton;
class ProgressBar;
class StrongBodyLabel;
class CaptionLabel;
class BodyLabel;
} // namespace qfw

namespace ui {
class NoAniComboBox;
} // namespace ui

// 主页: 文件选择 + 参数设置 + 处理状态 (Qt-Fluent-Widgets 卡片布局)
class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(QWidget* parent = nullptr);

    void setLanguage(const QString& lang); // 存储语言并刷新全部文案
    void refreshText();

    // ---- 处理管线反馈 ----
    void setStatus(const QString& text);
    void setProgress(int value);
    void setProcessing(bool running);

    // ---- 状态读取 (MainWindow 组装 ProcessingThread::Params) ----
    QString songPath() const;
    QString accompanimentPath() const;
    QString outputPath() const;
    void setSongPath(const QString& path);
    void setAccompanimentPath(const QString& path);
    void setOutputPath(const QString& path);
    bool autoFindEnabled() const;
    bool autoAlignEnabled() const;
    int strength() const;
    int sigmaTime() const;
    QString algorithmKey() const;

signals:
    void startRequested();
    void cancelRequested();
    void songChanged(const QString& path);
    void accompanimentChanged(const QString& path);
    void outputChanged(const QString& path);
    void autoFindToggled(bool enabled);

private:
    void selectSong();
    void selectAccompaniment();
    void selectOutput();

    QString m_songPath;
    QString m_accompanimentPath;
    QString m_outputPath;
    QString m_lang = QStringLiteral("zh_cn");

    QVBoxLayout* m_mainLayout = nullptr;
    qfw::StrongBodyLabel* m_fileTitle = nullptr;
    qfw::StrongBodyLabel* m_paramsTitle = nullptr;
    qfw::StrongBodyLabel* m_statusTitle = nullptr;

    // 行标签用 qfw::BodyLabel: 随主题变化自动换色 (普通 QLabel 不会)
    qfw::BodyLabel* m_songLabel = nullptr;
    qfw::BodyLabel* m_accLabel = nullptr;
    qfw::BodyLabel* m_outputLabel = nullptr;
    qfw::LineEdit* m_songPathEdit = nullptr;
    qfw::LineEdit* m_accPathEdit = nullptr;
    qfw::LineEdit* m_outputPathEdit = nullptr;
    qfw::PushButton* m_songBtn = nullptr;
    qfw::PushButton* m_accBtn = nullptr;
    qfw::PushButton* m_outputBtn = nullptr;
    qfw::SwitchButton* m_autoFindSwitch = nullptr;

    qfw::BodyLabel* m_algorithmLabel = nullptr;
    ui::NoAniComboBox* m_algorithmCombo = nullptr;
    qfw::BodyLabel* m_sigmaLabel = nullptr;
    ui::NoAniComboBox* m_sigmaCombo = nullptr;
    qfw::BodyLabel* m_strengthLabel = nullptr;
    qfw::BodyLabel* m_strengthValueLabel = nullptr;
    qfw::Slider* m_strengthSlider = nullptr;
    qfw::SwitchButton* m_alignSwitch = nullptr;

    qfw::CaptionLabel* m_statusLabel = nullptr;
    qfw::ProgressBar* m_progressBar = nullptr;

    qfw::PushButton* m_cancelBtn = nullptr;
    qfw::PrimaryPushButton* m_startBtn = nullptr;
};
