/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::Once;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use zbus::blocking::{Connection, Proxy};

const CHECK_INTERVAL: Duration = Duration::from_secs(2);

static ENABLED: AtomicBool = AtomicBool::new(false);
static WATCH: Once = Once::new();

pub fn assistive_technology_running() -> bool {
    WATCH.call_once(|| {
        let _ = std::thread::Builder::new()
            .name("xenon-at-spi-status".into())
            .spawn(watch_status);
    });
    ENABLED.load(Ordering::Relaxed)
}

fn watch_status() {
    let Ok(connection) = Connection::session() else {
        return;
    };
    let Ok(status) = Proxy::new(
        &connection,
        "org.a11y.Bus",
        "/org/a11y/bus",
        "org.a11y.Status",
    ) else {
        return;
    };
    let property = |name: &str| status.get_property::<bool>(name).unwrap_or(false);
    loop {
        let enabled = property("IsEnabled") || property("ScreenReaderEnabled");
        ENABLED.store(enabled, Ordering::Relaxed);
        std::thread::sleep(CHECK_INTERVAL);
    }
}
