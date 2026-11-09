//! Stereo Tap Delay, 2-Tap Delay, Bucket Brigade Delay and Echo.
//!
//! Stated (guide, panel pictures): Stereo Tap and Bucket Brigade up to 2.7 s, BPM or
//! ms, whole to 1/16 notes dotted and triplet; 2-Tap 5-1360 ms in 1 ms steps, LF Cut
//! 20 Hz-8 kHz, HF Cut 400 Hz-20 kHz (24 dB/oct); Echo: stereo in, mono out, Input
//! and Echo Volume +-15 dB, Mode 1-7 (three heads, 4-7 combinations), Intensity
//! oscillating past 75 %.
//!
//! Sidecar's reading: everything else. Scatter crosses each repeat to the other
//! side and Ping-Pong does so fully and feeds one side only; the feedback filter is
//! a low-pass of the panel's Slope; Stereo Tap's input filters take the 2-Tap's
//! ranges; the Echo's head speeds, head spacing 1:2:3 and Bass and Treble ranges;
//! Bucket Brigade's Dist choices. The console's Global Tap has no source in Sidecar
//! yet, so a delay set to GLOBAL takes its own GLOBAL BPM control, which a shared
//! tempo can drive later.

use super::{ch, p, pct, poles_of, tog, Cut24, Slope, LIN, LOG, SLOPES};
use crate::biquad::{Biquad, BiquadCoeffs};
use crate::model::{ParamSpec, Params};
use crate::wing::fx::blocks::{coef, fast_tanh, soft_clip, DelayLine, Lfo, Noise, OnePole, Smoothed, Svf, Wave};
use crate::wing::fx::delay::XfadeTap;
use crate::wing::fx::modulation::shared::{loop_limit, ms, CutFilter, Glide};
use crate::wing::fx::{FxAlgo, FxContext};
use std::f32::consts::TAU;

const UNITS: &[&str] = &["MS", "BPM"];
const TAP_SRC: &[&str] = &["LOCAL", "GLOBAL"];
const NOTES: &[&str] =
    &["1/1", "1/1.", "1/1T", "1/2", "1/2.", "1/2T", "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T"];
const FADE_MS: f32 = 50.0;

/// Length in quarter notes of `NOTES[i]`.
fn note_beats(i: usize) -> f32 {
    [4.0, 2.0, 1.0, 0.5, 0.25][(i / 3).min(4)] * [1.0, 1.5, 2.0 / 3.0][i % 3]
}

/// A delay time: in ms, or the note against the local or global tempo.
fn delay_time_ms(p: &Params, time: &str, note: &str) -> f32 {
    if p.choice("unit") == 1 {
        let bpm = if p.choice("tap") == 1 { p.f("gbpm") } else { p.f("bpm") };
        60_000.0 / bpm.max(1.0) * note_beats(p.choice(note))
    } else {
        p.f(time)
    }
}

/// Stereo Tap Delay.
pub(super) static TAP: [ParamSpec; 17] = [
    ch("unit", "UNIT", UNITS, 0.0),
    p("time_l", "LEFT TIME", "ms", LOG, 1.0, 2700.0, 301, 375.0),
    p("time_r", "RIGHT TIME", "ms", LOG, 1.0, 2700.0, 301, 500.0),
    ch("note_l", "LEFT NOTE", NOTES, 6.0),
    ch("note_r", "RIGHT NOTE", NOTES, 9.0),
    tog("link", "LINK", 0.0),
    p("bpm", "BPM", "", LIN, 20.0, 300.0, 281, 120.0),
    ch("tap", "TAP", TAP_SRC, 0.0),
    p("gbpm", "GLOBAL BPM", "", LIN, 20.0, 300.0, 281, 120.0),
    p("lc", "INPUT HP", "Hz", LOG, 20.0, 8000.0, 101, 20.0),
    p("hc", "INPUT LP", "Hz", LOG, 400.0, 20000.0, 101, 20000.0),
    p("fbf", "FB FILTER", "Hz", LOG, 200.0, 20000.0, 101, 8000.0),
    ch("fbs", "FB SLOPE", SLOPES, 1.0),
    p("fb", "FEEDBACK", "%", LIN, 0.0, 100.0, 101, 30.0),
    p("scat", "SCATTER", "%", LIN, 0.0, 100.0, 101, 0.0),
    tog("pp", "PING-PONG", 0.0),
    p("width", "WIDTH", "%", LIN, 0.0, 100.0, 101, 100.0),
];

