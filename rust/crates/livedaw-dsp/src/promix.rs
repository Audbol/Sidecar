//! The Yamaha ProMix 01's processing models (Phase 5, `docs/UNIVERSAL_MIXER.md`):
//! its 3-band parametric EQ and its dynamics processors' three types
//! (compressor, gate, ducking), described with the ranges and steps of its
//! User's Guide (pages as printed). Clean-room: built from the guide's
//! description of the parameters, not from the console's firmware.
//!
//! They run on the EQ and dynamics DSP of [`crate::wing`] (`design` and
//! `configure` pick them by token). The EQ's frequencies run in 1/6 octave
//! steps (p.21), named as the guide's presets write them (60 Hz, 110 Hz,
//! 360 Hz, 3.6 kHz: pp.23-24); its Q is in octaves (1/6 to 3, the guide's table of
//! decimal Qs), the outer bands' last value SHELF. The compressor's ratios
//! aren't listed (1:1 to oo:1, p.69): the steps are Sidecar's reading. The
//! gate's HOLD and DECAY are 128 even steps from 2.67 ms and 85 ms (3-341 ms,
//! 85 ms-10.9 s, p.70: the guide's preset values fall on them).

use crate::model::{ModelCategory, ModelSpec, ModelTables, ParamKind, ParamSpec};
use crate::wing::FILTER_TOOLS;

pub mod fx;
pub use fx::FX_MODELS;

/// A parameter, in `ParamSpec`'s field order.
#[allow(clippy::too_many_arguments)]
const fn p(
    id: &'static str,
    label: &'static str,
    unit: &'static str,
    kind: ParamKind,
    min: f32,
    max: f32,
    steps: u32,
    default: f32,
) -> ParamSpec {
    ParamSpec { id, label, unit, kind, min, max, steps, default }
}

/// The LOW band's frequencies: 32 Hz to 1 kHz in 1/6 octave steps (31 values).
pub const LOW_FREQS: &[f32] = &[
    32.0, 36.0, 40.0, 45.0, 50.0, 56.0, 60.0, 70.0, 80.0, 90.0, 100.0, 110.0, 125.0, 140.0, 160.0, 180.0, 200.0, 220.0, 250.0,
    280.0, 315.0, 360.0, 400.0, 450.0, 500.0, 560.0, 630.0, 700.0, 800.0, 900.0, 1000.0,
];

/// The MID band's: 32 Hz to 18 kHz (56 values).
pub const MID_FREQS: &[f32] = &[
    32.0, 36.0, 40.0, 45.0, 50.0, 56.0, 60.0, 70.0, 80.0, 90.0, 100.0, 110.0, 125.0, 140.0, 160.0, 180.0, 200.0, 220.0, 250.0,
    280.0, 315.0, 360.0, 400.0, 450.0, 500.0, 560.0, 630.0, 700.0, 800.0, 900.0, 1000.0, 1100.0, 1250.0, 1400.0, 1600.0, 1800.0,
    2000.0, 2200.0, 2500.0, 2800.0, 3200.0, 3600.0, 4000.0, 4500.0, 5000.0, 5600.0, 6300.0, 7000.0, 8000.0, 9000.0, 10000.0,
    11000.0, 12000.0, 14000.0, 16000.0, 18000.0,
];

/// The HIGH band's: 1 kHz to 18 kHz (26 values; the CC table says 25 for
/// channel 1's and 26 for the others', Add-12 / Add-13).
pub const HIGH_FREQS: &[f32] = &[
    1000.0, 1100.0, 1250.0, 1400.0, 1600.0, 1800.0, 2000.0, 2200.0, 2500.0, 2800.0, 3200.0, 3600.0, 4000.0, 4500.0, 5000.0,
    5600.0, 6300.0, 7000.0, 8000.0, 9000.0, 10000.0, 11000.0, 12000.0, 14000.0, 16000.0, 18000.0,
];

/// The Q values in octaves, then (LOW and HIGH only) SHELF.
pub const Q_NAMES: &[&str] = &["1/6", "1/4", "1/3", "1/2", "3/4", "1", "3/2", "2", "3", "SHELF"];

