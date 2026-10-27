/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <UI/ChromeStyle.h>
#include <UI/Extensions/Dialog.h>
#include <UI/Extensions/Registry.h>
#include <UI/PasswordManagers.h>
#include <UI/StringUtils.h>

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace Ladybird::Extensions {

static QString style_sheet(QPalette const& palette)
{
    auto text = ChromeStyle::style_sheet_color(ChromeStyle::chrome_text(palette));
    auto muted = ChromeStyle::style_sheet_color(ChromeStyle::chrome_muted_text(palette));
    auto surface = ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface(palette));
    auto recessed = ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface_recessed(palette));
    auto hover = ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface_hover(palette));
    auto pressed = ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface_pressed(palette));
    auto border = ChromeStyle::style_sheet_color(ChromeStyle::chrome_border(palette));
    auto accent = ChromeStyle::style_sheet_color(ChromeStyle::chrome_accent());

    return qformatted(R"(
QDialog#XenonExtensionsDialog {{
    color: {0};
    background: {2};
}}

QDialog#XenonExtensionsDialog QLabel {{
    color: {0};
    background: transparent;
}}

QLabel#XenonExtensionsTitle {{
    font-size: 18px;
    font-weight: 600;
}}

QLabel#XenonExtensionDetails,
QLabel#XenonExtensionsEmpty {{
    color: {1};
}}

QDialog#XenonExtensionsDialog QComboBox {{
    color: {0};
    background: {3};
    border: 1px solid {6};
    border-radius: 8px;
    min-height: 24px;
    padding: 2px 9px;
}}

QLabel#XenonExtensionWarning {{
    color: {7};
}}

QScrollArea, QWidget#XenonExtensionsList {{
    background: transparent;
    border: 0;
}}

QFrame#XenonExtensionRow {{
    background: {3};
    border: 1px solid {6};
    border-radius: 10px;
}}

QDialog#XenonExtensionsDialog QPushButton {{
    color: {0};
    background: {2};
    border: 1px solid {6};
    border-radius: 8px;
    padding: 5px 12px;
}}

QDialog#XenonExtensionsDialog QPushButton:hover {{
    background: {4};
}}

QDialog#XenonExtensionsDialog QPushButton:pressed {{
    background: {5};
}}

QPushButton#XenonExtensionsLoadButton {{
    color: white;
    background: {7};
    border-color: {7};
    font-weight: 600;
}}

QPushButton#XenonExtensionsLoadButton:hover,
QPushButton#XenonExtensionsLoadButton:pressed {{
    background: {7};
}}
)",
        text, muted, surface, recessed, hover, pressed, border, accent);
}

void ExtensionsDialog::show_dialog(QWidget* parent)
{
    static QPointer<ExtensionsDialog> dialog;
    if (!dialog)
        dialog = new ExtensionsDialog(parent);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

ExtensionsDialog::ExtensionsDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName("XenonExtensionsDialog");
    setWindowTitle("Extensions");
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(style_sheet(palette()));
    resize(620, 520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 20);
    layout->setSpacing(14);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel("Extensions", this);
    title->setObjectName("XenonExtensionsTitle");
    header->addWidget(title);
    header->addStretch();

    auto* load_button = new QPushButton("Load Unpacked…", this);
    load_button->setObjectName("XenonExtensionsLoadButton");
    load_button->setToolTip("Load an extension from its directory, as Chrome's developer mode does");
    QObject::connect(load_button, &QPushButton::clicked, this, [this] { load_unpacked(); });
    header->addWidget(load_button);
    layout->addLayout(header);

    auto* password_manager = new QHBoxLayout;
    password_manager->setSpacing(10);
    password_manager->addWidget(new QLabel("Password manager", this));
    m_password_manager = new QComboBox(this);
    m_password_manager->addItem("None", QString {});
    for (auto const& choice : PasswordManagers::the().choices())
        m_password_manager->addItem(qstring_from_ak_string(choice.name), qstring_from_ak_string(choice.id));
    QObject::connect(m_password_manager, &QComboBox::activated, this, [this](int index) { choose_password_manager(index); });
    password_manager->addWidget(m_password_manager);
    m_password_manager_status = new QLabel(this);
    m_password_manager_status->setObjectName("XenonExtensionDetails");
    m_password_manager_status->setWordWrap(true);
    password_manager->addWidget(m_password_manager_status, 1);
    layout->addLayout(password_manager);

    auto* scroll_area = new QScrollArea(this);
    scroll_area->setWidgetResizable(true);
    scroll_area->setFrameShape(QFrame::NoFrame);
    auto* list = new QWidget(scroll_area);
    list->setObjectName("XenonExtensionsList");
    m_list = new QVBoxLayout(list);
    m_list->setContentsMargins(0, 0, 0, 0);
    m_list->setSpacing(10);
    scroll_area->setWidget(list);
    layout->addWidget(scroll_area, 1);

    Registry::the().on_extensions_change([dialog = QPointer(this)] {
        if (!dialog)
            return false;
        dialog->rebuild();
        return true;
    });
    PasswordManagers::the().on_change([dialog = QPointer(this)] {
        if (!dialog)
            return false;
        dialog->update_password_manager();
        return true;
    });
    rebuild();
    update_password_manager();
}

