//! SMR Reverb (Classic, Hall, Room, EMT) and Gated Verb.
//!
//! SMR: the guide says each model is a space; Sidecar's reading has them as the
//! WING's chamber, hall, room and plate voices (the same reading the SQ's SMR
//! takes) with SMR's own controls around the voice. Stated: LF Cut 0-400 Hz and HF
//! Cut 2-20 kHz, 24 dB/oct on the input. Everything else in the Expert panel is
//! Sidecar's reading of the guide's names: Size and Decay are independent, Source
//! Diffusion is the input diffusers, Body Diffusion how long the build-up lasts,
//! Tail Diffusion the allpasses in the tank, Ref Detail how many early reflections
//! there are, Reflection and Decay the two levels, Echo 1 and 2 discrete taps,
//! Shape (EMT only) the build-up's contour, Colour a bell on the wet signal.
//!
//! Gated Verb: tapped delay lines read by a window of taps whose gains are the
//! envelope, so the reverb stops at the end of Attack + Hold + Release whatever
//! the input (500 ms at most together, as the guide states). Panner sweeps the
//! taps from left to right; Powerbox packs them densely and adds a little drive
//! (both Sidecar's reading).

use super::{ch, p, pct, Cut24, LIN, LOG};
use crate::biquad::{Biquad, BiquadCoeffs};
use crate::model::{ParamSpec, Params};
use crate::wing::fx::blocks::{fast_tanh, DelayLine};
use crate::wing::fx::modulation::shared::CutFilter;
use crate::wing::fx::reverb::parts::{fader, hash01, rate, run, sign, spread, Decay, Diffuser, Param, Pos, Taps, Voice, CTL};
use crate::wing::fx::reverb::space::{Config, Settings, Space};
use crate::wing::fx::{FxAlgo, FxContext};
use std::f32::consts::FRAC_PI_2;

const SMR_LEN: usize = 23;

/// The shared SMR panel. Defaults: the SQ's panel picture of the EMT 250 (its
/// Predelay, Decay, HF Decay, LF Cut and HF Cut), the rest Sidecar's.
const fn smr_params() -> [ParamSpec; SMR_LEN] {
    [
        p("pre", "PREDELAY", "ms", LIN, 0.0, 200.0, 201, 23.0),
        p("decay", "DECAY", "s", LOG, 0.3, 25.0, 101, 1.54),
        p("hfd", "HF DECAY", "Hz", LOG, 1000.0, 20000.0, 101, 1650.0),
        p("hfs", "HF SLOPE", "%", LIN, 0.0, 100.0, 101, 40.0),
        p("lc", "LF CUT", "Hz", LIN, 0.0, 400.0, 401, 161.0),
        p("hc", "HF CUT", "Hz", LOG, 2000.0, 20000.0, 101, 7530.0),
        p("sdiff", "SOURCE DIFFUSION", "%", LIN, 0.0, 100.0, 101, 70.0),
        p("size", "SIZE", "%", LIN, 0.0, 100.0, 101, 50.0),
        p("det", "REF DETAIL", "%", LIN, 0.0, 100.0, 101, 50.0),
        p("e1", "ECHO 1", "ms", LIN, 0.0, 500.0, 501, 80.0),
        p("e1l", "ECHO 1 LEVEL", "dB", LIN, -60.0, 0.0, 61, -60.0),
        p("e2", "ECHO 2", "ms", LIN, 0.0, 500.0, 501, 160.0),
        p("e2l", "ECHO 2 LEVEL", "dB", LIN, -60.0, 0.0, 61, -60.0),
        p("bdiff", "BODY DIFFUSION", "%", LIN, 0.0, 100.0, 101, 50.0),
        p("tdiff", "TAIL DIFFUSION", "%", LIN, 0.0, 100.0, 101, 60.0),
        p("mdep", "MOD DEPTH", "%", LIN, 0.0, 100.0, 101, 25.0),
        p("mspd", "MOD SPEED", "Hz", LOG, 0.05, 5.0, 101, 0.5),
        p("lfd", "LF DECAY", "x", LOG, 0.25, 2.5, 101, 1.0),
        p("lfx", "LF XOVER", "Hz", LOG, 100.0, 2000.0, 101, 400.0),
        p("col", "COLOUR", "dB", LIN, -12.0, 12.0, 49, 0.0),
        p("colf", "COLOUR FREQ", "Hz", LOG, 200.0, 8000.0, 101, 1500.0),
        p("rlev", "REFLECTION LEVEL", "dB", LIN, -60.0, 6.0, 67, 0.0),
        p("dlev", "DECAY LEVEL", "dB", LIN, -60.0, 6.0, 67, 0.0),
    ]
}

