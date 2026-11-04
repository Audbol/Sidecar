//! The X32's devices that run on one of the WING's (Phase 4 step 9d): the two
//! consoles' Hall, Vintage Room and Stereo Chorus are the same designs with
//! the same controls, so the X32 model's values are mapped onto the WING
//! device's controls (over ranges wide enough for both) and the WING device
//! runs them. The X32's Stereo Delay is the WING's with a factor and a
//! feedback per side (`delay::stereo`, X32DLY).
//!
//! Where the ranges differ the mapping is Sidecar's reading, to be measured
//! on an X32: Vintage Room's SIZE (0-100 on the console, factory 24) takes
//! the WING's 1-50 at half, its ER LEVEL (0-190) and ER LEFT / RIGHT (ms) are
//! the WING's units; Hall's LEVEL (the WING's has none) is an output gain;
//! the X32's LO CUT reaches 10-500 Hz and the chorus delays 0.5-20 ms.

use super::{FxAlgo, FxContext, FX_READOUTS};
use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec, Params};
use crate::util::db_to_gain;
use crate::wing::MAX_FX_PARAMS;

/// The X32 models built on a WING device.
pub(super) const TOKENS: &[&str] = &["X32HALL", "X32VRM", "X32CRS"];

pub(super) fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    if !TOKENS.contains(&token) {
        return None;
    }
    let (wing, spec, map): (&str, &'static ModelSpec, Map) = match token {
        "X32HALL" => ("HALL", &HALL, hall),
        "X32VRM" => ("V-ROOM", &VROOM, vroom),
        "X32CRS" => ("CHORUS", &CHORUS, chorus),
        _ => return None,
    };
    let dev = super::create(wing, ctx)?;
    Some(Box::new(Bridge { dev, spec, map, values: [0.0; MAX_FX_PARAMS], gain: 1.0, last_gain: 1.0 }))
}

/// Writes the WING device's values (by its ids) from the X32 model's; returns
/// an output gain in dB (0 when the WING device has the level itself).
type Map = fn(&Params, &mut dyn FnMut(&'static str, f32)) -> f32;

struct Bridge {
    dev: Box<dyn FxAlgo>,
    /// The WING device's controls over both consoles' ranges.
    spec: &'static ModelSpec,
    map: Map,
    values: [f32; MAX_FX_PARAMS],
    gain: f32,
    last_gain: f32,
}

impl FxAlgo for Bridge {
    fn set_params(&mut self, p: &Params) {
        let (spec, values) = (self.spec, &mut self.values);
        let db = (self.map)(p, &mut |id, v| {
            if let Some(i) = spec.param_index(id) {
                values[i] = v;
            }
        });
        self.gain = db_to_gain(db);
        self.dev.set_params(&Params::new(self.spec, &self.values));
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        self.dev.process(left, right);
        if self.gain == 1.0 && self.last_gain == 1.0 {
            return;
        }
        // A level change ramps over the block.
        let n = left.len().max(1) as f32;
        let step = (self.gain - self.last_gain) / n;
        let mut g = self.last_gain;
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            g += step;
            *l *= g;
            *r *= g;
        }
        self.last_gain = self.gain;
    }

    fn reset(&mut self) {
        self.dev.reset();
        self.last_gain = self.gain;
    }

    fn meter(&self) -> f32 {
        self.dev.meter()
    }

    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        self.dev.readouts(out);
    }

    fn latency(&self) -> u32 {
        self.dev.latency()
    }
}

/// A continuous control over `lo..hi` (the device reads values, not steps).
const fn r(id: &'static str, lo: f32, hi: f32) -> ParamSpec {
    ParamSpec { id, label: id, unit: "", kind: ParamKind::Linear, min: lo, max: hi, steps: 0, default: lo }
}

const fn spec(token: &'static str, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name: token, category: ModelCategory::FxPremium, params }
}

