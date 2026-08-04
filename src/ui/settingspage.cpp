#include "settingspage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "strtable.h"
#include "fluentcombo.h"
#include "fluentlabel.h"

#include "components/widgets/combo_box.h"
#include "components/widgets/label.h"

namespace {

constexpr int kLabelWidth = 96;

QHBoxLayout* makeRow(QWidget* label, QWidget* control) {
    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    label->setFixedWidth(kLabelWidth);
    row->addWidget(label);
    row->addWidget(control, 1);
    return row;
}

} // namespace

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(36, 28, 36, 28);
    m_mainLayout->setSpacing(16);

    // ---- 设置卡片 ----
    auto* settingsCard = new QWidget(this);
    auto* settingsLayout = new QVBoxLayout(settingsCard);
    settingsLayout->setSpacing(14);
    m_settingsTitle = ui::strongBodyLabel();
    settingsLayout->addWidget(m_settingsTitle);

    m_langLabel = ui::bodyLabel();
    m_langCombo = new ui::NoAniComboBox;
    m_langCombo->addItem(QStringLiteral("中文"), QVariant(), QStringLiteral("zh_cn"));
    m_langCombo->addItem(QStringLiteral("日本語"), QVariant(), QStringLiteral("ja_jp"));
    m_langCombo->addItem(QStringLiteral("한국어"), QVariant(), QStringLiteral("ko_kr"));
    connect(m_langCombo, &qfw::ComboBox::currentIndexChanged, this, [this](int index) {
        emit languageChanged(m_langCombo->itemData(index).toString());
    });
    settingsLayout->addLayout(makeRow(m_langLabel, m_langCombo));

    m_themeLabel = ui::bodyLabel();
    m_themeCombo = new ui::NoAniComboBox;
    connect(m_themeCombo, &qfw::ComboBox::currentIndexChanged, this, [this](int index) {
        emit themeChanged(m_themeCombo->itemData(index).toString());
    });
    settingsLayout->addLayout(makeRow(m_themeLabel, m_themeCombo));
    m_mainLayout->addWidget(settingsCard);

    // ---- 关于卡片 ----
    auto* aboutCard = new QWidget(this);
    auto* aboutLayout = new QVBoxLayout(aboutCard);
    aboutLayout->setSpacing(8);
    m_aboutTitle = ui::strongBodyLabel();
    aboutLayout->addWidget(m_aboutTitle);
    m_appNameLabel = ui::captionLabel();
    aboutLayout->addWidget(m_appNameLabel);
    m_appDescLabel = ui::captionLabel();
    aboutLayout->addWidget(m_appDescLabel);
    m_mainLayout->addWidget(aboutCard);

    m_mainLayout->addStretch();
    refreshText();
}

void SettingsPage::setLanguage(const QString& lang) {
    m_lang = lang;
    refreshText();
}

void SettingsPage::refreshText() {
    m_settingsTitle->setText(i18n::t(m_lang, QStringLiteral("nav_settings")));
    m_langLabel->setText(i18n::t(m_lang, QStringLiteral("lang_label")));
    m_themeLabel->setText(i18n::t(m_lang, QStringLiteral("theme_label")));
    m_aboutTitle->setText(i18n::t(m_lang, QStringLiteral("nav_settings")));
    m_appNameLabel->setText(i18n::t(m_lang, QStringLiteral("window_title")));
    m_appDescLabel->setText(i18n::t(m_lang, QStringLiteral("app_desc")));

    const QSignalBlocker blocker(m_themeCombo);
    const QVariant prevTheme = m_themeCombo->currentData();
    m_themeCombo->clear();
    m_themeCombo->addItem(i18n::t(m_lang, QStringLiteral("theme_light")), QVariant(),
                          QStringLiteral("light"));
    m_themeCombo->addItem(i18n::t(m_lang, QStringLiteral("theme_dark")), QVariant(),
                          QStringLiteral("dark"));
    m_themeCombo->addItem(i18n::t(m_lang, QStringLiteral("theme_auto")), QVariant(),
                          QStringLiteral("auto"));
    if (prevTheme.isValid()) {
        const int idx = m_themeCombo->findData(prevTheme);
        if (idx >= 0)
            m_themeCombo->setCurrentIndex(idx);
    }
}

QString SettingsPage::language() const { return m_lang; }

void SettingsPage::setLanguageSelection(const QString& lang) {
    const int idx = m_langCombo->findData(lang);
    if (idx >= 0)
        m_langCombo->setCurrentIndex(idx);
}

QString SettingsPage::theme() const { return m_themeCombo->currentData().toString(); }

void SettingsPage::setThemeSelection(const QString& theme) {
    const int idx = m_themeCombo->findData(theme);
    if (idx >= 0)
        m_themeCombo->setCurrentIndex(idx);
}
