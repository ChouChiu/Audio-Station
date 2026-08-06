#include "mrpage.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "strtable.h"
#include "fluentcombo.h"
#include "fluentlabel.h"
#include "fluentswitch.h"

#include "components/widgets/button.h"
#include "components/widgets/combo_box.h"
#include "components/widgets/label.h"
#include "components/widgets/line_edit.h"
#include "components/widgets/progress_bar.h"
#include "components/widgets/slider.h"
#include "components/widgets/switch_button.h"

namespace {

constexpr int kLabelWidth = 96;

QHBoxLayout* makeRow(QWidget* label, QWidget* control, QWidget* extra = nullptr,
                     QWidget* extra2 = nullptr) {
    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    label->setFixedWidth(kLabelWidth);
    row->addWidget(label);
    row->addWidget(control, 1);
    if (extra)
        row->addWidget(extra);
    if (extra2)
        row->addWidget(extra2);
    return row;
}

} // namespace

MrPage::MrPage(QWidget* parent) : QWidget(parent) {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(36, 28, 36, 28);
    m_mainLayout->setSpacing(16);

    m_title = ui::strongBodyLabel();
    m_mainLayout->addWidget(m_title);

    // ---- 文件选择卡片 ----
    auto* fileCard = new QWidget(this);
    auto* fileLayout = new QVBoxLayout(fileCard);
    fileLayout->setSpacing(14);
    m_fileTitle = ui::strongBodyLabel();
    fileLayout->addWidget(m_fileTitle);

    m_songLabel = ui::bodyLabel();
    m_songPathEdit = new qfw::LineEdit;
    m_songPathEdit->setReadOnly(true);
    m_songBtn = new qfw::PushButton;
    connect(m_songBtn, &qfw::PushButton::clicked, this, &MrPage::selectSong);
    fileLayout->addLayout(makeRow(m_songLabel, m_songPathEdit, m_songBtn));

    m_accLabel = ui::bodyLabel();
    m_accPathEdit = new qfw::LineEdit;
    m_accPathEdit->setReadOnly(true);
    m_accBtn = new qfw::PushButton;
    connect(m_accBtn, &qfw::PushButton::clicked, this, &MrPage::selectAccompaniment);
    m_autoFindSwitch = new ui::ThemeSwitchButton;
    m_autoFindSwitch->setChecked(true);
    connect(m_autoFindSwitch, &qfw::SwitchButton::checkedChanged, this,
            [this](bool checked) { emit autoFindToggled(checked); });
    fileLayout->addLayout(makeRow(m_accLabel, m_accPathEdit, m_accBtn, m_autoFindSwitch));

    m_outputLabel = ui::bodyLabel();
    m_outputPathEdit = new qfw::LineEdit;
    m_outputPathEdit->setReadOnly(true);
    m_outputBtn = new qfw::PushButton;
    connect(m_outputBtn, &qfw::PushButton::clicked, this, &MrPage::selectOutput);
    fileLayout->addLayout(makeRow(m_outputLabel, m_outputPathEdit, m_outputBtn));
    m_mainLayout->addWidget(fileCard);

    // ---- 参数设置卡片 ----
    auto* paramsCard = new QWidget(this);
    auto* paramsLayout = new QVBoxLayout(paramsCard);
    paramsLayout->setSpacing(14);
    m_paramsTitle = ui::strongBodyLabel();
    paramsLayout->addWidget(m_paramsTitle);

    m_algorithmLabel = ui::bodyLabel();
    m_algorithmCombo = new ui::NoAniComboBox;
    paramsLayout->addLayout(makeRow(m_algorithmLabel, m_algorithmCombo));

    m_sigmaLabel = ui::bodyLabel();
    m_sigmaCombo = new ui::NoAniComboBox;
    paramsLayout->addLayout(makeRow(m_sigmaLabel, m_sigmaCombo));

    m_strengthLabel = ui::bodyLabel();
    m_strengthValueLabel = ui::bodyLabel();
    m_strengthValueLabel->setText(QStringLiteral("50%"));
    m_strengthSlider = new qfw::Slider(Qt::Horizontal);
    m_strengthSlider->setRange(0, 100);
    m_strengthSlider->setValue(50);
    m_strengthSlider->setMinimumWidth(180); // 自适应宽度, 随窗口伸缩; 开关作为行末元素始终贴右
    connect(m_strengthSlider, &qfw::Slider::valueChanged, this, [this](int value) {
        m_strengthValueLabel->setText(QStringLiteral("%1%").arg(value));
    });
    m_alignSwitch = new ui::ThemeSwitchButton;
    m_alignSwitch->setChecked(true);
    auto* strengthRow = makeRow(m_strengthLabel, m_strengthSlider, m_strengthValueLabel,
                                m_alignSwitch);
    paramsLayout->addLayout(strengthRow);
    m_mainLayout->addWidget(paramsCard);

    // ---- 处理状态卡片 ----
    auto* statusCard = new QWidget(this);
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setSpacing(12);
    m_statusTitle = ui::strongBodyLabel();
    statusLayout->addWidget(m_statusTitle);
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
    connect(m_cancelBtn, &qfw::PushButton::clicked, this, &MrPage::cancelRequested);
    btnRow->addWidget(m_cancelBtn);
    m_startBtn = new qfw::PrimaryPushButton;
    connect(m_startBtn, &qfw::PushButton::clicked, this, &MrPage::startRequested);
    btnRow->addWidget(m_startBtn);
    m_mainLayout->addLayout(btnRow);

    m_mainLayout->addStretch();
    refreshText();
}

