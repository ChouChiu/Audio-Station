#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QFileInfo>
#include <QPalette>
#include <QSettings>
#include <QVBoxLayout>

#include "accompanimentfinder.h"
#include "aipage.h"
#include "filepaths.h"
#include "homepage.h"
#include "modelcatalog.h"
#include "mrpage.h"
#include "neuralpaths.h"
#include "settingspage.h"
#include "strtable.h"

#include "components/navigation/navigation_widget.h"
#include "components/widgets/info_bar.h"
#include "components/widgets/state_tool_tip.h"

namespace {

qfw::Theme themeFromKey(const QString& key) {
    if (key == QLatin1String("dark"))
        return qfw::Theme::Dark;
    if (key == QLatin1String("light"))
        return qfw::Theme::Light;
    return qfw::Theme::Auto;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : FluentWindow(parent) {
    QSettings settings;
    m_lang = settings.value(QStringLiteral("lang"), QStringLiteral("zh_cn")).toString();
    m_theme = settings.value(QStringLiteral("theme"), QStringLiteral("auto")).toString();

    setWindowTitle(t(QStringLiteral("window_title")));
    resize(960, 680);
    setMinimumSize(780, 600);

    qfw::setTheme(themeFromKey(m_theme));
    applySystemAccent();
    initNavigation();
    refreshAllText();
}

QString MainWindow::t(const QString& key, const QString& path, const QString& msg) const {
    QString s = i18n::t(m_lang, key);
    if (!path.isEmpty())
        s.replace(QStringLiteral("{path}"), path);
    if (!msg.isEmpty())
        s.replace(QStringLiteral("{msg}"), msg);
    return s;
}

void MainWindow::initNavigation() {
    m_homePage = new HomePage(this);
    m_homePage->setObjectName(QStringLiteral("homePage"));
    m_homeNav = addSubInterface(m_homePage, qfw::FluentIconEnum::Home, t(QStringLiteral("nav_home")));

    m_mrPage = new MrPage(this);
    m_mrPage->setObjectName(QStringLiteral("mrPage"));
    m_mrNav = addSubInterface(m_mrPage, qfw::FluentIconEnum::Music, t(QStringLiteral("nav_mr")));

    m_aiPage = new AiPage(this);
    m_aiPage->setObjectName(QStringLiteral("aiPage"));
    m_aiNav = addSubInterface(m_aiPage, qfw::FluentIconEnum::MixVolumes,
                              t(QStringLiteral("nav_ai")));

    m_settingsPage = new SettingsPage(this);
    m_settingsPage->setObjectName(QStringLiteral("settingsPage"));
    m_settingsNav = addSubInterface(m_settingsPage, qfw::FluentIconEnum::Settings,
                                    t(QStringLiteral("nav_settings")));

    m_homePage->setLanguage(m_lang);
    m_mrPage->setLanguage(m_lang);
    m_aiPage->setLanguage(m_lang);
    m_settingsPage->setLanguage(m_lang);
    m_settingsPage->setLanguageSelection(m_lang);
    m_settingsPage->setThemeSelection(m_theme);

    // 主页两个入口按钮: 跳转到对应功能页
    connect(m_homePage, &HomePage::mrRequested, this, [this] { switchTo(m_mrPage); });
    connect(m_homePage, &HomePage::aiRequested, this, [this] { switchTo(m_aiPage); });

    connect(m_mrPage, &MrPage::startRequested, this, &MainWindow::startProcessing);
    connect(m_mrPage, &MrPage::cancelRequested, this, &MainWindow::cancelProcessing);
    connect(m_aiPage, &AiPage::extractRequested, this, &MainWindow::startAiExtraction);
    connect(m_aiPage, &AiPage::cancelRequested, this, &MainWindow::cancelProcessing);
    connect(m_aiPage, &AiPage::modelChanged, this,
            [this](const QString& id) { updateAiModelStatus(id); });
    connect(m_mrPage, &MrPage::songChanged, this, [this](const QString&) {
        const QFileInfo info(m_mrPage->songPath());
        m_mrPage->setOutputPath(info.absolutePath() + QLatin1Char('/') + info.completeBaseName() +
                                QStringLiteral("_vocals.wav"));
        if (m_mrPage->autoFindEnabled())
            autoFindAccompaniment();
    });
    connect(m_mrPage, &MrPage::autoFindToggled, this, [this](bool enabled) {
        if (enabled && !m_mrPage->songPath().isEmpty())
            autoFindAccompaniment();
    });
    updateAiModelStatus(m_aiPage->modelId());
    connect(m_settingsPage, &SettingsPage::languageChanged, this, [this](const QString& lang) {
        m_lang = lang;
        QSettings().setValue(QStringLiteral("lang"), lang);
        m_homePage->setLanguage(lang);
        m_mrPage->setLanguage(lang);
        m_settingsPage->setLanguage(lang);
        refreshAllText();
    });
    connect(m_settingsPage, &SettingsPage::themeChanged, this, [this](const QString& theme) {
        m_theme = theme;
        QSettings().setValue(QStringLiteral("theme"), theme);
        qfw::setTheme(themeFromKey(theme));
        applySystemAccent(); // 深/浅色切换后系统调色板可能变化, 重新同步强调色
    });
}

void MainWindow::refreshAllText() {
    setWindowTitle(t(QStringLiteral("window_title")));
    if (auto* nav = qobject_cast<qfw::NavigationTreeWidget*>(m_homeNav))
        nav->setText(t(QStringLiteral("nav_home")));
    if (auto* nav = qobject_cast<qfw::NavigationTreeWidget*>(m_mrNav))
        nav->setText(t(QStringLiteral("nav_mr")));
    if (auto* nav = qobject_cast<qfw::NavigationTreeWidget*>(m_aiNav))
        nav->setText(t(QStringLiteral("nav_ai")));
    if (auto* nav = qobject_cast<qfw::NavigationTreeWidget*>(m_settingsNav))
        nav->setText(t(QStringLiteral("nav_settings")));
}

void MainWindow::applySystemAccent() {
    // Qt 平台主题把系统强调色暴露在 QPalette::Highlight (KDE/GNOME/Windows 均适用)
    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    if (!accent.isValid())
        return;
    // 防御: 近灰/无效强调色不应用 (避免整套控件变灰)
    if (accent.hsvSaturationF() < 0.08f)
        return;
    qfw::setThemeColor(accent, /*save=*/false, /*lazy=*/false);
}

bool MainWindow::event(QEvent* e) {
    // 系统主题/配色切换时 (如 KDE 换配色方案) 重新读取强调色
    if (e->type() == QEvent::ApplicationPaletteChange)
        applySystemAccent();
    return FluentWindow::event(e);
}

// ---- 处理管线 ----

void MainWindow::startProcessing() {
    if (m_thread)
        return;

    const auto warn = [this](const QString& key) {
        qfw::InfoBar::warning(t(QStringLiteral("warn_title")), t(key), Qt::Horizontal, true, 3000,
                              qfw::InfoBarPosition::TopRight, this);
    };
    if (m_mrPage->songPath().isEmpty()) {
        warn(QStringLiteral("warn_no_song"));
        return;
    }
    if (m_mrPage->accompanimentPath().isEmpty()) {
        warn(QStringLiteral("warn_no_acc"));
        return;
    }
    if (m_mrPage->outputPath().isEmpty()) {
        warn(QStringLiteral("warn_no_out"));
        return;
    }
    if (filepaths::equal(m_mrPage->outputPath(), m_mrPage->songPath()) ||
        filepaths::equal(m_mrPage->outputPath(), m_mrPage->accompanimentPath())) {
        warn(QStringLiteral("warn_output_conflict"));
        return;
    }

    ProcessingThread::Params params;
    params.strength = m_mrPage->strength();
    params.autoAlign = m_mrPage->autoAlignEnabled();
    const QByteArray algorithmKey = m_mrPage->algorithmKey().toUtf8();
    const auto algorithm = dsp::algorithmFromString(algorithmKey.constData());
    if (!algorithm.has_value()) {
        warn(QStringLiteral("warn_invalid_algorithm"));
        return;
    }
    params.algorithm = *algorithm;
    params.sigmaTime = m_mrPage->sigmaTime();
    params.lang = m_lang;

    auto* thread = new ProcessingThread(m_mrPage->songPath(), m_mrPage->accompanimentPath(),
                                        m_mrPage->outputPath(), params, this);
    m_thread = thread;
    connect(thread, &ProcessingThread::progressUpdated, this, &MainWindow::updateProgress);
    connect(thread, &ProcessingThread::statusUpdated, this, &MainWindow::updateStatus);
    connect(thread, &ProcessingThread::processingFinished, this, &MainWindow::processingDone);
    connect(thread, &ProcessingThread::processingCancelled, this,
            &MainWindow::processingCancelled);
    connect(thread, &ProcessingThread::errorOccurred, this, &MainWindow::processingError);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_thread == thread) {
            m_thread = nullptr;
            m_mrPage->setProcessing(false);
        }
        thread->deleteLater();
        if (m_closePending) {
            m_closePending = false;
            close();
        }
    });
    m_stateToolTip = new qfw::StateToolTip(t(QStringLiteral("processing")),
                                           t(QStringLiteral("loading_song")), this);
    connect(m_stateToolTip, &qfw::StateToolTip::closedSignal, m_stateToolTip, &QObject::deleteLater);
    m_stateToolTip->move(m_stateToolTip->getSuitablePos());
    m_stateToolTip->show();
    m_mrPage->setProgress(0);
    m_mrPage->setProcessing(true);
    thread->start();
}

