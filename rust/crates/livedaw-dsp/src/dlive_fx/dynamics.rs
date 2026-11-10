//! De-Esser, Dynamic EQ, Multiband Compressor (3- and 4-band) and Transient Controller.
//!
//! Stated: De-Esser Frequency 3-8 kHz, Reduction to 24 dB, Response Slow/Medium/Fast,
//! filter Classic/Shelf/Band Pass, Listen, auto-threshold, two channels and Link.
//! Dynamic EQ: frequency 40 Hz-18 kHz, bell 2.25 to 1/9 octave (2.1 to 1/9 on bands 1
//! and 4, which become shelves at the widest), threshold -50 to +18 dBu, Above or
//! Below, Std 9 or Fast 9, sidechain Cascade or Parallel, output trim +-12 dB.
//! Multiband: ratio 1:1 to 20:1, gain -6 to +18 dB, threshold -50 to +18 dBu,
//! crossover 6, 18 or 24 dB/oct, knee Hard or Soft, In, Split or Wide key, two manual
//! and four automatic ballistics. Transient Controller: Attack +-15 dB, Sustain +-24
//! dB, Pre and Post Gain +-15 dB, In, Transient or Continuous, two channels and Link.
//! Levels are in dBu with +18 dBu = 0 dBFS (the datasheets).
//!
//! Sidecar's reading: the De-Esser's Classic as a full-band dip keyed by the band, its
//! Shelf and Band Pass as dynamic cuts of those bands, and the auto-threshold as the
//! band's level against the whole signal's (the WING De-Esser's way); the ballistics
//! behind each name; the Dynamic EQ's gain range and how far the gain moves per dB
//! past the threshold; the Multiband's Link and Rel (Link: every band takes band 1's
//! threshold, ratio and gain; Rel: band 1's move from its centre carries the others'
//! thresholds and gains); the Transient Controller's detectors. Not modelled: a band's
//! External key, since an FX slot has no key input.

use super::{ch, p, tog, LIN, LOG};
use crate::biquad::{Biquad, BiquadCoeffs};
use crate::model::{ParamKind, ParamSpec, Params};
use crate::wing::dynamics::shelf_12;
use crate::wing::fx::blocks::{coef, Svf};
use crate::wing::fx::reverb::parts::{rate, Param, CTL};
use crate::wing::fx::utility::{db_gain, level_db, rail};
use crate::wing::fx::{FxAlgo, FxContext, FX_READOUTS};
use std::f64::consts::FRAC_1_SQRT_2;

/// dBu to dBFS (+18 dBu = 0 dBFS).
const DBU_OFFSET: f32 = 18.0;

// ---- De-Esser --------------------------------------------------------------------------

const RESPONSES: &[&str] = &["SLOW", "MEDIUM", "FAST"];
const DS_FILTERS: &[&str] = &["CLASSIC", "SHELF", "BAND PASS"];

/// De-Esser, per channel with a shared Link.
pub(super) static DE_ESSER: [ParamSpec; 11] = [
    tog("link", "LINK", 1.0),
    p("f_1", "FREQUENCY 1", "Hz", LOG, 3000.0, 8000.0, 101, 5500.0),
    p("r_1", "REDUCTION 1", "dB", LIN, 0.0, 24.0, 49, 8.0),
    ch("resp_1", "RESPONSE 1", RESPONSES, 1.0),
    ch("flt_1", "FILTER 1", DS_FILTERS, 1.0),
    tog("lis_1", "LISTEN 1", 0.0),
    p("f_2", "FREQUENCY 2", "Hz", LOG, 3000.0, 8000.0, 101, 5500.0),
    p("r_2", "REDUCTION 2", "dB", LIN, 0.0, 24.0, 49, 8.0),
    ch("resp_2", "RESPONSE 2", RESPONSES, 1.0),
    ch("flt_2", "FILTER 2", DS_FILTERS, 1.0),
    tog("lis_2", "LISTEN 2", 0.0),
];

/// Attack and release (ms) of the responses.
const RESP_MS: [(f32, f32); 3] = [(8.0, 180.0), (3.0, 80.0), (0.8, 30.0)];
/// The band this far below the whole signal starts the reduction, and this much
/// above that takes it all (the WING De-Esser's numbers).
const ONSET_DB: f32 = -16.0;
const SPAN_DB: f32 = 10.0;

struct DeSide {
    band: Biquad,
    /// High-pass and band-pass with the same poles: together a true 2nd-order shelf.
    shelf: Biquad,
    shelf_b: Biquad,
    env_b: f32,
    env_f: f32,
    red: f32,
    max_db: f32,
    att: f32,
    rel: f32,
    filter: usize,
    listen: bool,
    freq: f32,
}