pub(super) static SMR: [ParamSpec; SMR_LEN] = smr_params();

/// The EMT's panel: the shared one with Shape (the guide: plate only) after Size.
pub(super) static SMR_EMT: [ParamSpec; SMR_LEN + 1] = {
    let base = smr_params();
    let mut out = [p("shape", "SHAPE", "%", LIN, 0.0, 100.0, 101, 35.0); SMR_LEN + 1];
    let mut i = 0;
    while i < SMR_LEN {
        // Index 7 is SIZE: SHAPE takes index 8.
        out[if i <= 7 { i } else { i + 1 }] = base[i];
        i += 1;
    }
    out
};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(super) enum SmrKind {
    Classic,
    Hall,
    Room,
    Emt,
}

/// Early-reflection taps per side at REF DETAIL 100.
const REFL: usize = 14;

/// Tanks' build-up share in the reflections.
const BUILD_MIX: f32 = 0.3;

pub(super) struct Smr {
    kind: SmrKind,
    sr: f32,
    space: Space<16>,
    /// SIZE: shortest and longest mean line, ms.
    size_ms: (f32, f32),
    input: Cut24,
    reflections: Taps<REFL>,
    echoes: Taps<2>,
    refl_gain: Param,
    tail_gain: Param,
    tail_diff: Param,
    colour_db: Param,
    colour_hz: Param,
    colour: Biquad,
    a: f32,
    clock: usize,
    primed: bool,
    u: [[f32; REFL]; 2],
    sgn: [[f32; REFL]; 2],
}

impl Smr {
    pub(super) fn new(kind: SmrKind, ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let lex_diff = [4.8, 3.6, 12.7, 9.3];
        let plate_diff = [1.6, 2.3, 3.7, 5.3];
        let room_ap = Some([0.15, 0.3]);
        // Each room is the WING voice of the same sound: line spreads, loop allpasses,
        // diffuser times, level calibration (see `wing::fx::reverb::lexicon`).
        let (rel, size, loop_ap, diff_ms, diff_ref, seed, cal) = match kind {
            SmrKind::Classic => (spread::<16>(0.72, 1.28, 13), (20.0, 150.0), room_ap, lex_diff, 50.0, 0xC4A3, 1.74),
            SmrKind::Hall => (spread::<16>(0.55, 1.45, 11), (30.0, 130.0), room_ap, lex_diff, 80.0, 0x4A11, 1.75),
            SmrKind::Room => (spread::<16>(0.6, 1.4, 12), (10.0, 150.0), room_ap, lex_diff, 40.0, 0x2004, 1.57),
            SmrKind::Emt => (spread::<16>(0.5, 1.5, 14), (15.0, 110.0), Some([0.2, 0.25]), plate_diff, 30.0, 0x91A7, 1.12),
        };
        let cfg = Config { rel, max_size_ms: size.1, max_pre_ms: 200.0, loop_ap, diff_ms, diff_ref_ms: diff_ref, seed };
        let mut space = Space::new(sr, &cfg);
        space.cal = cal;
        // Reflections: a golden-ratio scatter per side.
        let mut u = [[0.0; REFL]; 2];
        let mut sgn = [[1.0; REFL]; 2];
        for c in 0..2 {
            let s = seed ^ (0x51 * c as u32 + 7);
            for k in 0..REFL {
                u[c][k] = (0.13 * c as f32 + k as f32 * 0.618_034 + 0.2 * hash01(k as u32, s)).rem_euclid(1.0);
                sgn[c][k] = sign(k as u32, s ^ 0x3F);
            }
        }
        Self {
            kind,
            sr,
            space,
            size_ms: size,
            input: Cut24::new(sr),
            reflections: Taps::new(sr, 500.0, REFL),
            echoes: Taps::new(sr, 520.0, 2),
            refl_gain: Param::default(),
            tail_gain: Param::default(),
            tail_diff: Param::default(),
            colour_db: Param::default(),
            colour_hz: Param { cur: 1500.0, target: 1500.0 },
            colour: Biquad::default(),
            a: rate(40.0, sr),
            clock: 0,
            primed: false,
            u,
            sgn,
        }
    }

