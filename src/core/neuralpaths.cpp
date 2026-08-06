#include "neuralpaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <vector>

namespace neuralpaths {
namespace {

QString envOverrideDir() {
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("MR_REMOVER_MODELS"));
}

} // namespace

QString resolveModelsDir(const QString& overrideDir) {
    if (!overrideDir.isEmpty())
        return QDir(overrideDir).absolutePath();
    const QString envDir = envOverrideDir();
    if (!envDir.isEmpty())
        return QDir(envDir).absolutePath();

    const QString appDir = QCoreApplication::applicationDirPath();
    const std::vector<QString> candidates = {
        QDir(appDir).filePath(QStringLiteral("models")),
        QDir(appDir + QStringLiteral("/../")).filePath(QStringLiteral("models")),
        QDir(appDir + QStringLiteral("/../../")).filePath(QStringLiteral("models")),
        QDir(appDir + QStringLiteral("/../../../")).filePath(QStringLiteral("models")),
        QDir::current().filePath(QStringLiteral("models")),
    };
    // 优先已存在的目录 (下载目标); 全都不存在时退回第一个候选 (由下载流程创建)
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isDir())
            return candidate;
    }
    return candidates.front();
}

QString resolveModelPath(const QString& fileName, const QString& overrideDir) {
    if (fileName.isEmpty())
        return {};
    const QString candidate = QDir(resolveModelsDir(overrideDir)).filePath(fileName);
    return QFileInfo(candidate).isFile() ? candidate : QString();
}

QString modelDataJsonPath(const QString& modelPath) {
    if (modelPath.isEmpty())
        return {};
    const QString json = QDir(QFileInfo(modelPath).absolutePath())
                             .filePath(QStringLiteral("model_data.json"));
    return QFileInfo(json).isFile() ? json : QString();
}

} // namespace neuralpaths