impl DeSide {
    fn design(&mut self, sr: f32) {
        let f = f64::from(self.freq.min(sr * 0.45));
        let s = f64::from(sr);
        self.band.set_coefficients(BiquadCoeffs::band_pass(s, f, 1.4));
        self.shelf.set_coefficients(BiquadCoeffs::high_pass(s, f, FRAC_1_SQRT_2));
        self.shelf_b.set_coefficients(BiquadCoeffs::band_pass(s, f, FRAC_1_SQRT_2));
    }
}

/// De-Esser: each channel's sibilance band, against the whole signal, sets its reduction.
pub(super) struct DeEsser {
    sr: f32,
    sides: [DeSide; 2],
    linked: bool,
    env_att: f32,
    env_rel: f32,
    meter: f32,
}

impl DeEsser {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let side = || DeSide {
            band: Biquad::default(),
            shelf: Biquad::default(),
            shelf_b: Biquad::default(),
            env_b: 0.0,
            env_f: 0.0,
            red: 0.0,
            max_db: 8.0,
            att: 1.0,
            rel: 1.0,
            filter: 1,
            listen: false,
            freq: 0.0,
        };
        Self { sr, sides: [side(), side()], linked: true, env_att: coef(0.5, sr), env_rel: coef(40.0, sr), meter: 0.0 }
    }
}

impl FxAlgo for DeEsser {
    fn set_params(&mut self, p: &Params) {
        self.linked = p.on("link");
        let sr = self.sr;
        let ids = [["f_1", "r_1", "resp_1", "flt_1", "lis_1"], ["f_2", "r_2", "resp_2", "flt_2", "lis_2"]];
        for (i, side) in self.sides.iter_mut().enumerate() {
            // Link: both channels take channel 1's controls.
            let ids = ids[if self.linked { 0 } else { i }];
            let (att, rel) = RESP_MS[p.choice(ids[2]).min(2)];
            side.att = coef(att, sr);
            side.rel = coef(rel, sr);
            side.max_db = p.f(ids[1]);
            side.filter = p.choice(ids[3]).min(2);
            side.listen = p.on(ids[4]);
            let f = p.f(ids[0]);
            if f != side.freq {
                side.freq = f;
                side.design(sr);
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let mut most = 0.0f32;
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let x = [*l, *r];
            let mut band = [0.0f32; 2];
            let mut amount = [0.0f32; 2];
            for (i, side) in self.sides.iter_mut().enumerate() {
                let b = side.band.tick(x[i], 0);
                band[i] = b;
                let vb = b.abs();
                side.env_b += (vb - side.env_b) * if vb > side.env_b { self.env_att } else { self.env_rel };
                let vf = x[i].abs();
                side.env_f += (vf - side.env_f) * if vf > side.env_f { self.env_att } else { self.env_rel };
                if side.env_f > 1e-6 {
                    amount[i] = ((level_db(side.env_b / side.env_f) - ONSET_DB) / SPAN_DB).clamp(0.0, 1.0);
                }
            }
            if self.linked {
                amount = [amount[0].max(amount[1]); 2];
            }
            let mut y = x;
            for (i, side) in self.sides.iter_mut().enumerate() {
                let target = side.max_db * amount[i];
                side.red += (target - side.red) * if target > side.red { side.att } else { side.rel };
                most = most.max(side.red);
                let hp = side.shelf.tick(x[i], 0);
                let hp_b = side.shelf_b.tick(x[i], 0);
                let g = db_gain(-side.red) - 1.0;
                y[i] = if side.listen {
                    // Listen: only the band that's being detected.
                    band[i]
                } else {
                    match side.filter {
                        0 => x[i] * (1.0 + g),
                        1 => {
                            let (k, kb) = shelf_12(-side.red);
                            x[i] + k * hp + kb * hp_b
                        }
                        _ => x[i] + g * band[i],
                    }
                };
            }
            *l = y[0];
            *r = y[1];
        }
        self.meter = -most;
    }

    fn reset(&mut self) {
        for s in &mut self.sides {
            s.band.reset();
            s.shelf.reset();
            s.shelf_b.reset();
            s.env_b = 0.0;
            s.env_f = 0.0;
            s.red = 0.0;
        }
        self.meter = 0.0;
    }

    fn meter(&self) -> f32 {
        self.meter
    }
}

// ---- Transient Controller --------------------------------------------------------------

const TC_MODES: &[&str] = &["TRANSIENT", "CONTINUOUS"];

/// Transient Controller.
pub(super) static TRANSIENT: [ParamSpec; 13] = [
    tog("link", "LINK", 0.0),
    p("att_1", "ATTACK 1", "dB", LIN, -15.0, 15.0, 61, 0.0),
    p("sus_1", "SUSTAIN 1", "dB", LIN, -24.0, 24.0, 97, 0.0),
    p("pre_1", "PRE GAIN 1", "dB", LIN, -15.0, 15.0, 61, 0.0),
    p("pst_1", "POST GAIN 1", "dB", LIN, -15.0, 15.0, 61, 0.0),
    tog("in_1", "IN 1", 1.0),
    ch("mode_1", "MODE 1", TC_MODES, 0.0),
    p("att_2", "ATTACK 2", "dB", LIN, -15.0, 15.0, 61, 0.0),
    p("sus_2", "SUSTAIN 2", "dB", LIN, -24.0, 24.0, 97, 0.0),
    p("pre_2", "PRE GAIN 2", "dB", LIN, -15.0, 15.0, 61, 0.0),
    p("pst_2", "POST GAIN 2", "dB", LIN, -15.0, 15.0, 61, 0.0),
    tog("in_2", "IN 2", 1.0),
    ch("mode_2", "MODE 2", TC_MODES, 0.0),
];

/// Levels of the two detectors this far apart (dB) reach the full change.
const FULL_DB: f32 = 12.0;
/// Transient mode ignores level differences smaller than this (dB).
const TC_GATE_DB: f32 = 1.5;

#[derive(Clone, Copy, Default)]
struct TcSide {
    onset_fast: f32,
    onset_slow: f32,
    decay_fast: f32,
    decay_slow: f32,
    gain_db: f32,
    pre: f32,
    post: f32,
    att_db: f32,
    sus_db: f32,
    on: bool,
    transient: bool,
}

/// A rise of the fast envelope over the slow one is an onset; the slow-release
/// envelope above the fast-release one is a decay.
pub(super) struct TransientController {
    sides: [TcSide; 2],
    fast_up: f32,
    slow_up: f32,
    onset_dn: f32,
    decay_fast_dn: f32,
    decay_slow_dn: f32,
    smooth: f32,
    meter: f32,
}

impl TransientController {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        Self {
            sides: [TcSide::default(); 2],
            fast_up: coef(0.5, sr),
            slow_up: coef(20.0, sr),
            onset_dn: coef(50.0, sr),
            decay_fast_dn: coef(15.0, sr),
            decay_slow_dn: coef(250.0, sr),
            smooth: coef(0.3, sr),
            meter: 0.0,
        }
    }
}