/// 2-Tap Delay (left out of the factory library since V1.5, kept here).
pub(super) static TWO_TAP: [ParamSpec; 10] = [
    p("time_l", "LEFT TIME", "ms", LIN, 5.0, 1360.0, 1356, 250.0),
    p("time_r", "RIGHT TIME", "ms", LIN, 5.0, 1360.0, 1356, 375.0),
    tog("link", "LINK", 0.0),
    p("lc", "LF CUT", "Hz", LOG, 20.0, 8000.0, 101, 20.0),
    p("hc", "HF CUT", "Hz", LOG, 400.0, 20000.0, 101, 20000.0),
    p("fbf", "FB FILTER", "Hz", LOG, 200.0, 20000.0, 101, 8000.0),
    ch("fbs", "FB SLOPE", SLOPES, 1.0),
    p("fb", "FEEDBACK", "%", LIN, 0.0, 100.0, 101, 30.0),
    p("scat", "SCATTER", "%", LIN, 0.0, 100.0, 101, 0.0),
    p("width", "WIDTH", "%", LIN, 0.0, 100.0, 101, 100.0),
];

/// Two delay lines, one per side, whose repeats cross by SCATTER.
pub(super) struct DualDelay {
    sr: f32,
    lines: [DelayLine; 2],
    taps: [XfadeTap; 2],
    max_ms: f32,
    fb: Smoothed,
    cross: Smoothed,
    width: Smoothed,
    input: Cut24,
    loop_lp: Slope,
    pingpong: bool,
    primed: bool,
}

impl DualDelay {
    pub(super) fn new(max_ms: f32, ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let len = ms(max_ms + 20.0, sr) as usize + 4;
        Self {
            sr,
            lines: [DelayLine::new(len), DelayLine::new(len)],
            taps: [XfadeTap::new(FADE_MS, sr); 2],
            max_ms,
            fb: Smoothed::new(0.3, 30.0, sr),
            cross: Smoothed::new(0.0, 30.0, sr),
            width: Smoothed::new(1.0, 30.0, sr),
            input: Cut24::new(sr),
            loop_lp: Slope::new(false),
            pingpong: false,
            primed: false,
        }
    }
}

impl FxAlgo for DualDelay {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        let left = delay_time_ms(p, "time_l", "note_l");
        let right = if p.on("link") { left } else { delay_time_ms(p, "time_r", "note_r") };
        let max = self.lines[0].max_delay() - 1;
        let tap = |t: f32| (ms(t.min(self.max_ms).max(0.0), sr).round() as usize).saturating_sub(1).min(max);
        self.taps[0].set(tap(left));
        self.taps[1].set(tap(right));
        self.input.set(p.f("lc"), p.f("hc"));
        self.loop_lp.set(p.f("fbf"), poles_of(p.choice("fbs")), sr);
        self.fb.target = pct(p.f("fb"));
        self.pingpong = p.on("pp");
        self.cross.target = if self.pingpong { 1.0 } else { pct(p.f("scat")) };
        self.width.target = pct(p.f("width"));
        if !self.primed {
            self.primed = true;
            for t in &mut self.taps {
                t.snap();
            }
            for s in [&mut self.fb, &mut self.cross, &mut self.width] {
                s.snap(s.target);
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let (mut xl, mut xr) = (self.input.tick(0, *l), self.input.tick(1, *r));
            if self.pingpong {
                xl = 0.5 * (xl + xr);
                xr = 0.0;
            }
            let yl = self.taps[0].read(&self.lines[0]);
            let yr = self.taps[1].read(&self.lines[1]);
            let g = self.fb.tick();
            let c = self.cross.tick();
            let fl = g * self.loop_lp.tick(0, yl);
            let fr = g * self.loop_lp.tick(1, yr);
            self.lines[0].push(loop_limit(xl + fl + c * (fr - fl)));
            self.lines[1].push(loop_limit(xr + fr + c * (fl - fr)));
            let w = self.width.tick();
            let m = 0.5 * (yl + yr);
            let s = 0.5 * (yl - yr) * w;
            *l = m + s;
            *r = m - s;
        }
    }

