#pragma once

#include <QColor>
#include <QPalette>

#include "common/config.h"
#include "components/widgets/switch_button.h"

namespace ui {

// 修复上游 SwitchButton 文字色: 库的 setTextColor 走 QSS 选择器
// (SwitchButton>QLabel{color:...}), 实测该规则不生效, 文字色实际由应用 palette
// 决定 → 深色系统 palette (WindowText=白) 下切浅色主题时白字白底不可见。
// 方案: 直接对 label 设 palette 文本色, 并监听主题变化持续应用。
class ThemeSwitchButton : public qfw::SwitchButton {
    Q_OBJECT
public:
    explicit ThemeSwitchButton(QWidget* parent = nullptr);

private:
    void applyThemeColor();
};

} // namespace ui
