#include "filepaths.h"

#include <QFileInfo>

namespace filepaths {

QString normalized(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool equal(const QString& first, const QString& second) {
#ifdef Q_OS_WIN
    return normalized(first).compare(normalized(second), Qt::CaseInsensitive) == 0;
#else
    return normalized(first) == normalized(second);
#endif
}

} // namespace filepaths