#[inline]
fn follow(v: &mut f32, x: f32, up: f32, dn: f32) {
    *v += (x - *v) * if x > *v { up } else { dn };
}

impl FxAlgo for TransientController {
    fn set_params(&mut self, p: &Params) {
        let ids =
            [["att_1", "sus_1", "pre_1", "pst_1", "in_1", "mode_1"], ["att_2", "sus_2", "pre_2", "pst_2", "in_2", "mode_2"]];
        let link = p.on("link");
        for (i, side) in self.sides.iter_mut().enumerate() {
            let ids = ids[if link { 0 } else { i }];
            side.att_db = p.f(ids[0]);
            side.sus_db = p.f(ids[1]);
            side.pre = 10f32.powf(p.f(ids[2]) * 0.05);
            side.post = 10f32.powf(p.f(ids[3]) * 0.05);
            side.on = p.on(ids[4]);
            side.transient = p.choice(ids[5]) == 0;
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let mut lowest = 0.0f32;
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            for (x, s) in [&mut *l, &mut *r].into_iter().zip(self.sides.iter_mut()) {
                if !s.on {
                    continue;
                }
                let v = *x * s.pre;
                let a = v.abs();
                follow(&mut s.onset_fast, a, self.fast_up, self.onset_dn);
                follow(&mut s.onset_slow, a, self.slow_up, self.onset_dn);
                follow(&mut s.decay_fast, a, self.fast_up, self.decay_fast_dn);
                follow(&mut s.decay_slow, a, self.fast_up, self.decay_slow_dn);
                let rise = (level_db(s.onset_fast) - level_db(s.onset_slow)).max(0.0);
                let tail = (level_db(s.decay_slow) - level_db(s.decay_fast)).max(0.0);
                let (rise, tail) =
                    if s.transient { ((rise - TC_GATE_DB).max(0.0), (tail - TC_GATE_DB).max(0.0)) } else { (rise, tail) };
                let want = s.att_db * (rise / FULL_DB).min(1.0) + s.sus_db * (tail / FULL_DB).min(1.0);
                s.gain_db += (want - s.gain_db) * self.smooth;
                lowest = lowest.min(s.gain_db);
                *x = rail(v * db_gain(s.gain_db) * s.post);
            }
        }
        self.meter = lowest.min(0.0);
    }

