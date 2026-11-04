//! The Behringer X32's processing models (Phase 4, `docs/UNIVERSAL_MIXER.md`):
//! its gate (five modes), its compressor / expander and its 4-band (inputs)
//! and 6-band (buses, mains, matrices) EQ, described with the console's
//! documented ranges and value steps. Clean-room: built from the public
//! protocol description of its parameters, not from its firmware.
//!
//! They run on the dynamics and EQ DSP of [`crate::wing`] (`configure` and
//! `design` pick them by token): the X32's compressor is a close relative of
//! the WING's, its gate modes are that DSP's gate, ducker and expanders.
//! Where the references are silent (the expander ratios of EXP2-4, the VEQ
//! band's shape) the choices are Sidecar's own; the defaults are what the
//! owner's X32 Compact shows on an untouched strip (firmware 4.09).

use crate::model::{ModelCategory, ModelSpec, ModelTables, ParamKind, ParamSpec};
use crate::wing::FILTER_TOOLS;

pub mod fx;
#[rustfmt::skip]
mod fx_models;
pub use fx_models::{CODES as FX_CODES, FX_MODELS, INSERT_ORDER};

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

/// The gate's parameters (`/ch/NN/gate`): THRESHOLD -80..0 dB in 0.5 dB steps,
/// RANGE 3..60 dB, ATTACK 0..120 ms, HOLD 0.02..2000 ms and RELEASE
/// 5..4000 ms on the console's log scales.
const GATE_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -80.0, 0.0, 161, -80.0),
    p("range", "RANGE", "dB", ParamKind::Linear, 3.0, 60.0, 58, 60.0),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 10.0),
    p("hld", "HOLD", "ms", ParamKind::Log, 0.02, 2000.0, 101, 50.2),
    p("rel", "RELEASE", "ms", ParamKind::Log, 5.0, 4000.0, 101, 258.0),
];

/// The compressor's ratios (`dyn/ratio` 0-11).
pub const DYN_RATIOS: &[f32] = &[1.1, 1.3, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 7.0, 10.0, 20.0, 100.0];

/// The compressor's / expander's parameters (`/ch/NN/dyn`); MIX is the slot's.
const DYN_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -60.0, 0.0, 121, 0.0),
    p("ratio", "RATIO", "", ParamKind::Values(DYN_RATIOS), 0.0, 11.0, 12, 5.0),
    p("knee", "KNEE", "", ParamKind::Int, 0.0, 5.0, 6, 1.0),
    p("mgain", "GAIN", "dB", ParamKind::Linear, 0.0, 24.0, 49, 0.0),
    p("det", "DETECT", "", ParamKind::Choice(&["PEAK", "RMS"]), 0.0, 1.0, 2, 0.0),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 10.0),
    p("hld", "HOLD", "ms", ParamKind::Log, 0.02, 2000.0, 101, 10.0),
    p("rel", "RELEASE", "ms", ParamKind::Log, 5.0, 4000.0, 101, 151.0),
    p("env", "ENVELOPE", "", ParamKind::Choice(&["LIN", "LOG"]), 0.0, 1.0, 2, 1.0),
    p("auto", "AUTO", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
];

/// The gate slot's and the dynamics slot's models: the gate's five modes
/// (`gate/mode` 0-4), then the compressor's two (`dyn/mode`).
pub static DYN_MODELS: &[ModelSpec] = &[
    ModelSpec { token: "X32EXP2", name: "Expander 1:2", category: ModelCategory::Gate, params: GATE_PARAMS },
    ModelSpec { token: "X32EXP3", name: "Expander 1:3", category: ModelCategory::Gate, params: GATE_PARAMS },
    ModelSpec { token: "X32EXP4", name: "Expander 1:4", category: ModelCategory::Gate, params: GATE_PARAMS },
    ModelSpec { token: "X32GATE", name: "Gate", category: ModelCategory::Gate, params: GATE_PARAMS },
    ModelSpec { token: "X32DUCK", name: "Ducker", category: ModelCategory::Gate, params: GATE_PARAMS },
    ModelSpec { token: "X32COMP", name: "Compressor", category: ModelCategory::Compressor, params: DYN_PARAMS },
    ModelSpec { token: "X32EXP", name: "Expander", category: ModelCategory::Compressor, params: DYN_PARAMS },
];