void MainWindow::startAiExtraction() {
    if (m_thread || m_neuralThread)
        return;

    const auto warn = [this](const QString& key) {
        qfw::InfoBar::warning(t(QStringLiteral("warn_title")), t(key), Qt::Horizontal, true, 3000,
                              qfw::InfoBarPosition::TopRight, this);
    };
    if (m_aiPage->songPath().isEmpty()) {
        warn(QStringLiteral("ai_need_song"));
        return;
    }
    const QString modelId = m_aiPage->modelId();
    const neural::ModelEntry* entry = neural::modelById(modelId.toStdString());
    if (entry == nullptr) {
        warn(QStringLiteral("ai_invalid_model"));
        return;
    }
    const QFileInfo songInfo(m_aiPage->songPath());
    const QString base = songInfo.absolutePath() + QLatin1Char('/') + songInfo.completeBaseName();
    const QString vocalOut = base + QStringLiteral("_vocal.wav");
    const QString backgroundOut = base + QStringLiteral("_background.wav");
    if (filepaths::equal(vocalOut, m_aiPage->songPath()) ||
        filepaths::equal(backgroundOut, m_aiPage->songPath())) {
        warn(QStringLiteral("warn_output_conflict"));
        return;
    }

    auto* thread = new NeuralProcessingThread(m_aiPage->songPath(), modelId, vocalOut,
                                              backgroundOut, m_lang, {}, this);
    m_neuralThread = thread;
    m_aiRunning = true;
    connect(thread, &NeuralProcessingThread::progressUpdated, this, &MainWindow::updateProgress);
    connect(thread, &NeuralProcessingThread::statusUpdated, this, &MainWindow::updateStatus);
    connect(thread, &NeuralProcessingThread::processingFinished, this,
            [this, thread](const QString&) { aiProcessingDone(thread); });
    connect(thread, &NeuralProcessingThread::processingCancelled, this,
            &MainWindow::processingCancelled);
    connect(thread, &NeuralProcessingThread::errorOccurred, this, &MainWindow::processingError);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_neuralThread == thread) {
            m_neuralThread = nullptr;
            m_aiRunning = false;
            m_aiPage->setProcessing(false);
        }
        thread->deleteLater();
        if (m_closePending) {
            m_closePending = false;
            close();
        }
    });
    m_stateToolTip = new qfw::StateToolTip(t(QStringLiteral("ai_inferring")),
                                           t(QStringLiteral("ai_loading_model")), this);
    connect(m_stateToolTip, &qfw::StateToolTip::closedSignal, m_stateToolTip, &QObject::deleteLater);
    m_stateToolTip->move(m_stateToolTip->getSuitablePos());
    m_stateToolTip->show();
    m_aiPage->setProgress(0);
    m_aiPage->setProcessing(true);
    thread->start();
}

