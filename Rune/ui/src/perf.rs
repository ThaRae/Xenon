/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::sync::{LazyLock, Mutex, MutexGuard};
use std::time::{Duration, Instant};

use rune::{FrameEncoded, FrameOutcome};

const REPORT_EVERY: Duration = Duration::from_secs(5);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stage {
    PageFrame,
    Convert,
    InputToFrame,
    ResizeToFrame,
    Drawn,
    Update,
    Encode,
}

impl Stage {
    const ALL: [Stage; 7] = [
        Stage::PageFrame,
        Stage::Convert,
        Stage::InputToFrame,
        Stage::ResizeToFrame,
        Stage::Drawn,
        Stage::Update,
        Stage::Encode,
    ];

    fn index(self) -> usize {
        Stage::ALL
            .iter()
            .position(|stage| *stage == self)
            .unwrap_or(0)
    }

    fn name(self) -> &'static str {
        match self {
            Stage::PageFrame => "page frame interval",
            Stage::Convert => "frame conversion",
            Stage::InputToFrame => "input to next frame",
            Stage::ResizeToFrame => "new size to its frame",
            Stage::Drawn => "window drawn interval",
            Stage::Update => "window update",
            Stage::Encode => "window encode",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Summary {
    pub count: usize,
    pub mean: f64,
    pub p50: f64,
    pub p95: f64,
    pub max: f64,
}

pub fn summarize(samples: &[f64]) -> Option<Summary> {
    if samples.is_empty() {
        return None;
    }
    let mut sorted = samples.to_vec();
    sorted.sort_by(f64::total_cmp);
    let at = |fraction: f64| sorted[((sorted.len() - 1) as f64 * fraction).round() as usize];
    Some(Summary {
        count: sorted.len(),
        mean: sorted.iter().sum::<f64>() / sorted.len() as f64,
        p50: at(0.5),
        p95: at(0.95),
        max: sorted[sorted.len() - 1],
    })
}

#[derive(Default)]
struct Meter {
    samples: [Vec<f64>; Stage::ALL.len()],
    since: Option<Instant>,
    last_page_frame: Option<Instant>,
    last_drawn: Option<Instant>,
    input_at: Option<Instant>,
    resize_at: Option<Instant>,
    page_size: (u32, u32),
}

impl Meter {
    fn add(&mut self, stage: Stage, duration: Duration, now: Instant) {
        self.samples[stage.index()].push(duration.as_secs_f64() * 1000.0);
        let since = *self.since.get_or_insert(now);
        let elapsed = now.duration_since(since);
        if elapsed >= REPORT_EVERY {
            self.report(elapsed);
            self.since = Some(now);
        }
    }

    fn report(&mut self, elapsed: Duration) {
        let seconds = elapsed.as_secs_f64();
        eprintln!("xenon-rune perf, last {seconds:.1} s:");
        for stage in Stage::ALL {
            let samples = &mut self.samples[stage.index()];
            if let Some(summary) = summarize(samples) {
                eprintln!(
                    "  {:<22} n={:<5} ({:>5.1}/s) mean={:.2} p50={:.2} p95={:.2} max={:.2} ms",
                    stage.name(),
                    summary.count,
                    summary.count as f64 / seconds,
                    summary.mean,
                    summary.p50,
                    summary.p95,
                    summary.max,
                );
            }
            samples.clear();
        }
    }
}

static ENABLED: LazyLock<bool> =
    LazyLock::new(|| std::env::var("XENON_RUNE_PERF").is_ok_and(|value| value == "1"));

static METER: LazyLock<Mutex<Meter>> = LazyLock::new(Mutex::default);

pub fn enabled() -> bool {
    *ENABLED
}

fn meter() -> MutexGuard<'static, Meter> {
    METER
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

pub fn time<T>(stage: Stage, f: impl FnOnce() -> T) -> T {
    if !enabled() {
        return f();
    }
    let start = Instant::now();
    let result = f();
    let now = Instant::now();
    meter().add(stage, now.duration_since(start), now);
    result
}

pub fn input_sent() {
    if enabled() {
        meter().input_at.get_or_insert_with(Instant::now);
    }
}

pub fn viewport_sent() {
    if enabled() {
        meter().resize_at.get_or_insert_with(Instant::now);
    }
}

pub fn page_frame(width: u32, height: u32) {
    if !enabled() {
        return;
    }
    let now = Instant::now();
    let mut meter = meter();
    if let Some(last) = meter.last_page_frame.replace(now) {
        meter.add(Stage::PageFrame, now.duration_since(last), now);
    }
    if let Some(input) = meter.input_at.take() {
        meter.add(Stage::InputToFrame, now.duration_since(input), now);
    }
    if std::mem::replace(&mut meter.page_size, (width, height)) != (width, height)
        && let Some(resize) = meter.resize_at.take()
    {
        meter.add(Stage::ResizeToFrame, now.duration_since(resize), now);
    }
}

pub fn window_frame(frame: &FrameEncoded) {
    if frame.outcome() != FrameOutcome::Drawn {
        return;
    }
    let now = frame.at();
    let mut meter = meter();
    if let Some(last) = meter.last_drawn.replace(now) {
        meter.add(Stage::Drawn, now.duration_since(last), now);
    }
    if let Some(update) = frame.update_ms() {
        meter.add(Stage::Update, Duration::from_secs_f64(update / 1000.0), now);
    }
    if let Some(encode) = frame.encode_ms() {
        meter.add(Stage::Encode, Duration::from_secs_f64(encode / 1000.0), now);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_summary_reads_percentiles_from_the_sorted_samples() {
        let samples: Vec<f64> = (1..=20).rev().map(f64::from).collect();
        let summary = summarize(&samples).unwrap();
        assert_eq!(summary.count, 20);
        assert_eq!(summary.mean, 10.5);
        assert_eq!(summary.p50, 11.0);
        assert_eq!(summary.p95, 19.0);
        assert_eq!(summary.max, 20.0);
    }

    #[test]
    fn no_samples_have_no_summary() {
        assert_eq!(summarize(&[]), None);
    }
}
