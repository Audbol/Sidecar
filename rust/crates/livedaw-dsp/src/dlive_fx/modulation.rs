//! ADT Doubler, Chorus, Symphonic Chorus, Flanger, MOO 12 Stage Phaser and Rotator.
//!
//! Stated (the guide's panels): the Chorus's stereo-field switches, the Flanger's
//! three types and five LFO choices, the Phaser's stage controls, Mix Resonance to
//! 10, LFO Rate 0.1-10 Hz and four LFO shapes, the Rotator's Speed from 0.1 Hz and
//! Drive 1-11. Everything else, and every algorithm, is Sidecar's reading: which
//! delay times and depths each model uses; the Chorus's and ADT's voice layouts; the
//! Flanger's voicing (the SQ's: Ambient long and gentle, Vintage in between, Wild
//! short and fed back harder); the Phaser's LFO shapes (Sine, Triangle, Square,
//! Ramp) and resonance as feedback from the last of the Resonant Stages; the
//! Rotator's horn and drum (the WING Rotary's design) with the guide's controls.

use super::{ch, p, pct, tog, LIN, LOG};
use crate::model::{ParamSpec, Params};
use crate::wing::fx::blocks::{coef, fast_tanh, shape, DelayLine, OnePole, Smoothed, Svf, Wave};
use crate::wing::fx::modulation::shared::{advance, loop_limit, ms, tri_sine_square, Morph};
use crate::wing::fx::readout::{LFO_L, LFO_R, NEEDLE};
use crate::wing::fx::{FxAlgo, FxContext, FX_READOUTS};
use std::f32::consts::{FRAC_1_SQRT_2, FRAC_PI_2, PI, TAU};

/// Equal-power gains (L, R) for a pan of -1..1.
#[inline]
fn pan_gains(pan: f32) -> (f32, f32) {
    let (s, c) = ((pan.clamp(-1.0, 1.0) + 1.0) * 0.5 * FRAC_PI_2).sin_cos();
    (c, s)
}

// ---- ADT Doubler ----------------------------------------------------------------------

/// ADT Doubler: voices tracked on tape a little late: Delay Separation places them,
/// Thickness moves them about.
pub(super) static ADT: [ParamSpec; 7] = [
    p("sep", "DELAY SEPARATION", "ms", LIN, 0.0, 100.0, 101, 30.0),
    p("thick", "THICKNESS", "%", LIN, 0.0, 100.0, 101, 40.0),
    ch("mode", "MODE", &["DOUBLE", "QUAD"], 0.0),
    tog("wide", "WIDE", 1.0),
    tog("auto", "AUTOPAN", 0.0),
    p("adep", "AUTOPAN DEPTH", "%", LIN, 0.0, 100.0, 101, 50.0),
    p("aspd", "AUTOPAN SPEED", "Hz", LOG, 0.05, 10.0, 101, 1.0),
];

const ADT_DELAY: [f32; 4] = [1.0, 1.37, 1.71, 2.13];
const ADT_RATE: [f32; 4] = [0.37, 0.53, 0.29, 0.47];
const ADT_PAN: [f32; 4] = [-1.0, 1.0, -0.5, 0.5];
/// Thickness's largest wander of a voice's timing, ms.
const ADT_WANDER_MS: f32 = 3.0;

pub(super) struct Adt {
    sr: f32,
    lines: [DelayLine; 2],
    sep: Smoothed,
    thick: Smoothed,
    quad: Smoothed,
    wide: Smoothed,
    pan: Smoothed,
    phase: f32,
    inc: f32,
    wander: [f32; 4],
    wander_inc: [f32; 4],
    primed: bool,
}

impl Adt {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let len = ms(100.0 * ADT_DELAY[3] + ADT_WANDER_MS * 2.0 + 2.0, sr) as usize + 8;
        let s = |v: f32, t: f32| Smoothed::new(v, t, sr);
        Self {
            sr,
            lines: [DelayLine::new(len), DelayLine::new(len)],
            sep: s(ms(30.0, sr), 120.0),
            thick: s(0.4, 30.0),
            quad: s(0.0, 30.0),
            wide: s(1.0, 30.0),
            pan: s(0.0, 30.0),
            phase: 0.0,
            inc: 0.0,
            wander: [0.0; 4],
            wander_inc: ADT_RATE.map(|r| r / sr),
            primed: false,
        }
    }

    fn smoothers(&mut self) -> [&mut Smoothed; 5] {
        [&mut self.sep, &mut self.thick, &mut self.quad, &mut self.wide, &mut self.pan]
    }
}

