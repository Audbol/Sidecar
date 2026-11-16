//! The consoles the CPU benchmark and the null test render: a WING, an X32, a dLive at its
//! full size and the plug-in host with 64 channels, each set up with processing and fed a
//! fixed multi-channel signal. Shared by `examples/bench_blocks.rs` and `tests/null_render.rs`
//! (through `#[path]`), so both measure and compare the same thing.

#![allow(dead_code)]

use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::{EqBlockParams, StripKind};
use livedaw_engine::profile::{DLiveLayout, HostSize, Profile};
use livedaw_engine::Engine;
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

/// Worker threads at the audio thread's priority, as the app lends them (realtime-priority
/// JUCE threads); ordinary ones are pushed aside by whatever else the machine is doing.
#[cfg(windows)]
pub fn raise_priority() {
    extern "system" {
        fn GetCurrentThread() -> isize;
        fn SetThreadPriority(thread: isize, priority: i32) -> i32;
    }
    // SAFETY: plain Win32 calls on the current thread.
    unsafe { SetThreadPriority(GetCurrentThread(), 15) }; // THREAD_PRIORITY_TIME_CRITICAL
}

#[cfg(not(windows))]
pub fn raise_priority() {}

pub const SAMPLE_RATE: f64 = 48_000.0;
pub const BLOCK: usize = 128;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Console {
    Wing,
    X32,
    DLive,
    Host64,
}

impl Console {
    pub const ALL: [Console; 4] = [Console::Wing, Console::X32, Console::DLive, Console::Host64];

    pub fn name(self) -> &'static str {
        match self {
            Console::Wing => "WING",
            Console::X32 => "X32",
            Console::DLive => "dLive",
            Console::Host64 => "PLUGIN HOST 64",
        }
    }

    fn config(self) -> ConsoleConfig {
        match self {
            Console::Wing => ConsoleConfig::wing(),
            Console::X32 => ConsoleConfig { profile: Profile::X32, counts: Counts::X32 },
            Console::DLive => ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: DLiveLayout::FOH.counts() },
            Console::Host64 => {
                let profile = Profile::PluginHost(HostSize::Ch64);
                ConsoleConfig { profile, counts: profile.spec().counts }
            }
        }
    }
}

/// How many loud and quiet blocks the looped input alternates between: the gates open and close.
const LOOP_BLOCKS: usize = 32;
const LOUD_BLOCKS: usize = 22;

pub struct Rig {
    pub engine: Arc<Engine>,
    pub inputs: Vec<Vec<f32>>,
    pub outputs: Vec<Vec<f32>>,
    /// One loop of every input (`LOOP_BLOCKS` blocks).
    source: Vec<Vec<f32>>,
    block: usize,
    workers: Vec<std::thread::JoinHandle<()>>,
}

fn lcg(state: &mut u64) -> f32 {
    *state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
    ((*state >> 40) as f32 / (1u64 << 24) as f32) * 2.0 - 1.0
}

/// A gain-like parameter of an EQ (dB, with room both ways): alternately up and down.
fn shape_eq(eq: &EqBlockParams) {
    eq.on.store(true, Relaxed);
    let Some(spec) = eq.model_spec() else { return };
    let mut sign = 1.0f32;
    for (i, p) in spec.params.iter().enumerate() {
        if p.unit == "dB" && p.lo() < 0.0 && p.hi() > 0.0 {
            if let Some(v) = eq.values.get(i) {
                v.store(p.sanitise(3.5 * sign));
                sign = -sign;
            }
        }
    }
}

