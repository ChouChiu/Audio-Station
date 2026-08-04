#include "accompanimentfinder.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <tuple>
#include <unordered_map>
#include <vector>

namespace accompaniment {

namespace {

std::u16string toUtf16(const QString& value) {
    return {reinterpret_cast<const char16_t*>(value.utf16()),
            static_cast<size_t>(value.size())};
}

double sequenceRatio(const std::u16string& first, const std::u16string& second) {
    if (first.empty() && second.empty())
        return 1.0;

    std::unordered_map<char16_t, std::vector<int>> secondPositions;
    for (size_t index = 0; index < second.size(); ++index)
        secondPositions[second[index]].push_back(static_cast<int>(index));

    std::vector<std::tuple<int, int, int, int>> pending;
    pending.emplace_back(0, static_cast<int>(first.size()), 0,
                         static_cast<int>(second.size()));
    int matched = 0;
    while (!pending.empty()) {
        const auto [firstLow, firstHigh, secondLow, secondHigh] = pending.back();
        pending.pop_back();
        int bestFirst = firstLow;
        int bestSecond = secondLow;
        int bestSize = 0;
        std::unordered_map<int, int> previousLengths;
        for (int firstIndex = firstLow; firstIndex < firstHigh; ++firstIndex) {
            std::unordered_map<int, int> lengths;
            const auto positions = secondPositions.find(first[static_cast<size_t>(firstIndex)]);
            if (positions != secondPositions.end()) {
                for (int secondIndex : positions->second) {
                    if (secondIndex < secondLow)
                        continue;
                    if (secondIndex >= secondHigh)
                        break;
                    const auto previous = previousLengths.find(secondIndex - 1);
                    const int length =
                        (previous == previousLengths.end() ? 0 : previous->second) + 1;
                    lengths[secondIndex] = length;
                    if (length > bestSize) {
                        bestFirst = firstIndex - length + 1;
                        bestSecond = secondIndex - length + 1;
                        bestSize = length;
                    }
                }
            }
            previousLengths = std::move(lengths);
        }

        while (bestFirst > firstLow && bestSecond > secondLow &&
               first[static_cast<size_t>(bestFirst - 1)] ==
                   second[static_cast<size_t>(bestSecond - 1)]) {
            --bestFirst;
            --bestSecond;
            ++bestSize;
        }
        while (bestFirst + bestSize < firstHigh && bestSecond + bestSize < secondHigh &&
               first[static_cast<size_t>(bestFirst) + static_cast<size_t>(bestSize)] ==
                   second[static_cast<size_t>(bestSecond) + static_cast<size_t>(bestSize)]) {
            ++bestSize;
        }
        if (bestSize <= 0)
            continue;

        matched += bestSize;
        if (firstLow < bestFirst && secondLow < bestSecond)
            pending.emplace_back(firstLow, bestFirst, secondLow, bestSecond);
        if (bestFirst + bestSize < firstHigh && bestSecond + bestSize < secondHigh) {
            pending.emplace_back(bestFirst + bestSize, firstHigh, bestSecond + bestSize,
                                 secondHigh);
        }
    }
    return 2.0 * matched / static_cast<double>(first.size() + second.size());
}

bool containsAccompanimentKeyword(const QString& filename) {
    static const QStringList keywords = {
        QStringLiteral("伴奏"),          QStringLiteral("accompaniment"),
        QStringLiteral("instrumental"), QStringLiteral("inst"),
        QStringLiteral("karaoke"),      QStringLiteral("off vocal"),
        QStringLiteral("minus one"),
    };
    for (const QString& keyword : keywords) {
        if (filename.contains(keyword))
            return true;
    }
    return false;
}

} // namespace

double filenameSimilarity(const QString& first, const QString& second) {
    return sequenceRatio(toUtf16(first.toLower()), toUtf16(second.toLower()));
}

Match findBestMatch(const QString& songPath, double minimumScore) {
    const QFileInfo songInfo(songPath);
    if (!songInfo.exists() || !songInfo.isFile())
        return {};

    const QString songBase = songInfo.completeBaseName().toLower();
    const QDir directory(songInfo.absolutePath());
    const QStringList extensions = {
        QStringLiteral("*.mp3"), QStringLiteral("*.wav"), QStringLiteral("*.flac"),
        QStringLiteral("*.m4a"),
    };
    Match best;
    const QStringList entries =
        directory.entryList(extensions, QDir::Files | QDir::Readable, QDir::Name);
    for (const QString& entry : entries) {
        const QString candidatePath = directory.absoluteFilePath(entry);
        if (QFileInfo(candidatePath) == songInfo)
            continue;
        const QString candidateBase = QFileInfo(entry).completeBaseName().toLower();
        double score = filenameSimilarity(songBase, candidateBase);
        if (containsAccompanimentKeyword(candidateBase))
            score += 0.2;
        if (score > best.score)
            best = {.path = candidatePath, .score = score};
    }
    if (best.score <= minimumScore)
        return {};
    return best;
}

} // namespace accompaniment