    fn reset(&mut self) {
        for s in &mut self.sides {
            s.onset_fast = 0.0;
            s.onset_slow = 0.0;
            s.decay_fast = 0.0;
            s.decay_slow = 0.0;
            s.gain_db = 0.0;
        }
        self.meter = 0.0;
    }

    fn meter(&self) -> f32 {
        self.meter
    }
}

// ---- Dynamic EQ ------------------------------------------------------------------------

const MODES: &[&str] = &["ABOVE", "BELOW"];
const BALLISTICS: &[&str] = &["STD 9", "FAST 9"];
const WIDEST_MID: f32 = 2.25;
const WIDEST_END: f32 = 2.1;
const NARROWEST: f32 = 1.0 / 9.0;

macro_rules! deq_band {
    ($n:literal, $f:expr, $w:expr) => {
        [
            p(concat!("f_", $n), concat!("FREQUENCY ", $n), "Hz", LOG, 40.0, 18000.0, 201, $f),
            p(concat!("w_", $n), concat!("WIDTH ", $n), "oct", LOG, NARROWEST, $w, 101, 1.0),
            p(concat!("g_", $n), concat!("GAIN ", $n), "dB", LIN, -15.0, 15.0, 61, 0.0),
            p(concat!("t_", $n), concat!("THRESHOLD ", $n), "dB", LIN, -50.0, 18.0, 137, -12.0),
            ch(concat!("m_", $n), concat!("MODE ", $n), MODES, 0.0),
            ch(concat!("b_", $n), concat!("BALLISTICS ", $n), BALLISTICS, 0.0),
        ]
    };
}

const DEQ_1: [ParamSpec; 6] = deq_band!("1", 100.0, WIDEST_END);
const DEQ_2: [ParamSpec; 6] = deq_band!("2", 500.0, WIDEST_MID);
const DEQ_3: [ParamSpec; 6] = deq_band!("3", 2000.0, WIDEST_MID);
const DEQ_4: [ParamSpec; 6] = deq_band!("4", 8000.0, WIDEST_END);
const DEQ_COMMON: [ParamSpec; 2] =
    [ch("side", "SIDECHAIN", &["CASCADE", "PARALLEL"], 0.0), p("trim", "OUTPUT TRIM", "dB", LIN, -12.0, 12.0, 49, 0.0)];