static HALL: ModelSpec = spec(
    "HALL",
    &[
        r("pdel", 0.0, 200.0),
        r("size", 2.0, 100.0),
        r("dcy", 0.2, 5.0),
        r("mult", 0.5, 2.0),
        r("damp", 1000.0, 20000.0),
        r("lc", 10.0, 500.0),
        r("hc", 200.0, 20000.0),
        r("shp", 0.0, 250.0),
        r("sprd", 0.0, 50.0),
        r("diff", 1.0, 30.0),
        r("mspd", 0.0, 100.0),
    ],
);

fn hall(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    for (wing, x32) in [
        ("pdel", "pre_delay"),
        ("size", "size"),
        ("dcy", "decay"),
        ("mult", "bass_multi"),
        ("damp", "damping"),
        ("lc", "lo_cut"),
        ("hc", "hi_cut"),
        ("shp", "shape"),
        ("sprd", "spread"),
        ("diff", "diffuse"),
        ("mspd", "mod_speed"),
    ] {
        set(wing, p.f(x32));
    }
    p.f("level")
}

static VROOM: ModelSpec = spec(
    "V-ROOM",
    &[
        r("pdel", 0.0, 200.0),
        r("size", 1.0, 50.0),
        r("dcy", 0.1, 20.0),
        r("dens", 1.0, 30.0),
        r("erlvl", 0.0, 190.0),
        r("lmult", 0.1, 10.0),
        r("hmult", 0.1, 10.0),
        r("lc", 10.0, 500.0),
        r("hc", 200.0, 20000.0),
        r("frz", 0.0, 1.0),
        r("erl", 0.0, 200.0),
        r("err", 0.0, 200.0),
        r("add", 0.0, 1.0),
        r("lvl", -12.0, 12.0),
    ],
);

fn vroom(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    for (wing, x32) in [
        ("pdel", "reverb_delay"),
        ("dcy", "decay"),
        ("dens", "density"),
        ("erlvl", "er_level"),
        ("lmult", "lo_multiply"),
        ("hmult", "hi_multiply"),
        ("lc", "lo_cut"),
        ("hc", "hi_cut"),
        ("frz", "freeze"),
        ("erl", "er_left"),
        ("err", "er_right"),
        ("lvl", "level"),
    ] {
        set(wing, p.f(x32));
    }
    set("size", (0.5 * p.f("size")).max(1.0));
    set("add", 0.0);
    0.0
}

static CHORUS: ModelSpec = spec(
    "CHORUS",
    &[
        r("lc", 10.0, 500.0),
        r("hc", 200.0, 20000.0),
        r("wave", 0.0, 100.0),
        r("phase", 0.0, 180.0),
        r("mix", 0.0, 100.0),
        r("dlyl", 0.5, 50.0),
        r("dlyr", 0.5, 50.0),
        r("depl", 0.0, 100.0),
        r("depr", 0.0, 100.0),
        r("sprd", 0.0, 100.0),
        r("spd", 0.05, 5.0),
    ],
);