impl FxAlgo for Adt {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        self.sep.target = ms(p.f("sep"), sr);
        self.thick.target = pct(p.f("thick"));
        self.quad.target = f32::from(u8::from(p.choice("mode") == 1));
        self.wide.target = if p.on("wide") { 1.0 } else { 0.35 };
        self.pan.target = if p.on("auto") { pct(p.f("adep")) } else { 0.0 };
        self.inc = (p.f("aspd") / sr).min(0.5);
        if !self.primed {
            self.primed = true;
            for s in self.smoothers() {
                s.snap(s.target);
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let (dl, dr) = (*l, *r);
            self.lines[0].push(dl);
            self.lines[1].push(dr);
            advance(&mut self.phase, self.inc);
            let (sep, thick, quad, wide, pan_depth) =
                (self.sep.tick(), self.thick.tick(), self.quad.tick(), self.wide.tick(), self.pan.tick());
            let mut out = [0.0f32; 2];
            for v in 0..4 {
                advance(&mut self.wander[v], self.wander_inc[v]);
                let g = if v < 2 { 1.0 } else { quad };
                if g <= 0.0 {
                    continue;
                }
                let wob = (TAU * self.wander[v]).sin() * thick * ADT_WANDER_MS * 0.001 * self.sr;
                let y = self.lines[v & 1].tap_hermite(sep * ADT_DELAY[v] + wob + 2.0);
                let auto = pan_depth * (TAU * (self.phase + 0.25 * v as f32)).sin();
                let (gl, gr) = pan_gains(ADT_PAN[v] * wide + auto);
                out[0] += g * gl * y;
                out[1] += g * gr * y;
            }
            let norm = 1.0 / (2.0 + 2.0 * quad).sqrt();
            *l = out[0] * norm;
            *r = out[1] * norm;
        }
    }

    fn reset(&mut self) {
        for l in &mut self.lines {
            l.clear();
        }
        self.phase = 0.0;
        self.wander = [0.0; 4];
        for s in self.smoothers() {
            s.snap(s.target);
        }
    }

    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        out[LFO_L] = self.phase;
    }
}

// ---- Chorus and Symphonic Chorus ------------------------------------------------------

/// Chorus: the panel's stereo-field switches, Rate, Depth, the LFO's wave and Pan
/// Depth (the guide's "Sine or Rectified").
pub(super) static CHORUS: [ParamSpec; 7] = [
    tog("split", "LFO SPLIT", 1.0),
    tog("wide", "WIDE", 1.0),
    tog("mv", "MULTIVOICE", 0.0),
    p("rate", "RATE", "Hz", LOG, 0.05, 5.0, 101, 0.5),
    p("depth", "DEPTH", "%", LIN, 0.0, 100.0, 101, 30.0),
    ch("wave", "LFO", &["SINE", "RECTIFIED"], 0.0),
    p("pan", "PAN DEPTH", "%", LIN, 0.0, 100.0, 101, 0.0),
];

/// Symphonic Chorus: Frequency and Depth.
pub(super) static SYMPHONIC: [ParamSpec; 2] =
    [p("freq", "FREQUENCY", "Hz", LOG, 0.1, 5.0, 101, 0.6), p("depth", "DEPTH", "%", LIN, 0.0, 100.0, 101, 40.0)];

/// Each voice's centre delay, ms.
const VOICE_MS: [f32; 4] = [7.3, 11.9, 17.1, 23.7];
/// Depth 100 % sweeps a voice this far around its delay, ms.
const SWEEP_MS: f32 = 5.0;
/// A second LFO, this many times the first, thickens the Symphonic Chorus.
const SECOND_LFO: f32 = 1.73;

/// Sine, or the sine rectified (`rect` blends towards it).
#[inline]
fn chorus_lfo(phase: f32, rect: f32) -> f32 {
    let s = (TAU * phase).sin();
    s + (2.0 * s.abs() - 1.0 - s) * rect
}

/// Four-voice chorus: the Chorus's two (four with MultiVoice) or the Symphonic's
/// four with a second, faster LFO on top.
pub(super) struct ChorusVoice {
    sr: f32,
    symphonic: bool,
    lines: [DelayLine; 2],
    phase: f32,
    phase2: f32,
    inc: f32,
    depth: Smoothed,
    split: Smoothed,
    wide: Smoothed,
    voices: Smoothed,
    pan: Smoothed,
    rect: Smoothed,
    primed: bool,
}