/// The decimal Q of each octave value (p.21).
pub const Q_VALUES: [f32; 9] = [8.65, 5.76, 4.32, 2.87, 1.90, 1.41, 0.92, 0.67, 0.40];

/// SHELF's index in [`Q_NAMES`].
pub const SHELF: f32 = 9.0;

const fn values_index(table: &[f32], v: f32) -> f32 {
    let mut i = 0;
    while i < table.len() {
        if table[i] == v {
            return i as f32;
        }
        i += 1;
    }
    panic!("not in the table");
}

/// The EQ's parameters: LOW, MID and HIGH (frequency, gain, Q each), as mix
/// scene 00 sets them (EQ program 1, RESET: 80 Hz SHELF, 2.0 kHz 3/2 oct,
/// 10 kHz SHELF, all at 0 dB; pp.23, 62).
static EQ_PARAMS: [ParamSpec; 9] = [
    p("lf", "LOW F", "Hz", ParamKind::Values(LOW_FREQS), 0.0, 30.0, 31, values_index(LOW_FREQS, 80.0)),
    p("lg", "LOW G", "dB", ParamKind::Linear, -15.0, 15.0, 31, 0.0),
    p("lq", "LOW Q", "", ParamKind::Choice(Q_NAMES), 0.0, 9.0, 10, SHELF),
    p("mf", "MID F", "Hz", ParamKind::Values(MID_FREQS), 0.0, 55.0, 56, values_index(MID_FREQS, 2000.0)),
    p("mg", "MID G", "dB", ParamKind::Linear, -15.0, 15.0, 31, 0.0),
    p("mq", "MID Q", "", ParamKind::Choice(Q_NAMES), 0.0, 8.0, 9, 6.0),
    p("hf", "HIGH F", "Hz", ParamKind::Values(HIGH_FREQS), 0.0, 25.0, 26, values_index(HIGH_FREQS, 10000.0)),
    p("hg", "HIGH G", "dB", ParamKind::Linear, -15.0, 15.0, 31, 0.0),
    p("hq", "HIGH Q", "", ParamKind::Choice(Q_NAMES), 0.0, 9.0, 10, SHELF),
];

/// The 3-band EQ of the input channels, ST IN, RTN1-2 and ST OUT.
pub static EQ: &[ModelSpec] = &[ModelSpec { token: "PMEQ", name: "3-Band PEQ", category: ModelCategory::Eq, params: &EQ_PARAMS }];

/// The compressor's ratios (1:1 to oo:1; oo as 100:1): Sidecar's reading.
pub const RATIOS: &[f32] = &[1.0, 1.1, 1.3, 1.5, 1.7, 2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 6.0, 8.0, 10.0, 20.0, 100.0];

/// The compressor's parameters (p.69), on program 1 TOTAL COMP's values
/// (0 dB, 2:1, 50 ms, 3.0 s, +1 dB: p.78).
static COMP_PARAMS: [ParamSpec; 5] = [
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -40.0, 18.0, 59, 0.0),
    p("ratio", "RATIO", "", ParamKind::Values(RATIOS), 0.0, 15.0, 16, 5.0),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 50.0),
    p("rel", "RELEASE", "s", ParamKind::Linear, 0.1, 6.0, 60, 3.0),
    p("gain", "OUT GAIN", "dB", ParamKind::Linear, -18.0, 18.0, 37, 1.0),
];

/// One step of HOLD (ms): 3-341 ms in 128 steps.
pub const HOLD_STEP_MS: f32 = 341.333_34 / 128.0;

/// The gate's and the ducker's parameters (pp.70-71), on program 7 GATE
/// FAST's values (-6 dB, 1 ms, 101 ms, 1.3 s, -20 dB: p.78).
static GATE_PARAMS: [ParamSpec; 5] = [
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -50.0, 10.0, 61, -6.0),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 1.0),
    p("hld", "HOLD", "ms", ParamKind::Linear, HOLD_STEP_MS, 128.0 * HOLD_STEP_MS, 128, 38.0 * HOLD_STEP_MS),
    p("dcy", "DECAY", "ms", ParamKind::Linear, 85.0, 10880.0, 128, 15.0 * 85.0),
    p("range", "RANGE", "dB", ParamKind::Linear, -80.0, 0.0, 81, -20.0),
];