/// Joins parameter arrays into one (const: each model's panel is a table).
const fn join<const N: usize>(parts: &[&[ParamSpec]]) -> [ParamSpec; N] {
    let mut out = [parts[0][0]; N];
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

/// Dynamic EQ (the guide's DynEQ4).
pub(super) static DYN_EQ: [ParamSpec; 26] = join(&[&DEQ_COMMON, &DEQ_1, &DEQ_2, &DEQ_3, &DEQ_4]);

const EQ_BANDS: usize = 4;
/// How far the gain moves per dB past the threshold.
const EQ_RATIO: f32 = 0.5;
const EQ_KNEE: f32 = 3.0;

struct DeqBand {
    /// The filter the band's gain acts through, the one for a Parallel detector, and
    /// the band-pass that completes a shelf (see `shelf_12`).
    filter: Biquad,
    det: Biquad,
    shape: Biquad,
    ln_f: Param,
    ln_w: Param,
    shelf: bool,
    is_end: bool,
    is_low: bool,
    thr: f32,
    target_db: f32,
    above: bool,
    att: f32,
    rel: f32,
    env: f32,
    gain_db: f32,
    designed: bool,
}

impl DeqBand {
    fn new(is_end: bool, is_low: bool) -> Self {
        Self {
            filter: Biquad::default(),
            det: Biquad::default(),
            shape: Biquad::default(),
            ln_f: Param::default(),
            ln_w: Param::default(),
            shelf: false,
            is_end,
            is_low,
            thr: 0.0,
            target_db: 0.0,
            above: true,
            att: 1.0,
            rel: 1.0,
            env: 0.0,
            gain_db: 0.0,
            designed: false,
        }
    }

    fn design(&mut self, sr: f32) {
        let s = f64::from(sr);
        let f = f64::from(self.ln_f.cur.exp().min(sr * 0.45));
        let w = f64::from(self.ln_w.cur.exp());
        let c = if self.is_end && self.shelf {
            if self.is_low {
                BiquadCoeffs::low_pass(s, f, FRAC_1_SQRT_2)
            } else {
                BiquadCoeffs::high_pass(s, f, FRAC_1_SQRT_2)
            }
        } else {
            let a = 2f64.powf(w);
            BiquadCoeffs::band_pass(s, f, a.sqrt() / (a - 1.0))
        };
        self.filter.set_coefficients(c);
        self.det.set_coefficients(c);
        self.shape.set_coefficients(BiquadCoeffs::band_pass(s, f, FRAC_1_SQRT_2));
    }

    #[inline]
    fn gain_for(&self, lvl_db: f32) -> f32 {
        let over = if self.above { lvl_db - self.thr } else { self.thr - lvl_db };
        let amount = if over >= 0.5 * EQ_KNEE {
            over
        } else if over > -0.5 * EQ_KNEE {
            (over + 0.5 * EQ_KNEE).powi(2) / (2.0 * EQ_KNEE)
        } else {
            0.0
        } * EQ_RATIO;
        amount.min(self.target_db.abs()).copysign(self.target_db)
    }

    fn reset(&mut self) {
        self.filter.reset();
        self.det.reset();
        self.shape.reset();
        self.env = 0.0;
        self.gain_db = 0.0;
    }
}

const fn band_ids(n: usize) -> [&'static str; 6] {
    match n {
        0 => ["f_1", "w_1", "g_1", "t_1", "m_1", "b_1"],
        1 => ["f_2", "w_2", "g_2", "t_2", "m_2", "b_2"],
        2 => ["f_3", "w_3", "g_3", "t_3", "m_3", "b_3"],
        _ => ["f_4", "w_4", "g_4", "t_4", "m_4", "b_4"],
    }
}

/// Four dynamic bands in series. Each band's filter runs on the chain's signal, its
/// detector on that signal (Cascade) or on the input (Parallel).
pub(super) struct DynEq {
    sr: f32,
    bands: [DeqBand; EQ_BANDS],
    parallel: bool,
    trim: f32,
    env_rel: f32,
    a: f32,
    clock: usize,
    primed: bool,
    meter: f32,
}

impl DynEq {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        Self {
            sr,
            bands: [DeqBand::new(true, true), DeqBand::new(false, false), DeqBand::new(false, false), DeqBand::new(true, false)],
            parallel: false,
            trim: 1.0,
            env_rel: coef(15.0, sr),
            a: rate(30.0, sr),
            clock: 0,
            primed: false,
            meter: 0.0,
        }
    }
}

impl FxAlgo for DynEq {
    fn set_params(&mut self, p: &Params) {
        self.parallel = p.choice("side") == 1;
        self.trim = 10f32.powf(p.f("trim") * 0.05);
        let sr = self.sr;
        for (i, b) in self.bands.iter_mut().enumerate() {
            let ids = band_ids(i);
            let f = p.f(ids[0]);
            let w = p.f(ids[1]);
            let widest = if b.is_end { WIDEST_END } else { WIDEST_MID };
            let shelf = b.is_end && w >= widest * 0.995;
            let moved = shelf != b.shelf;
            b.ln_f.set(f.ln());
            b.ln_w.set(w.ln());
            b.shelf = shelf;
            b.thr = p.f(ids[3]) - DBU_OFFSET;
            b.target_db = p.f(ids[2]);
            b.above = p.choice(ids[4]) == 0;
            let fast = p.choice(ids[5]) == 1;
            let (att, rel) = if fast { (1.0, 40.0) } else { (5.0, (120.0 * (1000.0 / f).powf(0.3)).clamp(60.0, 500.0)) };
            b.att = coef(att, sr);
            b.rel = coef(rel, sr);
            if !b.designed || !self.primed || moved {
                b.ln_f.snap();
                b.ln_w.snap();
                b.design(sr);
                b.designed = true;
            }
        }
        self.primed = true;
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let mut lowest = 0.0f32;
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            if self.clock == 0 {
                self.clock = CTL;
                for b in &mut self.bands {
                    if b.ln_f.step(self.a) | b.ln_w.step(self.a) {
                        b.design(self.sr);
                    }
                }
            }
            self.clock -= 1;
            let input = [*l, *r];
            let mut out = input;
            for b in &mut self.bands {
                let fl = b.filter.tick(out[0], 0);
                let fr = b.filter.tick(out[1], 1);
                let (dl, dr) = if self.parallel { (b.det.tick(input[0], 0), b.det.tick(input[1], 1)) } else { (fl, fr) };
                let det = dl.abs().max(dr.abs());
                b.env = if det > b.env { det } else { b.env + (det - b.env) * self.env_rel };
                let want = if b.target_db == 0.0 { 0.0 } else { b.gain_for(level_db(b.env)) };
                let c = if want.abs() > b.gain_db.abs() { b.att } else { b.rel };
                b.gain_db += (want - b.gain_db) * c;
                lowest = lowest.min(b.gain_db);
                if b.shelf {
                    let (k, kb) = shelf_12(b.gain_db);
                    out[0] += k * fl + kb * b.shape.tick(out[0], 0);
                    out[1] += k * fr + kb * b.shape.tick(out[1], 1);
                } else {
                    let k = db_gain(b.gain_db) - 1.0;
                    out[0] += k * fl;
                    out[1] += k * fr;
                }
            }
            *l = rail(out[0] * self.trim);
            *r = rail(out[1] * self.trim);
        }
        self.meter = lowest.min(0.0);
    }

    fn reset(&mut self) {
        for b in &mut self.bands {
            b.reset();
        }
        self.meter = 0.0;
    }

    fn meter(&self) -> f32 {
        self.meter
    }

    fn band_gain(&self, band: usize) -> f32 {
        self.bands.get(band).map_or(0.0, |b| b.gain_db)
    }
}

