/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use rune::Icon;

macro_rules! stroked {
    ($name:literal, $path:literal) => {
        Icon {
            name: $name,
            view: 24.0,
            body: concat!(
                r#"<path d=""#,
                $path,
                r#"" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>"#
            ),
        }
    };
}

pub static BACK: Icon = stroked!("xenon-back", "M19 12H5M11 6l-6 6 6 6");
pub static FORWARD: Icon = stroked!("xenon-forward", "M5 12h14M13 6l6 6-6 6");
pub static RELOAD: Icon = stroked!("xenon-reload", "M20 12a8 8 0 1 1-2.34-5.66M20 4v5h-5");
pub static STOP: Icon = stroked!("xenon-stop", "M6 6l12 12M18 6L6 18");
pub static CLOSE: Icon = stroked!("xenon-close", "M7 7l10 10M17 7L7 17");
pub static PLUS: Icon = stroked!("xenon-plus", "M12 5v14M5 12h14");
pub static MENU: Icon = stroked!("xenon-menu", "M4 7h16M4 12h16M4 17h16");
pub static STAR: Icon = stroked!(
    "xenon-star",
    "M12 3.5l2.6 5.3 5.9.9-4.3 4.1 1 5.8-5.2-2.7-5.2 2.7 1-5.8-4.3-4.1 5.9-.9z"
);
pub static STAR_FILLED: Icon = Icon {
    name: "xenon-star-filled",
    view: 24.0,
    body: r#"<path d="M12 3.5l2.6 5.3 5.9.9-4.3 4.1 1 5.8-5.2-2.7-5.2 2.7 1-5.8-4.3-4.1 5.9-.9z" fill="currentColor" stroke="currentColor" stroke-width="2" stroke-linejoin="round"/>"#,
};
pub static SIDEBAR: Icon = stroked!("xenon-sidebar", "M4 5h16v14H4zM10 5v14");
pub static GLOBE: Icon = stroked!(
    "xenon-globe",
    "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18M3 12h18M12 3c2.5 2.6 3.8 5.6 3.8 9s-1.3 6.4-3.8 9M12 3c-2.5 2.6-3.8 5.6-3.8 9s1.3 6.4 3.8 9"
);
pub static SPINNER: Icon = Icon {
    name: "xenon-spinner",
    view: 24.0,
    body: r#"<path d="M12 3a9 9 0 0 1 9 9" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"/>"#,
};
pub static SPINNER_TRACK: Icon = Icon {
    name: "xenon-spinner-track",
    view: 24.0,
    body: r#"<path d="M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18" fill="none" stroke="currentColor" stroke-width="2.5"/>"#,
};
pub static CRASHED: Icon = stroked!(
    "xenon-crashed",
    "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18M8.5 15.5c2-1.6 5-1.6 7 0M9 9.5h.01M15 9.5h.01"
);
