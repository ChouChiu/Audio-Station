#include "mainwindow.h"

#include <QCloseEvent>
#include <QFileInfo>
#include <QSettings>
#include <QVBoxLayout>

#include "accompanimentfinder.h"
#include "filepaths.h"
#include "homepage.h"
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

    m_settingsPage = new SettingsPage(this);
    m_settingsPage->setObjectName(QStringLiteral("settingsPage"));
    m_settingsNav = addSubInterface(m_settingsPage, qfw::FluentIconEnum::Settings,
                                    t(QStringLiteral("nav_settings")));

    m_homePage->setLanguage(m_lang);
    m_settingsPage->setLanguage(m_lang);
    m_settingsPage->setLanguageSelection(m_lang);
    m_settingsPage->setThemeSelection(m_theme);

    connect(m_homePage, &HomePage::startRequested, this, &MainWindow::startProcessing);
    connect(m_homePage, &HomePage::cancelRequested, this, &MainWindow::cancelProcessing);
    connect(m_homePage, &HomePage::songChanged, this, [this](const QString&) {
        const QFileInfo info(m_homePage->songPath());
        m_homePage->setOutputPath(info.absolutePath() + QLatin1Char('/') + info.completeBaseName() +
                                  QStringLiteral("_vocals.wav"));
        if (m_homePage->autoFindEnabled())
            autoFindAccompaniment();
    });
    connect(m_homePage, &HomePage::autoFindToggled, this, [this](bool enabled) {
        if (enabled && !m_homePage->songPath().isEmpty())
            autoFindAccompaniment();
    });
    connect(m_settingsPage, &SettingsPage::languageChanged, this, [this](const QString& lang) {
        m_lang = lang;
        QSettings().setValue(QStringLiteral("lang"), lang);
        m_homePage->setLanguage(lang);
        m_settingsPage->setLanguage(lang);
        refreshAllText();
    });
    connect(m_settingsPage, &SettingsPage::themeChanged, this, [this](const QString& theme) {
        m_theme = theme;
        QSettings().setValue(QStringLiteral("theme"), theme);
        qfw::setTheme(themeFromKey(theme));
    });
}

void MainWindow::refreshAllText() {
    setWindowTitle(t(QStringLiteral("window_title")));
    if (auto* nav = qobject_cast<qfw::NavigationTreeWidget*>(m_homeNav))
        nav->setText(t(QStringLiteral("nav_home")));
    if (auto* nav = qobject_cast<qfw::NavigationTreeWidget*>(m_settingsNav))
        nav->setText(t(QStringLiteral("nav_settings")));
}

// ---- 处理管线 ----

void MainWindow::startProcessing() {
    if (m_thread)
        return;

    const auto warn = [this](const QString& key) {
        qfw::InfoBar::warning(t(QStringLiteral("warn_title")), t(key), Qt::Horizontal, true, 3000,
                              qfw::InfoBarPosition::TopRight, this);
    };
    if (m_homePage->songPath().isEmpty()) {
        warn(QStringLiteral("warn_no_song"));
        return;
    }
    if (m_homePage->accompanimentPath().isEmpty()) {
        warn(QStringLiteral("warn_no_acc"));
        return;
    }
    if (m_homePage->outputPath().isEmpty()) {
        warn(QStringLiteral("warn_no_out"));
        return;
    }
    if (filepaths::equal(m_homePage->outputPath(), m_homePage->songPath()) ||
        filepaths::equal(m_homePage->outputPath(), m_homePage->accompanimentPath())) {
        warn(QStringLiteral("warn_output_conflict"));
        return;
    }

    ProcessingThread::Params params;
    params.strength = m_homePage->strength();
    params.autoAlign = m_homePage->autoAlignEnabled();
    const QByteArray algorithmKey = m_homePage->algorithmKey().toUtf8();
    const auto algorithm = dsp::algorithmFromString(algorithmKey.constData());
    if (!algorithm.has_value()) {
        warn(QStringLiteral("warn_invalid_algorithm"));
        return;
    }
    params.algorithm = *algorithm;
    params.sigmaTime = m_homePage->sigmaTime();
    params.lang = m_lang;

    auto* thread = new ProcessingThread(m_homePage->songPath(), m_homePage->accompanimentPath(),
                                        m_homePage->outputPath(), params, this);
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
            m_homePage->setProcessing(false);
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
    m_homePage->setProgress(0);
    m_homePage->setProcessing(true);
    thread->start();
}

void MainWindow::cancelProcessing() {
    if (!m_thread)
        return;
    if (m_thread->isRunning())
        m_thread->cancel();
    m_homePage->setStatus(t(QStringLiteral("cancelled")));
    if (m_stateToolTip) {
        m_stateToolTip->deleteLater();
        m_stateToolTip = nullptr;
    }
}

void MainWindow::updateProgress(int value) {
    m_homePage->setProgress(value);
}

void MainWindow::updateStatus(const QString& message) {
    m_homePage->setStatus(message);
    if (m_stateToolTip)
        m_stateToolTip->setContent(message);
}

void MainWindow::processingDone(const QString& outputPath) {
    m_homePage->setProgress(100);
    m_homePage->setStatus(t(QStringLiteral("done_status"), outputPath));
    if (m_stateToolTip) {
        m_stateToolTip->setState(true);
        m_stateToolTip = nullptr;
    }
    qfw::InfoBar::success(t(QStringLiteral("done_title")), t(QStringLiteral("done_msg"), outputPath),
                          Qt::Horizontal, true, 4000, qfw::InfoBarPosition::TopRight, this);
}

void MainWindow::processingCancelled() {
    m_homePage->setStatus(t(QStringLiteral("cancelled")));
    if (m_stateToolTip) {
        m_stateToolTip->deleteLater();
        m_stateToolTip = nullptr;
    }
}

void MainWindow::processingError(const QString& errorMessage) {
    m_homePage->setStatus(t(QStringLiteral("err_status"), QString(), errorMessage));
    if (m_stateToolTip) {
        m_stateToolTip->deleteLater();
        m_stateToolTip = nullptr;
    }
    qfw::InfoBar::error(t(QStringLiteral("err_title")), errorMessage, Qt::Horizontal, true, 5000,
                        qfw::InfoBarPosition::TopRight, this);
}

// ---- 伴奏自动查找 ----

void MainWindow::autoFindAccompaniment() {
    if (m_homePage->songPath().isEmpty())
        return;
    const accompaniment::Match match =
        accompaniment::findBestMatch(m_homePage->songPath());
    if (match.found()) {
        m_homePage->setAccompanimentPath(match.path);
        m_homePage->setStatus(t(QStringLiteral("auto_found")));
        return;
    }
    m_homePage->setStatus(t(QStringLiteral("auto_not_found")));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (m_thread && m_thread->isRunning()) {
        m_closePending = true;
        m_thread->cancel();
        e->ignore();
        return;
    }
    FluentWindow::closeEvent(e);
}