/// Program 9 DUCKING's (-8 dB, 9 ms, 296 ms, 6.5 s, -9 dB).
static DUCK_PARAMS: [ParamSpec; 5] = [
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -50.0, 10.0, 61, -8.0),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 9.0),
    p("hld", "HOLD", "ms", ParamKind::Linear, HOLD_STEP_MS, 128.0 * HOLD_STEP_MS, 128, 111.0 * HOLD_STEP_MS),
    p("dcy", "DECAY", "ms", ParamKind::Linear, 85.0, 10880.0, 128, 76.0 * 85.0),
    p("range", "RANGE", "dB", ParamKind::Linear, -80.0, 0.0, 81, -9.0),
];

/// A dynamics processor's types (p.68): the compressor (a limiter is a
/// compressor at a high ratio), the gate and ducking. A strip's dynamics
/// block holds any of them; its gate slot isn't used.
pub static DYN_MODELS: &[ModelSpec] = &[
    ModelSpec { token: "PMCOMP", name: "Compressor", category: ModelCategory::Compressor, params: &COMP_PARAMS },
    ModelSpec { token: "PMGATE", name: "Gate", category: ModelCategory::Gate, params: &GATE_PARAMS },
    ModelSpec { token: "PMDUCK", name: "Ducking", category: ModelCategory::Gate, params: &DUCK_PARAMS },
];

