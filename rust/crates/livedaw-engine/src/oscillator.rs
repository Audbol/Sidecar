//! A console's own oscillator (the ProMix 01's, the 02R's, the X32's): one tone
//! or noise switched straight onto buses, mains and matrices, not a source a
//! channel picks (the
//! WING's OSC group, `sources.rs`). The console's crate sets it from its own
//! state (`Engine::oscillator`, through the console text's hook); the engine
//! adds it to each destination's sum, after the sends and before the
//! destination's processing, each side at its own gain (a stereo bus's two
//! sides can take it apart), ramped over a block when it changes.

use crate::atomic::AtomicF32;
use crate::params::StripKind;
use crate::strip::Strip;
use livedaw_dsp::generator::{generator_mode, SignalGenerator};
use livedaw_dsp::util::{add_with_gain_ramp, apply_gain_ramp};
use std::cell::UnsafeCell;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::AtomicI32;
use std::sync::atomic::Ordering::Relaxed;

/// The buses, mains and matrices it can play on (the first of each).
pub const OSC_BUSES: usize = 32;
pub const OSC_MAINS: usize = 4;
pub const OSC_MATRICES: usize = 8;

/// The audio thread's part: the generator and the block it made.
struct OscAudio {
    generator: SignalGenerator,
    buffer: Vec<f32>,
    /// Whether this block's buffer holds the signal (it was on, or is ramping out).
    live: bool,
    was_on: bool,
    sample_rate: f64,
    /// Where the burst's four seconds have got to.
    burst_pos: usize,
}

/// Burst noise (the 01V's, p.204): this long on, once per period.
const BURST_ON_S: f64 = 0.2;
const BURST_PERIOD_S: f64 = 4.0;

pub struct ConsoleOscillator {
    pub on: AtomicBool,
    /// `generator_mode` (SINE, PINK, WHITE).
    pub mode: AtomicI32,
    pub freq_hz: AtomicF32,
    /// The sine's peak in dBFS (the noises at its RMS).
    pub level_db: AtomicF32,
    /// Plays in bursts (`BURST_ON_S` every `BURST_PERIOD_S`).
    pub burst: AtomicBool,
    /// A destination it's assigned to carries only the oscillator (the 01V's, p.204:
    /// "the oscillator signal has priority and other signals routed to that bus are muted").
    pub exclusive: AtomicBool,
    /// Per bus, main and matrix, each side's gain (0: not assigned).
    buses: [[AtomicF32; 2]; OSC_BUSES],
    mains: [[AtomicF32; 2]; OSC_MAINS],
    matrices: [[AtomicF32; 2]; OSC_MATRICES],
    audio: UnsafeCell<OscAudio>,
    /// Each destination's last gains (owned by its task).
    bus_last: [UnsafeCell<[f32; 2]>; OSC_BUSES],
    main_last: [UnsafeCell<[f32; 2]>; OSC_MAINS],
    matrix_last: [UnsafeCell<[f32; 2]>; OSC_MATRICES],
}

// SAFETY: `audio` is the audio thread's (prepare before it runs, generate in
// its single-threaded part); each `*_last` cell is its destination's task's;
// the rest is atomics.
unsafe impl Sync for ConsoleOscillator {}

impl Default for ConsoleOscillator {
    fn default() -> Self {
        ConsoleOscillator {
            on: AtomicBool::new(false),
            mode: AtomicI32::new(generator_mode::SINE),
            freq_hz: AtomicF32::new(1000.0),
            level_db: AtomicF32::new(-20.0),
            burst: AtomicBool::new(false),
            exclusive: AtomicBool::new(false),
            buses: std::array::from_fn(|_| [AtomicF32::new(0.0), AtomicF32::new(0.0)]),
            mains: std::array::from_fn(|_| [AtomicF32::new(0.0), AtomicF32::new(0.0)]),
            matrices: std::array::from_fn(|_| [AtomicF32::new(0.0), AtomicF32::new(0.0)]),
            audio: UnsafeCell::new(OscAudio {
                generator: SignalGenerator::new(48000.0, 0x5eed_05c1),
                buffer: Vec::new(),
                live: false,
                was_on: false,
                sample_rate: 48000.0,
                burst_pos: 0,
            }),
            bus_last: std::array::from_fn(|_| UnsafeCell::new([0.0; 2])),
            main_last: std::array::from_fn(|_| UnsafeCell::new([0.0; 2])),
            matrix_last: std::array::from_fn(|_| UnsafeCell::new([0.0; 2])),
        }
    }
}

impl ConsoleOscillator {
    /// Off and assigned nowhere (a reset).
    pub fn clear(&self) {
        self.on.store(false, Relaxed);
        self.burst.store(false, Relaxed);
        self.exclusive.store(false, Relaxed);
        for g in self.buses.iter().chain(&self.mains).chain(&self.matrices) {
            g[0].store(0.0);
            g[1].store(0.0);
        }
    }