// ---- Multiband Compressor --------------------------------------------------------------

const MB_SLOPES: &[&str] = &["6", "18", "24"];
const MB_KNEE: &[&str] = &["HARD", "SOFT"];
const MB_BALLISTICS: &[&str] = &["PEAK", "RMS", "PUNCH", "OPTO", "SLOW", "FAST"];
const MB_LINK: &[&str] = &["OFF", "LINK", "REL"];
const MB_KEY: &[&str] = &["SPLIT", "WIDE"];

macro_rules! mb_band {
    ($n:literal) => {
        [
            p(concat!("thr_", $n), concat!("THRESHOLD ", $n), "dB", LIN, -50.0, 18.0, 137, -12.0),
            p(concat!("gain_", $n), concat!("GAIN ", $n), "dB", LIN, -6.0, 18.0, 49, 0.0),
            p(concat!("ratio_", $n), concat!("RATIO ", $n), "", ParamKind::Log, 1.0, 20.0, 101, 2.0),
            tog(concat!("in_", $n), concat!("IN ", $n), 1.0),
            p(concat!("att_", $n), concat!("ATTACK ", $n), "ms", LOG, 0.3, 200.0, 101, 10.0),
            p(concat!("rel_", $n), concat!("RELEASE ", $n), "ms", LOG, 20.0, 2000.0, 101, 150.0),
            ch(concat!("key_", $n), concat!("KEY RANGE ", $n), MB_KEY, 0.0),
        ]
    };
}

const MB_1: [ParamSpec; 7] = mb_band!("1");
const MB_2: [ParamSpec; 7] = mb_band!("2");
const MB_3: [ParamSpec; 7] = mb_band!("3");
const MB_4: [ParamSpec; 7] = mb_band!("4");
const THR_ID: [&str; 4] = ["thr_1", "thr_2", "thr_3", "thr_4"];
const GAIN_ID: [&str; 4] = ["gain_1", "gain_2", "gain_3", "gain_4"];
const RATIO_ID: [&str; 4] = ["ratio_1", "ratio_2", "ratio_3", "ratio_4"];
const IN_ID: [&str; 4] = ["in_1", "in_2", "in_3", "in_4"];
const ATT_ID: [&str; 4] = ["att_1", "att_2", "att_3", "att_4"];
const REL_ID: [&str; 4] = ["rel_1", "rel_2", "rel_3", "rel_4"];
const KEY_ID: [&str; 4] = ["key_1", "key_2", "key_3", "key_4"];

const XO_1: ParamSpec = p("xo_1", "CROSSOVER 1", "Hz", LOG, 40.0, 16000.0, 201, 200.0);
const XO_2: ParamSpec = p("xo_2", "CROSSOVER 2", "Hz", LOG, 40.0, 16000.0, 201, 1000.0);
const XO_3: ParamSpec = p("xo_3", "CROSSOVER 3", "Hz", LOG, 40.0, 16000.0, 201, 5000.0);
const MB_COMMON: [ParamSpec; 4] = [
    ch("slope", "XOVER SLOPE", MB_SLOPES, 2.0),
    ch("knee", "KNEE", MB_KNEE, 1.0),
    ch("ball", "BALLISTICS", MB_BALLISTICS, 0.0),
    ch("link", "LINK", MB_LINK, 0.0),
];

/// Multiband Compressor, 3-band.
pub(super) static MULTI3: [ParamSpec; 27] = join(&[&[XO_1, XO_2], &MB_COMMON, &MB_1, &MB_2, &MB_3]);
/// Multiband Compressor, 4-band.
pub(super) static MULTI4: [ParamSpec; 35] = join(&[&[XO_1, XO_2, XO_3], &MB_COMMON, &MB_1, &MB_2, &MB_3, &MB_4]);