    fn redesign_colour(&mut self) {
        let c = BiquadCoeffs::peak(f64::from(self.sr), f64::from(self.colour_hz.cur), 0.7, f64::from(self.colour_db.cur));
        self.colour.set_coefficients(c);
    }
}

impl FxAlgo for Smr {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        self.input.set(p.f("lc"), p.f("hc"));
        let (lo, hi) = self.size_ms;
        let size_ms = lo + (hi - lo) * pct(p.f("size"));
        let mid = 1.0 / p.f("decay").max(0.05);
        let hfd = p.f("hfd").min(sr * 0.4);
        let slope = pct(p.f("hfs"));
        // HF SLOPE steepens how fast the highs die above HF DECAY.
        let decay = Decay {
            bands: [mid / p.f("lfd").max(0.05), mid, mid * (1.0 + 1.5 * slope), mid * (1.5 + 6.5 * slope)],
            xo: [p.f("lfx").min(hfd * 0.7), hfd, (hfd * 2.0).min(sr * 0.45)],
        };
        let shape = if self.kind == SmrKind::Emt { pct(p.f("shape")) } else { 0.4 };
        let s = Settings {
            pre_ms: p.f("pre"),
            size_ms,
            decay,
            diffusion: pct(p.f("sdiff")),
            shape,
            build_ms: size_ms * (0.05 + 2.0 * pct(p.f("bdiff"))),
            damp_hz: 20_000.0,
            mod_hz: p.f("mspd"),
            mod_ms: 0.02 + 1.5 * pct(p.f("mdep")),
            ..Settings::default()
        };
        self.space.set(&s);
        // Reflections follow the predelay, over a span set by SIZE.
        let ms = 0.001 * sr;
        let span = size_ms * 1.4;
        let active = 2.0 + pct(p.f("det")) * (REFL as f32 - 2.0);
        for c in 0..2 {
            let mut gains = [0.0f32; REFL];
            for (k, g) in gains.iter_mut().enumerate() {
                let fade = (active - k as f32).clamp(0.0, 1.0);
                *g = fade * (1.0 - 0.75 * self.u[c][k]) * self.sgn[c][k];
            }
            let norm = 1.0 / gains.iter().map(|g| g * g).sum::<f32>().sqrt().max(1e-3);
            for (k, g) in gains.into_iter().enumerate() {
                let t = (s.pre_ms + 2.0 + span * self.u[c][k]) * ms;
                self.reflections.set(c, k, t, g * norm * 0.5);
            }
            self.echoes.set(c, 0, p.f("e1") * ms, fader(p.f("e1l")));
            self.echoes.set(c, 1, p.f("e2") * ms, fader(p.f("e2l")));
        }
        self.refl_gain.set(fader(p.f("rlev")));
        self.tail_gain.set(fader(p.f("dlev")));
        self.tail_diff.set(0.1 + 0.5 * pct(p.f("tdiff")));
        self.colour_db.set(p.f("col"));
        self.colour_hz.set(p.f("colf"));
        if !self.primed {
            self.primed = true;
            self.space.snap();
            self.reflections.snap();
            self.echoes.snap();
            for q in [&mut self.refl_gain, &mut self.tail_gain, &mut self.tail_diff, &mut self.colour_db, &mut self.colour_hz] {
                q.snap();
            }
            self.redesign_colour();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        run(self, left, right);
    }

