#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QTranslator>

#include <cstdio>
#include <memory>

#include "config.h"
#include "filepaths.h"
#include "mainwindow.h"
#include "modelcatalog.h"
#include "neuralprocessingthread.h"
#include "processingthread.h"

namespace {

int runCli(QCommandLineParser& parser) {
    const QStringList args = parser.positionalArguments();
    if (args.size() != 3) {
        std::fprintf(stderr, "usage: mr_remover --process <song> <acc> <out> [options]\n");
        return 1;
    }
    ProcessingThread::Params params;
    const QString algorithm = parser.value(QStringLiteral("algorithm"));
    const QByteArray algorithmUtf8 = algorithm.toUtf8();
    const auto parsedAlgorithm = dsp::algorithmFromString(algorithmUtf8.constData());
    if (!parsedAlgorithm.has_value()) {
        std::fprintf(stderr, "error: unknown algorithm: %s\n", algorithm.toUtf8().constData());
        return 2;
    }
    params.algorithm = *parsedAlgorithm;
    bool strengthOk = false;
    const int strength = parser.value(QStringLiteral("strength")).toInt(&strengthOk);
    if (!strengthOk || strength < 0 || strength > 100) {
        std::fprintf(stderr, "error: strength must be an integer in [0, 100]\n");
        return 2;
    }
    params.strength = strength;
    bool sigmaOk = false;
    const int sigma = parser.value(QStringLiteral("sigma")).toInt(&sigmaOk);
    if (!sigmaOk || (sigma != 1 && sigma != 3 && sigma != 8 && sigma != 16)) {
        std::fprintf(stderr, "error: sigma must be one of 1, 3, 8, 16\n");
        return 2;
    }
    params.sigmaTime = sigma;
    const QString align = parser.value(QStringLiteral("align"));
    if (align != QLatin1String("on") && align != QLatin1String("off")) {
        std::fprintf(stderr, "error: align must be 'on' or 'off'\n");
        return 2;
    }
    params.autoAlign = align == QLatin1String("on");
    const QString lang = parser.value(QStringLiteral("lang"));
    if (lang != QLatin1String("zh_cn") && lang != QLatin1String("ja_jp") &&
        lang != QLatin1String("ko_kr")) {
        std::fprintf(stderr, "error: lang must be one of zh_cn, ja_jp, ko_kr\n");
        return 2;
    }
    params.lang = lang;

    ProcessingThread thread(args[0], args[1], args[2], params);
    QEventLoop loop;
    bool ok = false;
    QObject::connect(&thread, &ProcessingThread::progressUpdated, [](int value) {
        std::printf("progress: %d\n", value);
        std::fflush(stdout);
    });
    QObject::connect(&thread, &ProcessingThread::statusUpdated, [](const QString& msg) {
        std::printf("status: %s\n", msg.toUtf8().constData());
        std::fflush(stdout);
    });
    QObject::connect(&thread, &ProcessingThread::processingFinished, &loop, [&]() {
        ok = true;
        loop.quit();
    });
    QObject::connect(&thread, &ProcessingThread::errorOccurred, &loop, [&](const QString& msg) {
        std::fprintf(stderr, "error: %s\n", msg.toUtf8().constData());
        std::fflush(stderr);
        loop.quit();
    });
    thread.start();
    loop.exec();
    thread.wait();
    return ok ? 0 : 1;
}

} // namespace

