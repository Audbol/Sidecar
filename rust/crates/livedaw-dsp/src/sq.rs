//! The Allen & Heath SQ-5's processing blocks (`docs/UNIVERSAL_MIXER.md`): its
//! gate, its compressor (RMS or Peak, soft knee, parallel path), its ducker and
//! its 4-band PEQ, with the ranges of the SQ Reference Guide V1.6.0 (pages as
//! printed: 7.4 Gate p.36, 7.7 PEQ p.40, 7.8 Compressor pp.41-43). Clean-room:
//! built from the guide's description of the parameters, not from the console's
//! firmware.
//!
//! They run on the EQ and dynamics DSP of [`crate::wing`] (`design` and
//! `configure` pick them by token), on the SQ's own strips and, like every
//! console's blocks, on any strip of a Universal console. Where the guide is
//! silent the choices are Sidecar's reading: the compressor's ratio steps, the
//! soft knee's width, the slope of the PEQ's HPF and LPF modes, the ducker's
//! controls and every default value.
//!
//! The mixes' 28-band graphic EQ (p.38) is a block too, placed before their PEQ
//! (`ProfileSpec::factory_eq_block`): each mix has its own, none takes an FX slot.

use crate::model::{ModelCategory, ModelSpec, ModelTables, ParamKind, ParamSpec};
use crate::wing::FILTER_TOOLS;

pub mod fx;

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

/// The compressor's ratios: 1:1 (none) to infinity (p.42; the steps between
/// are Sidecar's reading). The last stands for infinity.
pub const RATIOS: [f32; 17] = [1.0, 1.1, 1.2, 1.3, 1.5, 1.7, 2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 6.0, 8.0, 10.0, 20.0, 1000.0];
const RATIO_NAMES: &[&str] = &[
    "1:1", "1.1:1", "1.2:1", "1.3:1", "1.5:1", "1.7:1", "2:1", "2.5:1", "3:1", "3.5:1", "4:1", "5:1", "6:1", "8:1", "10:1",
    "20:1", "INF",
];

/// The soft knee's width (dB): Sidecar's reading (the guide doesn't give it).
pub const SOFT_KNEE_DB: f32 = 12.0;

/// A level of the parallel path: -60 reads as -infinity (p.42: "-infinity to 0dB").
pub const PARALLEL_OFF_DB: f32 = -60.0;

/// The gate (p.36): THRESHOLD -72..+18 dB, DEPTH 0..60 dB, ATTACK 50 us..300 ms,
/// HOLD 10 ms..5 s, RELEASE 10 ms..1 s.
const GATE_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -72.0, 18.0, 181, -40.0),
    p("range", "DEPTH", "dB", ParamKind::Linear, 0.0, 60.0, 61, 60.0),
    p("att", "ATTACK", "ms", ParamKind::Log, 0.05, 300.0, 101, 1.0),
    p("hld", "HOLD", "ms", ParamKind::Log, 10.0, 5000.0, 101, 50.0),
    p("rel", "RELEASE", "ms", ParamKind::Log, 10.0, 1000.0, 101, 200.0),
];

/// The compressor (pp.41-42): THRESHOLD -46..+18 dB, RATIO, ATTACK 30 us..300 ms,
/// RELEASE 50 ms..2 s, GAIN 0..+18 dB, RMS or PEAK, SOFT KNEE, and the parallel
/// path's DRY and WET levels (-infinity..0 dB).
const COMP_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -46.0, 18.0, 129, 0.0),
    p("ratio", "RATIO", "", ParamKind::Choice(RATIO_NAMES), 0.0, 16.0, 17, 10.0),
    p("att", "ATTACK", "ms", ParamKind::Log, 0.03, 300.0, 101, 10.0),
    p("rel", "RELEASE", "ms", ParamKind::Log, 50.0, 2000.0, 101, 200.0),
    p("gain", "GAIN", "dB", ParamKind::Linear, 0.0, 18.0, 37, 0.0),
    p("det", "MODE", "", ParamKind::Choice(&["RMS", "PEAK"]), 0.0, 1.0, 2, 0.0),
    p("soft", "SOFT KNEE", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    p("par", "PARALLEL", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    p("dry", "DRY", "dB", ParamKind::Linear, PARALLEL_OFF_DB, 0.0, 61, PARALLEL_OFF_DB),
    p("wet", "WET", "dB", ParamKind::Linear, PARALLEL_OFF_DB, 0.0, 61, 0.0),
];

/// The ducker (p.43: a compressor mode that lowers the channel by the signal
/// on its side chain). The guide lists no controls of its own: Sidecar's
/// reading gives it the gate's.
const DUCK_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -46.0, 18.0, 129, -30.0),
    p("range", "DEPTH", "dB", ParamKind::Linear, 0.0, 60.0, 61, 20.0),
    p("att", "ATTACK", "ms", ParamKind::Log, 0.03, 300.0, 101, 10.0),
    p("hld", "HOLD", "ms", ParamKind::Log, 10.0, 5000.0, 101, 200.0),
    p("rel", "RELEASE", "ms", ParamKind::Log, 50.0, 2000.0, 101, 500.0),
];

