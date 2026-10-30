/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/ChromeStyle.h>
#include <UI/StringUtils.h>
#include <UI/WebAuthnPopover.h>
#include <UI/WebContentView.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace Ladybird {

using State = WebView::WebAuthnPrompt::State;

static constexpr int POPOVER_WIDTH = 360;
static constexpr int POPOVER_TOP_MARGIN = 12;
static constexpr auto POPOVER_OBJECT_NAME = "XenonWebAuthnPopover";

void WebAuthnPopover::attach(WebContentView& view)
{
    auto find_popover = [&view] {
        return static_cast<WebAuthnPopover*>(view.findChild<QWidget*>(POPOVER_OBJECT_NAME, Qt::FindDirectChildrenOnly));
    };

    view.on_web_authn_prompt = [&view, find_popover](WebView::WebAuthnPrompt const& prompt) {
        auto* popover = find_popover();
        if (!popover)
            popover = new WebAuthnPopover(view);
        popover->update(prompt);
        if (!popover->isVisible())
            popover->show_over_view();
    };

    view.on_web_authn_prompt_closed = [find_popover] {
        if (auto* popover = find_popover())
            popover->close_without_cancelling();
    };
}

WebAuthnPopover::WebAuthnPopover(WebContentView& view)
    : Popover(&view)
    , m_view(view)
{
    setObjectName(POPOVER_OBJECT_NAME);
    card().setObjectName("LadybirdMessagePopover");
    card().setFixedWidth(POPOVER_WIDTH);
    setStyleSheet(ChromeStyle::message_popover_style_sheet(view.palette()));

    auto* layout = new QVBoxLayout(&card());
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    m_title = new QLabel(&card());
    m_title->setObjectName("LadybirdMessagePopoverTitle");
    m_title->setWordWrap(true);
    m_title->setTextFormat(Qt::PlainText);
    layout->addWidget(m_title);

    m_caller = new QLabel(&card());
    m_caller->setObjectName("LadybirdMessagePopoverBody");
    m_caller->setWordWrap(true);
    m_caller->setTextFormat(Qt::PlainText);
    layout->addWidget(m_caller);

    m_body = new QLabel(&card());
    m_body->setObjectName("LadybirdMessagePopoverBody");
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::PlainText);
    layout->addWidget(m_body);

    m_pin = new QLineEdit(&card());
    m_pin->setEchoMode(QLineEdit::Password);
    m_pin->setPlaceholderText("PIN");
    QObject::connect(m_pin, &QLineEdit::returnPressed, this, [this] { submit(); });
    layout->addWidget(m_pin);

    m_accounts = new QListWidget(&card());
    m_accounts->setMaximumHeight(160);
    QObject::connect(m_accounts, &QListWidget::itemDoubleClicked, this, [this] { submit(); });
    layout->addWidget(m_accounts);

    auto* button_layout = new QHBoxLayout;
    button_layout->setSpacing(8);
    button_layout->addStretch();
    layout->addLayout(button_layout);

    m_dismiss_button = new QPushButton(&card());
    m_dismiss_button->setObjectName("LadybirdMessagePopoverDismissButton");
    m_dismiss_button->setFocusPolicy(Qt::NoFocus);
    QObject::connect(m_dismiss_button, &QPushButton::clicked, this, [this] {
        if (m_state == State::AttestationConsent) {
            m_view.web_authn_allow_attestation(false);
            return;
        }
        close();
    });
    button_layout->addWidget(m_dismiss_button);

    m_accept_button = new QPushButton(&card());
    m_accept_button->setObjectName("LadybirdMessagePopoverAcceptButton");
    m_accept_button->setDefault(true);
    QObject::connect(m_accept_button, &QPushButton::clicked, this, [this] { submit(); });
    button_layout->addWidget(m_accept_button);
}

static QString caller_description(WebView::WebAuthnPrompt const& prompt)
{
    if (!prompt.top_level_origin.has_value())
        return {};
    return qstring_from_ak_string(MUST(String::formatted("Asked by {}, in a frame on {}", prompt.origin.serialize(), prompt.top_level_origin->serialize())));
}

