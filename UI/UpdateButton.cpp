/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StdLibExtras.h>
#include <AK/Vector.h>
#include <UI/BrandColors.h>
#include <UI/ChromeStyle.h>
#include <UI/Popover.h>
#include <UI/RelaunchToUpdate.h>
#include <UI/StringUtils.h>
#include <UI/UpdateButton.h>
#include <UI/Updater.h>

#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QVBoxLayout>

namespace Ladybird {

static constexpr int PILL_HEIGHT = 26;
static constexpr int PILL_MARGIN = 2;
static constexpr int PILL_LEADING_PADDING = 9;
static constexpr int PILL_TRAILING_PADDING = 11;
static constexpr int ICON_SIZE = 14;
static constexpr int ICON_GAP = 5;
// A narrower toolbar, like the compact layout's, which shares its row with the tabs, gets the icon alone.
static constexpr int MIN_TOOLBAR_WIDTH_FOR_LABEL = 640;
static constexpr int POPOVER_WIDTH = 320;

// The version that Later put off, until Xenon starts again.
static Optional<ByteString> s_put_off_version;
// The version the buttons last showed for, which Xenon logs once (Meta/release.py test looks for it).
static Optional<ByteString> s_shown_version;

// Every window's buttons, which show and hide together.
static Vector<UpdateButton*>& buttons()
{
    static Vector<UpdateButton*> buttons;
    return buttons;
}

static void update_every_button()
{
    for (auto* button : buttons())
        button->update_from_status();
}

static void follow_updater()
{
    static bool following = false;
    if (exchange(following, true))
        return;
    Updater::on_status_change([] {
        update_every_button();
        return true;
    });
}

static QFont label_font(QFont font)
{
    font.setWeight(QFont::DemiBold);
    return font;
}

// An arrow pointing up, as Chrome's update chip has.
static void paint_update_arrow(QPainter& painter, QRectF const& rect, QColor const& color)
{
    painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    auto center = rect.center().x();
    painter.drawLine(QPointF(center, rect.bottom() - 1.0), QPointF(center, rect.top() + 1.5));
    QPointF head[] = { { center - 4.5, rect.top() + 6.0 }, { center, rect.top() + 1.5 }, { center + 4.5, rect.top() + 6.0 } };
    painter.drawPolyline(head, 3);
}

// Unlike a message popover, it's put off only by Later: clicking outside it, or Escape, only closes it.
class UpdatePopover final : public Popover {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit UpdatePopover(QWidget* parent)
        : Popover(parent)
    {
        setObjectName("XenonUpdatePopover");
        setAccessibleName("Update Xenon");
        setAttribute(Qt::WA_NoMouseReplay);
        card().setObjectName("LadybirdMessagePopover");
        card().setFixedWidth(POPOVER_WIDTH);

        auto* layout = new QVBoxLayout(&card());
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(10);

        m_title = new QLabel(&card());
        m_title->setObjectName("LadybirdMessagePopoverTitle");
        m_title->setWordWrap(true);
        layout->addWidget(m_title);

        m_body = new QLabel(&card());
        m_body->setObjectName("LadybirdMessagePopoverBody");
        m_body->setWordWrap(true);
        layout->addWidget(m_body);

        auto* button_layout = new QHBoxLayout;
        button_layout->setSpacing(8);
        button_layout->addStretch();
        layout->addLayout(button_layout);

        auto* later_button = new QPushButton("Later", &card());
        later_button->setObjectName("LadybirdMessagePopoverDismissButton");
        later_button->setToolTip("Hide the Update button until Xenon starts again");
        later_button->setFocusPolicy(Qt::NoFocus);
        QObject::connect(later_button, &QPushButton::clicked, this, [this] {
            close();
            if (on_later)
                on_later();
        });
        button_layout->addWidget(later_button);

        auto* relaunch_button = new QPushButton("Relaunch to Update", &card());
        relaunch_button->setObjectName("LadybirdMessagePopoverAcceptButton");
        relaunch_button->setFocusPolicy(Qt::NoFocus);
        QObject::connect(relaunch_button, &QPushButton::clicked, this, [this] {
            close();
            if (on_relaunch)
                on_relaunch();
        });
        button_layout->addWidget(relaunch_button);
    }

    void set_update(ByteString const& version, size_t private_windows)
    {
        m_title->setText(qformatted("Xenon {} is ready", version));

        // Every window reopens after the relaunch but the private ones (Updater::relaunched_to_update()).
        auto reopen = private_windows == 0 ? "your windows and tabs reopen"sv
            : private_windows == 1         ? "your windows and tabs reopen, but your private window doesn't"sv
                                           : "your windows and tabs reopen, but private windows don't"sv;
        m_body->setText(qformatted("Relaunch to finish updating, and {}. Otherwise, it installs when you quit Xenon.", reopen));
    }