void MainWindow::aiProcessingDone(NeuralProcessingThread* thread) {
    m_aiPage->setProgress(100);
    QString message = i18n::t(m_lang, QStringLiteral("ai_done_msg"));
    message.replace(QStringLiteral("{vocal}"), thread->vocalOutPath())
        .replace(QStringLiteral("{background}"), thread->backgroundOutPath());
    m_aiPage->setStatus(i18n::t(m_lang, QStringLiteral("ai_done"))
                            .replace(QStringLiteral("{vocal}"), thread->vocalOutPath())
                            .replace(QStringLiteral("{background}"), thread->backgroundOutPath()));
    if (m_stateToolTip) {
        m_stateToolTip->setState(true);
        m_stateToolTip = nullptr;
    }
    qfw::InfoBar::success(t(QStringLiteral("done_title")), message, Qt::Horizontal, true, 4000,
                          qfw::InfoBarPosition::TopRight, this);
}

void MainWindow::cancelProcessing() {
    if (m_thread && m_thread->isRunning())
        m_thread->cancel();
    if (m_neuralThread && m_neuralThread->isRunning())
        m_neuralThread->cancel();
    if (m_aiRunning)
        m_aiPage->setStatus(t(QStringLiteral("cancelled")));
    else
        m_mrPage->setStatus(t(QStringLiteral("cancelled")));
    if (m_stateToolTip) {
        m_stateToolTip->deleteLater();
        m_stateToolTip = nullptr;
    }
}

