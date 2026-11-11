/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <QWidget>

namespace Ladybird {

class SandboxOffBar final : public QWidget {
    Q_OBJECT

public:
    explicit SandboxOffBar(QWidget* parent = nullptr);

private:
    virtual bool event(QEvent*) override;
    virtual void paintEvent(QPaintEvent*) override;
};

}
