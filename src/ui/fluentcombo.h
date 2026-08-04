#pragma once

#include <QPoint>

#include "components/widgets/combo_box.h"

namespace ui {

// 禁用 ComboBox 弹出菜单的 DropDown/PullUp 位移动画 (原生即时弹出)。
// 上游 qfw::ComboBoxBase::showComboMenu 硬编码 ani=true, 250ms 滑动+渐隐
// 在 Linux/X11 渲染下表现不稳定; 通过覆写 execAt 强制走 DummyMenuAnimationManager。
class NoAniComboBoxMenu : public qfw::ComboBoxMenu {
    Q_OBJECT
public:
    explicit NoAniComboBoxMenu(QWidget* parent = nullptr);

    void execAt(const QPoint& pos, bool ani = true,
                qfw::MenuAnimationType aniType = qfw::MenuAnimationType::DropDown) override;
};

class NoAniComboBox : public qfw::ComboBox {
    Q_OBJECT
public:
    explicit NoAniComboBox(QWidget* parent = nullptr);

protected:
    qfw::ComboBoxMenu* createComboMenu() override;
    // 修复上游: 正常状态文本色用 palette.buttonText(), 主题切换不更新 palette,
    // 深色主题下黑字不可见; 改为按 isDarkTheme() 选色
    void paintEvent(QPaintEvent* e) override;
};

} // namespace ui
