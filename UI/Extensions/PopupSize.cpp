/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StdLibExtras.h>
#include <UI/Extensions/PopupSize.h>

namespace Ladybird::Extensions {

bool is_usable_popup_size(QSize reported)
{
    return reported.width() >= MINIMUM_POPUP_SIZE.width() && reported.height() >= MINIMUM_POPUP_SIZE.height();
}

Optional<QSize> popup_size_for(QSize reported, QSize limit)
{
    if (!is_usable_popup_size(reported))
        return {};

    auto maximum = QSize {
        max(MINIMUM_POPUP_SIZE.width(), min(MAXIMUM_POPUP_SIZE.width(), limit.width())),
        max(MINIMUM_POPUP_SIZE.height(), min(MAXIMUM_POPUP_SIZE.height(), limit.height())),
    };
    return QSize {
        clamp(reported.width(), MINIMUM_POPUP_SIZE.width(), maximum.width()),
        clamp(reported.height(), MINIMUM_POPUP_SIZE.height(), maximum.height()),
    };
}

}
