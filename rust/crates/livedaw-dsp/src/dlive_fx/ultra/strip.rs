//! The channel strip a RackUltra engine has (Firmware Reference Guide; the V2.0 block
//! diagram): send, 4-band PEQ, Gate / Expander / Compressor / Ducker, the FX, a second
//! dynamics section, a second PEQ, Stereo Width, return. Each dynamics section can be
//! keyed from itself, the engine's one external source, or the FX's input or output.
//!
//! Stated: the blocks, their order, the four dynamics types and the key sources. The
//! PEQ is the dLive's 4-band one and the dynamics take the gate's and the compressor's
//! ranges (the Processing specs give them for those blocks). Sidecar's reading: the
//! expander's slope, the Ducker as a keyed gate running backwards, every ballistic not
//! stated, Width as a mid/side balance from 0 (mono) through 100 % (as it comes) to 200 %.
//! A key taken from the FX's output by the first section is the previous block's output,
//! the one time this signal is not there yet.
//!
//! It runs around an FX device; the engine owns one per RackUltra engine:
//! [`UltraStrip::process`] takes the values of [`STRIP`] and a closure that runs the device.

use crate::dlive;
use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec};
use crate::sq::{FREQ, GAIN, WIDTH};
use crate::wing::eq::WingEq;
use crate::wing::fx::blocks::coef;
use crate::wing::fx::utility::{db_gain, level_db};

const TYPES: &[&str] = &["GATE", "EXPANDER", "COMPRESSOR", "DUCKER"];
const KEYS: &[&str] = &["SELF", "EXTERNAL", "FX INPUT", "FX OUTPUT"];

macro_rules! p {
    ($id:expr, $label:expr, $unit:expr, $kind:expr, $min:expr, $max:expr, $steps:expr, $d:expr) => {
        ParamSpec { id: $id, label: $label, unit: $unit, kind: $kind, min: $min, max: $max, steps: $steps, default: $d }
    };
}

