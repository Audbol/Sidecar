//! The X32's Program EQ (P1A, P1A2) and Midrange EQ (PQ5, PQ5S) (Phase 4
//! step 9g): the two halves of the WING's PULSAR channel EQ model, its EQ1
//! (the program equalizer) and its EQ5 (the midrange one), with the same
//! frequency lists. They run on the WING's EQ engine ([`WingEq`]) as an FX
//! device: the stereo models as one EQ, the dual ones one per side (A left,
//! B right). ACTIVE off passes the side; GAIN is a gain after the EQ.
//!
//! Sidecar's reading (to be measured on an X32): Program EQ's MID WIDTH and
//! MID BOOST are the EQ1's high-boost bandwidth and boost at MID FREQ, its
//! HI ATTENUATION at HI FREQ the EQ1's attenuator; Midrange EQ's MID BOOST is
//! the EQ5's mid section (the MEQ-5's dip); TRANSFORMER is kept but not acted on.

use super::{FxAlgo, FxContext};
use crate::model::{ModelSpec, Params};
use crate::util::db_to_gain;
use crate::wing::eq::WingEq;
use crate::wing::{EQ_MODELS_CH, MAX_EQ_PARAMS};

/// The X32 models run on the WING's PULSAR EQ.
pub(super) const TOKENS: &[&str] = &["X32P1A", "X32P1A2", "X32PQ5", "X32PQ5S"];

pub(super) fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    if !TOKENS.contains(&token) {
        return None;
    }
    let (dual, map): (bool, SideMap) = match token {
        "X32P1A" => (false, |p, _, set| program(p, None, set)),
        "X32P1A2" => (true, |p, s, set| program(p, Some(["a", "b"][s]), set)),
        "X32PQ5" => (false, |p, _, set| midrange(p, None, set)),
        _ => (true, |p, s, set| midrange(p, Some(["a", "b"][s]), set)),
    };
    let model = crate::model::model_index(EQ_MODELS_CH, "PULSAR")?;
    let spec = &EQ_MODELS_CH[model];
    let mut values = [0.0; MAX_EQ_PARAMS];
    spec.defaults_into(&mut values);
    let eq = || {
        let mut e = WingEq::default();
        e.prepare(ctx.sample_rate as f64);
        e
    };
    Some(Box::new(Eq {
        eqs: [eq(), eq()],
        spec,
        model,
        dual,
        map,
        values: [values; 2],
        on: [true; 2],
        gain: [1.0; 2],
        last_gain: [1.0; 2],
    }))
}

