//! The X32's insert effects that run on one of the WING's FX devices (Phase 4
//! step 9f): the graphic EQs (GEQ: the WING's STD type, TruEQ: its TRU), the
//! de-essers, the Precision Limiter, the Combinators, Enhancers, Exciters and
//! Guitar Amps are the same designs with the same controls. The stereo
//! models map onto one device ([`super::x32_voices::bridge`]); the dual ones
//! (their controls in an A and a B set) run one device per side
//! ([`super::x32_voices::Dual`], side A on the left input, B on the right).
//!
//! Where the controls differ, Sidecar's reading (to be measured on an X32):
//! the Limiter's OUT GAIN above 0 dB (the WING's stops there) is an output
//! gain after it; the Combinator's ACTIVE off sets its MIX to 0 (dry), its
//! RATIO and XOVER SLOPE take the WING's lists by position, its band LOCKs,
//! XOVER and BAND SOLO are kept but not acted on; the Enhancer's SPEED is
//! kept but not acted on.

use super::x32_voices::{bridge, format_id, Dual, IdBuf, Map};
use super::{FxAlgo, FxContext};
use crate::model::Params;

/// The insert models built on a WING device.
pub(super) const TOKENS: &[&str] = &[
    "X32GEQ2", "X32GEQ", "X32TEQ2", "X32TEQ", "X32DES2", "X32DES", "X32LIM", "X32CMB", "X32CMB2", "X32ENH2", "X32ENH", "X32EXC2",
    "X32EXC", "X32AMP2", "X32AMP",
];

pub(super) fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    if !TOKENS.contains(&token) {
        return None;
    }
    let (wing, map): (&str, Map) = match token {
        "X32GEQ2" => return Dual::boxed("GEQ", |p, s, set| side(geq(p, sfx(s), 0.0, set)), ctx),
        "X32TEQ2" => return Dual::boxed("GEQ", |p, s, set| side(geq(p, sfx(s), 1.0, set)), ctx),
        "X32DES2" => return Dual::boxed("DE-S2", dual_deesser, ctx),
        "X32CMB2" => return Dual::boxed("C5-CMB", |p, s, set| side(combinator(p, sfx(s), set)), ctx),
        "X32ENH2" => return Dual::boxed("ENHANCE", |p, s, set| side(enhancer(p, sfx(s), set)), ctx),
        "X32EXC2" => return Dual::boxed("EXCITER", |p, s, set| side(exciter(p, sfx(s), set)), ctx),
        "X32AMP2" => return Dual::boxed("RACKAMP", |p, s, set| side(amp(p, sfx(s), set)), ctx),
        "X32GEQ" => ("GEQ", |p, set| geq(p, None, 0.0, set)),
        "X32TEQ" => ("GEQ", |p, set| geq(p, None, 1.0, set)),
        "X32DES" => ("DE-S2", deesser),
        "X32LIM" => ("LIMITER", limiter),
        "X32CMB" => ("C5-CMB", |p, set| combinator(p, None, set)),
        "X32ENH" => ("ENHANCE", |p, set| enhancer(p, None, set)),
        "X32EXC" => ("EXCITER", |p, set| exciter(p, None, set)),
        "X32AMP" => ("RACKAMP", |p, set| amp(p, None, set)),
        _ => return None,
    };
    bridge(wing, None, map, ctx)
}

/// A dual model's control suffix for side 0 (A) and 1 (B).
fn sfx(side: usize) -> Option<&'static str> {
    Some(if side == 0 { "a" } else { "b" })
}

/// A side that always runs, its device's output as is (the dual maps' result).
fn side(_db: f32) -> (bool, Option<f32>) {
    (true, None)
}

/// Control `base` of the stereo model, or of side `sfx` of the dual one.
fn id(base: &str, sfx: Option<&str>) -> IdBuf {
    match sfx {
        Some(s) => format_id(base, s),
        None => IdBuf::from(base),
    }
}

/// The WING GEQ's bands, 20 Hz .. 20 kHz (the X32's BAND 1-31).
const GEQ_BANDS: [&str; 31] = [
    "20", "25", "31", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400", "500", "630", "800", "1k", "1k25",
    "1k6", "2k", "2k5", "3k15", "4k", "5k", "6k3", "8k", "10k", "12k5", "16k", "20k",
];