/// The gate slot's and the dynamics slot's models.
pub static DYN_MODELS: &[ModelSpec] = &[
    ModelSpec { token: "SQGATE", name: "Gate", category: ModelCategory::Gate, params: GATE_PARAMS },
    ModelSpec { token: "SQCOMP", name: "Compressor", category: ModelCategory::Compressor, params: COMP_PARAMS },
    ModelSpec { token: "SQDUCK", name: "Ducker", category: ModelCategory::Compressor, params: DUCK_PARAMS },
];

/// The bandwidth (octaves) as a filter's Q.
pub fn width_q(octaves: f32) -> f32 {
    let n = 2.0_f32.powf(octaves);
    n.sqrt() / (n - 1.0)
}

const FREQ: (f32, f32, u32) = (20.0, 20000.0, 241);
const GAIN: (f32, f32, u32) = (-15.0, 15.0, 61);
/// WIDTH runs from 1.5 octaves to 1/9 octave (p.40).
const WIDTH: (f32, f32, u32) = (1.5, 1.0 / 9.0, 41);

/// The PEQ (p.40): LF Shelf, Bell or HPF; two parametric bands; HF Shelf, Bell
/// or LPF. Every frequency 20 Hz..20 kHz, gain +-15 dB. The bands start flat
/// (their frequencies are Sidecar's choice).
static PEQ_PARAMS: [ParamSpec; 14] = [
    p("lm", "LF MODE", "", ParamKind::Choice(&["SHELF", "BELL", "HPF"]), 0.0, 2.0, 3, 0.0),
    p("lf", "LF F", "Hz", ParamKind::Log, FREQ.0, FREQ.1, FREQ.2, 100.0),
    p("lg", "LF G", "dB", ParamKind::Linear, GAIN.0, GAIN.1, GAIN.2, 0.0),
    p("lq", "LF W", "oct", ParamKind::Log, WIDTH.0, WIDTH.1, WIDTH.2, 1.0),
    p("1f", "LM F", "Hz", ParamKind::Log, FREQ.0, FREQ.1, FREQ.2, 400.0),
    p("1g", "LM G", "dB", ParamKind::Linear, GAIN.0, GAIN.1, GAIN.2, 0.0),
    p("1q", "LM W", "oct", ParamKind::Log, WIDTH.0, WIDTH.1, WIDTH.2, 1.0),
    p("2f", "HM F", "Hz", ParamKind::Log, FREQ.0, FREQ.1, FREQ.2, 2000.0),
    p("2g", "HM G", "dB", ParamKind::Linear, GAIN.0, GAIN.1, GAIN.2, 0.0),
    p("2q", "HM W", "oct", ParamKind::Log, WIDTH.0, WIDTH.1, WIDTH.2, 1.0),
    p("hm", "HF MODE", "", ParamKind::Choice(&["SHELF", "BELL", "LPF"]), 0.0, 2.0, 3, 0.0),
    p("hf", "HF F", "Hz", ParamKind::Log, FREQ.0, FREQ.1, FREQ.2, 8000.0),
    p("hg", "HF G", "dB", ParamKind::Linear, GAIN.0, GAIN.1, GAIN.2, 0.0),
    p("hq", "HF W", "oct", ParamKind::Log, WIDTH.0, WIDTH.1, WIDTH.2, 1.0),
];

/// The PEQ, on inputs, FX returns and mixes alike.
pub static EQ: &[ModelSpec] = &[ModelSpec { token: "SQPEQ", name: "PEQ", category: ModelCategory::Eq, params: &PEQ_PARAMS }];

/// The GEQ's bands (p.38), 1/3 octave wide.
pub const GEQ_HZ: [f32; 28] = [
    31.0, 40.0, 50.0, 63.0, 80.0, 100.0, 125.0, 160.0, 200.0, 250.0, 315.0, 400.0, 500.0, 630.0, 800.0, 1000.0, 1250.0, 1600.0,
    2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0, 10000.0, 12500.0, 16000.0,
];

/// The GEQ's width: 1/3 octave (p.38: constant).
pub const GEQ_WIDTH: f32 = 1.0 / 3.0;