    fn reset(&mut self) {
        self.space.clear();
        self.input.reset();
        self.reflections.clear();
        self.echoes.clear();
        self.colour.reset();
    }
}

impl Voice for Smr {
    fn clock(&mut self) -> &mut usize {
        &mut self.clock
    }

    fn control(&mut self) {
        self.space.control();
        self.tail_diff.step(self.a);
        self.space.fdn.set_ap_gain(self.tail_diff.cur);
        self.reflections.control(self.a, false);
        self.echoes.control(self.a, false);
        self.refl_gain.step(self.a);
        self.tail_gain.step(self.a);
        if self.colour_db.step(self.a) | self.colour_hz.step(self.a) {
            self.redesign_colour();
        }
    }

    #[inline]
    fn tick(&mut self, xl: f32, xr: f32) -> (f32, f32) {
        let xl = self.input.tick(0, xl);
        let xr = self.input.tick(1, xr);
        let (rl, rr) = self.reflections.tick(xl, xr);
        let (el, er) = self.echoes.tick(xl, xr);
        let ((bl, br), (tl, tr)) = self.space.tank_split(xl, xr);
        let (rg, tg) = (self.refl_gain.cur, self.tail_gain.cur);
        let l = rg * (rl + BUILD_MIX * bl) + tg * tl + el;
        let r = rg * (rr + BUILD_MIX * br) + tg * tr + er;
        let (l, r) = (self.colour.tick(l, 0), self.colour.tick(r, 1));
        self.space.out.tick(l, r)
    }
}

// ---- Gated Verb -----------------------------------------------------------------------

const TYPES: &[&str] = &["CLASSIC", "PANNER", "POWERBOX"];
const WIDTHS: &[&str] = &["MONO", "STEREO", "WIDE"];

/// Gated Verb. Stated: Lo cut 20 Hz-6 kHz (24 dB/oct), Hi cut 400 Hz-20 kHz
/// (12 dB/oct), Predelay 0-170 ms, Attack + Hold + Release 500 ms at most together
/// (a longer setting is scaled down). The individual Attack, Hold and Release
/// ranges and Diffusion are Sidecar's reading.
pub(super) static GATED: [ParamSpec; 9] = [
    ch("type", "TYPE", TYPES, 0.0),
    p("lc", "LO CUT", "Hz", LOG, 20.0, 6000.0, 101, 80.0),
    p("hc", "HI CUT", "Hz", LOG, 400.0, 20000.0, 101, 5000.0),
    p("pre", "PREDELAY", "ms", LIN, 0.0, 170.0, 171, 8.0),
    p("att", "ATTACK", "ms", LIN, 0.0, 100.0, 101, 6.0),
    p("hold", "HOLD", "ms", LIN, 0.0, 400.0, 401, 150.0),
    p("rel", "RELEASE", "ms", LIN, 5.0, 500.0, 496, 150.0),
    ch("width", "WIDTH", WIDTHS, 1.0),
    p("diff", "DIFFUSION", "%", LIN, 0.0, 100.0, 101, 80.0),
];

/// Taps per side.
const TAPS: usize = 40;
/// Taps active in the two sparser programs.
const SPARSE_TAPS: usize = 28;

