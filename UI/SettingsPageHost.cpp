/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <UI/Extensions/Dialog.h>
#include <UI/RelaunchToUpdate.h>
#include <UI/SettingsPage.h>
#include <UI/SettingsPageHost.h>
#include <UI/StringUtils.h>

#include <QApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QProcess>
#include <QUrl>

namespace Ladybird {

static void open_local_path(StringView path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(qstring_from_ak_string(path)));
}

void install_settings_page_host()
{
    SettingsPage::set_host({
        .open_directory = [](ByteString const& directory) { open_local_path(directory); },
        .reveal_file = [](ByteString const& file) {
#if defined(AK_OS_MACOS)
            QProcess::startDetached(QStringLiteral("/usr/bin/open"), { QStringLiteral("-R"), qstring_from_ak_string(file) });
#else
            open_local_path(LexicalPath::dirname(file));
#endif
        },
        .show_extensions = [] { Extensions::ExtensionsDialog::show_dialog(QApplication::activeWindow()); },
        .install_update = [] { RelaunchToUpdate::relaunch(nullptr); },
    });

    if (!qApp)
        return;
    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, qApp, [](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            SettingsPage::application_did_become_active();
    });
}

}