/// GEQ (`kind` 0, the WING's STD) or TruEQ (1, its TRU): the bands and MASTER as TRIM.
fn geq(p: &Params, sfx: Option<&str>, kind: f32, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    set("type", kind);
    let mut name = IdBuf::from("band_");
    for (k, band) in GEQ_BANDS.iter().enumerate() {
        name.truncate(5);
        name.push_number(k + 1);
        set(band, p.f(&id(&name, sfx)));
    }
    set("TRIM", p.f(&id("master", sfx)));
    0.0
}

fn deesser(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    // L / R, or M / S in M/S mode: the WING's two band pairs.
    for (wing, x32) in
        [("lo", "lo_band_l"), ("hi", "hi_band_l"), ("los", "lo_band_r"), ("his", "hi_band_r"), ("gdr", "voice"), ("mode", "mode")]
    {
        set(wing, p.f(x32));
    }
    0.0
}

fn dual_deesser(p: &Params, side: usize, set: &mut dyn FnMut(&'static str, f32)) -> (bool, Option<f32>) {
    // Each side mono: both of the device's band pairs alike, stereo mode.
    let s = sfx(side);
    let (lo, hi) = (p.f(&id("lo_band", s)), p.f(&id("hi_band", s)));
    for (wing, v) in [("lo", lo), ("hi", hi), ("los", lo), ("his", hi), ("gdr", p.f(&id("voice", s))), ("mode", 0.0)] {
        set(wing, v);
    }
    (true, None)
}

fn limiter(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let out = p.f("out_gain");
    for (wing, x32) in [
        ("gin", "input_gain"),
        ("sqz", "squeeze"),
        ("knee", "knee"),
        ("again", "auto_gain"),
        ("att", "attack"),
        ("rel", "release"),
    ] {
        set(wing, p.f(x32));
    }
    set("gout", out.min(0.0));
    out.max(0.0)
}

fn combinator(p: &Params, sfx: Option<&str>, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let f = |base: &str| p.f(&id(base, sfx));
    let pairs = [("thr", "threshold"), ("gain", "gain"), ("ratio", "ratio"), ("slope", "xover_slope"), ("att", "attack")];
    for (wing, x32) in pairs {
        set(wing, f(x32));
    }
    for (wing, x32) in [("rel", "release"), ("arel", "autorelease"), ("sbc", "sbc_speed"), ("sbcon", "sbc_on")] {
        set(wing, f(x32));
    }
    const THR: [&str; 5] = ["thr_1", "thr_2", "thr_3", "thr_4", "thr_5"];
    const GAIN: [&str; 5] = ["gain_1", "gain_2", "gain_3", "gain_4", "gain_5"];
    const X32_THR: [&str; 5] =
        ["band_1_threshold", "band_2_threshold", "band_3_threshold", "band_4_threshold", "band_5_threshold"];
    const X32_GAIN: [&str; 5] = ["band_1_gain", "band_2_gain", "band_3_gain", "band_4_gain", "band_5_gain"];
    for b in 0..5 {
        set(THR[b], f(X32_THR[b]));
        set(GAIN[b], f(X32_GAIN[b]));
    }
    // ACTIVE off: the dry signal.
    set("mix", if f("active") >= 0.5 { f("mix") } else { 0.0 });
    0.0
}

fn enhancer(p: &Params, sfx: Option<&str>, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("g", "out_gain"), ("bass", "bass_gain"), ("bassf", "bass_freq"), ("mid", "mid_gain"), ("midq", "mid_freq")];
    for (wing, x32) in pairs.into_iter().chain([("high", "hi_gain"), ("highf", "hi_freq"), ("solo", "solo")]) {
        set(wing, p.f(&id(x32, sfx)));
    }
    0.0
}

fn exciter(p: &Params, sfx: Option<&str>, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("tune", "tune"), ("peak", "peak"), ("zero", "zero_fill"), ("tmb", "timbre"), ("hrm", "harmonics")];
    for (wing, x32) in pairs.into_iter().chain([("mix", "mix"), ("solo", "solo")]) {
        set(wing, p.f(&id(x32, sfx)));
    }
    0.0
}

