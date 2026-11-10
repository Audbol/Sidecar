//! The Behringer XENYX Q1202USB's processing blocks, Sidecar's first analog mixer
//! (the surface is `Xenyx1202Surface`).
//!
//! The ranges below are from the published panel and quick-start guide: the mic
//! gain knob's +10 to +60 dB and the line scale's -10 to +40 dB, the low cut at
//! 75 Hz and 18 dB/oct, the compressor knob's 0 to 10 and its LED (channels 1-4
//! only), the British 3-band EQ (HIGH 12 kHz, MID 2.5 kHz, LOW 80 Hz, +/-15 dB).
//! The guide gives no curves. **Everything about how the blocks sound is
//! Sidecar's reading**: the EQ's shelf and bell widths, what the compressor
//! knob does to threshold, ratio and makeup (a single control, so one law over
//! all three), its times, and the soft clip (the Tube Stage's pentode curve at a
//! low drive, standing for the preamp and mixer stages running out of headroom).
//! No measurements of a real mixer were made.

use crate::biquad::BiquadCoeffs;
use crate::model::{ModelCategory, ModelSpec, ModelTables, ParamKind, ParamSpec, Params};
use crate::wing::eq::EqDesign;
use crate::wing::FILTER_TOOLS;

/// The mic scale of the GAIN knob (dB), and the line scale's.
pub const MIC_GAIN_DB: (f32, f32) = (10.0, 60.0);
pub const LINE_GAIN_DB: (f32, f32) = (-10.0, 40.0);

/// The GAIN knob's gain for a position 0..1 on the mic or the line scale.
pub fn preamp_gain_db(position: f32, line: bool) -> f32 {
    let (lo, hi) = if line { LINE_GAIN_DB } else { MIC_GAIN_DB };
    lo + (hi - lo) * position.clamp(0.0, 1.0)
}

/// The knob position 0..1 of a gain in dB on the mic or line scale.
pub fn preamp_position(db: f32, line: bool) -> f32 {
    let (lo, hi) = if line { LINE_GAIN_DB } else { MIC_GAIN_DB };
    ((db - lo) / (hi - lo)).clamp(0.0, 1.0)
}

/// The low cut: 75 Hz, 18 dB/oct (`FilterSettings::low_cut_slope` 2).
pub const LOW_CUT_HZ: f32 = 75.0;
pub const LOW_CUT_SLOPE: i32 = 2;

/// The EQ's centre and corner frequencies (Hz).
pub const EQ_HIGH_HZ: f32 = 12_000.0;
pub const EQ_MID_HZ: f32 = 2_500.0;
pub const EQ_LOW_HZ: f32 = 80.0;
/// The EQ's range (dB).
pub const EQ_RANGE_DB: f32 = 15.0;
/// The mid band's Q (Sidecar's reading: a broad British bell).
pub const EQ_MID_Q: f64 = 0.8;

/// What the COMP knob (0 to 10) sets, Sidecar's reading of a one-knob compressor.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct CompSetting {
    pub threshold_db: f32,
    pub ratio: f32,
    pub makeup_db: f32,
}

/// The law: 0 is off (1:1); turning up lowers the threshold from 0 to -25 dB,
/// raises the ratio from 1:1 to 4:1 and adds makeup up to 3.5 dB.
pub fn comp_setting(knob: f32) -> CompSetting {
    let k = knob.clamp(0.0, 10.0);
    CompSetting { threshold_db: -2.5 * k, ratio: 1.0 + 0.3 * k, makeup_db: 0.35 * k }
}

/// The COMP LED lights while the compressor takes at least this much off (dB).
pub const COMP_LED_DB: f32 = 0.5;

const fn lin(
    id: &'static str,
    label: &'static str,
    unit: &'static str,
    min: f32,
    max: f32,
    steps: u32,
    default: f32,
) -> ParamSpec {
    ParamSpec { id, label, unit, kind: ParamKind::Linear, min, max, steps, default }
}

static EQ_PARAMS: [ParamSpec; 3] = [
    lin("hg", "HIGH 12 kHz", "dB", -15.0, 15.0, 301, 0.0),
    lin("mg", "MID 2.5 kHz", "dB", -15.0, 15.0, 301, 0.0),
    lin("lg", "LOW 80 Hz", "dB", -15.0, 15.0, 301, 0.0),
];
pub const EQ_MODEL: ModelSpec =
    ModelSpec { token: "XYEQ", name: "British 3-band EQ", category: ModelCategory::Eq, params: &EQ_PARAMS };
pub static EQ: &[ModelSpec] = &[EQ_MODEL];

