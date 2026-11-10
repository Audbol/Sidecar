//! Rhythm Delay. Stated (guide): a Simple and an Advanced page; Simple: BPM, Dotted
//! or Triplet, Interval, Repeats per bar, Amplitude Shape and its amount, Auto Pan
//! (following the quarter note), LF Cut, Resonant and HF Cut in the feedback, Feedback,
//! Drive; Advanced: each repeat's position, gain and pan, Dry Repeat, Bar Length,
//! Pattern Tap. Sidecar's reading: every number, the shapes, the feedback filters as
//! a high-pass (12 dB/oct), a resonance (a bell at the geometric mean of the corners) and
//! a low-pass (12 dB/oct) in the loop, Drive as a soft clip in the loop; Dry Repeat as
//! a repeat at the bar's start (time zero); up to 8 repeats on the Advanced page;
//! Pattern Tap as positions taken from taps.

use super::super::{ch, p, pct, tog, LIN, LOG};
use crate::biquad::{Biquad, BiquadCoeffs};
use crate::model::{ParamSpec, Params};
use crate::wing::fx::blocks::{fast_tanh, DelayLine};
use crate::wing::fx::{FxAlgo, FxContext};
use std::f32::consts::{FRAC_PI_2, TAU};

const PAGES: &[&str] = &["SIMPLE", "ADVANCED"];
const NOTE_MOD: &[&str] = &["STRAIGHT", "DOTTED", "TRIPLET"];
const INTERVALS: &[&str] = &["1/1", "1/2", "1/4", "1/8", "1/16"];
const SHAPES: &[&str] = &["FLAT", "FALL", "RISE", "SWELL"];

const REPEATS: usize = 8;
/// The bar's end at the lowest tempo: 8 quarter notes at 30 BPM.
const MAX_SECONDS: f32 = 16.0;

macro_rules! repeat {
    ($n:literal, $at:expr) => {
        [
            p(concat!("t_", $n), concat!("POS ", $n), "%", LIN, 0.0, 100.0, 201, $at),
            p(concat!("g_", $n), concat!("GAIN ", $n), "dB", LIN, -40.0, 6.0, 93, 0.0),
            p(concat!("n_", $n), concat!("PAN ", $n), "", LIN, -100.0, 100.0, 201, 0.0),
        ]
    };
}

const COMMON: [ParamSpec; 13] = [
    ch("page", "PAGE", PAGES, 0.0),
    p("bpm", "BPM", "", LIN, 30.0, 300.0, 271, 120.0),
    ch("nm", "NOTE", NOTE_MOD, 0.0),
    ch("int", "INTERVAL", INTERVALS, 3.0),
    p("reps", "REPEATS", "", ParamKindInt, 1.0, 8.0, 8, 4.0),
    ch("shape", "SHAPE", SHAPES, 1.0),
    p("amt", "AMOUNT", "%", LIN, 0.0, 100.0, 101, 60.0),
    p("apan", "AUTO PAN", "%", LIN, 0.0, 100.0, 101, 0.0),
    p("lc", "LF CUT", "Hz", LOG, 20.0, 2000.0, 101, 80.0),
    p("res", "RESONANT", "dB", LIN, 0.0, 12.0, 49, 0.0),
    p("hc", "HF CUT", "Hz", LOG, 500.0, 20000.0, 101, 6000.0),
    p("fb", "FEEDBACK", "%", LIN, 0.0, 95.0, 96, 30.0),
    p("drv", "DRIVE", "%", LIN, 0.0, 100.0, 101, 0.0),
];
const ADVANCED: [ParamSpec; 3] =
    [tog("dry", "DRY REPEAT", 0.0), p("bar", "BAR LENGTH", "", ParamKindInt, 1.0, 8.0, 8, 4.0), tog("ptap", "PATTERN TAP", 0.0)];
const R1: [ParamSpec; 3] = repeat!("1", 12.5);
const R2: [ParamSpec; 3] = repeat!("2", 25.0);
const R3: [ParamSpec; 3] = repeat!("3", 37.5);
const R4: [ParamSpec; 3] = repeat!("4", 50.0);
const R5: [ParamSpec; 3] = repeat!("5", 62.5);
const R6: [ParamSpec; 3] = repeat!("6", 75.0);
const R7: [ParamSpec; 3] = repeat!("7", 87.5);
const R8: [ParamSpec; 3] = repeat!("8", 100.0);

use crate::model::ParamKind::Int as ParamKindInt;