fn amp(p: &Params, sfx: Option<&str>, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("pre", "preamp"), ("buzz", "buzz"), ("punch", "punch"), ("crunch", "crunch"), ("drive", "drive")];
    for (wing, x32) in pairs.into_iter().chain([("out", "level"), ("leq", "low"), ("heq", "high"), ("cab", "cabinet")]) {
        set(wing, p.f(&id(x32, sfx)));
    }
    0.0
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::ModelSpec;
    use crate::wing::MAX_FX_PARAMS;

    #[test]
    fn devices() {
        for token in TOKENS {
            crate::wing::fx::tests::check_device(token);
        }
    }

    /// The WING device and every id a model's mapping sets on it.
    fn mapped(token: &str) -> (&'static ModelSpec, Vec<&'static str>) {
        let x32 = &crate::x32::FX_MODELS[crate::model::model_index(crate::x32::FX_MODELS, token).unwrap()];
        let mut values = [0.0; MAX_FX_PARAMS];
        x32.defaults_into(&mut values);
        let p = Params::new(x32, &values);
        let mut ids = Vec::new();
        let mut set = |id: &'static str, _v: f32| ids.push(id);
        let wing = match token {
            "X32GEQ" | "X32TEQ" => {
                geq(&p, None, 0.0, &mut set);
                "GEQ"
            }
            "X32GEQ2" => {
                geq(&p, sfx(1), 0.0, &mut set);
                "GEQ"
            }
            "X32DES" => {
                deesser(&p, &mut set);
                "DE-S2"
            }
            "X32DES2" => {
                dual_deesser(&p, 1, &mut set);
                "DE-S2"
            }
            "X32LIM" => {
                limiter(&p, &mut set);
                "LIMITER"
            }
            "X32CMB" => {
                combinator(&p, None, &mut set);
                "C5-CMB"
            }
            "X32CMB2" => {
                combinator(&p, sfx(0), &mut set);
                "C5-CMB"
            }
            "X32ENH" => {
                enhancer(&p, None, &mut set);
                "ENHANCE"
            }
            "X32ENH2" => {
                enhancer(&p, sfx(1), &mut set);
                "ENHANCE"
            }
            "X32EXC" => {
                exciter(&p, None, &mut set);
                "EXCITER"
            }
            "X32AMP2" => {
                amp(&p, sfx(0), &mut set);
                "RACKAMP"
            }
            _ => unreachable!(),
        };
        (&crate::wing::FX_MODELS[crate::wing::fx::model_index(wing).unwrap()], ids)
    }

    #[test]
    fn every_mapping_reads_the_x32_model_and_sets_the_wing_devices_controls() {
        for token in
            ["X32GEQ", "X32GEQ2", "X32DES", "X32DES2", "X32LIM", "X32CMB", "X32CMB2", "X32ENH", "X32ENH2", "X32EXC", "X32AMP2"]
        {
            let (spec, ids) = mapped(token);
            assert!(!ids.is_empty(), "{token}");
            for id in ids {
                assert!(spec.param_index(id).is_some(), "{token} -> {}: {id}", spec.token);
            }
        }
        // The GEQ's 31 bands and its trim.
        assert_eq!(mapped("X32GEQ").1.len(), 33);
    }

    #[test]
    fn x32_ids_exist() {
        // Each X32 id a mapping reads is the model's (Params::f of a missing id is 0).
        for (token, ids) in [
            ("X32GEQ", &["band_1", "band_31", "master"][..]),
            ("X32GEQ2", &["band_1_a", "band_31_b", "master_b"][..]),
            ("X32DES2", &["lo_band_a", "hi_band_b", "voice_a"][..]),
            ("X32CMB2", &["band_5_threshold_b", "active_a", "sbc_on_b", "xover_slope_a"][..]),
            ("X32ENH2", &["out_gain_a", "hi_freq_b", "solo_a"][..]),
            ("X32EXC2", &["zero_fill_a", "harmonics_b"][..]),
            ("X32AMP2", &["preamp_a", "cabinet_b", "level_a"][..]),
        ] {
            let spec = &crate::x32::FX_MODELS[crate::model::model_index(crate::x32::FX_MODELS, token).unwrap()];
            for id in ids {
                assert!(spec.param_index(id).is_some(), "{token} {id}");
            }
        }
    }
}
