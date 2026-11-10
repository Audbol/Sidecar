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
    fx_premium_first: 0,
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
        // Every model has its algorithm (9g).
        for m in &FX_MODELS[2..] {
            assert!(fx::BUILT.contains(&m.token), "{} has no algorithm", m.token);
        }
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
        // The gate closes on a signal under its threshold by its range (still
        // falling half a second in, at the default 258 ms RELEASE).
        let g = run("X32GATE", &[("thr", -20.0), ("range", 40.0)], 0.01);
        assert!(g < 0.03, "gate {g}");
        assert!(run("X32GATE", &[("thr", -60.0)], 0.5) > 0.95, "open above the threshold");
        // The compressor turns a loud signal down; GAIN makes up.
        let c = run("X32COMP", &[("thr", -30.0), ("ratio", 9.0)], 0.8);
        assert!(c < 0.2, "comp {c}");
        let made_up = run("X32COMP", &[("thr", -30.0), ("ratio", 9.0), ("mgain", 12.0)], 0.8);
        assert!((made_up / c - 3.98).abs() < 0.3, "12 dB of makeup: {}", made_up / c);
    }

    fn dynamics(token: &str, set: &[(&str, f32)], input: &mut [f32]) {
        use crate::wing::dynamics::{SidechainSettings, WingDynamics, XoverSettings};
        let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
        let m = DYN_MODELS.iter().position(|m| m.token == token).unwrap();
        let v = values(&DYN_MODELS[m], set);
        let mut d = WingDynamics::default();
        d.set_table(DYN_MODELS);
        d.prepare(48000.0);
        d.process(&mut [input], None, m, &v[..DYN_MODELS[m].params.len()], &sc, &xo, 1.0, 0.0);
    }

    /// A 1 kHz sine of amplitude `amp(sample)`.
    fn tone(n: usize, amp: impl Fn(usize) -> f32) -> Vec<f32> {
        (0..n).map(|i| amp(i) * (i as f32 * std::f32::consts::TAU / 48.0).sin()).collect()
    }

    /// Gain (dB) on a steady 1 kHz sine at `db` dBFS.
    fn steady(token: &str, set: &[(&str, f32)], db: f32) -> f32 {
        let n = 96000;
        let t = tone(n, |_| 10f32.powf(db / 20.0));
        let mut y = t.clone();
        dynamics(token, set, &mut y);
        let rms = |x: &[f32]| (x.iter().map(|s| s * s).sum::<f32>() / x.len() as f32).sqrt();
        20.0 * (rms(&y[n - 9600..]) / rms(&t[n - 9600..])).log10()
    }

    /// Gain (dB), `at` ms after a tone falls from -10 to -70 dBFS.
    fn after_drop(token: &str, set: &[(&str, f32)], at: &[f32]) -> Vec<f32> {
        let up = 48000;
        let t = tone(up + 48 * 9000, |i| if i < up { 0.316 } else { 0.000316 });
        let mut y = t.clone();
        dynamics(token, set, &mut y);
        at.iter()
            .map(|ms| {
                let c = up + (*ms * 48.0) as usize;
                let (mut a, mut b) = (0.0, 0.0);
                for i in c - 96..c + 96 {
                    a += y[i] * y[i];
                    b += t[i] * t[i];
                }
                10.0 * (a / b).log10()
            })
            .collect()
    }

    fn near(what: &str, got: f32, want: f32, tol: f32) {
        assert!((got - want).abs() <= tol, "{what}: {got} dB, the X32 {want} dB");
    }

    // The figures below were measured on an X32 Compact (firmware 4.09).

    #[test]
    fn the_x32_gate_measured() {
        let open = [("thr", -30.0), ("range", 40.0), ("att", 1.0), ("hld", 50.2), ("rel", 198.0)];
        near("gate under", steady("X32GATE", &open, -40.0), -40.0, 0.2);
        near("gate over", steady("X32GATE", &open, -20.0), 0.0, 0.05);
        near("EXP2 -40", steady("X32EXP2", &open, -40.0), -10.0, 0.5);
        near("EXP2 -50", steady("X32EXP2", &open, -50.0), -20.0, 0.5);
        near("EXP4 -40", steady("X32EXP4", &open, -40.0), -30.0, 0.8);
        // EXP3 rounds into its range.
        for (range, input, want) in [(30.0, -40.0, -17.6), (30.0, -50.0, -27.6), (60.0, -50.0, -39.2), (60.0, -60.0, -54.0)] {
            let set = [("thr", -30.0), ("range", range), ("att", 1.0), ("hld", 50.2), ("rel", 198.0)];
            near("EXP3", steady("X32EXP3", &set, input), want, 0.8);
        }
        near("duck over", steady("X32DUCK", &open, -20.0), -40.0, 0.2);
        near("duck under", steady("X32DUCK", &open, -40.0), 0.0, 0.05);
    }

    #[test]
    fn the_x32_gate_closes_as_measured() {
        let set = |rel: f32| [("thr", -30.0), ("range", 40.0), ("att", 1.0), ("hld", 50.2), ("rel", rel)];
        let at = [100.0, 300.0, 1000.0, 2000.0];
        for (rel, want) in
            [(504.0, [-2.0, -9.4, -31.9, -39.8]), (983.0, [-1.0, -4.8, -17.9, -33.0]), (4000.0, [-0.2, -1.2, -4.5, -9.2])]
        {
            let got = after_drop("X32GATE", &set(rel), &at);
            for (g, w) in got.iter().zip(want) {
                near(&format!("gate release {rel}"), *g, w, 1.0);
            }
        }
        // HOLD waits, then the fall starts.
        let hold = [("thr", -30.0), ("range", 40.0), ("att", 1.0), ("hld", 1002.0), ("rel", 198.0)];
        let got = after_drop("X32GATE", &hold, &[900.0, 1300.0]);
        near("in hold", got[0], 0.0, 0.1);
        assert!(got[1] < -20.0, "after hold {}", got[1]);
    }

    #[test]
    fn the_x32_compressor_measured() {
        let c = |extra: &[(&str, f32)], db: f32| {
            let mut s = vec![("thr", -30.0), ("ratio", 6.0), ("knee", 0.0)];
            s.extend_from_slice(extra);
            steady("X32COMP", &s, db)
        };
        // Ratio index 6 is 4:1.
        for (db, want) in [(-20.0, -7.6), (-10.0, -15.1), (0.0, -22.6)] {
            near("4:1", c(&[], db), want, 0.3);
        }
        near("RMS", c(&[("det", 1.0)], -10.0), -12.9, 0.3);
        // KNEE at the threshold.
        for (knee, want) in [(1.0, -0.7), (3.0, -2.1), (5.0, -3.4)] {
            near("knee", c(&[("knee", knee)], -30.0), want, 0.4);
        }
        let base = [("thr", -30.0), ("ratio", 6.0), ("knee", 0.0), ("att", 10.0)];
        let with = |more: &[(&'static str, f32)]| {
            let mut s = base.to_vec();
            s.extend_from_slice(more);
            s
        };
        // LOG release: one pole in dB, the set time its constant.
        let got = after_drop("X32COMP", &with(&[("hld", 0.02), ("rel", 983.0)]), &[983.0, 1966.0]);
        near("1 x release", got[0], -5.5, 1.0);
        near("2 x release", got[1], -2.0, 0.8);
        // LIN release: 10 dB per set time.
        let lin = after_drop("X32COMP", &with(&[("hld", 0.02), ("rel", 983.0), ("env", 0.0)]), &[983.0]);
        near("LIN", lin[0], -5.1, 1.0);
        // Long HOLD stretches the fall instead of waiting.
        let got = after_drop("X32COMP", &with(&[("hld", 502.0), ("rel", 101.0)]), &[100.0, 300.0, 1000.0]);
        near("hold 100", got[0], -14.3, 1.0);
        near("hold 300", got[1], -9.6, 1.0);
        near("hold 1000", got[2], -0.1, 0.5);
    }

    #[test]
    fn the_x32_eq_shapes_measured() {
        use crate::wing::eq::design;
        let eq = &EQ_CHANNEL[0];
        let oct = [125.0, 250.0, 500.0, 1000.0, 2000.0];
        let check = |what: &str, set: &[(&str, f32)], want: [f32; 5], tol: f32| {
            let v = values(eq, set);
            let d = design(eq, &v[..eq.params.len()], 48000.0, 1.0);
            for (f, w) in oct.iter().zip(want) {
                near(&format!("{what} at {f}"), d.response_db(*f, 48000.0) as f32, w, tol);
            }
        };
        check("LShv +12", &[("1t", 1.0), ("1f", 195.4), ("1g", 12.0)], [11.4, 7.2, 1.3, 0.1, 0.0], 0.25);
        check("LShv -6", &[("1t", 1.0), ("1f", 195.4), ("1g", -6.0)], [-5.5, -2.7, -0.3, 0.0, 0.0], 0.25);
        check("HShv +12", &[("4t", 4.0), ("4f", 496.6), ("4g", 12.0)], [0.2, 2.7, 9.2, 11.7, 12.0], 0.25);
        check("VEQ +12", &[("2t", 3.0), ("2f", 990.9), ("2g", 12.0), ("2q", 2.1)], [0.2, 0.9, 3.8, 12.0, 3.8], 0.4);
        check("VEQ -6", &[("2t", 3.0), ("2f", 990.9), ("2g", -6.0), ("2q", 2.1)], [0.0, -0.2, -1.1, -6.0, -1.1], 0.3);
        check("PEQ -12", &[("2f", 990.9), ("2g", -12.0), ("2q", 2.1)], [-0.1, -0.3, -1.4, -12.0, -1.4], 0.25);
        check("LCut 195", &[("1t", 0.0), ("1f", 195.4)], [-8.6, -1.4, -0.1, 0.0, 0.0], 0.3);
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