/// Sets side `side`'s PULSAR controls; returns ACTIVE and GAIN (dB).
type SideMap = fn(&Params, usize, &mut dyn FnMut(&'static str, f32)) -> (bool, f32);

struct Eq {
    eqs: [WingEq; 2],
    spec: &'static ModelSpec,
    model: usize,
    dual: bool,
    map: SideMap,
    values: [[f32; MAX_EQ_PARAMS]; 2],
    on: [bool; 2],
    gain: [f32; 2],
    last_gain: [f32; 2],
}

impl FxAlgo for Eq {
    fn set_params(&mut self, p: &Params) {
        for side in 0..if self.dual { 2 } else { 1 } {
            let (spec, values) = (self.spec, &mut self.values[side]);
            let (on, db) = (self.map)(p, side, &mut |id, v| {
                if let Some(i) = spec.param_index(id) {
                    values[i] = v;
                }
            });
            self.on[side] = on;
            self.gain[side] = if on { db_to_gain(db) } else { 1.0 };
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let table = EQ_MODELS_CH;
        if self.dual {
            for (k, x) in [&mut *left, &mut *right].into_iter().enumerate() {
                let mut chans: [&mut [f32]; 1] = [&mut *x];
                self.eqs[k].process(&mut chans, table, self.model, self.on[k], 100.0, &self.values[k]);
                crate::util::apply_gain_ramp(x, self.last_gain[k], self.gain[k]);
                self.last_gain[k] = self.gain[k];
            }
        } else {
            let mut chans: [&mut [f32]; 2] = [&mut *left, &mut *right];
            self.eqs[0].process(&mut chans, table, self.model, self.on[0], 100.0, &self.values[0]);
            crate::util::apply_gain_ramp(left, self.last_gain[0], self.gain[0]);
            crate::util::apply_gain_ramp(right, self.last_gain[0], self.gain[0]);
            self.last_gain[0] = self.gain[0];
        }
    }

    fn reset(&mut self) {
        for e in &mut self.eqs {
            e.reset();
        }
        self.last_gain = self.gain;
    }
}

/// Control `base` of the stereo model, or of side `sfx` ("base_a").
fn f(p: &Params, base: &str, sfx: Option<&str>) -> f32 {
    match sfx {
        Some(s) => p.f(&super::x32_voices::format_id(base, s)),
        None => p.f(base),
    }
}

fn program(p: &Params, sfx: Option<&str>, set: &mut dyn FnMut(&'static str, f32)) -> (bool, f32) {
    set("eq1", 1.0);
    set("eq5", 0.0);
    let pairs = [("1lb", "lo_boost"), ("1latt", "lo_atten"), ("1lf", "lo_freq"), ("1hw", "mid_width"), ("1hb", "mid_boost")];
    for (wing, x32) in pairs.into_iter().chain([("1hf", "mid_freq"), ("1hatt", "hi_attenuation"), ("1hattf", "hi_freq")]) {
        set(wing, f(p, x32, sfx));
    }
    (f(p, "active", sfx) >= 0.5, f(p, "gain", sfx))
}

fn midrange(p: &Params, sfx: Option<&str>, set: &mut dyn FnMut(&'static str, f32)) -> (bool, f32) {
    set("eq1", 0.0);
    set("eq5", 1.0);
    let pairs = [("5lf", "lo_freq"), ("5lb", "lo_boost"), ("5mf", "mid_freq"), ("5md", "mid_boost"), ("5hf", "hi_freq")];
    for (wing, x32) in pairs.into_iter().chain([("5hb", "hi_boost")]) {
        set(wing, f(p, x32, sfx));
    }
    (f(p, "active", sfx) >= 0.5, f(p, "gain", sfx))
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

    #[test]
    fn every_mapped_control_is_pulsars_and_every_read_one_the_x32_models() {
        let spec = &EQ_MODELS_CH[crate::model::model_index(EQ_MODELS_CH, "PULSAR").unwrap()];
        for (token, side) in [("X32P1A", None), ("X32P1A2", Some("b")), ("X32PQ5", None), ("X32PQ5S", Some("a"))] {
            let x32 = &crate::x32::FX_MODELS[crate::model::model_index(crate::x32::FX_MODELS, token).unwrap()];
            let mut values = [0.0; MAX_FX_PARAMS];
            x32.defaults_into(&mut values);
            let p = Params::new(x32, &values);
            let mut ids = Vec::new();
            let mut set = |id: &'static str, _: f32| ids.push(id);
            if token.contains("P1A") {
                program(&p, side, &mut set);
            } else {
                midrange(&p, side, &mut set);
            }
            for id in ids {
                assert!(spec.param_index(id).is_some(), "{token}: {id}");
            }
            for base in ["active", "gain", "lo_boost", "mid_freq", "hi_freq"] {
                let id = match side {
                    Some(s) => super::super::x32_voices::format_id(base, s).to_string(),
                    None => base.to_string(),
                };
                assert!(x32.param_index(&id).is_some(), "{token}: {id}");
            }
        }
    }

    #[test]
    fn active_off_passes_and_gain_scales() {
        let x32 = crate::x32::FX_MODELS;
        let spec = &x32[crate::model::model_index(x32, "X32P1A").unwrap()];
        let mut values = [0.0; MAX_FX_PARAMS];
        spec.defaults_into(&mut values);
        let run = |values: &[f32]| {
            let mut dev = create("X32P1A", FxContext { sample_rate: 48_000.0, max_block: 256 }).unwrap();
            dev.set_params(&Params::new(spec, values));
            let (mut l, mut r) = (vec![0.0f32; 256], vec![0.0f32; 256]);
            for _ in 0..40 {
                l.fill(0.25);
                r.fill(0.25);
                dev.process(&mut l, &mut r);
            }
            l[255]
        };
        values[spec.param_index("active").unwrap()] = 0.0;
        assert!((run(&values) - 0.25).abs() < 1e-6, "off");
        values[spec.param_index("active").unwrap()] = 1.0;
        // Flat (no boost or cut) and GAIN +6 dB: DC doubles.
        for id in ["lo_boost", "lo_atten", "mid_boost", "hi_attenuation"] {
            values[spec.param_index(id).unwrap()] = 0.0;
        }
        values[spec.param_index("gain").unwrap()] = 6.0206;
        assert!((run(&values) - 0.5).abs() < 0.01, "{}", run(&values));
    }
}