impl Rig {
    pub fn new(console: Console, num_workers: usize) -> Self {
        let config = console.config();
        let engine = Arc::new(Engine::new(num_workers));
        engine.configure(&config);
        let host = console == Console::Host64;
        let channels = engine.strips(StripKind::Channel);
        let in_ports = if host { channels.len() + 2 } else { channels.len().next_power_of_two().max(32) };

        // Every strip that isn't a channel gets a stereo output pair of its own.
        let others: Vec<_> = [StripKind::Aux, StripKind::Bus, StripKind::Main, StripKind::Matrix]
            .into_iter()
            .flat_map(|k| engine.strips(k))
            .collect();
        let out_ports = if host { in_ports } else { 2 * others.len() + 2 };

        for (i, ch) in channels.iter().enumerate() {
            let p = &ch.params;
            ch.params.mute.store(false, Relaxed);
            if host {
                p.fader_db.store(-3.0);
                continue;
            }
            p.fader_db.store(-26.0);
            p.input_left.store(i as i32, Relaxed);
            p.input_right.store(-1, Relaxed);
            p.stereo.store(false, Relaxed);
            let f = p.filter();
            f.low_cut.store(true, Relaxed);
            f.low_cut_freq.store(90.0);
            f.high_cut.store(true, Relaxed);
            f.high_cut_freq.store(14_000.0);
            p.gate().on.store(true, Relaxed);
            if let Some(thr) = p.gate().param("thr") {
                thr.store(-45.0);
            }
            p.dynamics().on.store(true, Relaxed);
            if let Some(thr) = p.dynamics().param("thr") {
                thr.store(-28.0);
            }
            shape_eq(p.eq());
        }
        // Mains first in the output map (ports 0 and 1), then the rest in order.
        let mut port = 0;
        let mut ordered: Vec<_> = others.iter().filter(|s| s.kind() == StripKind::Main).cloned().collect();
        ordered.extend(others.iter().filter(|s| s.kind() != StripKind::Main).cloned());
        for s in &ordered {
            if !host {
                s.params.output_left.store(port, Relaxed);
                s.params.output_right.store(port + 1, Relaxed);
                port += 2;
                s.params.dynamics().on.store(true, Relaxed);
                if let Some(thr) = s.params.dynamics().param("thr") {
                    thr.store(-20.0);
                }
                shape_eq(s.params.eq());
            }
            s.params.fader_db.store(0.0);
        }
        // Sends from every channel to every bus and matrix, and to the main.
        if !host {
            for ch in channels.iter() {
                for slot in 0..engine.strips(StripKind::Bus).len() {
                    if let Some(send) = ch.sends.get_or_create(slot) {
                        send.enabled.store(true, Relaxed);
                        send.level_db.store(-20.0);
                        send.pan_link.store(true, Relaxed);
                        send.mode.store(livedaw_engine::sends::send_mode::POST, Relaxed);
                    }
                }
                for m in (0..engine.strips(StripKind::Matrix).len()).filter_map(|i| ch.matrix_send(i)) {
                    m.on.store(true, Relaxed);
                    m.level_db.store(-12.0);
                }
                if let Some(m) = ch.main_send(0) {
                    m.on.store(true, Relaxed);
                }
            }
            for bus in engine.strips(StripKind::Bus) {
                if let Some(m) = bus.main_send(0) {
                    m.on.store(false, Relaxed);
                }
                for m in (0..engine.strips(StripKind::Matrix).len()).filter_map(|i| bus.matrix_send(i)) {
                    m.on.store(true, Relaxed);
                }
            }
        }
        engine.collect();

        let in_map: Vec<i32> = (0..in_ports as i32).collect();
        let out_map: Vec<i32> = (0..out_ports as i32).collect();
        // SAFETY: nothing is processing yet.
        unsafe { engine.prepare(SAMPLE_RATE, BLOCK, &in_map, &out_map) };
        engine.collect();

        let workers = (0..num_workers)
            .map(|i| {
                let e = engine.clone();
                std::thread::spawn(move || {
                    raise_priority();
                    e.pool().run_worker(i)
                })
            })
            .collect();
        while engine.pool().num_running_workers() < num_workers {
            std::thread::yield_now();
        }

        // The input: a different tone and some noise on each, loud for most of the loop and
        // quiet for the rest so the gates move.
        let source = (0..in_ports)
            .map(|n| {
                let mut rng = 0x9E3779B97F4A7C15u64 ^ (n as u64 + 1).wrapping_mul(0xD1B54A32D192ED03);
                let freq = 110.0 * (1.0 + 0.37 * n as f32);
                (0..LOOP_BLOCKS * BLOCK)
                    .map(|i| {
                        let loud = i / BLOCK < LOUD_BLOCKS;
                        let amp = if loud { 0.35 } else { 0.0015 };
                        let t = i as f32 / SAMPLE_RATE as f32;
                        amp * ((std::f32::consts::TAU * freq * t).sin() * 0.8 + 0.2 * lcg(&mut rng))
                    })
                    .collect()
            })
            .collect();
        Self {
            engine,
            inputs: vec![vec![0.0; BLOCK]; in_ports],
            outputs: vec![vec![0.0; BLOCK]; out_ports],
            source,
            block: 0,
            workers,
        }
    }

    /// Loads the next block of input.
    pub fn feed(&mut self) {
        let at = (self.block % LOOP_BLOCKS) * BLOCK;
        for (dst, src) in self.inputs.iter_mut().zip(&self.source) {
            dst.copy_from_slice(&src[at..at + BLOCK]);
        }
        self.block += 1;
    }

    /// Processes the block `feed` loaded.
    pub fn process(&mut self) {
        let ins: Vec<*const f32> = self.inputs.iter().map(|v| v.as_ptr()).collect();
        let outs: Vec<*mut f32> = self.outputs.iter_mut().map(|v| v.as_mut_ptr()).collect();
        // SAFETY: every buffer holds BLOCK samples; this is the only audio thread.
        unsafe { self.engine.process(&ins, &outs, BLOCK) };
    }

    /// Feeds and processes one block.
    pub fn step(&mut self) {
        self.feed();
        self.process();
    }
}

impl Drop for Rig {
    fn drop(&mut self) {
        self.engine.pool().shutdown();
        for w in self.workers.drain(..) {
            let _ = w.join();
        }
    }
}