impl ChorusVoice {
    pub(super) fn new(symphonic: bool, ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let len = ms(VOICE_MS[3] + SWEEP_MS + 2.0, sr) as usize + 8;
        let s = |v: f32, t: f32| Smoothed::new(v, t, sr);
        Self {
            sr,
            symphonic,
            lines: [DelayLine::new(len), DelayLine::new(len)],
            phase: 0.0,
            phase2: 0.0,
            inc: 0.0,
            depth: s(ms(1.5, sr), 60.0),
            split: s(0.0, 60.0),
            wide: s(0.0, 30.0),
            voices: s(0.0, 30.0),
            pan: s(0.0, 30.0),
            rect: s(0.0, 30.0),
            primed: false,
        }
    }

    fn smoothers(&mut self) -> [&mut Smoothed; 6] {
        [&mut self.depth, &mut self.split, &mut self.wide, &mut self.voices, &mut self.pan, &mut self.rect]
    }
}

impl FxAlgo for ChorusVoice {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        if self.symphonic {
            self.inc = (p.f("freq") / sr).min(0.5);
            self.split.target = 1.0;
            self.wide.target = 1.0;
            self.voices.target = 1.0;
        } else {
            self.inc = (p.f("rate") / sr).min(0.5);
            self.split.target = f32::from(u8::from(p.on("split")));
            self.wide.target = f32::from(u8::from(p.on("wide")));
            self.voices.target = f32::from(u8::from(p.on("mv")));
            self.pan.target = pct(p.f("pan"));
            self.rect.target = f32::from(u8::from(p.choice("wave") == 1));
        }
        self.depth.target = ms(pct(p.f("depth")) * SWEEP_MS, sr);
        if !self.primed {
            self.primed = true;
            for s in self.smoothers() {
                s.snap(s.target);
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            self.lines[0].push(*l);
            self.lines[1].push(*r);
            advance(&mut self.phase, self.inc);
            advance(&mut self.phase2, self.inc * SECOND_LFO);
            let (depth, split, wide, voices, pan_depth, rect) =
                (self.depth.tick(), self.split.tick(), self.wide.tick(), self.voices.tick(), self.pan.tick(), self.rect.tick());
            let mut out = [0.0f32; 2];
            for (v, &base) in VOICE_MS.iter().enumerate() {
                let g = if v < 2 { 1.0 } else { voices };
                if g <= 0.0 {
                    continue;
                }
                let at = self.phase + 0.25 * v as f32;
                let lfo_c = |c: usize, phase: f32| {
                    let m = chorus_lfo(phase + 0.25 * c as f32 * split, rect);
                    if self.symphonic {
                        0.7 * m + 0.3 * (TAU * (self.phase2 + 0.25 * (v + c) as f32)).sin()
                    } else {
                        m
                    }
                };
                let pan = pan_depth * chorus_lfo(at + 0.5, rect);
                for (c, o) in out.iter_mut().enumerate() {
                    let d = ms(base, self.sr) + depth * lfo_c(c, at);
                    let y = self.lines[c].tap_hermite(d);
                    *o += g * y * if c == 0 { 1.0 - 0.5 * pan } else { 1.0 + 0.5 * pan };
                }
            }
            let norm = 1.0 / (2.0 + 2.0 * voices).sqrt();
            let m = 0.5 * (out[0] + out[1]);
            let s = 0.5 * (out[0] - out[1]) * (0.5 + wide);
            *l = (m + s) * norm;
            *r = (m - s) * norm;
        }
    }

    fn reset(&mut self) {
        for l in &mut self.lines {
            l.clear();
        }
        self.phase = 0.0;
        self.phase2 = 0.0;
        for s in self.smoothers() {
            s.snap(s.target);
        }
    }

    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        out[LFO_L] = self.phase;
        out[LFO_R] = (self.phase + 0.25 * self.split.value).rem_euclid(1.0);
    }
}

// ---- Flanger ---------------------------------------------------------------------------

const FLANGER_TYPES: &[&str] = &["AMBIENT", "VINTAGE", "WILD"];
const FLANGER_LFO: &[&str] = &["MANUAL", "SINE", "RECTIFIED", "INV RECTIFIED", "TRIANGLE"];

