//! Times the engine's audio callback, headless: the WING, the X32, a dLive at its full size
//! and the plug-in host with 64 channels, at 48 kHz and 128 samples, each set up with
//! processing and fed a fixed multi-channel signal (`tests/common/rigs.rs`).
//!
//! cargo run --release -p livedaw-engine --example bench_blocks -- [--workers N] [--blocks N] [--only NAME]
//!
//! Prints the mean, the median, the 99th percentile and the worst time per block, the
//! thread's CPU time per block (cycles, which a busy machine doesn't stretch) and the realtime
//! load (the mean as a share of the 2.67 ms a block lasts). With no workers the engine runs
//! on the one thread, which is the cleanest measure of the work itself.

#[path = "../tests/common/rigs.rs"]
mod rigs;

use rigs::{Console, Rig, BLOCK, SAMPLE_RATE};
use std::time::Instant;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut workers = 0usize;
    let mut blocks = 20_000usize;
    let mut only: Option<String> = None;
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--workers" => workers = it.next().and_then(|v| v.parse().ok()).expect("--workers N"),
            "--blocks" => blocks = it.next().and_then(|v| v.parse().ok()).expect("--blocks N"),
            "--only" => only = it.next().cloned(),
            other => panic!("unknown argument {other}"),
        }
    }
    rigs::raise_priority();
    let budget_us = 1.0e6 * BLOCK as f64 / SAMPLE_RATE;
    println!("{BLOCK} samples at {SAMPLE_RATE} Hz ({budget_us:.0} us a block), {workers} workers, {blocks} blocks");
    println!(
        "{:<16} {:>9} {:>9} {:>9} {:>9} {:>9} {:>7}",
        "console", "mean us", "median us", "p99 us", "max us", "Mcyc/blk", "load"
    );
    for console in Console::ALL {
        if only.as_deref().is_some_and(|o| !console.name().eq_ignore_ascii_case(o)) {
            continue;
        }
        let mut rig = Rig::new(console, workers);
        for _ in 0..2000 {
            rig.step();
        }
        let mut times = Vec::with_capacity(blocks);
        let mut cycles = 0u64;
        for _ in 0..blocks {
            rig.feed();
            let c = livedaw_engine::trace::thread_cycles();
            let t = Instant::now();
            rig.process();
            times.push(t.elapsed().as_secs_f64() * 1.0e6);
            cycles += livedaw_engine::trace::thread_cycles().wrapping_sub(c);
        }
        times.sort_by(|a, b| a.total_cmp(b));
        let mean = times.iter().sum::<f64>() / times.len() as f64;
        let median = times[times.len() / 2];
        let p99 = times[((0.99 * times.len() as f64).ceil() as usize - 1).min(times.len() - 1)];
        let max = *times.last().unwrap();
        println!(
            "{:<16} {:>9.1} {:>9.1} {:>9.1} {:>9.1} {:>9.3} {:>6.1}%",
            console.name(),
            mean,
            median,
            p99,
            max,
            cycles as f64 / blocks as f64 / 1.0e6,
            100.0 * mean / budget_us
        );
        let faults = rig.engine.controls.engine_faults.load(std::sync::atomic::Ordering::Relaxed);
        if faults != 0 {
            println!("  ({faults} engine faults)");
        }
    }
}
