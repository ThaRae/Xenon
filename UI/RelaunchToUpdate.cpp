/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TypeCasts.h>
#include <UI/Application.h>
#include <UI/BrowserWindow.h>
#include <UI/RelaunchToUpdate.h>
#include <UI/Updater.h>

#include <QApplication>
#include <QMessageBox>
#include <QPushButton>

namespace Ladybird::RelaunchToUpdate {

size_t private_window_count()
{
    size_t count = 0;
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* window = as_if<BrowserWindow>(widget); window && window->isVisible() && window->is_private() == WebView::IsPrivate::Yes)
            ++count;
    }
    return count;
}

void relaunch(QWidget* parent)
{
    auto private_windows = private_window_count();
    if (private_windows == 0) {
        Updater::install_and_relaunch();
        return;
    }

    // NB: Settings asks from inside a page's message, so the question doesn't run an event loop of its own.
    auto* dialog = new QMessageBox(parent ? parent : Application::the().active_window_if_any());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Xenon");
    dialog->setIcon(QMessageBox::Warning);
    dialog->setText("Relaunch to update Xenon?");
    dialog->setInformativeText(private_windows == 1
            ? "Your windows and tabs reopen, but your private window doesn't."
            : "Your windows and tabs reopen, but private windows don't.");
    auto* relaunch_button = dialog->addButton("Relaunch", QMessageBox::AcceptRole);
    dialog->addButton(QMessageBox::Cancel);
    dialog->setDefaultButton(relaunch_button);
    QObject::connect(dialog, &QDialog::finished, dialog, [dialog, relaunch_button] {
        if (dialog->clickedButton() == relaunch_button)
            Updater::install_and_relaunch();
    });
    dialog->open();
}

}