const fn join_panel<const N: usize>() -> [ParamSpec; N] {
    let parts: [&[ParamSpec]; 10] = [&COMMON, &ADVANCED, &R1, &R2, &R3, &R4, &R5, &R6, &R7, &R8];
    let mut out = [COMMON[0]; N];
    let (mut n, mut i) = (0, 0);
    while i < parts.len() {
        let mut j = 0;
        while j < parts[i].len() {
            out[n] = parts[i][j];
            n += 1;
            j += 1;
        }
        i += 1;
    }
    assert!(n == N);
    out
}

/// Rhythm Delay's panel.
pub(in super::super) static RHYTHM: [ParamSpec; 40] = join_panel();

const fn r_ids(n: usize) -> [&'static str; 3] {
    match n {
        0 => ["t_1", "g_1", "n_1"],
        1 => ["t_2", "g_2", "n_2"],
        2 => ["t_3", "g_3", "n_3"],
        3 => ["t_4", "g_4", "n_4"],
        4 => ["t_5", "g_5", "n_5"],
        5 => ["t_6", "g_6", "n_6"],
        6 => ["t_7", "g_7", "n_7"],
        _ => ["t_8", "g_8", "n_8"],
    }
}

#[derive(Clone, Copy, Default)]
struct Repeat {
    pos: f32,
    gl: f32,
    gr: f32,
}

/// A loop of up to 8 repeats per bar, each a tap on the same line.
pub(in super::super) struct RhythmDelay {
    sr: f32,
    lines: [DelayLine; 2],
    /// The repeats (positions in samples) and the longest one, which feeds back.
    reps: [Repeat; REPEATS + 1],
    count: usize,
    fb_pos: f32,
    fb: f32,
    drive: f32,
    /// Feedback's high-pass, resonance and low-pass, per side.
    hp: [Biquad; 2],
    bell: [Biquad; 2],
    lp: [Biquad; 2],
    /// The Auto Pan's phase and step; depth 0..1.
    pan_phase: f32,
    pan_step: f32,
    pan_depth: f32,
    /// Pattern Tap: what the first tap's sample count is, and the positions taken so far.
    clock: u32,
    tap_prev: bool,
    tap_start: Option<u32>,
    taps: [f32; REPEATS],
    ntaps: usize,
    /// Keeps the bar across a tap, for the tapped positions.
    bar_samples: f32,
}

impl RhythmDelay {
    pub(in super::super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let cap = (MAX_SECONDS * sr) as usize + 8;
        let filter = || [Biquad::default(), Biquad::default()];
        Self {
            sr,
            lines: [DelayLine::new(cap), DelayLine::new(cap)],
            reps: [Repeat::default(); REPEATS + 1],
            count: 0,
            fb_pos: 1.0,
            fb: 0.0,
            drive: 0.0,
            hp: filter(),
            bell: filter(),
            lp: filter(),
            pan_phase: 0.0,
            pan_step: 0.0,
            pan_depth: 0.0,
            clock: 0,
            tap_prev: false,
            tap_start: None,
            taps: [0.0; REPEATS],
            ntaps: 0,
            bar_samples: 1.0,
        }
    }

    fn cap(&self) -> f32 {
        (self.lines[0].max_delay() as f32 - 4.0).max(1.0)
    }
}

#[inline]
fn gains(pan: f32) -> (f32, f32) {
    let (s, c) = ((pan.clamp(-1.0, 1.0) + 1.0) * 0.5 * FRAC_PI_2).sin_cos();
    (c * std::f32::consts::SQRT_2, s * std::f32::consts::SQRT_2)
}

