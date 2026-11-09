/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Platform.h>
#include <UI/ChromeStyle.h>
#include <UI/PageUnresponsivePrompt.h>
#include <UI/Popover.h>
#include <UI/StringUtils.h>
#include <UI/WebContentView.h>

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace Ladybird {

static constexpr int PROMPT_WIDTH = 360;
static constexpr int PROMPT_TOP_MARGIN = 12;
static constexpr int PROMPT_SHADOW_MARGIN = 24;
static constexpr auto PROMPT_OBJECT_NAME = "XenonPageUnresponsivePrompt";

static QString message_for(QString const& page_title, QString const& host)
{
    auto name = page_title.trimmed();
    if (name.isEmpty())
        name = host;
    if (name.isEmpty())
        return "This page isn't responding. You can wait for it to respond, or exit the page.";
    return QString("%1 isn't responding. You can wait for it to respond, or exit the page.").arg(name);
}

void PageUnresponsivePrompt::attach(WebContentView& view)
{
    view.hub_view().on_page_unresponsive_changed = [&view](bool unresponsive) {
        auto* prompt = static_cast<PageUnresponsivePrompt*>(view.findChild<QWidget*>(PROMPT_OBJECT_NAME, Qt::FindDirectChildrenOnly));
        dbgln("Page unresponsive prompt {} for {}", unresponsive ? "shown" : "hidden", view.url());
        if (!unresponsive) {
            if (prompt)
                prompt->hide();
            return;
        }
        if (!prompt)
            prompt = new PageUnresponsivePrompt(view);
        prompt->show_for_current_page();
    };
}

PageUnresponsivePrompt::PageUnresponsivePrompt(WebContentView& view)
    : QWidget(&view)
    , m_view(view)
{
    setObjectName(PROMPT_OBJECT_NAME);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::ArrowCursor);
#if defined(AK_OS_MACOS) || defined(USE_DIRECTX) || defined(USE_VULKAN_DMABUF_IMAGES)
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_NativeWindow);
#endif

    auto* outer_layout = new QVBoxLayout(this);
    outer_layout->setContentsMargins(PROMPT_SHADOW_MARGIN, PROMPT_SHADOW_MARGIN, PROMPT_SHADOW_MARGIN, PROMPT_SHADOW_MARGIN);

    m_card = new QFrame(this);
    m_card->setObjectName("LadybirdMessagePopover");
    m_card->setFixedWidth(PROMPT_WIDTH);
    m_card->setAttribute(Qt::WA_StyledBackground);
    cast_popover_shadow(*m_card);
    outer_layout->addWidget(m_card);

    auto* layout = new QVBoxLayout(m_card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* title = new QLabel("Page unresponsive", m_card);
    title->setObjectName("LadybirdMessagePopoverTitle");
    layout->addWidget(title);

    m_message = new QLabel(m_card);
    m_message->setObjectName("LadybirdMessagePopoverBody");
    m_message->setWordWrap(true);
    m_message->setTextFormat(Qt::PlainText);
    layout->addWidget(m_message);

    auto* button_layout = new QHBoxLayout;
    button_layout->setSpacing(8);
    button_layout->addStretch();
    layout->addLayout(button_layout);

    auto* exit_button = new QPushButton("Exit page", m_card);
    exit_button->setObjectName("LadybirdMessagePopoverDismissButton");
    QObject::connect(exit_button, &QPushButton::clicked, this, [this] {
        hide();
        m_view.hub_view().terminate_unresponsive_page();
    });
    button_layout->addWidget(exit_button);

    auto* wait_button = new QPushButton("Wait", m_card);
    wait_button->setObjectName("LadybirdMessagePopoverAcceptButton");
    wait_button->setDefault(true);
    QObject::connect(wait_button, &QPushButton::clicked, this, [this] {
        hide();
        m_view.hub_view().keep_waiting_for_unresponsive_page();
    });
    button_layout->addWidget(wait_button);

    view.installEventFilter(this);
    update_chrome_style();
    hide();
}

void PageUnresponsivePrompt::show_for_current_page()
{
    m_message->setText(message_for(qstring_from_utf16_string(m_view.title()), qstring_from_ak_string(m_view.url().serialized_host())));
    place_over_view();
    show();
    raise();
}

void PageUnresponsivePrompt::place_over_view()
{
    adjustSize();
    move((m_view.width() - width()) / 2, PROMPT_TOP_MARGIN - PROMPT_SHADOW_MARGIN);
}

void PageUnresponsivePrompt::update_chrome_style()
{
    if (m_is_updating_chrome_style)
        return;
    m_is_updating_chrome_style = true;
    setStyleSheet(ChromeStyle::message_popover_style_sheet(palette()));
    m_is_updating_chrome_style = false;
}

bool PageUnresponsivePrompt::event(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange)
        update_chrome_style();
    return QWidget::event(event);
}

bool PageUnresponsivePrompt::eventFilter(QObject* object, QEvent* event)
{
    if (object == &m_view && event->type() == QEvent::Resize)
        place_over_view();
    return QWidget::eventFilter(object, event);
}

}