/// Flanger: Types Ambient, Vintage, Wild, Stereo Split and Spread, LFO Manual, Sine,
/// Rectified, Inverted Rectified, Triangle, Speed, Depth, Regenerate.
pub(super) static FLANGER: [ParamSpec; 7] = [
    ch("type", "TYPE", FLANGER_TYPES, 1.0),
    p("split", "STEREO SPLIT", "deg", LIN, 0.0, 180.0, 181, 90.0),
    p("spread", "STEREO SPREAD", "%", LIN, 0.0, 100.0, 101, 50.0),
    ch("lfo", "LFO", FLANGER_LFO, 4.0),
    p("speed", "SPEED", "Hz", LOG, 0.05, 5.0, 101, 0.3),
    p("depth", "DEPTH", "%", LIN, 0.0, 100.0, 101, 60.0),
    p("regen", "REGENERATE", "%", LIN, 0.0, 100.0, 101, 40.0),
];

/// Sweep at 100 % depth: +-2 octaves of delay around the type's centre.
const FLANGE_OCT: f32 = 2.0;

pub(super) struct Flanger {
    sr: f32,
    lines: [DelayLine; 2],
    phase: f32,
    inc: f32,
    wave: usize,
    /// Centre delay in samples.
    base: Smoothed,
    depth: Smoothed,
    split: Smoothed,
    spread: Smoothed,
    regen: Smoothed,
    fb_lp: [OnePole; 2],
    damp: f32,
    primed: bool,
}

impl Flanger {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let len = ms(10.0 * FLANGE_OCT.exp2() * 1.4 + 2.0, sr) as usize + 8;
        let s = |v: f32, t: f32| Smoothed::new(v, t, sr);
        Self {
            sr,
            lines: [DelayLine::new(len), DelayLine::new(len)],
            phase: 0.0,
            inc: 0.0,
            wave: 4,
            base: s(ms(4.2, sr), 80.0),
            depth: s(0.6, 40.0),
            split: s(0.25, 80.0),
            spread: s(0.5, 30.0),
            regen: s(0.3, 30.0),
            fb_lp: [OnePole::new(9000.0, sr); 2],
            damp: 9000.0,
            primed: false,
        }
    }

    fn smoothers(&mut self) -> [&mut Smoothed; 5] {
        [&mut self.base, &mut self.depth, &mut self.split, &mut self.spread, &mut self.regen]
    }

    /// The LFO's value at `phase`, -1..1.
    #[inline]
    fn lfo(&self, phase: f32) -> f32 {
        match self.wave {
            1 => (TAU * phase).sin(),
            2 => 2.0 * (PI * phase).sin().abs() - 1.0,
            3 => 1.0 - 2.0 * (PI * phase).sin().abs(),
            _ => shape(Wave::Triangle, phase),
        }
    }
}

impl FxAlgo for Flanger {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        let (delay_ms, feed, damp) = match p.text("type") {
            "AMBIENT" => (8.0, 0.7, 6000.0),
            "WILD" => (2.0, 1.0, 16000.0),
            _ => (4.2, 0.85, 9000.0),
        };
        self.base.target = ms(delay_ms, sr);
        self.depth.target = pct(p.f("depth"));
        self.split.target = p.f("split") / 360.0;
        self.spread.target = pct(p.f("spread"));
        self.regen.target = pct(p.f("regen")) * feed * 0.95;
        if damp != self.damp {
            self.damp = damp;
            for f in &mut self.fb_lp {
                f.set(damp, sr);
            }
        }
        self.inc = (p.f("speed") / sr).min(0.5);
        self.wave = p.choice("lfo").min(4);
        if !self.primed {
            self.primed = true;
            for s in self.smoothers() {
                s.snap(s.target);
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            advance(&mut self.phase, self.inc);
            let base = self.base.tick();
            let depth = self.depth.tick();
            let off = self.split.tick();
            let spread = self.spread.tick();
            let fb = self.regen.tick();
            let mut wet = [0.0f32; 2];
            for (c, x) in [&mut *l, &mut *r].into_iter().enumerate() {
                let (b, phase) = if c == 0 { (base, self.phase) } else { (base * (1.0 + 0.4 * spread), self.phase + off) };
                let m = if self.wave == 0 { 2.0 * depth - 1.0 } else { depth * self.lfo(phase) };
                let d = b * (FLANGE_OCT * m).exp2();
                let y = self.lines[c].tap_hermite(d);
                let f = self.fb_lp[c].lp(y);
                self.lines[c].push(loop_limit(*x + fb * f));
                wet[c] = y;
            }
            let m = 0.5 * (wet[0] + wet[1]);
            let s = 0.5 * (wet[0] - wet[1]) * (0.3 + 0.7 * spread);
            *l = m + s;
            *r = m - s;
        }
    }

