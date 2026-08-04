#include "fluentcombo.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionButton>
#include <QVariantMap>

#include "common/config.h"
#include "common/icon.h"

namespace ui {

NoAniComboBoxMenu::NoAniComboBoxMenu(QWidget* parent) : qfw::ComboBoxMenu(parent) {}

void NoAniComboBoxMenu::execAt(const QPoint& pos, bool,
                               qfw::MenuAnimationType aniType) {
    qfw::ComboBoxMenu::execAt(pos, false, aniType);
}

NoAniComboBox::NoAniComboBox(QWidget* parent) : qfw::ComboBox(parent) {}

qfw::ComboBoxMenu* NoAniComboBox::createComboMenu() {
    return new NoAniComboBoxMenu(this);
}

void NoAniComboBox::paintEvent(QPaintEvent* e) {
    Q_UNUSED(e);

    QStyleOptionButton opt;
    initStyleOption(&opt);

    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform |
                           QPainter::TextAntialiasing);
    painter.setFont(font());

    style()->drawControl(QStyle::CE_PushButtonBevel, &opt, &painter, this);

    // 文本色按主题选择: 上游正常状态用 palette.buttonText(),
    // 而 qfw::setTheme 不更新应用 palette, 深色主题下文本保持黑色不可见
    if (!opt.text.isEmpty()) {
        QColor textColor = qfw::isDarkTheme() ? QColor(255, 255, 255) : QColor(0, 0, 0);
        if (!isEnabled()) {
            painter.setOpacity(0.3628);
            textColor = qfw::isDarkTheme() ? QColor(255, 255, 255, 92) : QColor(0, 0, 0, 92);
        } else if (isPressed) {
            painter.setOpacity(0.786);
            textColor = qfw::isDarkTheme() ? QColor(255, 255, 255, 161) : QColor(0, 0, 0, 161);
        }

        const QRect contentRect = style()->subElementRect(QStyle::SE_PushButtonContents, &opt, this);
        painter.setPen(textColor);
        painter.drawText(contentRect, Qt::AlignVCenter | Qt::AlignLeft, opt.text);
    }

    // 下拉箭头
    if (isHover) {
        painter.setOpacity(0.8);
    } else if (isPressed) {
        painter.setOpacity(0.7);
    } else {
        painter.setOpacity(1.0);
    }

    const QRect rect(width() - 22, height() / 2 - 5, 10, 10);
    const qfw::FluentIcon arrowDown(qfw::FluentIconEnum::ArrowDown);
    if (qfw::isDarkTheme()) {
        arrowDown.render(&painter, rect);
    } else {
        QVariantMap attrs;
        attrs.insert(QStringLiteral("fill"), QStringLiteral("#646464"));
        attrs.insert(QStringLiteral("stroke"), QStringLiteral("#646464"));
        arrowDown.render(&painter, rect, qfw::Theme::Auto, attrs);
    }
}

} // namespace ui