void WebAuthnPopover::update(WebView::WebAuthnPrompt const& prompt)
{
    m_state = prompt.state;
    auto rp = prompt.rp_id;
    auto is_create = prompt.operation == WebView::WebAuthnPrompt::Operation::Create;

    m_title->setText(qstring_from_ak_string(is_create
            ? MUST(String::formatted("Register a security key with {}", rp))
            : MUST(String::formatted("Sign in to {} with a security key", rp))));

    auto caller = caller_description(prompt);
    m_caller->setText(caller);
    m_caller->setVisible(!caller.isEmpty());

    String body;
    QString accept_text;
    QString dismiss_text = "Cancel";

    switch (prompt.state) {
    case State::InsertKey:
        body = is_create && !prompt.user_name.is_empty()
            ? MUST(String::formatted("Insert your security key and touch it, to register it for {}.", prompt.user_name))
            : "Insert your security key and touch it."_string;
        break;
    case State::EnterPin: {
        StringBuilder builder;
        builder.append(prompt.pin_was_wrong ? "That PIN was wrong. Enter your security key's PIN."sv : "Enter your security key's PIN."sv);
        if (prompt.pin_retries.has_value())
            builder.appendff(" {} {} left.", *prompt.pin_retries, *prompt.pin_retries == 1 ? "attempt" : "attempts");
        body = builder.to_string_without_validation();
        accept_text = "Continue";
        break;
    }
    case State::TouchKey:
        body = "Touch your security key to finish."_string;
        break;
    case State::ChooseAccount:
        body = MUST(String::formatted("Your security key has more than one account for {}. Choose the one to sign in with.", rp));
        accept_text = "Sign in";
        break;
    case State::AttestationConsent:
        body = MUST(String::formatted("{} wants to know your security key's make and model, which could help identify you.", rp));
        accept_text = "Share";
        dismiss_text = "Don't share";
        break;
    case State::KeyNotRegistered:
        body = MUST(String::formatted("This security key isn't registered with {}. Try another one.", rp));
        break;
    case State::KeyAlreadyRegistered:
        body = MUST(String::formatted("This security key is already registered with {}.", rp));
        dismiss_text = "OK";
        break;
    case State::PinBlocked:
        body = "This security key's PIN is blocked after too many wrong attempts. Try another key, or reset this one with its maker's app."_string;
        break;
    case State::PinNotSet:
        body = MUST(String::formatted("{} needs your security key to have a PIN, and it has none. Set one with the key maker's app, then try again.", rp));
        break;
    case State::KeyNotSupported:
        body = MUST(String::formatted("This security key can't do what {} asked for. Try another one.", rp));
        break;
    case State::NoPlatformAuthenticator:
        body = MUST(String::formatted("{} asked for an authenticator built into this device, such as Touch ID, which Xenon can't use yet.", rp));
        break;
    }

    m_body->setText(qstring_from_ak_string(body));

    m_pin->setVisible(prompt.state == State::EnterPin);
    if (prompt.state == State::EnterPin) {
        m_pin->clear();
        m_pin->setFocus();
    }

    m_accounts->setVisible(prompt.state == State::ChooseAccount);
    if (prompt.state == State::ChooseAccount) {
        m_accounts->clear();
        for (auto const& account : prompt.accounts) {
            auto label = account.display_name.is_empty() || account.display_name == account.name
                ? account.name
                : MUST(String::formatted("{} ({})", account.display_name, account.name));
            m_accounts->addItem(qstring_from_ak_string(label.is_empty() ? "Unnamed account"_string : label));
        }
        m_accounts->setCurrentRow(0);
        m_accounts->setFocus();
    }

    m_dismiss_button->setText(dismiss_text);
    m_accept_button->setText(accept_text);
    m_accept_button->setVisible(!accept_text.isEmpty());

    adjustSize();
    if (isVisible())
        show_over_view();
}

void WebAuthnPopover::submit()
{
    switch (m_state) {
    case State::EnterPin: {
        auto pin = m_pin->text();
        m_pin->clear();
        if (pin.isEmpty())
            return;
        m_view.web_authn_provide_pin(ak_string_from_qstring(pin));
        return;
    }
    case State::ChooseAccount:
        if (auto row = m_accounts->currentRow(); row >= 0)
            m_view.web_authn_choose_account(static_cast<size_t>(row));
        return;
    case State::AttestationConsent:
        m_view.web_authn_allow_attestation(true);
        return;
    default:
        return;
    }
}

void WebAuthnPopover::show_over_view()
{
    adjustSize();
    auto view_top_center = m_view.mapToGlobal(QPoint(m_view.width() / 2, 0));
    move(view_top_center.x() - width() / 2, view_top_center.y() + POPOVER_TOP_MARGIN - (height() - card().height()) / 2);
    show();
    raise();
    activateWindow();
    if (m_state == State::EnterPin)
        m_pin->setFocus();
}

void WebAuthnPopover::close_without_cancelling()
{
    m_is_closing_without_cancelling = true;
    close();
}

void WebAuthnPopover::hideEvent(QHideEvent* event)
{
    Popover::hideEvent(event);
    if (!m_is_closing_without_cancelling)
        m_view.web_authn_cancel();
    deleteLater();
}

}