    fn reset(&mut self) {
        for l in &mut self.lines {
            l.clear();
        }
        for f in &mut self.fb_lp {
            f.reset();
        }
        self.phase = 0.0;
        for s in self.smoothers() {
            s.snap(s.target);
        }
    }

    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        out[LFO_L] = self.phase;
        out[LFO_R] = (self.phase + self.split.value).rem_euclid(1.0);
    }
}

// ---- MOO 12 Stage Phaser --------------------------------------------------------------

const STAGES: &[&str] = &["2", "4", "6", "8", "10", "12"];
const PHASER_LFO: &[&str] = &["SINE", "TRIANGLE", "SQUARE", "RAMP"];

/// MOO 12 Stage Phaser.
pub(super) static PHASER: [ParamSpec; 8] = [
    ch("stg", "PHASING STAGES", STAGES, 5.0),
    ch("res", "RESONANT STAGES", STAGES, 3.0),
    p("mres", "MIX RESONANCE", "", LIN, 0.0, 10.0, 101, 2.0),
    p("rate", "LFO RATE", "Hz", LOG, 0.1, 10.0, 101, 0.8),
    ch("wave", "LFO SHAPE", PHASER_LFO, 0.0),
    ch("stereo", "STEREO", &["MONO", "SPLIT"], 1.0),
    p("depth", "DEPTH", "%", LIN, 0.0, 100.0, 101, 50.0),
    p("offset", "OFFSET", "%", LIN, 0.0, 100.0, 101, 20.0),
];

const MAX_STAGES: usize = 12;
/// OFFSET 0..100 sets the sweep's bottom (the WING Phaser's range), DEPTH its reach.
const RANGE_LO: f32 = 20.0;
const RANGE_OCT: f32 = 8.0;
const DEPTH_OCT: f32 = 4.0;

pub(super) struct MooPhaser {
    sr: f32,
    phase: f32,
    inc: f32,
    wave: usize,
    /// Rounds the square's and the ramp's edges a little.
    sweep: [f32; 2],
    sweep_a: f32,
    bottom: Smoothed,
    depth: Smoothed,
    fb: Smoothed,
    off: Smoothed,
    state: [[f32; MAX_STAGES]; 2],
    last: [f32; 2],
    stages: usize,
    next_stages: usize,
    res: usize,
    morph: Morph,
    primed: bool,
}

impl MooPhaser {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let s = |v: f32, t: f32| Smoothed::new(v, t, sr);
        Self {
            sr,
            phase: 0.0,
            inc: 0.0,
            wave: 0,
            sweep: [0.5; 2],
            sweep_a: coef(2.0, sr),
            bottom: s(8.0, 50.0),
            depth: s(2.0, 50.0),
            fb: s(0.2, 30.0),
            off: s(0.0, 30.0),
            state: [[0.0; MAX_STAGES]; 2],
            last: [0.0; 2],
            stages: 12,
            next_stages: 12,
            res: 8,
            morph: Morph::new(8.0, sr),
            primed: false,
        }
    }

    fn smoothers(&mut self) -> [&mut Smoothed; 4] {
        [&mut self.bottom, &mut self.depth, &mut self.fb, &mut self.off]
    }

    fn clear(&mut self) {
        self.state = [[0.0; MAX_STAGES]; 2];
        self.last = [0.0; 2];
    }

    #[inline]
    fn lfo(&self, phase: f32) -> f32 {
        match self.wave {
            1 => tri_sine_square(phase, -1.0),
            2 => tri_sine_square(phase, 1.0),
            3 => shape(Wave::SawUp, phase),
            _ => tri_sine_square(phase, 0.0),
        }
    }
}