void ExtensionsDialog::update_password_manager()
{
    auto& password_managers = PasswordManagers::the();
    auto current = password_managers.current();
    auto installing = password_managers.installing();

    StringView shown;
    if (installing.has_value())
        shown = installing->id;
    else if (current.has_value())
        shown = current->id;
    m_password_manager->setCurrentIndex(m_password_manager->findData(qstring_from_ak_string(shown)));
    m_password_manager->setEnabled(!installing.has_value());

    if (installing.has_value())
        m_password_manager_status->setText(qformatted("Installing {} from the Chrome Web Store…", installing->name));
    else if (auto const& error = password_managers.error(); error.has_value())
        m_password_manager_status->setText(qstring_from_ak_string(*error));
    else if (current.has_value())
        m_password_manager_status->setText("From the Chrome Web Store, and kept up to date");
    else
        m_password_manager_status->setText({});
}

void ExtensionsDialog::choose_password_manager(int index)
{
    auto id = ak_string_from_qstring(m_password_manager->itemData(index).toString());
    auto current = PasswordManagers::the().current();
    if (current.has_value() && current->id != id) {
        auto answer = QMessageBox::question(this, "Change password manager?", qformatted("Remove {}? Its data in Xenon is deleted too, so it would need signing in again.", current->name));
        if (answer != QMessageBox::Yes) {
            update_password_manager();
            return;
        }
    }
    PasswordManagers::the().choose(id.is_empty() ? Optional<StringView> {} : id.bytes_as_string_view());
}

void ExtensionsDialog::rebuild()
{
    while (auto* item = m_list->takeAt(0)) {
        if (auto* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    auto const& extensions = Registry::the().extensions();
    if (extensions.is_empty()) {
        auto* empty = new QLabel("No extensions yet. Load an unpacked extension from its directory to try one.", this);
        empty->setObjectName("XenonExtensionsEmpty");
        empty->setWordWrap(true);
        m_list->addWidget(empty);
    }

    for (auto const& extension : extensions) {
        auto const& manifest = extension->manifest();

        auto* row = new QFrame(this);
        row->setObjectName("XenonExtensionRow");
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(14, 12, 14, 12);
        row_layout->setSpacing(12);

        auto* icon = new QLabel(row);
        icon->setFixedSize(40, 40);
        if (auto path = best_icon(manifest.icons, 80); path.has_value()) {
            auto file = LexicalPath::join(extension->root(), path->bytes_as_string_view()).string();
            icon->setPixmap(QIcon(qstring_from_ak_string(file)).pixmap(40, 40));
        }
        row_layout->addWidget(icon, 0, Qt::AlignTop);

        auto* text = new QVBoxLayout;
        text->setSpacing(3);

        auto* name = new QLabel(QString("<b>%1</b>&nbsp; %2").arg(qstring_from_ak_string(manifest.name).toHtmlEscaped(), qstring_from_ak_string(manifest.version)), row);
        text->addWidget(name);

        if (manifest.description.has_value()) {
            auto* description = new QLabel(qstring_from_ak_string(*manifest.description), row);
            description->setWordWrap(true);
            text->addWidget(description);
        }

        auto* details = new QLabel(Registry::the().is_installed(*extension)
                ? qformatted("ID: {}\nInstalled from the Chrome Web Store", extension->id())
                : qformatted("ID: {}\n{}{}", extension->id(), extension->root(), Registry::the().is_persisted(*extension) ? ""sv : "\nLoaded for this session only"sv),
            row);
        details->setObjectName("XenonExtensionDetails");
        details->setTextInteractionFlags(Qt::TextSelectableByMouse);
        details->setWordWrap(true);
        text->addWidget(details);

        for (auto const& warning : manifest.warnings) {
            auto* label = new QLabel(qstring_from_ak_string(warning), row);
            label->setObjectName("XenonExtensionWarning");
            label->setWordWrap(true);
            text->addWidget(label);
        }
        row_layout->addLayout(text, 1);

        auto* buttons = new QVBoxLayout;
        buttons->setSpacing(6);

        auto* reload = new QPushButton("Reload", row);
        reload->setToolTip("Load the extension again from its directory");
        QObject::connect(reload, &QPushButton::clicked, this, [this, id = extension->id()] {
            if (auto result = Registry::the().reload(id); result.is_error())
                QMessageBox::warning(this, "Couldn't reload the extension", qstring_from_ak_string(result.error()));
        });
        buttons->addWidget(reload);

        auto* remove = new QPushButton("Remove", row);
        QObject::connect(remove, &QPushButton::clicked, this, [this, id = extension->id(), name = manifest.name] {
            auto answer = QMessageBox::question(this, "Remove extension?", qformatted("Remove {}? Its data in Xenon is deleted too.", name));
            if (answer == QMessageBox::Yes)
                Registry::the().remove(id);
        });
        buttons->addWidget(remove);
        buttons->addStretch();
        row_layout->addLayout(buttons);

        m_list->addWidget(row);
    }
    m_list->addStretch();
}

void ExtensionsDialog::load_unpacked()
{
    auto directory = QFileDialog::getExistingDirectory(this, "Select the extension's directory");
    if (directory.isEmpty())
        return;

    auto result = Registry::the().load_unpacked(ak_byte_string_from_qstring(directory), Registry::Persist::Yes);
    if (result.is_error())
        QMessageBox::warning(this, "Couldn't load the extension", qstring_from_ak_string(result.error()));
}

QAction* create_extensions_action(QWidget* parent)
{
    auto* action = new QAction("&Extensions", parent);
    QObject::connect(action, &QAction::triggered, parent, [parent] { ExtensionsDialog::show_dialog(parent); });
    return action;
}

}