void MrPage::setLanguage(const QString& lang) {
    m_lang = lang;
    refreshText();
}

void MrPage::refreshText() {
    m_title->setText(i18n::t(m_lang, QStringLiteral("nav_mr")));
    m_fileTitle->setText(i18n::t(m_lang, QStringLiteral("file_select")));
    m_paramsTitle->setText(i18n::t(m_lang, QStringLiteral("params")));
    m_statusTitle->setText(i18n::t(m_lang, QStringLiteral("status_group")));
    m_songLabel->setText(i18n::t(m_lang, QStringLiteral("song_label")));
    m_accLabel->setText(i18n::t(m_lang, QStringLiteral("acc_label")));
    m_outputLabel->setText(i18n::t(m_lang, QStringLiteral("output_file")));
    m_songBtn->setText(i18n::t(m_lang, QStringLiteral("browse")));
    m_accBtn->setText(i18n::t(m_lang, QStringLiteral("browse")));
    m_outputBtn->setText(i18n::t(m_lang, QStringLiteral("browse")));
    // SwitchButton 切换时自动在 on/off 文案间切换 (库的 updateText 会覆盖 setText)
    m_autoFindSwitch->setOnText(i18n::t(m_lang, QStringLiteral("auto_find_on")));
    m_autoFindSwitch->setOffText(i18n::t(m_lang, QStringLiteral("auto_find_off")));
    m_alignSwitch->setOnText(i18n::t(m_lang, QStringLiteral("auto_align")));
    m_alignSwitch->setOffText(i18n::t(m_lang, QStringLiteral("auto_align")));
    m_alignSwitch->setToolTip(i18n::t(m_lang, QStringLiteral("auto_align_tip")));
    m_algorithmLabel->setText(i18n::t(m_lang, QStringLiteral("algorithm")));
    m_sigmaLabel->setText(i18n::t(m_lang, QStringLiteral("reverb_label")));
    m_strengthLabel->setText(i18n::t(m_lang, QStringLiteral("strength")));
    m_cancelBtn->setText(i18n::t(m_lang, QStringLiteral("cancel")));
    m_startBtn->setText(i18n::t(m_lang, QStringLiteral("start")));

    const QVariant prevAlgo = m_algorithmCombo->currentData();
    m_algorithmCombo->clear();
    // qfw addItem(text, icon, userData) — 第 3 参才是 userData
    m_algorithmCombo->addItem(i18n::t(m_lang, QStringLiteral("algo_lossless")), QVariant(),
                              QStringLiteral("lossless"));
    m_algorithmCombo->addItem(i18n::t(m_lang, QStringLiteral("algo_soft_mask")), QVariant(),
                              QStringLiteral("soft_mask"));
    m_algorithmCombo->addItem(i18n::t(m_lang, QStringLiteral("algo_spectral")), QVariant(),
                              QStringLiteral("spectral_subtraction"));
    m_algorithmCombo->addItem(i18n::t(m_lang, QStringLiteral("algo_wiener")), QVariant(),
                              QStringLiteral("wiener_filter"));
    m_algorithmCombo->addItem(i18n::t(m_lang, QStringLiteral("algo_freq_weight")), QVariant(),
                              QStringLiteral("frequency_weighted"));
    m_algorithmCombo->addItem(i18n::t(m_lang, QStringLiteral("algo_binary")), QVariant(),
                              QStringLiteral("binary_mask"));
    m_algorithmCombo->addItem(i18n::t(m_lang, QStringLiteral("algo_phase")), QVariant(),
                              QStringLiteral("phase_sensitive"));
    if (prevAlgo.isValid()) {
        const int idx = m_algorithmCombo->findData(prevAlgo);
        if (idx >= 0)
            m_algorithmCombo->setCurrentIndex(idx);
    }

    const QVariant prevSigma = m_sigmaCombo->currentData();
    m_sigmaCombo->clear();
    m_sigmaCombo->addItem(i18n::t(m_lang, QStringLiteral("sigma_0")), QVariant(), 1);
    m_sigmaCombo->addItem(i18n::t(m_lang, QStringLiteral("sigma_1")), QVariant(), 3);
    m_sigmaCombo->addItem(i18n::t(m_lang, QStringLiteral("sigma_2")), QVariant(), 8);
    m_sigmaCombo->addItem(i18n::t(m_lang, QStringLiteral("sigma_3")), QVariant(), 16);
    if (prevSigma.isValid()) {
        const int idx = m_sigmaCombo->findData(prevSigma);
        if (idx >= 0)
            m_sigmaCombo->setCurrentIndex(idx);
        else
            m_sigmaCombo->setCurrentIndex(0);
    } else {
        m_sigmaCombo->setCurrentIndex(0);
    }
}