impl FxAlgo for RhythmDelay {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        let quarter = 60.0 * sr / p.f("bpm").max(1.0);
        let bar_q = p.f("bar").max(1.0);
        let bar = quarter * bar_q;
        self.bar_samples = bar;
        self.pan_step = 1.0 / quarter;
        self.pan_depth = pct(p.f("apan"));
        self.fb = (pct(p.f("fb"))).min(0.95);
        self.drive = pct(p.f("drv"));
        let (lc, hc) = (p.f("lc"), p.f("hc").min(sr * 0.45));
        let bell_hz = (lc * hc).sqrt().min(sr * 0.45);
        for c in 0..2 {
            self.hp[c].set_coefficients(BiquadCoeffs::high_pass(f64::from(sr), f64::from(lc), 0.707));
            self.lp[c].set_coefficients(BiquadCoeffs::low_pass(f64::from(sr), f64::from(hc), 0.707));
            self.bell[c].set_coefficients(BiquadCoeffs::peak(f64::from(sr), f64::from(bell_hz), 2.0, f64::from(p.f("res"))));
        }
        let cap = self.cap();
        let advanced = p.choice("page") == 1;
        // Pattern Tap: each rising edge on the Advanced page sets the next repeat, as a
        // share of the bar from the first tap.
        let tap = p.on("ptap");
        if advanced && tap && !self.tap_prev {
            match self.tap_start {
                None => {
                    self.tap_start = Some(self.clock);
                    self.ntaps = 0;
                }
                Some(t0) if self.ntaps < REPEATS => {
                    let dt = self.clock.wrapping_sub(t0) as f32;
                    self.taps[self.ntaps] = (dt / bar.max(1.0)).clamp(0.0, 1.0);
                    self.ntaps += 1;
                }
                _ => {}
            }
        }
        self.tap_prev = tap;
        let mut reps = [Repeat::default(); REPEATS + 1];
        let mut n = 0;
        if advanced {
            let tapped = self.ntaps;
            for r in 0..REPEATS {
                let ids = r_ids(r);
                let at = if r < tapped { self.taps[r] } else { pct(p.f(ids[0])) };
                let (gl, gr) = gains(p.f(ids[2]) * 0.01);
                let g = 10f32.powf(p.f(ids[1]) * 0.05);
                reps[n] = Repeat { pos: (at * bar).clamp(1.0, cap), gl: gl * g, gr: gr * g };
                n += 1;
            }
            if p.on("dry") {
                reps[n] = Repeat { pos: 1.0, gl: 1.0, gr: 1.0 };
                n += 1;
            }
        } else {
            let beat =
                [4.0, 2.0, 1.0, 0.5, 0.25][p.choice("int").min(4)] * [1.0, 1.5, 2.0 / 3.0][p.choice("nm").min(2)] * quarter;
            let reps_n = p.f("reps").max(1.0) as usize;
            let (shape, amt) = (p.choice("shape"), pct(p.f("amt")));
            let last = (reps_n as f32 - 1.0).max(1.0);
            for r in 0..reps_n.min(REPEATS) {
                let x = r as f32 / last;
                let curve = match shape {
                    1 => 1.0 - x,
                    2 => x,
                    3 => 1.0 - (2.0 * x - 1.0).abs(),
                    _ => 1.0,
                };
                let g = 1.0 - amt * (1.0 - curve);
                reps[n] = Repeat { pos: (beat * (r + 1) as f32).clamp(1.0, cap), gl: g, gr: g };
                n += 1;
            }
        }
        self.count = n;
        self.reps = reps;
        self.fb_pos = reps[..n].iter().fold(1.0f32, |a, r| a.max(r.pos)).min(cap);
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            self.clock = self.clock.wrapping_add(1);
            // Feedback, through the loop filters and the drive.
            let mut fb = [0.0f32; 2];
            for (c, line) in self.lines.iter().enumerate() {
                let x = line.tap_linear(self.fb_pos);
                let x = self.lp[c].tick(self.bell[c].tick(self.hp[c].tick(x, 0), 0), 0);
                fb[c] = if self.drive > 0.0 {
                    let k = 1.0 + 8.0 * self.drive;
                    fast_tanh(k * x) / k.min(fast_tanh(k).max(1e-3) * k)
                } else {
                    x
                };
            }
            let (xl, xr) = (*l, *r);
            self.lines[0].push(xl + self.fb * fb[0]);
            self.lines[1].push(xr + self.fb * fb[1]);
            let (mut ol, mut or) = (0.0, 0.0);
            let (s, c) = (self.pan_phase * TAU).sin_cos();
            let (apl, apr) = if self.pan_depth > 0.0 {
                (1.0 - self.pan_depth * (0.5 + 0.5 * s), 1.0 - self.pan_depth * (0.5 + 0.5 * c))
            } else {
                (1.0, 1.0)
            };
            for rep in &self.reps[..self.count] {
                ol += rep.gl * self.lines[0].tap_linear(rep.pos);
                or += rep.gr * self.lines[1].tap_linear(rep.pos);
            }
            self.pan_phase += self.pan_step;
            if self.pan_phase >= 1.0 {
                self.pan_phase -= 1.0;
            }
            *l = ol * apl;
            *r = or * apr;
        }
    }

    fn reset(&mut self) {
        for l in &mut self.lines {
            l.clear();
        }
        for b in self.hp.iter_mut().chain(self.bell.iter_mut()).chain(self.lp.iter_mut()) {
            b.reset();
        }
        self.pan_phase = 0.0;
        self.tap_start = None;
        self.ntaps = 0;
    }
}
