#pragma once

#include <QPointer>
#include <QString>

#include "neuralprocessingthread.h"
#include "processingthread.h"
#include "qtfluentwidgets.h"

class AiPage;
class HomePage;
class MrPage;
class SettingsPage;

namespace qfw {
class NavigationWidget;
class StateToolTip;
} // namespace qfw

// 主窗口: FluentWindow + 左侧导航 (主页 / AI 人声提取 / 设置)
class MainWindow : public qfw::FluentWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;
    bool event(QEvent* e) override;

private:
    // 查表 + {path}/{msg} 占位符替换
    QString t(const QString& key, const QString& path = QString(), const QString& msg = QString()) const;
    void initNavigation();
    void refreshAllText();
    void applySystemAccent(); // 读取系统强调色 (QPalette::Highlight) 应用到 qfw 主题色

    void startProcessing();
    void startAiExtraction();
    void cancelProcessing();
    void updateProgress(int value);
    void updateStatus(const QString& message);
    void processingDone(const QString& outputPath);
    void aiProcessingDone(NeuralProcessingThread* thread);
    void processingCancelled();
    void processingError(const QString& errorMessage);
    void autoFindAccompaniment();
    void updateAiModelStatus(const QString& modelId);

    QString m_lang = QStringLiteral("zh_cn");
    QString m_theme = QStringLiteral("auto");
    QPointer<ProcessingThread> m_thread;
    QPointer<NeuralProcessingThread> m_neuralThread;
    QPointer<qfw::StateToolTip> m_stateToolTip;
    bool m_closePending = false;
    bool m_aiRunning = false; // 进度/状态更新路由到 AiPage 而非 MrPage

    HomePage* m_homePage = nullptr;
    MrPage* m_mrPage = nullptr;
    AiPage* m_aiPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    qfw::NavigationWidget* m_homeNav = nullptr;
    qfw::NavigationWidget* m_mrNav = nullptr;
    qfw::NavigationWidget* m_aiNav = nullptr;
    qfw::NavigationWidget* m_settingsNav = nullptr;
};