    fn reset(&mut self) {
        for l in &mut self.lines {
            l.clear();
        }
        for t in &mut self.taps {
            t.snap();
        }
        self.input.reset();
        self.loop_lp.reset();
        for s in [&mut self.fb, &mut self.cross, &mut self.width] {
            s.snap(s.target);
        }
    }
}

// ---- Bucket Brigade -------------------------------------------------------------------

const DIST: &[&str] = &["OFF", "SOFT", "TAPE", "HARD"];

/// Bucket Brigade Delay.
pub(super) static BBD: [ParamSpec; 12] = [
    ch("unit", "UNIT", UNITS, 0.0),
    p("time", "TIME", "ms", LOG, 1.0, 2700.0, 301, 350.0),
    ch("note", "NOTE", NOTES, 6.0),
    p("bpm", "BPM", "", LIN, 20.0, 300.0, 281, 120.0),
    ch("tap", "TAP", TAP_SRC, 0.0),
    p("gbpm", "GLOBAL BPM", "", LIN, 20.0, 300.0, 281, 120.0),
    p("hp", "INPUT HP", "Hz", LOG, 20.0, 2000.0, 101, 60.0),
    p("ldamp", "LOW DAMP", "Hz", LOG, 20.0, 2000.0, 101, 80.0),
    p("hdamp", "HIGH DAMP", "Hz", LOG, 500.0, 20000.0, 101, 3000.0),
    ch("fbs", "FB SLOPE", SLOPES, 1.0),
    ch("dist", "DIST", DIST, 1.0),
    p("fb", "FEEDBACK", "%", LIN, 0.0, 100.0, 101, 40.0),
];

/// The chips' bandwidth follows the clock: as the WING's BBD-DL (9 kHz at 20 ms).
const BW_HZ: f32 = 9000.0;
const BW_EXP: f32 = 0.42;
const COMP_ATTACK_MS: f32 = 5.0;
const COMP_RELEASE_MS: f32 = 80.0;

/// The BBD pedal of the WING's BBD-DL with the guide's controls: the time glides
/// (a bend, as the clock changes), the bandwidth follows the clock, every repeat is
/// damped at both ends through the slope filters and Dist picks what the loop does
/// to the signal.
pub(super) struct BucketBrigade {
    sr: f32,
    lines: [DelayLine; 2],
    time: Glide,
    fb: Smoothed,
    input: CutFilter,
    low: Slope,
    high: Slope,
    aa: [Svf; 2],
    rc: [Svf; 2],
    bw: f32,
    env: [f32; 2],
    att: f32,
    rel: f32,
    dist: usize,
    primed: bool,
}

impl BucketBrigade {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let len = ms(2710.0, sr) as usize + 4;
        let mut dev = Self {
            sr,
            lines: [DelayLine::new(len), DelayLine::new(len)],
            time: Glide::new(150.0, 0.5, sr),
            fb: Smoothed::new(0.4, 30.0, sr),
            input: CutFilter::new(sr),
            low: Slope::new(true),
            high: Slope::new(false),
            aa: [Svf::default(); 2],
            rc: [Svf::default(); 2],
            bw: 0.0,
            env: [0.0; 2],
            att: coef(COMP_ATTACK_MS, sr),
            rel: coef(COMP_RELEASE_MS, sr),
            dist: 1,
            primed: false,
        };
        dev.time.target = ms(20.0, sr);
        dev.time.snap();
        dev.track_clock();
        dev
    }

    fn track_clock(&mut self) {
        let delay_ms = (self.time.value * 1000.0 / self.sr).max(1.0);
        let hz = (BW_HZ * (20.0 / delay_ms).powf(BW_EXP)).clamp(250.0, self.sr * 0.45);
        if (hz / self.bw - 1.0).abs() > 0.005 {
            self.bw = hz;
            for f in self.aa.iter_mut().chain(self.rc.iter_mut()) {
                f.set(hz, 0.7, self.sr);
            }
        }
    }
}

