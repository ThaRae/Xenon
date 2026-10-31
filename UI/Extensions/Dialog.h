/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/kmalloc.h>

#include <QDialog>

class QAction;
class QComboBox;
class QLabel;
class QVBoxLayout;

namespace Ladybird::Extensions {

class ExtensionsDialog final : public QDialog {
public:
    AK_ALLOC_WITH_KMALLOC;

    static void show_dialog(QWidget* parent);

private:
    explicit ExtensionsDialog(QWidget* parent);

    void rebuild();
    void load_unpacked();
    void update_password_manager();
    void choose_password_manager(int index);

    QComboBox* m_password_manager { nullptr };
    QLabel* m_password_manager_status { nullptr };
    QVBoxLayout* m_list { nullptr };
};

QAction* create_extensions_action(QWidget* parent);

}
