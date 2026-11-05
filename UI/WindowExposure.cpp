/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/WebContentView.h>
#include <UI/WindowExposure.h>

#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <QWindow>

namespace Ladybird::WindowExposure {

static constexpr auto WATCHER_OBJECT_NAME = "XenonWindowExposureWatcher";
static constexpr int UNEXPOSED_TIME_BEFORE_HIDING_PAGES_MS = 1000;

class Watcher final : public QObject {
public:
    explicit Watcher(QWidget& window)
        : QObject(&window)
        , m_window(window)
    {
        setObjectName(WATCHER_OBJECT_NAME);
        m_unexposed_timer.setSingleShot(true);
        m_unexposed_timer.setInterval(UNEXPOSED_TIME_BEFORE_HIDING_PAGES_MS);
        QObject::connect(&m_unexposed_timer, &QTimer::timeout, this, [this] { set_hides_pages(true); });
        window.installEventFilter(this);
        watch_window_handle();
    }

    bool hides_pages() const { return m_hides_pages; }

private:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == &m_window) {
            if (event->type() == QEvent::Show)
                watch_window_handle();
            else if (event->type() == QEvent::WindowStateChange)
                update();
        } else if (watched == m_window_handle && event->type() == QEvent::Expose) {
            QTimer::singleShot(0, this, [this] { update(); });
        }
        return QObject::eventFilter(watched, event);
    }

    void watch_window_handle()
    {
        auto* window_handle = m_window.windowHandle();
        if (!window_handle || window_handle == m_window_handle)
            return;
        if (m_window_handle)
            m_window_handle->removeEventFilter(this);
        m_window_handle = window_handle;
        m_window_handle->installEventFilter(this);
        update();
    }

    void update()
    {
        if (m_window.isMinimized()) {
            m_unexposed_timer.stop();
            set_hides_pages(true);
            return;
        }
        if (!exposure_shows_visibility() || !m_window_handle || m_window_handle->isExposed() || !m_window.isVisible()) {
            m_unexposed_timer.stop();
            set_hides_pages(false);
            return;
        }
        if (!m_hides_pages && !m_unexposed_timer.isActive())
            m_unexposed_timer.start();
    }

    static bool exposure_shows_visibility()
    {
        static bool const is_wayland = QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
        return !is_wayland;
    }

    void set_hides_pages(bool hides_pages)
    {
        if (m_hides_pages == hides_pages)
            return;
        m_hides_pages = hides_pages;
        for (auto* view : m_window.findChildren<WebContentView*>())
            view->window_exposure_changed();
    }

    QWidget& m_window;
    QPointer<QWindow> m_window_handle;
    QTimer m_unexposed_timer;
    bool m_hides_pages { false };
};

void watch(QWidget& window)
{
    if (!window.findChild<QObject*>(WATCHER_OBJECT_NAME, Qt::FindDirectChildrenOnly))
        new Watcher(window);
}

bool hides_pages_in(QWidget const* window)
{
    if (!window)
        return false;
    auto const* watcher = static_cast<Watcher const*>(window->findChild<QObject*>(WATCHER_OBJECT_NAME, Qt::FindDirectChildrenOnly));
    return watcher && watcher->hides_pages();
}

}
