/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

fn main() {
    let rune_args = std::env::args().skip(1).collect();
    std::process::exit(xenon_ui::run_with_rune_args(rune_args));
}