/// A bilinear one-pole: `lo + hi` is the input exactly.
#[derive(Clone, Copy, Default)]
struct OnePoleSplit {
    g: f32,
    s: [f32; 2],
}

impl OnePoleSplit {
    fn set(&mut self, hz: f32, sr: f32) {
        let g = (std::f32::consts::PI * hz / sr).tan();
        self.g = g / (1.0 + g);
    }

    #[inline]
    fn tick(&mut self, ch: usize, x: f32) -> (f32, f32) {
        let v = (x - self.s[ch]) * self.g;
        let lo = v + self.s[ch];
        self.s[ch] = lo + v;
        (lo, x - lo)
    }

    fn reset(&mut self) {
        self.s = [0.0; 2];
    }
}

/// A crossover point: a low and a high band of 6, 18 (Butterworth, an all-pass sum)
/// or 24 dB/oct (Linkwitz-Riley).
#[derive(Clone, Copy, Default)]
struct Split {
    poles: usize,
    one: OnePoleSplit,
    a: [Svf; 2],
    low: [Svf; 2],
    high: [Svf; 2],
}

impl Split {
    fn set(&mut self, hz: f32, poles: usize, sr: f32) {
        self.poles = poles;
        self.one.set(hz, sr);
        let q2 = 0.707;
        for s in self.a.iter_mut() {
            s.set(hz, q2, sr);
        }
        let q = if poles == 3 { 1.0 } else { q2 };
        for s in self.low.iter_mut().chain(self.high.iter_mut()) {
            s.set(hz, q, sr);
        }
    }

    #[inline]
    fn tick(&mut self, ch: usize, x: f32) -> (f32, f32) {
        match self.poles {
            1 => self.one.tick(ch, x),
            3 => {
                let (l, h) = self.one.tick(ch, x);
                (self.low[ch].tick(l).lp, self.high[ch].tick(h).hp)
            }
            _ => {
                let o = self.a[ch].tick(x);
                (self.low[ch].tick(o.lp).lp, self.high[ch].tick(o.hp).hp)
            }
        }
    }

    fn reset(&mut self) {
        self.one.reset();
        for s in self.a.iter_mut().chain(self.low.iter_mut()).chain(self.high.iter_mut()) {
            s.reset();
        }
    }
}

const MAX_MB: usize = 4;

#[derive(Clone, Copy, Default)]
struct MbBand {
    thr: f32,
    slope: f32,
    gain: f32,
    on: bool,
    att: f32,
    rel: f32,
    wide: bool,
    env: f32,
    gr: f32,
}

/// A multiband compressor: crossovers in series with all-pass compensation of the
/// lower bands, so the bands add back up flat, and a linked compressor per band.
pub(super) struct Multiband {
    sr: f32,
    n: usize,
    splits: [Split; 3],
    comp: [Split; 3],
    bands: [MbBand; MAX_MB],
    soft: bool,
    rms: bool,
    rms_coef: f32,
    meter: f32,
}

impl Multiband {
    pub(super) fn new(n: usize, ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        Self {
            sr,
            n,
            splits: [Split::default(); 3],
            comp: [Split::default(); 3],
            bands: [MbBand::default(); MAX_MB],
            soft: true,
            rms: false,
            rms_coef: coef(20.0, sr),
            meter: 0.0,
        }
    }
}

/// How far `over` dB past the threshold reduces at `slope` (1 - 1/ratio).
#[inline]
fn knee_gr(over: f32, slope: f32, soft: bool) -> f32 {
    const KNEE: f32 = 6.0;
    if !soft {
        return over.max(0.0) * slope;
    }
    if over >= 0.5 * KNEE {
        over * slope
    } else if over > -0.5 * KNEE {
        (over + 0.5 * KNEE).powi(2) / (2.0 * KNEE) * slope
    } else {
        0.0
    }
}

/// Attack and release (ms) of the automatic ballistics (Sidecar's reading), with
/// whether the mode detects RMS.
const fn auto_ballistics(mode: usize) -> (f32, f32, bool) {
    match mode {
        2 => (25.0, 150.0, false),
        3 => (15.0, 400.0, true),
        4 => (40.0, 800.0, true),
        _ => (1.0, 60.0, false),
    }
}