namespace {

bool validateLang(const QString& lang) {
    return lang == QLatin1String("zh_cn") || lang == QLatin1String("ja_jp") ||
           lang == QLatin1String("ko_kr");
}

int runCliNeural(QCommandLineParser& parser) {
    const QStringList args = parser.positionalArguments();
    if (args.size() != 1) {
        std::fprintf(stderr, "usage: mr_remover --extract-vocal <song> [--out-dir <dir>]\n");
        return 1;
    }
    const QString lang = parser.value(QStringLiteral("lang"));
    if (!validateLang(lang)) {
        std::fprintf(stderr, "error: lang must be one of zh_cn, ja_jp, ko_kr\n");
        return 2;
    }
    QString modelId = parser.value(QStringLiteral("model"));
    if (modelId.isEmpty())
        modelId = QString::fromStdString(neural::defaultModel()->id);
    if (neural::modelById(modelId.toStdString()) == nullptr) {
        std::fprintf(stderr, "error: unknown model id: %s\n", modelId.toUtf8().constData());
        std::fprintf(stderr, "available models:");
        for (const neural::ModelEntry& entry : neural::modelCatalog())
            std::fprintf(stderr, " %s", entry.id.c_str());
        std::fprintf(stderr, "\n");
        return 2;
    }
    const QString modelsDir = parser.value(QStringLiteral("models-dir"));
    const QFileInfo songInfo(args[0]);
    QString outDir = parser.value(QStringLiteral("out-dir"));
    if (outDir.isEmpty())
        outDir = songInfo.absolutePath();
    const QString base = QDir(outDir).filePath(songInfo.completeBaseName());
    const QString vocalOut = base + QStringLiteral("_vocal.wav");
    const QString backgroundOut = base + QStringLiteral("_background.wav");
    if (filepaths::equal(vocalOut, args[0]) || filepaths::equal(backgroundOut, args[0])) {
        std::fprintf(stderr, "error: output files would overwrite the song\n");
        return 2;
    }

    NeuralProcessingThread thread(args[0], modelId, vocalOut, backgroundOut, lang, modelsDir);
    QEventLoop loop;
    bool ok = false;
    QObject::connect(&thread, &NeuralProcessingThread::progressUpdated, [](int value) {
        std::printf("progress: %d\n", value);
        std::fflush(stdout);
    });
    QObject::connect(&thread, &NeuralProcessingThread::statusUpdated, [](const QString& msg) {
        std::printf("status: %s\n", msg.toUtf8().constData());
        std::fflush(stdout);
    });
    QObject::connect(&thread, &NeuralProcessingThread::processingFinished, &loop, [&]() {
        ok = true;
        loop.quit();
    });
    QObject::connect(&thread, &NeuralProcessingThread::errorOccurred, &loop,
                     [&](const QString& msg) {
                         std::fprintf(stderr, "error: %s\n", msg.toUtf8().constData());
                         std::fflush(stderr);
                         loop.quit();
                     });
    thread.start();
    loop.exec();
    thread.wait();
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    bool cliRequested = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--process") || arg == QLatin1String("--extract-vocal")) {
            cliRequested = true;
            break;
        }
    }

    std::unique_ptr<QCoreApplication> app;
    if (cliRequested) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    } else {
        // Qt-Fluent-Widgets 静态库资源 (图标/样式/翻译)
        Q_INIT_RESOURCE(resource);
        app = std::make_unique<QApplication>(argc, argv);
    }
    QCoreApplication::setApplicationName(QStringLiteral("mr_remover"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MR_REMOVER_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Audio Station - vocal and accompaniment separation (C++/Qt6)"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption processOpt(QStringLiteral("process"),
                                  QStringLiteral("Run headless processing: <song> <acc> <out>"));
    parser.addOption(processOpt);
    QCommandLineOption extractVocalOpt(
        QStringLiteral("extract-vocal"),
        QStringLiteral("AI vocal extraction with background removal (UVR MDX-Net): <song>"));
    parser.addOption(extractVocalOpt);
    QCommandLineOption modelOpt(
        QStringLiteral("model"),
        QStringLiteral("Model id for --extract-vocal (default: mdxnet_1)"), QStringLiteral("id"),
        QString());
    parser.addOption(modelOpt);
    QCommandLineOption modelsDirOpt(
        QStringLiteral("models-dir"),
        QStringLiteral("Directory for AI model weights (default: auto-detect)"),
        QStringLiteral("dir"));
    parser.addOption(modelsDirOpt);
    QCommandLineOption outDirOpt(
        QStringLiteral("out-dir"),
        QStringLiteral("Output directory for extracted stems (default: song directory)"),
        QStringLiteral("dir"));
    parser.addOption(outDirOpt);
    QCommandLineOption algorithmOpt(QStringLiteral("algorithm"), QStringLiteral("Algorithm key (default: lossless)"),
                                    QStringLiteral("key"), QStringLiteral("lossless"));
    parser.addOption(algorithmOpt);
    QCommandLineOption strengthOpt(QStringLiteral("strength"), QStringLiteral("Strength 0-100 (default: 75)"),
                                   QStringLiteral("value"), QStringLiteral("75"));
    parser.addOption(strengthOpt);
    QCommandLineOption sigmaOpt(QStringLiteral("sigma"), QStringLiteral("Sigma time 1|3|8|16 (default: 1)"),
                                QStringLiteral("value"), QStringLiteral("1"));
    parser.addOption(sigmaOpt);
    QCommandLineOption alignOpt(QStringLiteral("align"), QStringLiteral("Auto-align on|off (default: on)"),
                                QStringLiteral("mode"), QStringLiteral("on"));
    parser.addOption(alignOpt);
    QCommandLineOption langOpt(QStringLiteral("lang"),
                               QStringLiteral("Language zh_cn|ja_jp|ko_kr (default: zh_cn)"),
                               QStringLiteral("code"), QStringLiteral("zh_cn"));
    parser.addOption(langOpt);
    QCommandLineOption selftestOpt(QStringLiteral("selftest"),
                                   QStringLiteral("Show the window briefly and exit 0"));
    parser.addOption(selftestOpt);
    parser.addPositionalArgument(QStringLiteral("song"), QStringLiteral("Song audio file"));
    parser.addPositionalArgument(QStringLiteral("acc"), QStringLiteral("Accompaniment audio file"));
    parser.addPositionalArgument(QStringLiteral("out"), QStringLiteral("Output wav file"));
    parser.process(*app);

    if (parser.isSet(processOpt))
        return runCli(parser);
    if (parser.isSet(extractVocalOpt))
        return runCliNeural(parser);

    // 中文界面时加载库自带翻译 (内部控件文案)
    const QString lang = QSettings().value(QStringLiteral("lang"), QStringLiteral("zh_cn")).toString();
    if (lang == QLatin1String("zh_cn")) {
        auto* translator = new QTranslator(app.get());
        if (translator->load(QStringLiteral(":/qfluentwidgets/i18n/qtfluentwidgets.zh_CN.qm")))
            app->installTranslator(translator);
        else
            delete translator;
    }

    MainWindow window;
    window.show();
    if (parser.isSet(selftestOpt))
        QTimer::singleShot(500, app.get(), &QCoreApplication::quit);
    return app->exec();
}