pub(super) struct GatedVerb {
    sr: f32,
    kind: usize,
    width: f32,
    diff: [Diffuser; 2],
    post: [Diffuser; 2],
    lines: [DelayLine; 2],
    u: [[f32; TAPS]; 2],
    sgn: [[f32; TAPS]; 2],
    pos: [[Pos; TAPS]; 2],
    gain: [[f32; TAPS]; 2],
    gain_to: [[f32; TAPS]; 2],
    ginc: [[f32; TAPS]; 2],
    reading: usize,
    pre: Param,
    win: Param,
    att_s: f32,
    hold_s: f32,
    diffusion: Param,
    hp: Cut24,
    lp: CutFilter,
    out_hp: CutFilter,
    a: f32,
    a_slow: f32,
    /// New values to apply at the next control step (the first of all at once).
    force: bool,
    dirty: bool,
    clock: usize,
    primed: bool,
}

impl GatedVerb {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let cap = (0.68 * sr) as usize + 8;
        let mut u = [[0.0; TAPS]; 2];
        let mut sgn = [[1.0; TAPS]; 2];
        for c in 0..2 {
            let seed = 0x6A7E + c as u32 * 0x101;
            for k in 0..TAPS {
                let j = (hash01(k as u32, seed) - 0.5) / TAPS as f32;
                u[c][k] = (0.31 * c as f32 + k as f32 * 0.618_034 + j).rem_euclid(1.0);
                sgn[c][k] = sign(k as u32, seed ^ 0x3F);
            }
        }
        Self {
            sr,
            kind: 0,
            width: 1.0,
            diff: [Diffuser::new(sr, [1.3, 2.1, 3.4, 5.1], 1.0), Diffuser::new(sr, [1.5, 2.3, 3.1, 4.7], 1.0)],
            post: [Diffuser::new(sr, [0.7, 1.1, 1.9, 2.9], 1.0), Diffuser::new(sr, [0.8, 1.3, 1.7, 2.7], 1.0)],
            lines: [DelayLine::new(cap), DelayLine::new(cap)],
            u,
            sgn,
            pos: [[Pos::default(); TAPS]; 2],
            gain: [[0.0; TAPS]; 2],
            gain_to: [[0.0; TAPS]; 2],
            ginc: [[0.0; TAPS]; 2],
            reading: SPARSE_TAPS,
            pre: Param::default(),
            win: Param::default(),
            att_s: 0.0,
            hold_s: 0.0,
            diffusion: Param::default(),
            hp: Cut24::new(sr),
            lp: CutFilter::new(sr),
            out_hp: CutFilter::new(sr),
            a: rate(30.0, sr),
            a_slow: rate(150.0, sr),
            force: true,
            dirty: false,
            clock: 0,
            primed: false,
        }
    }

    /// The envelope at `t` seconds into the window.
    fn envelope(&self, t: f32, win: f32) -> f32 {
        let attack = self.att_s.max(1e-4);
        if t < attack {
            t / attack
        } else if t < attack + self.hold_s {
            1.0
        } else {
            let tail = (win - attack - self.hold_s).max(1e-4);
            (1.0 - (t - attack - self.hold_s) / tail).clamp(0.0, 1.0).powf(1.5)
        }
    }

    fn update_taps(&mut self, moving: bool, snap: bool) {
        let frac = self.pre.moving() || self.win.moving();
        let max = self.lines[0].max_delay() as f32 - 3.0;
        let active = if self.kind == 2 { TAPS } else { SPARSE_TAPS };
        let win = self.win.cur / self.sr;
        for c in 0..2 {
            let mut sum = 0.0;
            for k in 0..TAPS {
                let u = self.u[c][k];
                if moving {
                    self.pos[c][k].set((self.pre.cur + self.win.cur * u).clamp(1.0, max), frac);
                }
                let mut g = if k < active { self.envelope(u * win, win) } else { 0.0 };
                sum += g * g;
                if self.kind == 1 {
                    // Panner: each tap's place in the window is its place across the field.
                    let (s, co) = (u * FRAC_PI_2).sin_cos();
                    g *= if c == 0 { co } else { s };
                }
                self.gain_to[c][k] = g * self.sgn[c][k];
            }
            let norm = 0.8 / sum.sqrt().max(1e-3);
            for g in &mut self.gain_to[c][..active] {
                *g *= norm;
            }
        }
        self.reading = active;
        if snap {
            self.gain = self.gain_to;
            self.ginc = [[0.0; TAPS]; 2];
        } else {
            let k = 1.0 / CTL as f32;
            for c in 0..2 {
                for i in 0..TAPS {
                    self.ginc[c][i] = (self.gain_to[c][i] - self.gain[c][i]) * k;
                }
            }
        }
    }
}