void MainWindow::updateProgress(int value) {
    if (m_aiRunning)
        m_aiPage->setProgress(value);
    else
        m_mrPage->setProgress(value);
}

void MainWindow::updateStatus(const QString& message) {
    if (m_aiRunning)
        m_aiPage->setStatus(message);
    else
        m_mrPage->setStatus(message);
    if (m_stateToolTip)
        m_stateToolTip->setContent(message);
}

void MainWindow::processingDone(const QString& outputPath) {
    m_mrPage->setProgress(100);
    m_mrPage->setStatus(t(QStringLiteral("done_status"), outputPath));
    if (m_stateToolTip) {
        m_stateToolTip->setState(true);
        m_stateToolTip = nullptr;
    }
    qfw::InfoBar::success(t(QStringLiteral("done_title")), t(QStringLiteral("done_msg"), outputPath),
                          Qt::Horizontal, true, 4000, qfw::InfoBarPosition::TopRight, this);
}

void MainWindow::processingCancelled() {
    m_mrPage->setStatus(t(QStringLiteral("cancelled")));
    if (m_stateToolTip) {
        m_stateToolTip->deleteLater();
        m_stateToolTip = nullptr;
    }
}

void MainWindow::processingError(const QString& errorMessage) {
    m_mrPage->setStatus(t(QStringLiteral("err_status"), QString(), errorMessage));
    if (m_stateToolTip) {
        m_stateToolTip->deleteLater();
        m_stateToolTip = nullptr;
    }
    qfw::InfoBar::error(t(QStringLiteral("err_title")), errorMessage, Qt::Horizontal, true, 5000,
                        qfw::InfoBarPosition::TopRight, this);
}

// ---- 伴奏自动查找 ----

void MainWindow::updateAiModelStatus(const QString& modelId) {
    const neural::ModelEntry* entry = neural::modelById(modelId.toStdString());
    if (entry == nullptr)
        entry = neural::defaultModel();
    const QString modelPath = neuralpaths::resolveModelPath(QString::fromStdString(entry->fileName));
    m_aiPage->setModelStatus(modelPath.isEmpty()
                                 ? i18n::t(m_lang, QStringLiteral("ai_model_need_download"))
                                 : i18n::t(m_lang, QStringLiteral("ai_model_ready")));
}

void MainWindow::autoFindAccompaniment() {
    if (m_mrPage->songPath().isEmpty())
        return;
    const accompaniment::Match match =
        accompaniment::findBestMatch(m_mrPage->songPath());
    if (match.found()) {
        m_mrPage->setAccompanimentPath(match.path);
        m_mrPage->setStatus(t(QStringLiteral("auto_found")));
        return;
    }
    m_mrPage->setStatus(t(QStringLiteral("auto_not_found")));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if ((m_thread && m_thread->isRunning()) || (m_neuralThread && m_neuralThread->isRunning())) {
        m_closePending = true;
        if (m_thread)
            m_thread->cancel();
        if (m_neuralThread)
            m_neuralThread->cancel();
        e->ignore();
        return;
    }
    FluentWindow::closeEvent(e);
}