/// The EQ's filters: a low shelf, a bell and a high shelf (`k`: MIX / 100).
pub(crate) fn design_eq(p: &Params, sr: f64, k: f32, d: &mut EqDesign) {
    let q = std::f64::consts::FRAC_1_SQRT_2;
    let g = |id: &str| (p.f(id) * k) as f64;
    for (gain, make) in [
        (g("lg"), BiquadCoeffs::low_shelf(sr, EQ_LOW_HZ as f64, q, g("lg"))),
        (g("mg"), BiquadCoeffs::peak(sr, EQ_MID_HZ as f64, EQ_MID_Q, g("mg"))),
        (g("hg"), BiquadCoeffs::high_shelf(sr, EQ_HIGH_HZ as f64, q, g("hg"))),
    ] {
        if gain.abs() > 1e-3 {
            d.push(make);
        }
    }
}

static COMP_PARAMS: [ParamSpec; 1] = [lin("comp", "COMP", "", 0.0, 10.0, 101, 0.0)];
static CLIP_PARAMS: [ParamSpec; 1] = [lin("drive", "DRIVE", "%", 0.0, 100.0, 101, 20.0)];

/// The compressor (the DYNAMICS slot; channels 1-4) and the soft clip (the GATE
/// slot, right after the preamp, on every channel).
pub static DYN_MODELS: &[ModelSpec] = &[
    ModelSpec { token: "XYCMP", name: "One-knob compressor", category: ModelCategory::Compressor, params: &COMP_PARAMS },
    ModelSpec { token: "XYCLIP", name: "Analog soft clip", category: ModelCategory::Special, params: &CLIP_PARAMS },
];

/// Whether `token` is the soft clip (it goes in the GATE slot, the compressor in DYNAMICS).
pub fn is_clip(token: &str) -> bool {
    token == "XYCLIP"
}

/// The FX table every console has (empty slot and EXT first); the Q1202USB has no effects, and its console no slots.
pub static FX_MODELS: [ModelSpec; 2] = [crate::wing::FX_MODELS[0], crate::wing::FX_MODELS[1]];

/// The Q1202USB's models.
pub static MODELS: ModelTables = ModelTables {
    dynamics: DYN_MODELS,
    aux_dynamics: &[],
    eq_channel: EQ,
    eq_bus: EQ,
    filter_tools: FILTER_TOOLS,
    fx: &FX_MODELS,
    fx_premium_first: 0,
    fx_premium_slots: 0,
    gate_default: "XYCLIP",
    comp_default: "XYCMP",
};

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::model_index;

    #[test]
    fn the_gain_knob_spans_the_two_scales() {
        assert_eq!(preamp_gain_db(0.0, false), 10.0);
        assert_eq!(preamp_gain_db(1.0, false), 60.0);
        assert_eq!(preamp_gain_db(0.0, true), -10.0);
        assert_eq!(preamp_gain_db(1.0, true), 40.0);
        assert!((preamp_position(preamp_gain_db(0.3, true), true) - 0.3).abs() < 1e-6);
        assert_eq!(preamp_gain_db(2.0, false), 60.0);
    }

    #[test]
    fn the_comp_knob_is_off_at_zero_and_works_harder_turned_up() {
        let off = comp_setting(0.0);
        assert_eq!((off.threshold_db, off.ratio, off.makeup_db), (0.0, 1.0, 0.0));
        let (a, b) = (comp_setting(4.0), comp_setting(10.0));
        assert!(b.threshold_db < a.threshold_db && b.ratio > a.ratio && b.makeup_db > a.makeup_db);
        assert!((b.ratio - 4.0).abs() < 1e-6 && (b.threshold_db + 25.0).abs() < 1e-6);
    }

    #[test]
    fn the_eq_boosts_and_cuts_fifteen_db_at_its_corners() {
        let spec = &EQ_MODEL;
        let at = |id: &str, gain: f32, freq: f64| {
            let mut v = [0.0f32; 3];
            v[spec.param_index(id).unwrap()] = gain;
            let mut d = EqDesign::default();
            design_eq(&Params::new(spec, &v), 48_000.0, 1.0, &mut d);
            d.response_db(freq, 48_000.0)
        };
        assert!((at("hg", 15.0, 20_000.0) - 15.0).abs() < 1.5);
        assert!((at("mg", 15.0, EQ_MID_HZ as f64) - 15.0).abs() < 0.2);
        assert!((at("lg", -15.0, 20.0) + 15.0).abs() < 1.5);
        assert!(at("hg", 15.0, 100.0).abs() < 0.5 && at("lg", 15.0, 10_000.0).abs() < 0.5);
        assert_eq!(at("mg", 0.0, 1000.0), 0.0);
    }

    #[test]
    fn the_models_are_named_and_placed() {
        assert!(model_index(DYN_MODELS, MODELS.gate_default).is_some() && model_index(DYN_MODELS, MODELS.comp_default).is_some());
        assert!(is_clip("XYCLIP") && !is_clip("XYCMP"));
        for m in DYN_MODELS {
            let mut v = [9.0f32; 4];
            m.defaults_into(&mut v);
            assert_eq!(v[0], m.params[0].default);
        }
    }
}