impl FxAlgo for Multiband {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        let poles = [1, 3, 4][p.choice("slope").min(2)];
        let mut f = [p.f("xo_1"), p.f("xo_2"), if self.n == 4 { p.f("xo_3") } else { p.f("xo_2") }];
        f[1] = f[1].max(f[0] * 1.2);
        f[2] = f[2].max(f[1] * 1.2);
        for (s, hz) in self.splits.iter_mut().zip(f) {
            s.set(hz.min(sr * 0.45), poles, sr);
        }
        // Compensation: the lowest band through the upper crossovers; the second
        // through the third.
        self.comp[0].set(f[1].min(sr * 0.45), poles, sr);
        self.comp[1].set(f[2].min(sr * 0.45), poles, sr);
        self.comp[2].set(f[2].min(sr * 0.45), poles, sr);
        self.soft = p.choice("knee") == 1;
        let mode = p.choice("ball").min(5);
        self.rms = mode == 1 || (mode >= 2 && auto_ballistics(mode).2);
        // Link: the bands take band 1's. Rel: band 1's change from its centre moves
        // each band's own value.
        let link = p.choice("link");
        let (t0, g0, r0) = (p.f(THR_ID[0]), p.f(GAIN_ID[0]), p.f(RATIO_ID[0]));
        for i in 0..self.n {
            let b = &mut self.bands[i];
            let (thr, gain, ratio) = match link {
                1 => (t0, g0, r0),
                2 => (p.f(THR_ID[i]) + (t0 + 12.0), p.f(GAIN_ID[i]) + g0, p.f(RATIO_ID[i])),
                _ => (p.f(THR_ID[i]), p.f(GAIN_ID[i]), p.f(RATIO_ID[i])),
            };
            b.thr = thr.clamp(-50.0, 18.0) - DBU_OFFSET;
            b.gain = gain.clamp(-6.0, 18.0);
            b.slope = 1.0 - 1.0 / ratio.max(1.0);
            b.on = p.on(IN_ID[i]);
            let (att, rel) =
                if mode < 2 { (p.f(ATT_ID[i]), p.f(REL_ID[i])) } else { (auto_ballistics(mode).0, auto_ballistics(mode).1) };
            b.att = coef(att, sr);
            b.rel = coef(rel, sr);
            b.wide = p.choice(KEY_ID[i]) == 1;
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = self.n;
        let mut lowest = 0.0f32;
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let mut band = [[0.0f32; 2]; MAX_MB];
            for (c, x) in [*l, *r].into_iter().enumerate() {
                let (b0, rest) = self.splits[0].tick(c, x);
                let (b1, rest) = self.splits[1].tick(c, rest);
                let (ap1l, ap1h) = self.comp[0].tick(c, b0);
                let mut b0 = ap1l + ap1h;
                if n == 4 {
                    let (b2, b3) = self.splits[2].tick(c, rest);
                    let (a, b) = self.comp[1].tick(c, b0);
                    b0 = a + b;
                    let (a, b) = self.comp[2].tick(c, b1);
                    band[1][c] = a + b;
                    band[2][c] = b2;
                    band[3][c] = b3;
                } else {
                    band[1][c] = b1;
                    band[2][c] = rest;
                }
                band[0][c] = b0;
            }
            let full = l.abs().max(r.abs());
            let (mut ol, mut or) = (0.0, 0.0);
            for (b, s) in self.bands[..n].iter_mut().zip(band) {
                if !b.on {
                    ol += s[0];
                    or += s[1];
                    continue;
                }
                let key = if b.wide { full } else { s[0].abs().max(s[1].abs()) };
                if self.rms {
                    b.env += (key * key - b.env) * self.rms_coef;
                } else {
                    b.env = if key > b.env { key } else { b.env + (key - b.env) * b.rel };
                }
                let lvl = if self.rms { b.env.sqrt() } else { b.env };
                let target = knee_gr(level_db(lvl) - b.thr, b.slope, self.soft);
                b.gr += (target - b.gr) * if target > b.gr { b.att } else { b.rel };
                lowest = lowest.min(-b.gr);
                let g = db_gain(b.gain - b.gr);
                ol += g * s[0];
                or += g * s[1];
            }
            *l = rail(ol);
            *r = rail(or);
        }
        self.meter = lowest.min(0.0);
    }

    fn reset(&mut self) {
        for s in self.splits.iter_mut().chain(self.comp.iter_mut()) {
            s.reset();
        }
        for b in &mut self.bands {
            b.env = 0.0;
            b.gr = 0.0;
        }
        self.meter = 0.0;
    }

    fn meter(&self) -> f32 {
        self.meter
    }

    /// Readouts 0-3: each band's gain reduction (dB, <= 0).
    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        for (o, b) in out.iter_mut().zip(&self.bands[..self.n]) {
            *o = -b.gr;
        }
    }
}

mod dyn8;
pub(super) use dyn8::{Dyn8, DYN8};
