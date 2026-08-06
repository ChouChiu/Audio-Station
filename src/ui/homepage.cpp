#include "homepage.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include "strtable.h"
#include "fluentlabel.h"

#include "common/icon.h"
#include "components/widgets/button.h"
#include "components/widgets/label.h"

namespace {

constexpr int kButtonWidth = 220;
constexpr int kButtonHeight = 52;
constexpr int kIntroMaxWidth = 560;

} // namespace

HomePage::HomePage(QWidget* parent) : QWidget(parent) {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(48, 32, 48, 32);
    m_mainLayout->setSpacing(0);

    m_mainLayout->addStretch(3);

    // 问候语
    m_greetingLabel = new qfw::TitleLabel(this);
    // 上游 FluentLabelBase 深色主题下文字为黑色, 显式设 (黑, 白)
    m_greetingLabel->setTextColor(QColor(0, 0, 0), QColor(255, 255, 255));
    m_greetingLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_greetingLabel);

    m_mainLayout->addSpacing(12);

    // 介绍
    m_introLabel = new qfw::SubtitleLabel(this);
    m_introLabel->setTextColor(QColor(0, 0, 0), QColor(255, 255, 255));
    m_introLabel->setAlignment(Qt::AlignCenter);
    m_introLabel->setWordWrap(true);
    m_introLabel->setMaximumWidth(kIntroMaxWidth);
    m_introLabel->setMinimumWidth(kIntroMaxWidth);
    auto* introWrap = new QHBoxLayout;
    introWrap->addStretch();
    introWrap->addWidget(m_introLabel);
    introWrap->addStretch();
    m_mainLayout->addLayout(introWrap);

    m_mainLayout->addSpacing(36);

    // 两个功能入口按钮
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(20);
    btnRow->addStretch();
    m_mrBtn = new qfw::PrimaryPushButton(this);
    m_mrBtn->setFixedSize(kButtonWidth, kButtonHeight);
    m_mrBtn->setIcon(qfw::FluentIcon(qfw::FluentIconEnum::Music).qicon());
    connect(m_mrBtn, &qfw::PushButton::clicked, this, &HomePage::mrRequested);
    btnRow->addWidget(m_mrBtn);
    m_aiBtn = new qfw::PrimaryPushButton(this);
    m_aiBtn->setFixedSize(kButtonWidth, kButtonHeight);
    m_aiBtn->setIcon(qfw::FluentIcon(qfw::FluentIconEnum::MixVolumes).qicon());
    connect(m_aiBtn, &qfw::PushButton::clicked, this, &HomePage::aiRequested);
    btnRow->addWidget(m_aiBtn);
    btnRow->addStretch();
    m_mainLayout->addLayout(btnRow);

    m_mainLayout->addStretch(4);
    refreshText();
}

void HomePage::setLanguage(const QString& lang) {
    m_lang = lang;
    refreshText();
}

void HomePage::refreshText() {
    m_greetingLabel->setText(i18n::t(m_lang, QStringLiteral("home_greeting")));
    m_introLabel->setText(i18n::t(m_lang, QStringLiteral("home_intro")));
    m_mrBtn->setText(i18n::t(m_lang, QStringLiteral("nav_mr")));
    m_aiBtn->setText(i18n::t(m_lang, QStringLiteral("nav_ai")));
}
