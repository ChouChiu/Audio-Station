#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

#include "accompanimentfinder.h"

namespace {

bool touch(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly);
}

} // namespace

int main() {
    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            passed = false;
        }
    };

    expect(std::abs(accompaniment::filenameSimilarity(QStringLiteral("Song"),
                                                       QStringLiteral("song")) -
                    1.0) < 1e-12,
           "filename similarity is not case-insensitive");

    QTemporaryDir directory;
    expect(directory.isValid(), "temporary directory creation failed");
    const QString song = directory.filePath(QStringLiteral("Artist - Title.wav"));
    expect(touch(song), "song fixture creation failed");
    expect(touch(directory.filePath(QStringLiteral("unrelated.wav"))),
           "unrelated fixture creation failed");
    const QString expected =
        directory.filePath(QStringLiteral("Artist - Title instrumental.flac"));
    expect(touch(expected), "accompaniment fixture creation failed");

    const accompaniment::Match match = accompaniment::findBestMatch(song);
    expect(match.found(), "matching accompaniment was not found");
    expect(match.path == expected, "wrong accompaniment candidate was selected");
    expect(!accompaniment::findBestMatch(directory.filePath(QStringLiteral("missing.wav"))).found(),
           "missing song path produced a match");

    return passed ? 0 : 1;
}
