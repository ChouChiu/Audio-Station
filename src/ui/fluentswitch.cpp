#include "fluentswitch.h"

#include <QLabel>

namespace ui {

ThemeSwitchButton::ThemeSwitchButton(QWidget* parent) : qfw::SwitchButton(parent) {
    connect(&qfw::QConfig::instance(), &qfw::QConfig::themeChanged, this,
            [this](qfw::Theme) { applyThemeColor(); });
    applyThemeColor();
}

void ThemeSwitchButton::applyThemeColor() {
    auto* label = findChild<QLabel*>();
    if (!label)
        return;
    const QColor textColor =
        qfw::isDarkTheme() ? QColor(255, 255, 255) : QColor(0, 0, 0);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, textColor);
    label->setPalette(pal);
}

} // namespace ui