    /// What it plays: ON, `generator_mode`, the sine's frequency, its level (dBFS).
    pub fn set(&self, on: bool, mode: i32, freq_hz: f32, level_db: f32) {
        self.mode.store(mode, Relaxed);
        self.freq_hz.store(freq_hz);
        self.level_db.store(level_db);
        self.on.store(on, Relaxed);
    }

    fn gains(&self, kind: StripKind, i: usize) -> Option<&[AtomicF32; 2]> {
        match kind {
            StripKind::Bus => self.buses.get(i),
            StripKind::Main => self.mains.get(i),
            StripKind::Matrix => self.matrices.get(i),
            _ => None,
        }
    }

    /// Bus, main or matrix `i` (0-based) takes it at `[left, right]` (0 0: not at all).
    pub fn assign(&self, kind: StripKind, i: usize, gains: [f32; 2]) {
        if let Some(g) = self.gains(kind, i) {
            g[0].store(gains[0]);
            g[1].store(gains[1]);
        }
    }

    /// Where bus, main or matrix `i` takes it.
    pub fn assigned(&self, kind: StripKind, i: usize) -> [f32; 2] {
        self.gains(kind, i).map_or([0.0; 2], |g| [g[0].load(), g[1].load()])
    }

    /// # Safety
    /// Not while the audio thread runs.
    pub(crate) unsafe fn prepare(&self, sample_rate: f64, block_size: usize) {
        let a = &mut *self.audio.get();
        a.generator.prepare(sample_rate);
        a.generator.reset();
        a.buffer = vec![0.0; block_size];
        a.live = false;
        a.was_on = false;
        a.sample_rate = sample_rate;
        a.burst_pos = 0;
    }

    /// This block's signal (on, or ramping out the block after it went off).
    ///
    /// # Safety
    /// The audio thread's single-threaded part, before the buses, mains and matrices.
    pub(crate) unsafe fn generate(&self, n: usize) {
        let a = &mut *self.audio.get();
        let on = self.on.load(Relaxed);
        a.live = (on || a.was_on) && a.buffer.len() >= n;
        a.was_on = on;
        if a.live {
            let level = self.level_db.load().min(0.0);
            a.generator.process_peak(&mut a.buffer[..n], self.mode.load(Relaxed), self.freq_hz.load(), 10f32.powf(level / 20.0));
            if self.burst.load(Relaxed) {
                let on_for = (a.sample_rate * BURST_ON_S) as usize;
                let period = ((a.sample_rate * BURST_PERIOD_S) as usize).max(1);
                for x in &mut a.buffer[..n] {
                    if a.burst_pos >= on_for {
                        *x = 0.0;
                    }
                    a.burst_pos = (a.burst_pos + 1) % period;
                }
            } else {
                a.burst_pos = 0;
            }
        }
    }

    /// Adds it to `dst` (bus, main or matrix `i`) at its gains.
    ///
    /// # Safety
    /// `dst`'s task, after `generate` and its sum, before it processes.
    pub(crate) unsafe fn add_to(&self, dst: &Strip, kind: StripKind, i: usize, n: usize) {
        let last = match kind {
            StripKind::Bus => self.bus_last.get(i),
            StripKind::Main => self.main_last.get(i),
            StripKind::Matrix => self.matrix_last.get(i),
            _ => None,
        };
        let Some(last) = last else { return };
        let last = &mut *last.get();
        let a = &*self.audio.get();
        let target = if self.on.load(Relaxed) { self.assigned(kind, i) } else { [0.0; 2] };
        if !a.live || (target == [0.0; 2] && *last == [0.0; 2]) {
            *last = [0.0; 2];
            return;
        }
        let exclusive = self.exclusive.load(Relaxed);
        let d = dst.dsp_mut();
        for (ch, (&from, &to)) in last.iter().zip(&target).enumerate() {
            if exclusive && (from != 0.0 || to != 0.0) {
                let gain = |g: f32| if g != 0.0 { 0.0 } else { 1.0 };
                apply_gain_ramp(d.work_mut(ch, n), gain(from), gain(to));
            }
            if from != 0.0 || to != 0.0 {
                add_with_gain_ramp(d.work_mut(ch, n), &a.buffer[..n], from, to);
            }
        }
        *last = target;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_keeps_its_assignments_and_clears() {
        let o = ConsoleOscillator::default();
        o.assign(StripKind::Bus, 2, [1.0, 0.0]);
        o.assign(StripKind::Main, 0, [1.0, 1.0]);
        o.assign(StripKind::Bus, OSC_BUSES, [1.0, 1.0]); // out of range: ignored
        assert_eq!(o.assigned(StripKind::Bus, 2), [1.0, 0.0]);
        assert_eq!(o.assigned(StripKind::Channel, 0), [0.0, 0.0]);
        o.set(true, generator_mode::PINK, 1000.0, -10.0);
        o.burst.store(true, Relaxed);
        o.exclusive.store(true, Relaxed);
        o.clear();
        assert!(!o.on.load(Relaxed));
        assert!(!o.burst.load(Relaxed) && !o.exclusive.load(Relaxed));
        assert_eq!(o.assigned(StripKind::Main, 0), [0.0, 0.0]);
    }
}
