#include "aipage.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "modelcatalog.h"
#include "strtable.h"
#include "fluentcombo.h"
#include "fluentlabel.h"

#include "components/widgets/button.h"
#include "components/widgets/combo_box.h"
#include "components/widgets/label.h"
#include "components/widgets/line_edit.h"
#include "components/widgets/progress_bar.h"

namespace {

constexpr int kLabelWidth = 96;

QHBoxLayout* makeRow(QWidget* label, QWidget* control, QWidget* extra = nullptr) {
    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    label->setFixedWidth(kLabelWidth);
    row->addWidget(label);
    row->addWidget(control, 1);
    if (extra)
        row->addWidget(extra);
    return row;
}

} // namespace

AiPage::AiPage(QWidget* parent) : QWidget(parent) {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(36, 28, 36, 28);
    m_mainLayout->setSpacing(16);

    // ---- 标题 ----
    m_title = ui::strongBodyLabel();
    m_mainLayout->addWidget(m_title);

    // ---- 歌曲文件 ----
    auto* fileCard = new QWidget(this);
    auto* fileLayout = new QVBoxLayout(fileCard);
    fileLayout->setSpacing(14);
    m_songLabel = ui::bodyLabel();
    m_songPathEdit = new qfw::LineEdit;
    m_songPathEdit->setReadOnly(true);
    m_songBtn = new qfw::PushButton;
    connect(m_songBtn, &qfw::PushButton::clicked, this, &AiPage::selectSong);
    fileLayout->addLayout(makeRow(m_songLabel, m_songPathEdit, m_songBtn));
    m_mainLayout->addWidget(fileCard);

    // ---- 模型选择 ----
    auto* modelCard = new QWidget(this);
    auto* modelLayout = new QVBoxLayout(modelCard);
    modelLayout->setSpacing(14);
    m_modelLabel = ui::bodyLabel();
    m_modelCombo = new ui::NoAniComboBox;
    connect(m_modelCombo, &ui::NoAniComboBox::currentIndexChanged, this,
            [this](int) { emit modelChanged(modelId()); });
    modelLayout->addLayout(makeRow(m_modelLabel, m_modelCombo));
    m_modelStatus = ui::bodyLabel();
    m_modelStatus->setWordWrap(true);
    modelLayout->addWidget(m_modelStatus);
    m_mainLayout->addWidget(modelCard);

    // ---- 处理状态 ----
    auto* statusCard = new QWidget(this);
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setSpacing(12);
    m_statusLabel = ui::captionLabel();
    statusLayout->addWidget(m_statusLabel);
    m_progressBar = new qfw::ProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    statusLayout->addWidget(m_progressBar);
    m_mainLayout->addWidget(statusCard);

    // ---- 操作按钮 ----
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_cancelBtn = new qfw::PushButton;
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &qfw::PushButton::clicked, this, &AiPage::cancelRequested);
    btnRow->addWidget(m_cancelBtn);
    m_extractBtn = new qfw::PrimaryPushButton;
    connect(m_extractBtn, &qfw::PushButton::clicked, this, &AiPage::extractRequested);
    btnRow->addWidget(m_extractBtn);
    m_mainLayout->addLayout(btnRow);

    m_mainLayout->addStretch();
    refreshText();
}

void AiPage::setLanguage(const QString& lang) {
    m_lang = lang;
    refreshText();
}

void AiPage::refreshText() {
    m_title->setText(i18n::t(m_lang, QStringLiteral("ai_title")));
    m_songLabel->setText(i18n::t(m_lang, QStringLiteral("song_label")));
    m_songBtn->setText(i18n::t(m_lang, QStringLiteral("browse")));
    m_modelLabel->setText(i18n::t(m_lang, QStringLiteral("ai_model_label")));
    m_cancelBtn->setText(i18n::t(m_lang, QStringLiteral("cancel")));
    m_extractBtn->setText(i18n::t(m_lang, QStringLiteral("ai_extract")));

    const QVariant prevModel = m_modelCombo->currentData();
    m_modelCombo->clear();
    // qfw addItem(text, icon, userData) — 第 3 参才是 userData
    for (const neural::ModelEntry& entry : neural::modelCatalog()) {
        const QString id = QString::fromStdString(entry.id);
        const QString description = i18n::t(m_lang, QStringLiteral("mdl_") + id);
        m_modelCombo->addItem(QString::fromStdString(entry.displayName) +
                                  (description == QStringLiteral("mdl_") + id
                                       ? QString()
                                       : QStringLiteral(" — ") + description),
                              QVariant(), id);
    }
    const QVariant target = prevModel.isValid() ? prevModel
                             : QVariant(QString::fromStdString(neural::defaultModel()->id));
    const int idx = m_modelCombo->findData(target);
    m_modelCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void AiPage::setStatus(const QString& text) {
    m_statusLabel->setText(text);
}

void AiPage::setProgress(int value) {
    m_progressBar->setValue(value);
}

void AiPage::setProcessing(bool running) {
    m_extractBtn->setEnabled(!running);
    m_cancelBtn->setEnabled(running);
    m_songBtn->setEnabled(!running);
}

void AiPage::setModelStatus(const QString& text) {
    m_modelStatus->setText(text);
}

QString AiPage::songPath() const {
    return m_songPath;
}

void AiPage::setSongPath(const QString& path) {
    m_songPath = path;
    m_songPathEdit->setText(path);
}

QString AiPage::modelId() const {
    const QVariant data = m_modelCombo->currentData();
    return data.isValid() ? data.toString()
                          : QString::fromStdString(neural::defaultModel()->id);
}

void AiPage::setModelId(const QString& id) {
    const int idx = m_modelCombo->findData(id);
    if (idx >= 0)
        m_modelCombo->setCurrentIndex(idx);
}

// ---- 文件选择 ----

void AiPage::selectSong() {
    const QString filePath = QFileDialog::getOpenFileName(
        this, i18n::t(m_lang, QStringLiteral("song_label")), QString(),
        QStringLiteral("Audio (*.mp3 *.wav *.flac *.m4a)"));
    if (filePath.isEmpty())
        return;
    m_songPath = filePath;
    m_songPathEdit->setText(filePath);
    emit songChanged(filePath);
}