/// The 28 bands' gains, -12..+12 dB (p.38).
static GEQ_PARAMS: [ParamSpec; 28] = [
    p("g1", "31", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g2", "40", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g3", "50", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g4", "63", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g5", "80", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g6", "100", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g7", "125", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g8", "160", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g9", "200", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g10", "250", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g11", "315", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g12", "400", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g13", "500", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g14", "630", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g15", "800", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g16", "1k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g17", "1.25k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g18", "1.6k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g19", "2k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g20", "2.5k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g21", "3.15k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g22", "4k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g23", "5k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g24", "6.3k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g25", "8k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g26", "10k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g27", "12.5k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
    p("g28", "16k", "dB", ParamKind::Linear, -12.0, 12.0, 49, 0.0),
];

/// The mixes' EQs: the PEQ, then the graphic EQ.
pub static EQ_BUS: [ModelSpec; 2] =
    [EQ[0], ModelSpec { token: "SQGEQ", name: "GEQ", category: ModelCategory::Eq, params: &GEQ_PARAMS }];

/// The SQ-5's models.
pub static MODELS: ModelTables = ModelTables {
    dynamics: DYN_MODELS,
    aux_dynamics: &[],
    eq_channel: EQ,
    eq_bus: &EQ_BUS,
    filter_tools: FILTER_TOOLS,
    fx: &fx::FX_MODELS,
    fx_premium_slots: 8,
    gate_default: "SQGATE",
    comp_default: "SQCOMP",
};

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wing::{MAX_DYN_PARAMS, MAX_EQ_PARAMS};

    #[test]
    fn the_blocks_have_the_guides_ranges() {
        let gate = DYN_MODELS[0].params;
        let range = |ps: &[ParamSpec], id: &str| {
            let s = ps.iter().find(|s| s.id == id).unwrap();
            (s.min, s.max)
        };
        assert_eq!(range(gate, "thr"), (-72.0, 18.0));
        assert_eq!(range(gate, "range"), (0.0, 60.0));
        assert_eq!(range(gate, "att"), (0.05, 300.0));
        assert_eq!(range(gate, "hld"), (10.0, 5000.0));
        assert_eq!(range(gate, "rel"), (10.0, 1000.0));
        let comp = DYN_MODELS[1].params;
        assert_eq!(range(comp, "thr"), (-46.0, 18.0));
        assert_eq!(range(comp, "att"), (0.03, 300.0));
        assert_eq!(range(comp, "rel"), (50.0, 2000.0));
        assert_eq!(range(comp, "gain"), (0.0, 18.0));
        assert_eq!(RATIOS.len(), RATIO_NAMES.len());
        assert_eq!((RATIOS[0], *RATIO_NAMES.last().unwrap()), (1.0, "INF"));
        let eq = EQ[0].params;
        assert_eq!(range(eq, "lf"), (20.0, 20000.0));
        assert_eq!(range(eq, "2g"), (-15.0, 15.0));
        assert_eq!(range(eq, "1q"), (1.5, 1.0 / 9.0));
        assert!(DYN_MODELS.iter().all(|m| m.params.len() <= MAX_DYN_PARAMS));
        assert!(eq.len() <= MAX_EQ_PARAMS);
        // One octave is Q 1.41; a ninth of an octave about 13.
        assert!((width_q(1.0) - std::f32::consts::SQRT_2).abs() < 1e-4);
        assert!((width_q(1.0 / 9.0) - 12.99).abs() < 0.05);
    }

    #[test]
    fn the_peq_is_flat_until_a_band_is_moved_and_each_mode_does_its_shape() {
        use crate::wing::eq::design;
        let spec = &EQ[0];
        let mut v = vec![0.0; spec.params.len()];
        spec.defaults_into(&mut v);
        let sr = 96000.0;
        let d = design(spec, &v, sr, 1.0);
        for f in [30.0, 1000.0, 15000.0] {
            assert!(d.response_db(f, sr).abs() < 0.01, "{f}");
        }
        let at = |id: &str| spec.param_index(id).unwrap();
        // A +6 dB bell at 1 kHz.
        let mut bell = v.clone();
        bell[at("1f")] = 1000.0;
        bell[at("1g")] = 6.0;
        let d = design(spec, &bell, sr, 1.0);
        assert!((d.response_db(1000.0, sr) - 6.0).abs() < 0.05);
        assert!(d.response_db(100.0, sr).abs() < 0.2);
        // The LF band as an HPF, the HF band as an LPF.
        let mut cut = v.clone();
        cut[at("lm")] = 2.0;
        cut[at("lf")] = 200.0;
        cut[at("hm")] = 2.0;
        cut[at("hf")] = 4000.0;
        let d = design(spec, &cut, sr, 1.0);
        assert!(d.response_db(1000.0, sr).abs() < 0.2);
        assert!(d.response_db(50.0, sr) < -20.0);
        assert!(d.response_db(16000.0, sr) < -20.0);
        // Narrower is narrower.
        let mut wide = bell.clone();
        wide[at("1q")] = 1.5;
        let mut narrow = bell.clone();
        narrow[at("1q")] = 1.0 / 9.0;
        let (w, n) = (design(spec, &wide, sr, 1.0), design(spec, &narrow, sr, 1.0));
        assert!(w.response_db(1400.0, sr) > n.response_db(1400.0, sr) + 2.0);
    }
}

