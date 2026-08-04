#pragma once

#include <QString>

namespace accompaniment {

struct Match {
    QString path;
    double score = 0.0;

    [[nodiscard]] bool found() const noexcept { return !path.isEmpty(); }
};

[[nodiscard]] double filenameSimilarity(const QString& first, const QString& second);
[[nodiscard]] Match findBestMatch(const QString& songPath, double minimumScore = 0.4);

} // namespace accompaniment