fn chorus(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    for (wing, x32) in [
        ("lc", "lo_cut"),
        ("hc", "hi_cut"),
        ("wave", "wave"),
        ("phase", "phase"),
        ("mix", "mix"),
        ("dlyl", "delay_l"),
        ("dlyr", "delay_r"),
        ("depl", "depth_l"),
        ("depr", "depth_r"),
        ("sprd", "spread"),
        ("spd", "speed"),
    ] {
        set(wing, p.f(x32));
    }
    0.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_mapped_control_is_the_wing_devices() {
        // Each bridge names only controls the WING device has.
        for (wing, bridge) in [("HALL", &HALL), ("V-ROOM", &VROOM), ("CHORUS", &CHORUS)] {
            let real = &crate::wing::FX_MODELS[crate::wing::fx::model_index(wing).unwrap()];
            for p in bridge.params {
                assert!(real.param_index(p.id).is_some(), "{wing} {}", p.id);
            }
            assert_eq!(bridge.params.len(), real.params.len(), "{wing}: every control mapped");
        }
    }

    /// One second of a left impulse through model `token` of `table` with `edit`.
    fn impulse(table: &'static [ModelSpec], token: &str, edit: &[(&str, f32)]) -> Vec<f32> {
        let spec = &table[crate::model::model_index(table, token).unwrap()];
        let mut values = [0.0; MAX_FX_PARAMS];
        spec.defaults_into(&mut values);
        for (id, v) in edit {
            values[spec.param_index(id).unwrap()] = *v;
        }
        let mut dev = super::super::create(token, FxContext { sample_rate: 48_000.0, max_block: 256 }).unwrap();
        dev.set_params(&Params::new(spec, &values));
        let (mut l, mut r) = (vec![0.0f32; 48_000], vec![0.0f32; 48_000]);
        l[0] = 1.0;
        for (cl, cr) in l.chunks_mut(256).zip(r.chunks_mut(256)) {
            dev.process(cl, cr);
        }
        l
    }

    #[test]
    fn the_x32_devices_sound_as_the_wings_with_the_same_settings() {
        let (x32, wing) = (crate::x32::FX_MODELS, crate::wing::FX_MODELS);
        let hall = [("pdel", 20.0), ("size", 60.0), ("dcy", 2.0), ("lc", 83.0), ("hc", 7200.0)];
        let x32_hall = [("pre_delay", 20.0), ("size", 60.0), ("decay", 2.0), ("lo_cut", 83.0), ("hi_cut", 7200.0)];
        let mut w = hall.to_vec();
        w.extend([("mult", 0.95), ("damp", 5740.0), ("shp", 50.0), ("sprd", 25.0), ("diff", 25.0), ("mspd", 30.0)]);
        let (a, b) = (impulse(x32, "X32HALL", &x32_hall), impulse(wing, "HALL", &w));
        assert!(a.iter().zip(&b).all(|(a, b)| (a - b).abs() < 1e-6), "HALL");
        // LEVEL +6 dB: twice the WING hall's output.
        let louder = impulse(x32, "X32HALL", &[&x32_hall[..], &[("level", 6.0206)]].concat());
        assert!(louder.iter().zip(&a).all(|(l, a)| (l - 2.0 * a).abs() < 1e-5), "LEVEL");

        // The chorus, its delays inside the WING's range.
        let crs = [("speed", 0.5), ("depth_l", 15.0), ("depth_r", 15.0), ("delay_l", 19.9), ("delay_r", 19.9), ("mix", 100.0)];
        let wcrs = [("spd", 0.5), ("depl", 15.0), ("depr", 15.0), ("dlyl", 19.9), ("dlyr", 19.9), ("mix", 100.0)];
        let rest = [("lc", 97.0), ("hc", 15100.0), ("phase", 120.0), ("wave", 100.0), ("sprd", 85.0)];
        let (a, b) = (impulse(x32, "X32CRS", &crs), impulse(wing, "CHORUS", &[&wcrs[..], &rest[..]].concat()));
        assert!(a.iter().zip(&b).all(|(a, b)| (a - b).abs() < 1e-6), "CHORUS");

        // Vintage Room: SIZE at half, the rest as set.
        let vrm = [("size", 30.0), ("decay", 1.5), ("er_level", 22.0), ("level", 0.0)];
        let wvrm = [("size", 15.0), ("dcy", 1.5), ("erlvl", 22.0), ("lvl", 0.0), ("pdel", 20.0), ("dens", 30.0)];
        let wrest = [("lmult", 1.1), ("hmult", 0.69), ("lc", 97.0), ("hc", 10400.0), ("erl", 28.0), ("err", 34.0)];
        let (a, b) = (impulse(x32, "X32VRM", &vrm), impulse(wing, "V-ROOM", &[&wvrm[..], &wrest[..]].concat()));
        assert!(a.iter().zip(&b).all(|(a, b)| (a - b).abs() < 1e-6), "V-ROOM");
    }

    #[test]
    fn devices() {
        for token in TOKENS {
            crate::wing::fx::tests::check_device(token);
        }
    }
}
