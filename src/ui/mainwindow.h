#pragma once

#include <QPointer>
#include <QString>

#include "processingthread.h"
#include "qtfluentwidgets.h"

class HomePage;
class SettingsPage;

namespace qfw {
class NavigationWidget;
class StateToolTip;
} // namespace qfw

// 主窗口: FluentWindow + 左侧导航 (主页 / 设置)
class MainWindow : public qfw::FluentWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    // 查表 + {path}/{msg} 占位符替换
    QString t(const QString& key, const QString& path = QString(), const QString& msg = QString()) const;
    void initNavigation();
    void refreshAllText();

    void startProcessing();
    void cancelProcessing();
    void updateProgress(int value);
    void updateStatus(const QString& message);
    void processingDone(const QString& outputPath);
    void processingCancelled();
    void processingError(const QString& errorMessage);
    void autoFindAccompaniment();

    QString m_lang = QStringLiteral("zh_cn");
    QString m_theme = QStringLiteral("auto");
    QPointer<ProcessingThread> m_thread;
    QPointer<qfw::StateToolTip> m_stateToolTip;
    bool m_closePending = false;

    HomePage* m_homePage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    qfw::NavigationWidget* m_homeNav = nullptr;
    qfw::NavigationWidget* m_settingsNav = nullptr;
};
