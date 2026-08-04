#pragma once

#include <QColor>
#include <QString>

#include "components/widgets/label.h"

namespace ui {

// 上游 C++ 移植版 FluentLabelBase 默认 setTextColor() 的 light/dark 均为黑色,
// 导致深色主题下文字不可见。创建后显式设为 (黑, 白);
// 库的 themeChanged 监听会持续重应用这对颜色, 一次设置即永久生效。
inline qfw::BodyLabel* bodyLabel(QWidget* parent = nullptr) {
    auto* label = new qfw::BodyLabel(parent);
    label->setTextColor(QColor(0, 0, 0), QColor(255, 255, 255));
    return label;
}

inline qfw::CaptionLabel* captionLabel(QWidget* parent = nullptr) {
    auto* label = new qfw::CaptionLabel(parent);
    label->setTextColor(QColor(0, 0, 0), QColor(255, 255, 255));
    return label;
}

inline qfw::StrongBodyLabel* strongBodyLabel(QWidget* parent = nullptr) {
    auto* label = new qfw::StrongBodyLabel(parent);
    label->setTextColor(QColor(0, 0, 0), QColor(255, 255, 255));
    return label;
}

} // namespace ui