// ---- 状态读取 ----

QString MrPage::songPath() const { return m_songPath; }
QString MrPage::accompanimentPath() const { return m_accompanimentPath; }
QString MrPage::outputPath() const { return m_outputPath; }
void MrPage::setSongPath(const QString& path) {
    m_songPath = path;
    m_songPathEdit->setText(path);
}
void MrPage::setAccompanimentPath(const QString& path) {
    m_accompanimentPath = path;
    m_accPathEdit->setText(path);
}
void MrPage::setOutputPath(const QString& path) {
    m_outputPath = path;
    m_outputPathEdit->setText(path);
}
bool MrPage::autoFindEnabled() const { return m_autoFindSwitch->isChecked(); }
bool MrPage::autoAlignEnabled() const { return m_alignSwitch->isChecked(); }
int MrPage::strength() const { return m_strengthSlider->value(); }
int MrPage::sigmaTime() const { return m_sigmaCombo->currentData().toInt(); }
QString MrPage::algorithmKey() const { return m_algorithmCombo->currentData().toString(); }

void MrPage::setStatus(const QString& text) {
    m_statusLabel->setText(text);
}

void MrPage::setProgress(int value) {
    m_progressBar->setValue(value);
}

void MrPage::setProcessing(bool running) {
    m_startBtn->setEnabled(!running);
    m_cancelBtn->setEnabled(running);
    m_songBtn->setEnabled(!running);
    m_accBtn->setEnabled(!running);
    m_outputBtn->setEnabled(!running);
    m_autoFindSwitch->setEnabled(!running);
    m_algorithmCombo->setEnabled(!running);
    m_sigmaCombo->setEnabled(!running);
    m_strengthSlider->setEnabled(!running);
    m_alignSwitch->setEnabled(!running);
}

// ---- 文件选择 ----

void MrPage::selectSong() {
    const QString filePath = QFileDialog::getOpenFileName(
        this, i18n::t(m_lang, QStringLiteral("song_label")), QString(),
        QStringLiteral("Audio (*.mp3 *.wav *.flac *.m4a)"));
    if (filePath.isEmpty())
        return;
    m_songPath = filePath;
    m_songPathEdit->setText(filePath);
    emit songChanged(filePath);
}

void MrPage::selectAccompaniment() {
    const QString filePath = QFileDialog::getOpenFileName(
        this, i18n::t(m_lang, QStringLiteral("acc_label")), QString(),
        QStringLiteral("Audio (*.mp3 *.wav *.flac *.m4a)"));
    if (filePath.isEmpty())
        return;
    m_accompanimentPath = filePath;
    m_accPathEdit->setText(filePath);
    emit accompanimentChanged(filePath);
}

void MrPage::selectOutput() {
    const QString filePath = QFileDialog::getSaveFileName(
        this, i18n::t(m_lang, QStringLiteral("output_file")), m_outputPath,
        QStringLiteral("Audio (*.wav)"));
    if (filePath.isEmpty())
        return;
    m_outputPath = filePath;
    m_outputPathEdit->setText(filePath);
    emit outputChanged(filePath);
}
