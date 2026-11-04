//! The X32's compressors and Wave Designer (Phase 4 step 9f): the Fair
//! Compressor (FAC, FAC2, FAC1M), Leisure Compressor (LEC, LEC2), Ultimo
//! Compressor (ULC, ULC2) and Wave Designer (WAVD) are the designs of the
//! WING's channel dynamics models F670, LA, 76LA and WAVE, so they run on
//! the WING's dynamics engine ([`WingDynamics`]) as an FX device: the stereo
//! models as one linked unit, the dual ones as one unit per side (A left, B
//! right), FAC1M on the mid and the side signals.
//!
//! Each side has its ACTIVE (off: the signal passes) and its GAIN after the
//! unit. Where the controls differ, Sidecar's reading (to be measured on an
//! X32): the Fair's INPUT GAIN up to 0 dB is the F670's IN, above it a gain
//! ahead of it; its TIME 0 is the F670's 1; BIAS 0-100 its 0-1; BALANCE the
//! output's left / right balance. The Wave Designer's ATTACK and SUSTAIN
//! (+-100) are the WAVE's +-15 and +-24 dB at full scale.

use super::{FxAlgo, FxContext};
use crate::model::{ModelSpec, Params};
use crate::util::db_to_gain;
use crate::wing::dynamics::{SidechainSettings, WingDynamics, XoverSettings};
use crate::wing::{DYN_MODELS, MAX_DYN_PARAMS};

/// The X32 models run on the WING's dynamics.
pub(super) const TOKENS: &[&str] = &["X32FAC", "X32FAC2", "X32FAC1M", "X32LEC", "X32LEC2", "X32ULC", "X32ULC2", "X32WAVD"];

pub(super) fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    if !TOKENS.contains(&token) {
        return None;
    }
    let (wing, layout, map): (&str, Layout, SideMap) = match token {
        "X32FAC" => ("F670", Layout::Stereo, |p, _, set| fair(p, None, set)),
        "X32FAC2" => ("F670", Layout::Dual, |p, s, set| fair(p, Some(["a", "b"][s]), set)),
        "X32FAC1M" => ("F670", Layout::MidSide, |p, s, set| fair(p, Some(["m", "s"][s]), set)),
        "X32LEC" => ("LA", Layout::Stereo, |p, _, set| leisure(p, None, set)),
        "X32LEC2" => ("LA", Layout::Dual, |p, s, set| leisure(p, Some(["a", "b"][s]), set)),
        "X32ULC" => ("76LA", Layout::Stereo, |p, _, set| ultimo(p, None, set)),
        "X32ULC2" => ("76LA", Layout::Dual, |p, s, set| ultimo(p, Some(["a", "b"][s]), set)),
        "X32WAVD" => ("WAVE", Layout::Dual, wave),
        _ => return None,
    };
    let model = crate::model::model_index(DYN_MODELS, wing)?;
    let spec = &DYN_MODELS[model];
    let mut values = [0.0; MAX_DYN_PARAMS];
    spec.defaults_into(&mut values);
    let unit = || {
        let mut d = WingDynamics::default();
        d.prepare(ctx.sample_rate as f64);
        d
    };
    Some(Box::new(Dynamics {
        units: [unit(), unit()],
        spec,
        model,
        layout,
        map,
        values: [values; 2],
        sides: [Side::default(); 2],
        last_balance: (1.0, 1.0),
        work: [vec![0.0; ctx.max_block], vec![0.0; ctx.max_block]],
    }))
}

#[derive(Clone, Copy, PartialEq)]
enum Layout {
    /// One unit on both channels (linked).
    Stereo,
    /// A unit per side.
    Dual,
    /// A unit on the mid signal, one on the side.
    MidSide,
}

/// One unit's settings besides its model's controls.
#[derive(Clone, Copy)]
struct Side {
    on: bool,
    /// GAIN after the unit, dB.
    makeup_db: f32,
    /// Gain ahead of the unit, linear.
    pre: f32,
    /// BALANCE as (left, right) output gains.
    balance: (f32, f32),
}

impl Default for Side {
    fn default() -> Self {
        Self { on: true, makeup_db: 0.0, pre: 1.0, balance: (1.0, 1.0) }
    }
}

