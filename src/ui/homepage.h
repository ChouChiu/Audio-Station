#pragma once

#include <QString>
#include <QWidget>

class QVBoxLayout;

namespace qfw {
class PrimaryPushButton;
class TitleLabel;
class SubtitleLabel;
} // namespace qfw

// 主页: 品牌问候语 + 两个功能入口按钮 (MR Remove / AI 人声提取)
class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(QWidget* parent = nullptr);

    void setLanguage(const QString& lang); // 存储语言并刷新全部文案
    void refreshText();

signals:
    void mrRequested(); // 前往 MR Remove 页
    void aiRequested(); // 前往 AI 人声提取页

private:
    QString m_lang = QStringLiteral("zh_cn");

    QVBoxLayout* m_mainLayout = nullptr;
    qfw::TitleLabel* m_greetingLabel = nullptr;
    qfw::SubtitleLabel* m_introLabel = nullptr;
    qfw::PrimaryPushButton* m_mrBtn = nullptr;
    qfw::PrimaryPushButton* m_aiBtn = nullptr;
};