    void update_chrome_style(QPalette const& palette)
    {
        setPalette(palette);
        setStyleSheet(ChromeStyle::message_popover_style_sheet(palette));
    }

    Function<void()> on_relaunch;
    Function<void()> on_later;

private:
    virtual void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        Popover::keyPressEvent(event);
    }

    QLabel* m_title { nullptr };
    QLabel* m_body { nullptr };
};

UpdateButton::UpdateButton(QWidget& toolbar, int height)
    : QToolButton(&toolbar)
    , m_height(height)
{
    setObjectName("XenonUpdateButton");
    setText("Update");
    setAccessibleName("Update Xenon");
    setAutoRaise(true);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover);
    QObject::connect(this, &QToolButton::clicked, this, [this] { show_popover(); });

    // The label comes and goes with the toolbar's width.
    toolbar.installEventFilter(this);

    buttons().append(this);
    follow_updater();
    update_size();
    update_from_status();
}

UpdateButton::~UpdateButton()
{
    buttons().remove_first_matching([this](auto* button) { return button == this; });
}

void UpdateButton::update_from_status()
{
    auto version = Updater::status().ready_version;
    if (version == s_put_off_version)
        version.clear();

    m_version = AK::move(version);
    if (!m_version.has_value()) {
        if (m_popover)
            m_popover->close();
        hide();
        return;
    }

    if (s_shown_version != m_version) {
        s_shown_version = m_version;
        dbgln("The toolbar offers to relaunch to update to Xenon {}", *m_version);
    }
    setToolTip(qformatted("Xenon {} is ready to install", *m_version));
    show();
}

void UpdateButton::show_popover()
{
    if (!m_version.has_value())
        return;

    if (!m_popover) {
        m_popover = new UpdatePopover(this);
        m_popover->on_relaunch = [] { Updater::install_and_relaunch(); };
        m_popover->on_later = [this] {
            if (!m_version.has_value())
                return;
            s_put_off_version = m_version;
            update_every_button();
        };
    }

    m_popover->set_update(*m_version, RelaunchToUpdate::private_window_count());
    m_popover->update_chrome_style(palette());
    m_popover->adjustSize();
    move_popover_below(*m_popover, *this);
    m_popover->show();
    m_popover->adjustSize();
    move_popover_below(*m_popover, *this);
}

void UpdateButton::update_size()
{
    auto* toolbar = parentWidget();
    m_shows_label = !toolbar || toolbar->width() >= MIN_TOOLBAR_WIDTH_FOR_LABEL;

    auto width = m_height;
    if (m_shows_label) {
        auto label_width = QFontMetrics(label_font(font())).horizontalAdvance(text());
        width = PILL_MARGIN + PILL_LEADING_PADDING + ICON_SIZE + ICON_GAP + label_width + PILL_TRAILING_PADDING + PILL_MARGIN;
    }
    setFixedSize(width, m_height);
    update();
}

QRectF UpdateButton::pill_rect() const
{
    auto pill_width = m_shows_label ? width() - 2 * PILL_MARGIN : PILL_HEIGHT;
    return { (width() - pill_width) / 2.0, (height() - PILL_HEIGHT) / 2.0, static_cast<qreal>(pill_width), static_cast<qreal>(PILL_HEIGHT) };
}

bool UpdateButton::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        update_size();
    return QToolButton::eventFilter(watched, event);
}

void UpdateButton::changeEvent(QEvent* event)
{
    QToolButton::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        update_size();
}

void UpdateButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    auto pill = pill_rect();
    auto background = isDown() ? BrandColors::Rose500 : underMouse() ? BrandColors::Rose300
                                                                     : BrandColors::Rose100;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(background));
    painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);

    // Only the keyboard focuses the button, so a ring around it shows where the keyboard is.
    if (hasFocus()) {
        painter.setPen(QPen(QColor(BrandColors::Rose300), 1.5));
        painter.setBrush(Qt::NoBrush);
        auto ring = pill.adjusted(-2.25, -2.25, 2.25, 2.25);
        painter.drawRoundedRect(ring, ring.height() / 2, ring.height() / 2);
    }

    auto icon_left = m_shows_label ? pill.left() + PILL_LEADING_PADDING : pill.center().x() - ICON_SIZE / 2.0;
    QRectF icon { icon_left, pill.center().y() - ICON_SIZE / 2.0, ICON_SIZE, ICON_SIZE };
    paint_update_arrow(painter, icon, Qt::white);

    if (!m_shows_label)
        return;
    painter.setPen(Qt::white);
    painter.setFont(label_font(font()));
    QRectF label { icon.right() + ICON_GAP, pill.top(), pill.right() - PILL_TRAILING_PADDING - icon.right() - ICON_GAP, pill.height() };
    painter.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, text());
}

}
