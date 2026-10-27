/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>

class QWidget;

namespace Ladybird::ElementPicker {

// The context menu's Block Element... action (from Patches/ladybird) lets the user click an element and makes a uBlock
// Origin cosmetic filter that hides it on its site. This shows the filter, lets the user edit it, and adds it to the
// content blocker's custom filters, which applies it everywhere at once.
void confirm_filter(QWidget& parent, String const& filter);

}