impl FxAlgo for MooPhaser {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        self.inc = (p.f("rate") / sr).min(0.5);
        self.wave = p.choice("wave").min(3);
        self.bottom.target = RANGE_LO.log2() + pct(p.f("offset")) * RANGE_OCT;
        self.depth.target = pct(p.f("depth")) * DEPTH_OCT;
        // MIX RESONANCE 0-10 sets the feedback from the last Resonant Stage.
        self.fb.target = p.f("mres") * 0.09;
        self.off.target = if p.choice("stereo") == 1 { 0.25 } else { 0.0 };
        self.next_stages = (p.choice("stg") + 1) * 2;
        self.res = (p.choice("res") + 1) * 2;
        if !self.primed {
            self.primed = true;
            self.stages = self.next_stages;
            for s in self.smoothers() {
                s.snap(s.target);
            }
        } else if self.next_stages != self.stages {
            self.morph.request();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let (sr, nyq) = (self.sr, self.sr * 0.45);
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            if self.morph.tick() {
                self.stages = self.next_stages;
                self.clear();
            }
            advance(&mut self.phase, self.inc);
            let bottom = self.bottom.tick();
            let depth = self.depth.tick();
            let fb = self.fb.tick();
            let off = self.off.tick();
            let g_out = self.morph.gain();
            let res = self.res.min(self.stages);
            for (c, x) in [l, r].into_iter().enumerate() {
                let w = 0.5 + 0.5 * self.lfo(self.phase + off * c as f32);
                self.sweep[c] += (w - self.sweep[c]) * self.sweep_a;
                let hz = (bottom + depth * self.sweep[c]).exp2().clamp(20.0, nyq);
                let g = (PI * hz / sr).tan();
                let gg = g / (1.0 + g);
                let mut y = loop_limit(*x + fb * self.last[c]);
                let mut tap = 0.0;
                for (i, s) in self.state[c].iter_mut().take(self.stages).enumerate() {
                    let v = (y - *s) * gg;
                    let lp = v + *s;
                    *s = lp + v;
                    y = 2.0 * lp - y;
                    if i + 1 == res {
                        tap = y;
                    }
                }
                self.last[c] = tap;
                *x = y * g_out;
            }
        }
    }

    fn reset(&mut self) {
        self.clear();
        self.stages = self.next_stages;
        self.morph.reset();
        self.phase = 0.0;
        self.sweep = [0.5; 2];
        for s in self.smoothers() {
            s.snap(s.target);
        }
    }

    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        out[LFO_L] = self.phase;
        out[LFO_R] = (self.phase + self.off.value).rem_euclid(1.0);
    }
}

// ---- Rotator ---------------------------------------------------------------------------

/// Rotator: Rotors on/off, Speed from 0.1 Hz (the panel's chorale and tremolo marks
/// are about 0.8 and 6.7 Hz: display only, not stops), Width, Balance, Deflectors,
/// Back Panel, Mic Select with Separation, Tube Drive on/off and amount 1-11, Mains
/// Hum and its frequency.
pub(super) static ROTATOR: [ParamSpec; 12] = [
    tog("on", "ROTORS", 1.0),
    p("spd", "SPEED", "Hz", LOG, 0.1, 10.0, 101, 0.8),
    p("width", "WIDTH", "%", LIN, 0.0, 100.0, 101, 100.0),
    p("bal", "BALANCE", "%", LIN, -100.0, 100.0, 201, 0.0),
    tog("defl", "DEFLECTORS", 1.0),
    tog("back", "BACK PANEL", 0.0),
    ch("mic", "MIC SELECT", &["1", "2", "3"], 0.0),
    p("sep", "SEPARATION", "%", LIN, 0.0, 100.0, 101, 60.0),
    tog("tube", "TUBE DRIVE", 0.0),
    p("drv", "DRIVE", "", LIN, 1.0, 11.0, 101, 3.0),
    p("hum", "MAINS HUM", "%", LIN, 0.0, 100.0, 101, 0.0),
    ch("mains", "MAINS", &["50", "60"], 0.0),
];

const CROSSOVER_HZ: f32 = 800.0;
const DRUM_RATIO: f32 = 0.85;
const HORN_BASE_MS: f32 = 1.2;
const HORN_RADIUS_MS: f32 = 0.44;
const HORN_REFLECT_MS: f32 = 2.6;
const DRUM_BASE_MS: f32 = 1.2;
const DRUM_RADIUS_MS: f32 = 0.3;
const HORN_TAU_S: f32 = 0.5;
const DRUM_TAU_S: f32 = 2.0;
const SLOW_DOWN: f32 = 1.5;
/// MAINS HUM 100 % is about -30 dBFS.
const HUM_LEVEL: f32 = 0.03;

