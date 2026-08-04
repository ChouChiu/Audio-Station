#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace qfw {
class StrongBodyLabel;
class CaptionLabel;
class BodyLabel;
} // namespace qfw

namespace ui {
class NoAniComboBox;
} // namespace ui

// 设置页: 语言 + 主题 + 版本信息 (Qt-Fluent-Widgets 卡片布局)
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);

    void setLanguage(const QString& lang);
    void refreshText();

    QString language() const;
    void setLanguageSelection(const QString& lang);

    QString theme() const;
    void setThemeSelection(const QString& theme);

signals:
    void languageChanged(const QString& lang);
    void themeChanged(const QString& theme);

private:
    QString m_lang = QStringLiteral("zh_cn");

    QVBoxLayout* m_mainLayout = nullptr;
    qfw::StrongBodyLabel* m_settingsTitle = nullptr;
    qfw::BodyLabel* m_langLabel = nullptr;
    ui::NoAniComboBox* m_langCombo = nullptr;
    qfw::BodyLabel* m_themeLabel = nullptr;
    ui::NoAniComboBox* m_themeCombo = nullptr;
    qfw::StrongBodyLabel* m_aboutTitle = nullptr;
    qfw::CaptionLabel* m_appNameLabel = nullptr;
    qfw::CaptionLabel* m_appDescLabel = nullptr;
};
