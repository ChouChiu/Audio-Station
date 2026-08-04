#pragma once

#include <QString>

namespace filepaths {

[[nodiscard]] QString normalized(const QString& path);
[[nodiscard]] bool equal(const QString& first, const QString& second);

} // namespace filepaths