/// Each mic's position: how much of the rotation reaches it and the reflections.
const MICS: [(f32, f32); 3] = [(0.85, 0.2), (0.6, 0.35), (0.4, 0.5)];

#[derive(Clone, Copy, Default)]
struct Rotor {
    angle: f32,
    rate: f32,
    target: f32,
    up: f32,
    down: f32,
}

impl Rotor {
    fn set_inertia(&mut self, tau_s: f32, sr: f32) {
        self.up = 1.0 - (-1.0 / (tau_s * sr)).exp();
        self.down = 1.0 - (-1.0 / (tau_s * SLOW_DOWN * sr)).exp();
    }

    #[inline]
    fn tick(&mut self, inv_sr: f32) -> f32 {
        let a = if self.target > self.rate { self.up } else { self.down };
        self.rate += (self.target - self.rate) * a;
        self.angle += self.rate * inv_sr;
        if self.angle >= 1.0 {
            self.angle -= 1.0;
        }
        self.angle
    }
}

pub(super) struct Rotator {
    sr: f32,
    horn_line: DelayLine,
    drum_line: DelayLine,
    xo_lp: [Svf; 2],
    xo_hp: [Svf; 2],
    dull: [OnePole; 2],
    dark: [OnePole; 2],
    horn: Rotor,
    drum: Rotor,
    horn_gain: Smoothed,
    drum_gain: Smoothed,
    width: Smoothed,
    sep: Smoothed,
    am: Smoothed,
    refl: Smoothed,
    back: Smoothed,
    tube: Smoothed,
    drive: Smoothed,
    hum: Smoothed,
    hum_phase: f32,
    hum_inc: f32,
    primed: bool,
}

impl Rotator {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let len = (0.001 * sr * (HORN_REFLECT_MS + HORN_RADIUS_MS + 1.0)) as usize + 8;
        let mut xo_lp = [Svf::default(); 2];
        let mut xo_hp = [Svf::default(); 2];
        for f in xo_lp.iter_mut().chain(xo_hp.iter_mut()) {
            f.set(CROSSOVER_HZ, FRAC_1_SQRT_2, sr);
        }
        let s = |v: f32, t: f32| Smoothed::new(v, t, sr);
        let mut horn = Rotor::default();
        horn.set_inertia(HORN_TAU_S, sr);
        let mut drum = Rotor::default();
        drum.set_inertia(DRUM_TAU_S, sr);
        Self {
            sr,
            horn_line: DelayLine::new(len),
            drum_line: DelayLine::new(len),
            xo_lp,
            xo_hp,
            dull: [OnePole::new(2500.0, sr); 2],
            dark: [OnePole::new(2500.0, sr); 2],
            horn,
            drum,
            horn_gain: s(1.0, 30.0),
            drum_gain: s(1.0, 30.0),
            width: s(1.0, 30.0),
            sep: s(0.3, 30.0),
            am: s(0.85, 30.0),
            refl: s(0.2, 30.0),
            back: s(0.0, 30.0),
            tube: s(0.0, 30.0),
            drive: s(2.0, 30.0),
            hum: s(0.0, 30.0),
            hum_phase: 0.0,
            hum_inc: 50.0 / sr,
            primed: false,
        }
    }

    fn smoothers(&mut self) -> [&mut Smoothed; 10] {
        [
            &mut self.horn_gain,
            &mut self.drum_gain,
            &mut self.width,
            &mut self.sep,
            &mut self.am,
            &mut self.refl,
            &mut self.back,
            &mut self.tube,
            &mut self.drive,
            &mut self.hum,
        ]
    }
}