/// An EQ band's types (`eq/N/type` 0-5).
pub const EQ_TYPES: &[&str] = &["LCut", "LShv", "PEQ", "VEQ", "HShv", "HCut"];

macro_rules! band {
    ($t:literal, $f:literal, $g:literal, $q:literal, $n:literal, $type:expr, $freq:expr) => {
        [
            p($t, concat!($n, " TYPE"), "", ParamKind::Choice(EQ_TYPES), 0.0, 5.0, 6, $type),
            p($f, concat!($n, " FREQ"), "Hz", ParamKind::Log, 20.0, 20000.0, 201, $freq),
            p($g, concat!($n, " GAIN"), "dB", ParamKind::Linear, -15.0, 15.0, 121, 0.0),
            // Q from 10 (narrow) down to 0.3 on the console's 72 steps.
            p($q, concat!($n, " Q"), "", ParamKind::Log, 10.0, 0.3, 72, 2.0),
        ]
    };
}

/// The bands' parameters in one list (band 1's TYPE, FREQ, GAIN, Q, then band 2's...).
const fn flatten<const N: usize, const M: usize>(bands: [[ParamSpec; 4]; N]) -> [ParamSpec; M] {
    assert!(M == 4 * N);
    let mut out = [bands[0][0]; M];
    let mut i = 0;
    while i < M {
        out[i] = bands[i / 4][i % 4];
        i += 1;
    }
    out
}

/// The inputs' 4 bands (Low, Lo-Mid, Hi-Mid, High): three bells and a high
/// shelf to start.
static EQ4_PARAMS: [ParamSpec; 16] = flatten([
    band!("1t", "1f", "1g", "1q", "LOW", 2.0, 124.7),
    band!("2t", "2f", "2g", "2q", "LO-MID", 2.0, 496.6),
    band!("3t", "3f", "3g", "3q", "HI-MID", 2.0, 1970.0),
    band!("4t", "4f", "4g", "4q", "HIGH", 4.0, 10020.0),
]);

/// Buses', mains' and matrices' 6 bands (Low, Low2, Lo-Mid, Hi-Mid, High2, High).
static EQ6_PARAMS: [ParamSpec; 24] = flatten([
    band!("1t", "1f", "1g", "1q", "LOW", 1.0, 79.6),
    band!("2t", "2f", "2g", "2q", "LOW2", 2.0, 158.9),
    band!("3t", "3f", "3g", "3q", "LO-MID", 2.0, 496.6),
    band!("4t", "4f", "4g", "4q", "HI-MID", 2.0, 1970.0),
    band!("5t", "5f", "5g", "5q", "HIGH2", 2.0, 5020.0),
    band!("6t", "6f", "6g", "6q", "HIGH", 4.0, 10020.0),
]);

/// The inputs' EQ (`/ch/NN/eq`, `/auxin`, `/fxrtn`).
pub static EQ_CHANNEL: &[ModelSpec] =
    &[ModelSpec { token: "X32EQ4", name: "4-Band EQ", category: ModelCategory::Eq, params: &EQ4_PARAMS }];

/// Buses', mains' and matrices' EQ.
pub static EQ_BUS: &[ModelSpec] =
    &[ModelSpec { token: "X32EQ6", name: "6-Band EQ", category: ModelCategory::Eq, params: &EQ6_PARAMS }];