#[cfg(test)]
mod dynamics_tests {
    use super::*;
    use crate::model::model_index;
    use crate::wing::dynamics::{SidechainSettings, WingDynamics, XoverSettings};

    /// The output RMS over the last half of 1 s of a 1 kHz tone at `level`, through `token` set by `set`.
    fn run(token: &str, set: &[(&str, f32)], level: f32) -> f32 {
        let m = model_index(DYN_MODELS, token).unwrap();
        let spec = &DYN_MODELS[m];
        let mut v = vec![0.0; spec.params.len()];
        spec.defaults_into(&mut v);
        for (id, x) in set {
            v[spec.param_index(id).unwrap()] = *x;
        }
        let sr = 48000.0;
        let mut d = WingDynamics::default();
        d.set_table(DYN_MODELS);
        d.prepare(sr as f64);
        let (mut sum, mut n) = (0.0f32, 0usize);
        for b in 0..(48000 / 256) {
            let mut x: Vec<f32> =
                (0..256).map(|i| level * (2.0 * std::f32::consts::PI * 1000.0 * (b * 256 + i) as f32 / sr).sin()).collect();
            let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
            d.process(&mut [&mut x[..]], None, m, &v, &sc, &xo, 1.0, 0.0);
            if b > 48000 / 512 {
                sum += x.iter().map(|s| s * s).sum::<f32>();
                n += x.len();
            }
        }
        (sum / n as f32).sqrt()
    }

    #[test]
    fn the_compressor_compresses_and_its_parallel_path_blends() {
        let level = 0.5;
        let dry = level / std::f32::consts::SQRT_2;
        // RATIO INF at a threshold well under the signal: flattened to it.
        let hard = run("SQCOMP", &[("thr", -30.0), ("ratio", 16.0)], level);
        assert!(hard < dry * 0.2, "{hard}");
        // GAIN makes up after it.
        let made_up = run("SQCOMP", &[("thr", -30.0), ("ratio", 16.0), ("gain", 12.0)], level);
        assert!((made_up / hard - 3.98).abs() < 0.2, "{}", made_up / hard);
        // 1:1 does nothing.
        assert!((run("SQCOMP", &[("thr", -30.0), ("ratio", 0.0)], level) / dry - 1.0).abs() < 0.02);
        // Parallel, DRY 0 dB and WET at -infinity: the dry signal alone.
        let dry_only =
            run("SQCOMP", &[("thr", -30.0), ("ratio", 16.0), ("par", 1.0), ("dry", 0.0), ("wet", PARALLEL_OFF_DB)], level);
        assert!((dry_only / dry - 1.0).abs() < 0.02, "{}", dry_only / dry);
        // Both at 0 dB: the compressed path on top of the dry one.
        let both = run("SQCOMP", &[("thr", -30.0), ("ratio", 16.0), ("par", 1.0), ("dry", 0.0), ("wet", 0.0)], level);
        assert!(both > dry && both > hard, "{both}");
    }

    #[test]
    fn the_gate_closes_by_its_depth() {
        let quiet = 0.01;
        let open = run("SQGATE", &[("thr", -60.0)], quiet);
        assert!((open / (quiet / std::f32::consts::SQRT_2) - 1.0).abs() < 0.03, "{open}");
        let closed = run("SQGATE", &[("thr", -20.0), ("range", 20.0)], quiet);
        assert!((closed / open - 0.1).abs() < 0.02, "{}", closed / open);
    }
}

#[cfg(test)]
mod geq_tests {
    use super::*;

    #[test]
    fn the_geq_has_the_guides_28_bands_and_each_lifts_its_own() {
        use crate::wing::eq::design;
        let spec = &EQ_BUS[1];
        assert_eq!(spec.params.len(), 28);
        assert!(spec.params.len() <= crate::wing::MAX_EQ_PARAMS);
        assert_eq!((GEQ_HZ[0], GEQ_HZ[15], GEQ_HZ[27]), (31.0, 1000.0, 16000.0));
        assert_eq!((spec.params[0].label, spec.params[27].label), ("31", "16k"));
        let sr = 96000.0;
        let mut v = vec![0.0; 28];
        assert_eq!(design(spec, &v, sr, 1.0).len, 0, "flat: no filters at all");
        v[15] = 12.0;
        let one = design(spec, &v, sr, 1.0);
        assert!((one.response_db(1000.0, sr) - 12.0).abs() < 0.1);
        assert!(one.response_db(500.0, sr).abs() < 0.8, "1/3 octave: an octave away is nearly flat");
        // Every band at once fits.
        let all = design(spec, &[3.0; 28], sr, 1.0);
        assert_eq!(all.len, 28);
    }
}