impl FxAlgo for Rotator {
    fn set_params(&mut self, p: &Params) {
        let speed = if p.on("on") { p.f("spd") } else { 0.0 };
        self.horn.target = speed;
        self.drum.target = speed * DRUM_RATIO;
        let bal = pct(p.f("bal"));
        self.horn_gain.target = (1.0 + bal).min(1.0);
        self.drum_gain.target = (1.0 - bal).min(1.0);
        self.width.target = pct(p.f("width"));
        // Two mics a SEPARATION apart in angle, up to half a turn.
        self.sep.target = 0.5 * pct(p.f("sep"));
        let (am, refl) = MICS[p.choice("mic").min(2)];
        // DEFLECTORS spread the horn's beam: less of the rotation reaches the mic.
        self.am.target = am * if p.on("defl") { 0.7 } else { 1.0 };
        self.refl.target = refl * if p.on("back") { 1.5 } else { 1.0 };
        self.back.target = f32::from(u8::from(p.on("back")));
        self.tube.target = f32::from(u8::from(p.on("tube")));
        self.drive.target = 1.0 + 0.6 * p.f("drv");
        self.hum.target = pct(p.f("hum")) * HUM_LEVEL;
        self.hum_inc = if p.choice("mains") == 1 { 60.0 } else { 50.0 } / self.sr;
        if !self.primed {
            self.primed = true;
            self.horn.rate = self.horn.target;
            self.drum.rate = self.drum.target;
            for s in self.smoothers() {
                s.snap(s.target);
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let sr = self.sr;
        let inv_sr = 1.0 / sr;
        let (hb, hr, hx) = (ms(HORN_BASE_MS, sr), ms(HORN_RADIUS_MS, sr), ms(HORN_REFLECT_MS, sr));
        let (db, dr) = (ms(DRUM_BASE_MS, sr), ms(DRUM_RADIUS_MS, sr));
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let m = 0.5 * (*l + *r);
            let tube = self.tube.tick();
            let drive = self.drive.tick();
            let driven = fast_tanh(m * drive) / drive.sqrt();
            let x = m + (driven - m) * tube;
            let lo = self.xo_lp[0].tick(x).lp;
            let lo = self.xo_lp[1].tick(lo).lp;
            let hi = self.xo_hp[0].tick(x).hp;
            let hi = self.xo_hp[1].tick(hi).hp;
            self.horn_line.push(hi);
            self.drum_line.push(lo);
            let ha = self.horn.tick(inv_sr);
            let da = self.drum.tick(inv_sr);
            let (hg, dg) = (self.horn_gain.tick(), self.drum_gain.tick());
            let (am, refl, back, sep) = (self.am.tick(), self.refl.tick(), self.back.tick(), self.sep.tick());
            let mut out = [0.0f32; 2];
            for (c, o) in out.iter_mut().enumerate() {
                let mic = if c == 0 { -0.5 * sep } else { 0.5 * sep };
                let co = (TAU * (ha - mic)).cos();
                let direct = self.horn_line.tap_hermite(hb - hr * co);
                let dull = self.dull[c].lp(direct);
                let direct = dull + (direct - dull) * (0.5 + 0.5 * co);
                let reflect = self.horn_line.tap_hermite(hx + hr * co);
                let dark = self.dark[c].lp(reflect);
                let reflect = reflect + (dark - reflect) * back;
                let horn = direct * (1.0 - am * 0.5 * (1.0 - co)) + reflect * refl * (1.0 - am * 0.5 * (1.0 + co));
                let cd = (TAU * (da - mic)).cos();
                let drum = self.drum_line.tap_hermite(db - dr * cd) * (1.0 - 0.6 * am * 0.5 * (1.0 - cd));
                *o = hg * horn + dg * drum;
            }
            let w = self.width.tick();
            let mid = 0.5 * (out[0] + out[1]);
            let side = 0.5 * (out[0] - out[1]) * w;
            advance(&mut self.hum_phase, self.hum_inc);
            let h = self.hum.tick();
            let hum = h * ((TAU * self.hum_phase).sin() + 0.5 * (TAU * 3.0 * self.hum_phase).sin());
            *l = mid + side + hum;
            *r = mid - side + hum;
        }
    }

    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        out[LFO_L] = self.horn.angle;
        out[LFO_R] = self.drum.angle;
        out[NEEDLE] = self.horn.rate;
    }

    fn reset(&mut self) {
        self.horn_line.clear();
        self.drum_line.clear();
        for f in self.xo_lp.iter_mut().chain(self.xo_hp.iter_mut()) {
            f.reset();
        }
        for f in self.dull.iter_mut().chain(self.dark.iter_mut()) {
            f.reset();
        }
        self.horn.angle = 0.0;
        self.drum.angle = 0.0;
        self.horn.rate = self.horn.target;
        self.drum.rate = self.drum.target;
        self.hum_phase = 0.0;
        for s in self.smoothers() {
            s.snap(s.target);
        }
        // A reset starts silent: the hum fades up from nothing.
        self.hum.snap(0.0);
    }
}