/// The ProMix 01's models, as its profile names them; its two FX slots take
/// its 15 effect types (`fx`).
pub static MODELS: ModelTables = ModelTables {
    dynamics: DYN_MODELS,
    aux_dynamics: &[],
    eq_channel: EQ,
    eq_bus: EQ,
    filter_tools: FILTER_TOOLS,
    fx: &fx::FX_MODELS,
    fx_premium_slots: 2,
    gate_default: "PMGATE",
    comp_default: "PMCOMP",
};

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::Params;

    fn values(spec: &ModelSpec, set: &[(&str, f32)]) -> [f32; 32] {
        let mut v = [0.0; 32];
        spec.defaults_into(&mut v);
        for (id, x) in set {
            v[spec.param_index(id).unwrap()] = *x;
        }
        v
    }

    #[test]
    fn the_promix01s_tables() {
        assert_eq!((LOW_FREQS.len(), MID_FREQS.len(), HIGH_FREQS.len()), (31, 56, 26), "the CC table's steps");
        // 1/6 octave steps: each value within 6% of 1 kHz x 2^(k/6) (12 kHz
        // is the guide's name for 12.7 kHz).
        for (table, first) in [(LOW_FREQS, -30), (MID_FREQS, -30), (HIGH_FREQS, 0)] {
            for (k, &f) in table.iter().enumerate() {
                let exact = 1000.0 * 2f32.powf((first + k as i32) as f32 / 6.0);
                assert!((f / exact - 1.0).abs() < 0.06, "{f} Hz");
            }
        }
        assert_eq!(&MID_FREQS[..31], LOW_FREQS);
        assert_eq!(&MID_FREQS[30..], HIGH_FREQS);
        let eq = &EQ[0];
        let v = values(eq, &[]);
        let p = Params::new(eq, &v[..eq.params.len()]);
        assert_eq!((p.f("lf"), p.text("lq"), p.f("mf"), p.text("mq"), p.f("hf")), (80.0, "SHELF", 2000.0, "3/2", 10000.0));
        assert!(eq.params.len() <= crate::wing::MAX_EQ_PARAMS);
        assert!(DYN_MODELS.iter().all(|m| m.params.len() <= crate::wing::MAX_DYN_PARAMS));
        // The gate's steps hold the guide's preset values (p.78).
        let v = values(&DYN_MODELS[1], &[]);
        let gate = Params::new(&DYN_MODELS[1], &v[..5]);
        assert_eq!((gate.f("hld").round(), (gate.f("dcy") / 100.0).round() / 10.0), (101.0, 1.3));
        assert_eq!((128.0 * HOLD_STEP_MS).round(), 341.0);
    }

    #[test]
    fn the_promix01s_eq_bands() {
        use crate::wing::eq::design;
        let eq = &EQ[0];
        let at = |set: &[(&str, f32)], f: f64| {
            let v = values(eq, set);
            design(eq, &v[..eq.params.len()], 48000.0, 1.0).response_db(f, 48000.0)
        };
        let index = |table: &[f32], f: f32| table.iter().position(|&x| x == f).unwrap() as f32;
        assert!(at(&[], 1000.0).abs() < 0.01, "flat at 0 dB");
        assert!((at(&[("mg", 9.0), ("mf", index(MID_FREQS, 500.0))], 500.0) - 9.0).abs() < 0.1, "a bell");
        // A narrow bell (1/6 oct) is down a lot an octave away; a wide one (3 oct) isn't.
        let narrow = at(&[("mg", 12.0), ("mq", 0.0)], 1000.0);
        let wide = at(&[("mg", 12.0), ("mq", 8.0)], 1000.0);
        assert!(narrow < 1.0 && wide > 6.0, "{narrow} {wide}");
        assert!((at(&[("lg", 6.0)], 30.0) - 6.0).abs() < 0.5, "the low shelf");
        assert!((at(&[("hg", -6.0)], 18000.0) + 6.0).abs() < 0.5, "the high shelf");
        // LOW as a bell: back to 0 dB well below it.
        assert!(at(&[("lg", 10.0), ("lq", 5.0), ("lf", index(LOW_FREQS, 250.0))], 30.0).abs() < 1.0);
    }

    #[test]
    fn the_promix01s_dynamics_process() {
        use crate::wing::dynamics::{SidechainSettings, WingDynamics, XoverSettings};
        let sine = |a: f32, n: usize| (0..n).map(|i| a * (i as f32 * 0.13).sin()).collect::<Vec<f32>>();
        let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
        let tail = |x: &[f32]| x[x.len() / 2..].iter().fold(0.0f32, |m, s| m.max(s.abs()));
        let run = |token: &str, set: &[(&str, f32)], level: f32| {
            let m = DYN_MODELS.iter().position(|m| m.token == token).unwrap();
            let v = values(&DYN_MODELS[m], set);
            let mut d = WingDynamics::default();
            d.set_table(DYN_MODELS);
            d.prepare(48000.0);
            let mut x = sine(level, 48000);
            d.process(&mut [&mut x[..]], None, m, &v[..DYN_MODELS[m].params.len()], &sc, &xo, 1.0, 0.0);
            tail(&x) / level
        };
        // The compressor turns a loud signal down (10:1 from -30 dB); OUT GAIN makes up.
        let c = run("PMCOMP", &[("thr", -30.0), ("ratio", 13.0), ("gain", 0.0)], 0.8);
        assert!(c < 0.2, "comp {c}");
        let made_up = run("PMCOMP", &[("thr", -30.0), ("ratio", 13.0), ("gain", 12.0)], 0.8);
        assert!((made_up / c - 3.98).abs() < 0.3, "12 dB of out gain: {}", made_up / c);
        assert!((run("PMCOMP", &[("ratio", 0.0), ("gain", 0.0)], 0.5) - 1.0).abs() < 0.01, "1:1 does nothing");
        // The gate closes by its range under its threshold, and is open above it.
        assert!(run("PMGATE", &[("thr", -20.0), ("range", -40.0), ("dcy", 85.0)], 0.01) < 0.02);
        assert!(run("PMGATE", &[("thr", -40.0)], 0.5) > 0.95);
        assert!(
            (run("PMGATE", &[("thr", -20.0), ("range", 0.0), ("dcy", 85.0)], 0.01) - 1.0).abs() < 0.01,
            "RANGE 0 dB: no effect"
        );
        // Ducking turns the signal down by its range while its key is over the threshold.
        let ducked = run("PMDUCK", &[("thr", -30.0), ("range", -20.0)], 0.5);
        assert!((ducked - 0.1).abs() < 0.02, "ducked by 20 dB: {ducked}");
    }
}