/// Sets side `side`'s WING controls; returns its other settings.
type SideMap = fn(&Params, usize, &mut dyn FnMut(&'static str, f32)) -> Side;

struct Dynamics {
    units: [WingDynamics; 2],
    spec: &'static ModelSpec,
    model: usize,
    layout: Layout,
    map: SideMap,
    values: [[f32; MAX_DYN_PARAMS]; 2],
    sides: [Side; 2],
    last_balance: (f32, f32),
    /// Mid and side (FAC1M), or a side's copy.
    work: [Vec<f32>; 2],
}

impl FxAlgo for Dynamics {
    fn set_params(&mut self, p: &Params) {
        let n = if self.layout == Layout::Stereo { 1 } else { 2 };
        for side in 0..n {
            let (spec, values) = (self.spec, &mut self.values[side]);
            self.sides[side] = (self.map)(p, side, &mut |id, v| {
                if let Some(i) = spec.param_index(id) {
                    values[i] = v;
                }
            });
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len().min(self.work[0].len());
        let (left, right) = (&mut left[..n], &mut right[..n]);
        let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
        match self.layout {
            Layout::Stereo => {
                let s = self.sides[0];
                if s.on || self.units[0].is_running(s.on) {
                    scale(left, s.pre);
                    scale(right, s.pre);
                    let mut chans: [&mut [f32]; 2] = [&mut *left, &mut *right];
                    self.units[0].process_slot(&mut chans, None, self.model, &self.values[0], &sc, &xo, 1.0, s.makeup_db, s.on);
                }
                self.balance(left, right, s.balance);
            }
            Layout::Dual => {
                for (k, x) in [&mut *left, &mut *right].into_iter().enumerate() {
                    let s = self.sides[k];
                    if s.on || self.units[k].is_running(s.on) {
                        scale(x, s.pre);
                        let mut chans: [&mut [f32]; 1] = [x];
                        self.units[k].process_slot(
                            &mut chans,
                            None,
                            self.model,
                            &self.values[k],
                            &sc,
                            &xo,
                            1.0,
                            s.makeup_db,
                            s.on,
                        );
                    }
                }
            }
            Layout::MidSide => {
                let [m, sd] = &mut self.work;
                for i in 0..n {
                    m[i] = 0.5 * (left[i] + right[i]);
                    sd[i] = 0.5 * (left[i] - right[i]);
                }
                for (k, x) in [&mut m[..n], &mut sd[..n]].into_iter().enumerate() {
                    let s = self.sides[k];
                    if s.on || self.units[k].is_running(s.on) {
                        scale(x, s.pre);
                        let mut chans: [&mut [f32]; 1] = [x];
                        self.units[k].process_slot(
                            &mut chans,
                            None,
                            self.model,
                            &self.values[k],
                            &sc,
                            &xo,
                            1.0,
                            s.makeup_db,
                            s.on,
                        );
                    }
                }
                for i in 0..n {
                    left[i] = m[i] + sd[i];
                    right[i] = m[i] - sd[i];
                }
            }
        }
    }

    fn reset(&mut self) {
        for u in &mut self.units {
            u.reset();
        }
    }
}

impl Dynamics {
    /// The output's balance, ramped over the block.
    fn balance(&mut self, left: &mut [f32], right: &mut [f32], to: (f32, f32)) {
        let from = self.last_balance;
        self.last_balance = to;
        if from == (1.0, 1.0) && to == (1.0, 1.0) {
            return;
        }
        let n = left.len().max(1) as f32;
        for (i, (l, r)) in left.iter_mut().zip(right.iter_mut()).enumerate() {
            let t = (i + 1) as f32 / n;
            *l *= from.0 + (to.0 - from.0) * t;
            *r *= from.1 + (to.1 - from.1) * t;
        }
    }
}

fn scale(x: &mut [f32], g: f32) {
    if g != 1.0 {
        x.iter_mut().for_each(|v| *v *= g);
    }
}

/// Control `base` of the stereo model, or of side `sfx` ("base_a").
fn f(p: &Params, base: &str, sfx: Option<&str>) -> f32 {
    match sfx {
        Some(s) => p.f(&super::x32_voices::format_id(base, s)),
        None => p.f(base),
    }
}

fn on(p: &Params, sfx: Option<&str>) -> bool {
    f(p, "active", sfx) >= 0.5
}

fn fair(p: &Params, sfx: Option<&str>, set: &mut dyn FnMut(&'static str, f32)) -> Side {
    let input = f(p, "input_gain", sfx);
    set("in", input.clamp(-20.0, 0.0));
    set("thr", f(p, "threshold", sfx));
    set("time", f(p, "time", sfx).max(1.0));
    set("bias", 0.01 * f(p, "bias", sfx));
    let b = (0.01 * f(p, "balance", sfx)).clamp(-1.0, 1.0);
    Side {
        on: on(p, sfx),
        makeup_db: f(p, "gain", sfx),
        pre: db_to_gain(input.max(0.0)),
        balance: ((1.0 - b).min(1.0), (1.0 + b).min(1.0)),
    }
}

fn leisure(p: &Params, sfx: Option<&str>, set: &mut dyn FnMut(&'static str, f32)) -> Side {
    set("ingain", f(p, "gain", sfx));
    set("peak", f(p, "peak", sfx));
    set("mode", f(p, "mode", sfx));
    // The output GAIN: "gain_2" on the stereo model, "gain_a_2" on the dual.
    let out = match sfx {
        Some(s) => {
            let mut id = super::x32_voices::format_id("gain", s);
            id.push("_2");
            p.f(&id)
        }
        None => p.f("gain_2"),
    };
    Side { on: on(p, sfx), makeup_db: out, ..Side::default() }
}

fn ultimo(p: &Params, sfx: Option<&str>, set: &mut dyn FnMut(&'static str, f32)) -> Side {
    for (wing, x32) in [("in", "input_gain"), ("out", "out_gain"), ("att", "attack"), ("rel", "release"), ("ratio", "ratio")] {
        set(wing, f(p, x32, sfx));
    }
    Side { on: on(p, sfx), ..Side::default() }
}

fn wave(p: &Params, side: usize, set: &mut dyn FnMut(&'static str, f32)) -> Side {
    let sfx = Some(["a", "b"][side]);
    set("att", 0.15 * f(p, "attack", sfx));
    set("sust", 0.24 * f(p, "sustain", sfx));
    set("g", 0.0);
    Side { makeup_db: f(p, "gain", sfx), ..Side::default() }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wing::MAX_FX_PARAMS;

    #[test]
    fn devices() {
        for token in TOKENS {
            crate::wing::fx::tests::check_device(token);
        }
    }

    /// The level (dB) of a 1 kHz sine at `in_db` through `token` with `edit`, after 1 s.
    fn level(token: &str, edit: &[(&str, f32)], in_db: f32) -> (f32, f32) {
        let x32 = crate::x32::FX_MODELS;
        let spec = &x32[crate::model::model_index(x32, token).unwrap()];
        let mut values = [0.0; MAX_FX_PARAMS];
        spec.defaults_into(&mut values);
        for (id, v) in edit {
            values[spec.param_index(id).unwrap()] = *v;
        }
        let mut dev = create(token, FxContext { sample_rate: 48_000.0, max_block: 256 }).unwrap();
        dev.set_params(&Params::new(spec, &values));
        let a = db_to_gain(in_db);
        let (mut peak_l, mut peak_r) = (0.0f32, 0.0f32);
        for b in 0..(48_000 / 256) {
            let mut l: Vec<f32> =
                (0..256).map(|i| a * (std::f32::consts::TAU * 1000.0 * (b * 256 + i) as f32 / 48_000.0).sin()).collect();
            let mut r = l.clone();
            dev.process(&mut l, &mut r);
            if b > 48_000 / 512 {
                peak_l = l.iter().fold(peak_l, |m, x| m.max(x.abs()));
                peak_r = r.iter().fold(peak_r, |m, x| m.max(x.abs()));
            }
        }
        (crate::util::gain_to_db(peak_l), crate::util::gain_to_db(peak_r))
    }

    #[test]
    fn the_compressors_reduce_loud_signals_and_pass_when_inactive() {
        for token in ["X32FAC", "X32LEC", "X32ULC"] {
            let (on, _) = level(token, &[], 0.0);
            let (off, _) = level(token, &[("active", 0.0)], 0.0);
            assert!((off - 0.0).abs() < 0.1, "{token} off passes: {off}");
            assert!(on < off - 1.0 || token == "X32ULC", "{token} compresses: {on} vs {off}");
        }
        // The dual: B off passes the right side; A's GAIN is its output gain.
        let (l, r) = level("X32LEC2", &[("active_b", 0.0), ("gain_a_2", -6.0)], -20.0);
        assert!((r + 20.0).abs() < 0.1, "B off: {r}");
        assert!(l < r - 3.0, "A down: {l}");
        // The Fair's BALANCE takes one side down.
        let (l, r) = level("X32FAC", &[("balance", 100.0)], -30.0);
        assert!(l <= -99.0 && r > -40.0, "{l} {r}");
    }
}
