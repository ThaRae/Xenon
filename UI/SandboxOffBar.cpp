/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/ChromeStyle.h>
#include <UI/SandboxOffBar.h>

#include <QEvent>
#include <QPainter>

namespace Ladybird {

static constexpr int BAR_HEIGHT = 32;
static constexpr int DIVIDER_HEIGHT = 1;
static constexpr int HORIZONTAL_INSET = 12;

static constexpr auto MESSAGE = "The sandbox is off. Websites are less isolated from your computer.";

SandboxOffBar::SandboxOffBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("LadybirdSandboxOffBar");
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFixedHeight(BAR_HEIGHT + DIVIDER_HEIGHT);
    setAccessibleName(MESSAGE);
}

bool SandboxOffBar::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::FontChange:
    case QEvent::LayoutDirectionChange:
        update();
        break;
    default:
        break;
    }
    return QWidget::event(event);
}

void SandboxOffBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(QRect { 0, 0, width(), BAR_HEIGHT }, ChromeStyle::chrome_background(palette()));
    painter.fillRect(QRect { 0, BAR_HEIGHT, width(), DIVIDER_HEIGHT }, ChromeStyle::chrome_separator(palette()));

    QRect text_rect { HORIZONTAL_INSET, 0, width() - 2 * HORIZONTAL_INSET, BAR_HEIGHT };
    painter.setPen(ChromeStyle::chrome_text(palette()));
    auto text = fontMetrics().elidedText(MESSAGE, Qt::ElideRight, text_rect.width());
    painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeading, text);
}

}