/// A section's Dynamics: on, type, key, threshold, ratio, depth, attack, hold, release.
macro_rules! dyn_section {
    ($pre:literal, $lab:literal) => {
        [
            p!(concat!($pre, "on"), concat!($lab, "DYN IN"), "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
            p!(concat!($pre, "ty"), concat!($lab, "DYN TYPE"), "", ParamKind::Choice(TYPES), 0.0, 3.0, 4, 2.0),
            p!(concat!($pre, "ky"), concat!($lab, "DYN KEY"), "", ParamKind::Choice(KEYS), 0.0, 3.0, 4, 0.0),
            p!(concat!($pre, "th"), concat!($lab, "THRESHOLD"), "dB", ParamKind::Linear, -72.0, 18.0, 181, -20.0),
            p!(concat!($pre, "ra"), concat!($lab, "RATIO"), "", ParamKind::Log, 1.0, 20.0, 101, 3.0),
            p!(concat!($pre, "de"), concat!($lab, "DEPTH"), "dB", ParamKind::Linear, 0.0, 60.0, 61, 20.0),
            p!(concat!($pre, "at"), concat!($lab, "ATTACK"), "ms", ParamKind::Log, 0.05, 300.0, 101, 5.0),
            p!(concat!($pre, "ho"), concat!($lab, "HOLD"), "ms", ParamKind::Log, 10.0, 5000.0, 101, 50.0),
            p!(concat!($pre, "re"), concat!($lab, "RELEASE"), "ms", ParamKind::Log, 10.0, 1000.0, 101, 200.0),
        ]
    };
}

/// The 14 values of a 4-band PEQ with its In first (`dlive::EQ[0]`'s order after it).
macro_rules! peq_section {
    ($pre:literal, $lab:literal) => {
        [
            p!(concat!($pre, "in"), concat!($lab, "PEQ IN"), "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
            p!(concat!($pre, "lm"), concat!($lab, "LF MODE"), "", ParamKind::Choice(&["SHELF", "BELL", "HPF"]), 0.0, 2.0, 3, 0.0),
            p!(concat!($pre, "lf"), concat!($lab, "LF F"), "Hz", ParamKind::Log, FREQ.0, FREQ.1, FREQ.2, 100.0),
            p!(concat!($pre, "lg"), concat!($lab, "LF G"), "dB", ParamKind::Linear, GAIN.0, GAIN.1, GAIN.2, 0.0),
            p!(concat!($pre, "lq"), concat!($lab, "LF W"), "oct", ParamKind::Log, WIDTH.0, WIDTH.1, WIDTH.2, 1.0),
            p!(concat!($pre, "1f"), concat!($lab, "LM F"), "Hz", ParamKind::Log, FREQ.0, FREQ.1, FREQ.2, 400.0),
            p!(concat!($pre, "1g"), concat!($lab, "LM G"), "dB", ParamKind::Linear, GAIN.0, GAIN.1, GAIN.2, 0.0),
            p!(concat!($pre, "1q"), concat!($lab, "LM W"), "oct", ParamKind::Log, WIDTH.0, WIDTH.1, WIDTH.2, 1.0),
            p!(concat!($pre, "2f"), concat!($lab, "HM F"), "Hz", ParamKind::Log, FREQ.0, FREQ.1, FREQ.2, 2000.0),
            p!(concat!($pre, "2g"), concat!($lab, "HM G"), "dB", ParamKind::Linear, GAIN.0, GAIN.1, GAIN.2, 0.0),
            p!(concat!($pre, "2q"), concat!($lab, "HM W"), "oct", ParamKind::Log, WIDTH.0, WIDTH.1, WIDTH.2, 1.0),
            p!(concat!($pre, "hm"), concat!($lab, "HF MODE"), "", ParamKind::Choice(&["SHELF", "BELL", "LPF"]), 0.0, 2.0, 3, 0.0),
            p!(concat!($pre, "hf"), concat!($lab, "HF F"), "Hz", ParamKind::Log, FREQ.0, FREQ.1, FREQ.2, 8000.0),
            p!(concat!($pre, "hg"), concat!($lab, "HF G"), "dB", ParamKind::Linear, GAIN.0, GAIN.1, GAIN.2, 0.0),
            p!(concat!($pre, "hq"), concat!($lab, "HF W"), "oct", ParamKind::Log, WIDTH.0, WIDTH.1, WIDTH.2, 1.0),
        ]
    };
}

const PEQ_LEN: usize = 15;
const DYN_LEN: usize = 9;
const PRE_PEQ: usize = 0;
const PRE_DYN: usize = PRE_PEQ + PEQ_LEN;
const POST_DYN: usize = PRE_DYN + DYN_LEN;
const POST_PEQ: usize = POST_DYN + DYN_LEN;
const WIDTH_AT: usize = POST_PEQ + PEQ_LEN;
const STRIP_LEN: usize = WIDTH_AT + 1;

const fn strip_params() -> [ParamSpec; STRIP_LEN] {
    let pre_peq: [ParamSpec; PEQ_LEN] = peq_section!("a_", "PRE ");
    let pre_dyn: [ParamSpec; DYN_LEN] = dyn_section!("b_", "PRE ");
    let post_dyn: [ParamSpec; DYN_LEN] = dyn_section!("c_", "POST ");
    let post_peq: [ParamSpec; PEQ_LEN] = peq_section!("d_", "POST ");
    let mut out = [p!("width", "WIDTH", "%", ParamKind::Linear, 0.0, 200.0, 201, 100.0); STRIP_LEN];
    let mut i = 0;
    while i < PEQ_LEN {
        out[PRE_PEQ + i] = pre_peq[i];
        out[POST_PEQ + i] = post_peq[i];
        i += 1;
    }
    let mut i = 0;
    while i < DYN_LEN {
        out[PRE_DYN + i] = pre_dyn[i];
        out[POST_DYN + i] = post_dyn[i];
        i += 1;
    }
    out
}

/// The strip's controls.
pub static STRIP_PARAMS: [ParamSpec; STRIP_LEN] = strip_params();
/// The strip's values, in this order (not a block a strip or slot holds: the engine
/// runs one per RackUltra engine around its FX device).
pub static STRIP: ModelSpec =
    ModelSpec { token: "DLVUSTRIP", name: "RackUltra Strip", category: ModelCategory::FxChannel, params: &STRIP_PARAMS };

/// dBu to dBFS (+18 dBu = 0 dBFS, as everywhere on the dLive).
const DBU_OFFSET: f32 = 18.0;
const SOFT_DB: f32 = 2.0;

/// A dynamics section.
struct Dyn {
    on: bool,
    kind: usize,
    key: usize,
    thr: f32,
    ratio: f32,
    depth: f32,
    att: f32,
    rel: f32,
    hold: u32,
    env: f32,
    gain_db: f32,
    timer: u32,
}

impl Default for Dyn {
    fn default() -> Self {
        Self {
            on: false,
            kind: 2,
            key: 0,
            thr: 0.0,
            ratio: 1.0,
            depth: 0.0,
            att: 1.0,
            rel: 1.0,
            hold: 0,
            env: 0.0,
            gain_db: 0.0,
            timer: 0,
        }
    }
}

impl Dyn {
    fn set(&mut self, v: &[f32], sr: f32) {
        self.on = v[0] >= 0.5;
        self.kind = (v[1] as usize).min(3);
        self.key = (v[2] as usize).min(3);
        self.thr = v[3] - DBU_OFFSET;
        self.ratio = v[4].max(1.0);
        self.depth = v[5];
        self.att = coef(v[6], sr);
        self.hold = (v[7] * 0.001 * sr) as u32;
        self.rel = coef(v[8], sr);
    }

    #[inline]
    fn target(&mut self, level_db: f32) -> f32 {
        let over = level_db - self.thr;
        match self.kind {
            0 => {
                // Gate: closed by DEPTH below the threshold, held open HOLD after.
                if over >= 0.0 {
                    self.timer = self.hold;
                    0.0
                } else if self.timer > 0 {
                    self.timer -= 1;
                    0.0
                } else {
                    -self.depth
                }
            }
            1 => (over * 2.0).clamp(-self.depth, 0.0),
            2 => {
                let k = over + SOFT_DB;
                if over >= SOFT_DB {
                    -over * (1.0 - 1.0 / self.ratio)
                } else if over > -SOFT_DB {
                    -(k * k) / (4.0 * SOFT_DB) * (1.0 - 1.0 / self.ratio)
                } else {
                    0.0
                }
            }
            _ => {
                // Ducker: down by DEPTH while the key is over the threshold, then HOLD.
                if over >= 0.0 {
                    self.timer = self.hold;
                    -self.depth
                } else if self.timer > 0 {
                    self.timer -= 1;
                    -self.depth
                } else {
                    0.0
                }
            }
        }
    }

    /// Runs the section on the block (linked): `key` is its detector's signal.
    fn run(&mut self, left: &mut [f32], right: &mut [f32], key: (&[f32], &[f32])) {
        for (i, (l, r)) in left.iter_mut().zip(right.iter_mut()).enumerate() {
            let a = key.0[i].abs().max(key.1[i].abs());
            self.env = if a > self.env { a } else { self.env + (a - self.env) * self.rel };
            let want = self.target(level_db(self.env));
            let c = if want < self.gain_db { self.att } else { self.rel };
            self.gain_db += (want - self.gain_db) * c;
            let g = db_gain(self.gain_db);
            *l *= g;
            *r *= g;
        }
    }

    fn reset(&mut self) {
        self.env = 0.0;
        self.gain_db = 0.0;
        self.timer = 0;
    }
}

/// One RackUltra engine's strip, around its FX device.
pub struct UltraStrip {
    sr: f32,
    pre_eq: WingEq,
    post_eq: WingEq,
    pre_dyn: Dyn,
    post_dyn: Dyn,
    // Taps of the block: the FX's input and output.
    fx_in: [Vec<f32>; 2],
    fx_out: [Vec<f32>; 2],
    /// A copy of the block, as a section's key when it keys from itself.
    scratch: [Vec<f32>; 2],
    width: f32,
}

impl UltraStrip {
    /// Message thread (allocates): blocks are at most `max_block` long.
    pub fn new(sample_rate: f32, max_block: usize) -> Self {
        let mut pre_eq = WingEq::default();
        let mut post_eq = WingEq::default();
        pre_eq.prepare(f64::from(sample_rate));
        post_eq.prepare(f64::from(sample_rate));
        let buf = || vec![0.0f32; max_block];
        Self {
            sr: sample_rate,
            pre_eq,
            post_eq,
            pre_dyn: Dyn::default(),
            post_dyn: Dyn::default(),
            fx_in: [buf(), buf()],
            fx_out: [buf(), buf()],
            scratch: [buf(), buf()],
            width: 1.0,
        }
    }

    pub fn reset(&mut self) {
        self.pre_eq.reset();
        self.post_eq.reset();
        self.pre_dyn.reset();
        self.post_dyn.reset();
        for b in self.fx_in.iter_mut().chain(self.fx_out.iter_mut()) {
            b.fill(0.0);
        }
    }

    /// Audio thread: the whole strip around `device` (which runs the FX in place),
    /// `values` in [`STRIP`]'s order, `key` the engine's external key if one is
    /// patched. Blocks of at most the `max_block` given to [`Self::new`].
    pub fn process(
        &mut self,
        values: &[f32],
        left: &mut [f32],
        right: &mut [f32],
        key: Option<(&[f32], &[f32])>,
        device: &mut dyn FnMut(&mut [f32], &mut [f32]),
    ) {
        if values.len() < STRIP_LEN || left.len() != right.len() || left.len() > self.fx_in[0].len() {
            return;
        }
        let n = left.len();
        let v = |at: usize, len: usize| &values[at..at + len];
        self.pre_dyn.set(v(PRE_DYN, DYN_LEN), self.sr);
        self.post_dyn.set(v(POST_DYN, DYN_LEN), self.sr);
        self.width = (values[WIDTH_AT] * 0.01).clamp(0.0, 2.0);

        // ---- pre: PEQ, dynamics ----
        run_eq(&mut self.pre_eq, values[PRE_PEQ] >= 0.5, &values[PRE_PEQ + 1..PRE_PEQ + PEQ_LEN], left, right);
        if self.pre_dyn.on {
            match self.pre_dyn.key {
                3 => self.pre_dyn.run(left, right, (&self.fx_out[0][..n], &self.fx_out[1][..n])),
                1 if key.is_some() => {
                    let k = key.unwrap_or((&[], &[]));
                    self.pre_dyn.run(left, right, k);
                }
                _ => {
                    self.scratch[0][..n].copy_from_slice(left);
                    self.scratch[1][..n].copy_from_slice(right);
                    self.pre_dyn.run(left, right, (&self.scratch[0][..n], &self.scratch[1][..n]));
                }
            }
        }
        self.fx_in[0][..n].copy_from_slice(left);
        self.fx_in[1][..n].copy_from_slice(right);

        // ---- the FX ----
        device(left, right);
        self.fx_out[0][..n].copy_from_slice(left);
        self.fx_out[1][..n].copy_from_slice(right);

        // ---- post: dynamics, PEQ, Width ----
        if self.post_dyn.on {
            let k = match self.post_dyn.key {
                1 => key.unwrap_or((&self.fx_out[0][..n], &self.fx_out[1][..n])),
                2 => (&self.fx_in[0][..n], &self.fx_in[1][..n]),
                _ => (&self.fx_out[0][..n], &self.fx_out[1][..n]),
            };
            self.post_dyn.run(left, right, k);
        }
        run_eq(&mut self.post_eq, values[POST_PEQ] >= 0.5, &values[POST_PEQ + 1..POST_PEQ + PEQ_LEN], left, right);
        let w = self.width;
        if (w - 1.0).abs() > 1e-4 {
            for (l, r) in left.iter_mut().zip(right.iter_mut()) {
                let (m, s) = (0.5 * (*l + *r), 0.5 * (*l - *r) * w);
                *l = m + s;
                *r = m - s;
            }
        }
    }
}

fn run_eq(eq: &mut WingEq, on: bool, values: &[f32], left: &mut [f32], right: &mut [f32]) {
    eq.process(&mut [left, right], &dlive::EQ, 0, on, 100.0, values);
}

#[cfg(test)]
pub(super) const fn len() -> usize {
    STRIP_LEN
}