impl FxAlgo for GatedVerb {
    fn set_params(&mut self, p: &Params) {
        let ms = 0.001 * self.sr;
        self.kind = p.choice("type");
        self.width = [0.0, 1.0, 1.6][p.choice("width").min(2)];
        let (a, h, r) = (p.f("att"), p.f("hold"), p.f("rel"));
        let total = (a + h + r).max(5.0);
        let scale = (500.0 / total).min(1.0);
        self.att_s = a * scale * 0.001;
        self.hold_s = h * scale * 0.001;
        self.pre.set(p.f("pre") * ms);
        self.win.set(total * scale * ms);
        self.diffusion.set(pct(p.f("diff")));
        self.hp.set(p.f("lc"), 24_000.0);
        self.lp.set(5.0, p.f("hc"));
        self.out_hp.set(20.0, 24_000.0);
        self.dirty = true;
        if !self.primed {
            self.primed = true;
            self.pre.snap();
            self.win.snap();
            self.diffusion.snap();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        run(self, left, right);
    }

    fn reset(&mut self) {
        for d in self.diff.iter_mut().chain(&mut self.post) {
            d.clear();
        }
        for l in &mut self.lines {
            l.clear();
        }
        self.hp.reset();
        self.lp.reset();
        self.out_hp.reset();
    }
}

impl Voice for GatedVerb {
    fn clock(&mut self) -> &mut usize {
        &mut self.clock
    }

    fn control(&mut self) {
        let force = std::mem::take(&mut self.force);
        let dirty = std::mem::take(&mut self.dirty);
        self.gain = self.gain_to;
        self.ginc = [[0.0; TAPS]; 2];
        let lim = 0.4 * CTL as f32;
        let moved = self.pre.step_limited(self.a_slow, lim) | self.win.step_limited(self.a_slow, lim);
        if moved || force || dirty {
            self.update_taps(moved || force, force);
        }
        if self.diffusion.step(self.a) || force {
            let powerbox = if self.kind == 2 { 0.2 } else { 0.0 };
            let d = (self.diffusion.cur + powerbox).min(1.0);
            for dd in &mut self.diff {
                dd.set(1.0, d, false);
            }
            for dd in &mut self.post {
                dd.set(1.0, 0.3 + 0.5 * d, false);
            }
        }
    }

    #[inline]
    fn tick(&mut self, xl: f32, xr: f32) -> (f32, f32) {
        let mut out = [0.0f32; 2];
        let n = self.reading;
        for (c, x) in [xl, xr].into_iter().enumerate() {
            let x = self.hp.tick(c, x);
            let x = self.lp.tick(c, x);
            let line = &mut self.lines[c];
            line.push(self.diff[c].tick(x));
            let mut s = 0.0;
            for ((p, g), &inc) in self.pos[c][..n].iter_mut().zip(&mut self.gain[c][..n]).zip(&self.ginc[c][..n]) {
                s += *g * p.read(line);
                *g += inc;
            }
            out[c] = self.post[c].tick(s);
            if self.kind == 2 {
                out[c] = fast_tanh(1.5 * out[c]);
            }
        }
        let m = 0.5 * (out[0] + out[1]);
        let s = 0.5 * (out[0] - out[1]) * self.width;
        (self.out_hp.tick(0, m + s), self.out_hp.tick(1, m - s))
    }
}