/// The X32's models, as its profile names them. Its aux in and FX returns have
/// no dynamics; the filter tools are the WING's. Its FX rack has its own 61
/// models, the send / return ones (`FxPremium`) in FX 1-4 only.
pub static MODELS: ModelTables = ModelTables {
    dynamics: DYN_MODELS,
    aux_dynamics: &[],
    eq_channel: EQ_CHANNEL,
    eq_bus: EQ_BUS,
    filter_tools: FILTER_TOOLS,
    fx: FX_MODELS,
    fx_premium_slots: 4,
    gate_default: "X32GATE",
    comp_default: "X32COMP",
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_x32s_fx_rack() {
        use crate::wing::fx::{allowed_in, create, FxContext};
        // An empty slot, a plugin (as the WING's EXT), then the 61 models in the X32's order.
        assert_eq!(FX_MODELS.len(), 63);
        assert_eq!((FX_MODELS[0].token, FX_MODELS[1].token), ("NONE", "EXT"));
        assert_eq!(FX_MODELS[1], crate::wing::FX_MODELS[1], "EXT's parameters are the engine's");
        assert_eq!((FX_MODELS[2].token, FX_MODELS[9].token, FX_MODELS[62].token), ("X32HALL", "X32VRM", "X32PIT"));
        assert!(FX_MODELS.iter().all(|m| m.params.len() <= crate::wing::MAX_FX_PARAMS));
        assert_eq!(FX_MODELS.iter().map(|m| m.params.len()).max(), Some(64), "the dual GEQ");
        // FX 5-8 take the 34 insert models, FX 1-4 everything.
        let index = |code: &str| crate::model::model_index(FX_MODELS, &format!("X32{code}")).unwrap();
        for code in INSERT_ORDER {
            assert!(allowed_in(&MODELS, index(code), 7), "{code}");
        }
        assert!(!allowed_in(&MODELS, index("HALL"), 4) && allowed_in(&MODELS, index("HALL"), 3));
        assert_eq!(FX_MODELS.iter().filter(|m| m.category == ModelCategory::FxStandard).count(), 34);
        // The factory values of the owner's X32 Compact: VRM's decay, DLY's time.
        let vrm = &FX_MODELS[index("VRM")];
        assert_eq!((vrm.params[1].label, vrm.params[1].default), ("DECAY", 1.94));
        assert_eq!(FX_MODELS[index("DLY")].params[1].default, 223.0);
        // Every model has a device (silent or passing through until its algorithm lands).
        let ctx = FxContext { sample_rate: 48000.0, max_block: 64 };
        for m in &FX_MODELS[2..] {
            let mut dev = create(m.token, ctx).unwrap_or_else(|| panic!("{}", m.token));
            if fx::BUILT.contains(&m.token) {
                crate::wing::fx::tests::check_device(m.token);
                continue;
            }
            let (mut l, mut r) = ([0.5f32; 16], [0.5f32; 16]);
            dev.process(&mut l, &mut r);
            let passes = m.category == ModelCategory::FxStandard;
            assert_eq!(l[0], if passes { 0.5 } else { 0.0 }, "{}", m.token);
        }
    }

    #[test]
    fn the_x32s_models() {
        assert_eq!(DYN_MODELS.len(), 7);
        assert_eq!(MODELS.table(0)[3].token, "X32GATE");
        assert_eq!(EQ_CHANNEL[0].params.len(), 16);
        assert_eq!(EQ_BUS[0].params.len(), 24);
        assert_eq!(EQ_BUS[0].param_index("6q"), Some(23));
        let q = EQ_CHANNEL[0].params[3];
        assert_eq!((q.min, q.max, q.steps), (10.0, 0.3, 72), "Q runs from narrow to wide");
        assert!(DYN_MODELS.iter().all(|m| m.params.len() <= crate::wing::MAX_DYN_PARAMS));
        assert!(EQ_BUS[0].params.len() <= crate::wing::MAX_EQ_PARAMS);
        let ratio = DYN_MODELS[5].params[1];
        assert_eq!(ratio.kind, ParamKind::Values(DYN_RATIOS));
    }

    fn values(spec: &ModelSpec, set: &[(&str, f32)]) -> [f32; 32] {
        let mut v = [0.0; 32];
        spec.defaults_into(&mut v);
        for (id, x) in set {
            v[spec.param_index(id).unwrap()] = *x;
        }
        v
    }

    fn tail_peak(x: &[f32]) -> f32 {
        x[x.len() / 2..].iter().fold(0.0f32, |m, s| m.max(s.abs()))
    }

    #[test]
    fn the_x32s_dynamics_process() {
        use crate::wing::dynamics::{SidechainSettings, WingDynamics, XoverSettings};
        let sine = |a: f32, n: usize| (0..n).map(|i| a * (i as f32 * 0.13).sin()).collect::<Vec<f32>>();
        let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
        let run = |token: &str, set: &[(&str, f32)], level: f32| {
            let m = DYN_MODELS.iter().position(|m| m.token == token).unwrap();
            let v = values(&DYN_MODELS[m], set);
            let mut d = WingDynamics::default();
            d.set_table(DYN_MODELS);
            d.prepare(48000.0);
            let mut x = sine(level, 48000);
            d.process(&mut [&mut x[..]], None, m, &v[..DYN_MODELS[m].params.len()], &sc, &xo, 1.0, 0.0);
            tail_peak(&x) / level
        };
        // The gate closes on a signal under its threshold by its range.
        let g = run("X32GATE", &[("thr", -20.0), ("range", 40.0)], 0.01);
        assert!(g < 0.02, "gate {g}");
        assert!(run("X32GATE", &[("thr", -60.0)], 0.5) > 0.95, "open above the threshold");
        // The compressor turns a loud signal down; GAIN makes up.
        let c = run("X32COMP", &[("thr", -30.0), ("ratio", 9.0)], 0.8);
        assert!(c < 0.2, "comp {c}");
        let made_up = run("X32COMP", &[("thr", -30.0), ("ratio", 9.0), ("mgain", 12.0)], 0.8);
        assert!((made_up / c - 3.98).abs() < 0.3, "12 dB of makeup: {}", made_up / c);
    }

    #[test]
    fn the_x32s_eqs_run_finite_from_their_defaults() {
        use crate::wing::eq::WingEq;
        for table in [EQ_CHANNEL, EQ_BUS] {
            let v = values(&table[0], &[]);
            let mut eq = WingEq::default();
            eq.prepare(48000.0);
            let mut l: Vec<f32> = (0..4800).map(|i| (i as f32 * 0.01).sin() * 0.5).collect();
            let mut r = vec![0.0f32; 4800];
            for _ in 0..20 {
                eq.process(&mut [&mut l[..], &mut r[..]], table, 0, true, 100.0, &v[..table[0].params.len()]);
            }
            assert!(l.iter().chain(&r).all(|x| x.is_finite()), "{}", table[0].token);
            // Its gains glide from the jump of a new model, so a band later set rings in clean.
        }
    }

    #[test]
    fn the_x32s_eq_bands() {
        use crate::wing::eq::design;
        let eq = &EQ_CHANNEL[0];
        let at = |set: &[(&str, f32)], f: f64| {
            let v = values(eq, set);
            design(eq, &v[..eq.params.len()], 48000.0, 1.0).response_db(f, 48000.0)
        };
        assert!(at(&[], 1000.0).abs() < 0.01, "flat at 0 dB");
        assert!((at(&[("2g", 9.0)], 500.0) - 9.0).abs() < 0.1, "a bell at its frequency");
        assert!((at(&[("2t", 3.0), ("2g", -6.0)], 500.0) + 6.0).abs() < 0.1, "a vintage bell");
        assert!(at(&[("1t", 0.0), ("1f", 200.0)], 40.0) < -20.0, "the low cut");
        assert!(at(&[("4t", 5.0), ("4f", 2000.0)], 10000.0) < -20.0, "the high cut");
        assert!((at(&[("4g", 6.0)], 18000.0) - 6.0).abs() < 0.5, "the high shelf");
    }
}