impl FxAlgo for BucketBrigade {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        self.time.target = ms(delay_time_ms(p, "time", "note").clamp(1.0, 2700.0), sr);
        self.fb.target = pct(p.f("fb"));
        self.input.set(p.f("hp"), 24_000.0);
        let poles = poles_of(p.choice("fbs"));
        self.low.set(p.f("ldamp"), poles, sr);
        self.high.set(p.f("hdamp"), poles, sr);
        self.dist = p.choice("dist").min(3);
        if !self.primed {
            self.primed = true;
            self.time.snap();
            self.fb.snap(self.fb.target);
            self.track_clock();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        self.track_clock();
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let d = self.time.tick();
            let fb = self.fb.tick();
            for (c, x) in [l, r].into_iter().enumerate() {
                let y = self.rc[c].tick(self.lines[c].tap_hermite(d)).lp;
                let back = self.high.tick(c, self.low.tick(c, y));
                let v = self.input.tick(c, *x) + fb * back;
                let a = v.abs();
                let e = &mut self.env[c];
                *e += (a - *e) * if a > *e { self.att } else { self.rel };
                let shaped = match self.dist {
                    0 => loop_limit(v),
                    1 => {
                        let g = 1.0 + 0.5 * *e;
                        g * fast_tanh(1.2 * v / g) / 1.2
                    }
                    2 => fast_tanh(1.6 * v) / 1.6,
                    _ => soft_clip(1.4 * v) / 1.4,
                };
                self.lines[c].push(self.aa[c].tick(shaped).lp);
                *x = y;
            }
        }
    }

    fn reset(&mut self) {
        for l in &mut self.lines {
            l.clear();
        }
        for f in self.aa.iter_mut().chain(self.rc.iter_mut()) {
            f.reset();
        }
        self.input.reset();
        self.low.reset();
        self.high.reset();
        self.env = [0.0; 2];
        self.time.snap();
        self.fb.snap(self.fb.target);
        self.track_clock();
    }
}

// ---- Echo -----------------------------------------------------------------------------

const ECHO_MODES: &[&str] = &["1", "2", "3", "4", "5", "6", "7"];

/// Echo: a tape loop with three playback heads.
pub(super) static ECHO: [ParamSpec; 8] = [
    p("in", "INPUT", "dB", LIN, -15.0, 15.0, 61, 0.0),
    p("noise", "NOISE", "%", LIN, 0.0, 100.0, 101, 0.0),
    ch("mode", "MODE", ECHO_MODES, 0.0),
    p("bass", "BASS", "dB", LIN, -12.0, 12.0, 49, 0.0),
    p("treble", "TREBLE", "dB", LIN, -12.0, 12.0, 49, 0.0),
    p("rate", "REPEAT RATE", "%", LIN, 0.0, 100.0, 101, 40.0),
    p("int", "INTENSITY", "%", LIN, 0.0, 100.0, 101, 40.0),
    p("vol", "ECHO VOLUME", "dB", LIN, -15.0, 15.0, 61, 0.0),
];

/// Longest and shortest delay of the first head, ms (Sidecar's reading).
const HEAD_MS: (f32, f32) = (170.0, 40.0);
/// Which of the heads a mode plays: 1-3 one head each, then the combinations.
const HEADS: [[bool; 3]; 7] = [
    [true, false, false],
    [false, true, false],
    [false, false, true],
    [true, true, false],
    [false, true, true],
    [true, false, true],
    [true, true, true],
];

/// Wow and flutter: rate (Hz) and pitch deviation (a fixed tape's).
const WOW: (f32, f32) = (0.7, 0.002);
const FLUTTER: (f32, f32) = (6.3, 0.001);
/// Tape's overload level.
const HEADROOM: f32 = 1.3;

/// Echo: one tape, three heads at 1:2:3 of the first head's delay; each head that
/// the Mode plays goes to the output and, by Intensity, back onto the tape. The
/// loop gain is unity at Intensity 75 % (the guide: it oscillates past that),
/// the tape saturation keeps the oscillation bounded.
pub(super) struct TapeEcho {
    sr: f32,
    line: DelayLine,
    time: Glide,
    wow: Lfo,
    flutter: Lfo,
    depth: [f32; 2],
    heads: [Smoothed; 3],
    intensity: Smoothed,
    input: Smoothed,
    volume: Smoothed,
    noise: Smoothed,
    rng: Noise,
    hiss: OnePole,
    head_lp: Svf,
    head_hp: OnePole,
    bass: Biquad,
    treble: Biquad,
    primed: bool,
}

impl TapeEcho {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let lfo = |hz: f32, phase: f32| {
            let mut l = Lfo::default();
            l.set_rate(hz, sr);
            l.reset(phase);
            l
        };
        let swing = |(hz, dev): (f32, f32)| dev / (TAU * hz) * sr;
        let mut head_lp = Svf::default();
        head_lp.set(4200.0, 0.6, sr);
        let s = |v: f32| Smoothed::new(v, 30.0, sr);
        Self {
            sr,
            line: DelayLine::new(ms(3.0 * HEAD_MS.0 + 20.0, sr) as usize),
            time: Glide::new(250.0, 0.3, sr),
            wow: lfo(WOW.0, 0.0),
            flutter: lfo(FLUTTER.0, 0.3),
            depth: [swing(WOW), swing(FLUTTER)],
            heads: [s(0.0), s(0.0), s(0.0)],
            intensity: s(0.5),
            input: s(1.0),
            volume: s(1.0),
            noise: s(0.0),
            rng: Noise::default(),
            hiss: OnePole::new(4000.0, sr),
            head_lp,
            head_hp: OnePole::new(90.0, sr),
            bass: Biquad::default(),
            treble: Biquad::default(),
            primed: false,
        }
    }

    fn smoothers(&mut self) -> [&mut Smoothed; 7] {
        let [a, b, c] = &mut self.heads;
        [a, b, c, &mut self.intensity, &mut self.input, &mut self.volume, &mut self.noise]
    }
}

impl FxAlgo for TapeEcho {
    fn set_params(&mut self, p: &Params) {
        let sr = f64::from(self.sr);
        let (slow, fast) = HEAD_MS;
        self.time.target = ms(slow * (fast / slow).powf(pct(p.f("rate"))), self.sr);
        let on = HEADS[p.choice("mode").min(6)];
        let gain = 1.0 / (on.iter().filter(|&&h| h).count().max(1) as f32).sqrt();
        for (s, &h) in self.heads.iter_mut().zip(&on) {
            s.target = if h { gain } else { 0.0 };
        }
        self.intensity.target = p.f("int") / 75.0;
        self.input.target = 10f32.powf(p.f("in") * 0.05);
        self.volume.target = 10f32.powf(p.f("vol") * 0.05);
        // NOISE: tape hiss, -38 dB at 100 %.
        self.noise.target = pct(p.f("noise")) * 0.012;
        self.bass.set_coefficients(BiquadCoeffs::low_shelf(sr, 250.0, 0.7, f64::from(p.f("bass"))));
        self.treble.set_coefficients(BiquadCoeffs::high_shelf(sr, 3000.0, 0.7, f64::from(p.f("treble"))));
        if !self.primed {
            self.primed = true;
            self.time.snap();
            for s in self.smoothers() {
                s.snap(s.target);
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let [dw, df] = self.depth;
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let input = self.input.tick();
            let mono = 0.5 * (*l + *r) * input;
            let wobble = dw * self.wow.next(Wave::Sine, 0.0) + df * self.flutter.next(Wave::Sine, 0.0);
            let d = self.time.tick() + wobble;
            let mut y = 0.0;
            for (k, h) in self.heads.iter_mut().enumerate() {
                let g = h.tick();
                if g > 0.0 {
                    y += g * self.line.tap_hermite(d * (k + 1) as f32);
                }
            }
            let y = self.head_hp.hp(self.head_lp.tick(y).lp);
            let y = self.treble.tick(self.bass.tick(y, 0), 0);
            let n = self.noise.tick();
            let y = y + if n > 0.0 { n * self.hiss.lp(self.rng.sample()) * 4.0 } else { 0.0 };
            let fb = self.intensity.tick();
            let rec = HEADROOM * fast_tanh((mono + fb * y) / HEADROOM);
            self.line.push(rec);
            let v = self.volume.tick();
            *l = v * y;
            *r = v * y;
        }
    }

    fn reset(&mut self) {
        self.line.clear();
        self.head_lp.reset();
        self.head_hp.reset();
        self.hiss.reset();
        self.bass.reset();
        self.treble.reset();
        self.wow.reset(0.0);
        self.flutter.reset(0.3);
        self.time.snap();
        for s in self.smoothers() {
            s.snap(s.target);
        }
        // A reset starts silent: hiss fades up from nothing.
        self.noise.snap(0.0);
    }
}
