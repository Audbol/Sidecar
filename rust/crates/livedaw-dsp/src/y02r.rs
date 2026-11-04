//! The Yamaha 02R's processing models (Phase 6, `docs/UNIVERSAL_MIXER.md`): its
//! 4-band parametric EQ and its dynamics processor's six types (compressor,
//! expander, compander hard and soft, gate, ducking), described with the
//! ranges and steps of its User's Guides (MAN, and V2 for Version 2; pages as
//! printed). Clean-room: built from the guides' description of the
//! parameters, not from the console's firmware.
//!
//! They run on the EQ and dynamics DSP of [`crate::wing`] (`design` and
//! `configure` pick them by token). The EQ's frequencies run in 1/12 octave
//! steps (MAN p.50): the guide doesn't list them, but every frequency its 40
//! presets use falls on 1 kHz x 2^((n - 67) / 12). Its Q runs from 10 to 0.1
//! in 41 steps: the presets use 33 values, the other 8 are Sidecar's reading.
//! The dynamics' RELEASE, DECAY and HOLD have 160 and 216 points whose values
//! the guide doesn't give: Sidecar's reading spaces them evenly in log time
//! between the guide's ends (at 48 kHz).

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

/// The index of the value of `table` nearest `v`.
const fn nearest(table: &[f32], v: f32) -> f32 {
    let mut best = 0;
    let mut i = 1;
    while i < table.len() {
        let (d, b) = (table[i] - v, table[best] - v);
        if d * d < b * b {
            best = i;
        }
        i += 1;
    }
    best as f32
}

/// The EQ's 120 frequencies (Hz): 1/12 octave steps from 21 Hz to 20.1 kHz,
/// 1 kHz x 2^((n - 67) / 12) (every preset's frequency is one of them).
pub const FREQS: [f32; 120] = [
    20.86, 22.1, 23.41, 24.8, 26.28, 27.84, 29.5, 31.25, 33.11, 35.08, 37.16, 39.37, 41.71, 44.19, 46.82, 49.61, 52.56, 55.68,
    58.99, 62.5, 66.22, 70.15, 74.33, 78.75, 83.43, 88.39, 93.64, 99.21, 105.1, 111.4, 118.0, 125.0, 132.4, 140.3, 148.7, 157.5,
    166.9, 176.8, 187.3, 198.4, 210.2, 222.7, 236.0, 250.0, 264.9, 280.6, 297.3, 315.0, 333.7, 353.6, 374.6, 396.9, 420.4, 445.4,
    471.9, 500.0, 529.7, 561.2, 594.6, 630.0, 667.4, 707.1, 749.2, 793.7, 840.9, 890.9, 943.9, 1000.0, 1059.0, 1122.0, 1189.0,
    1260.0, 1335.0, 1414.0, 1498.0, 1587.0, 1682.0, 1782.0, 1888.0, 2000.0, 2119.0, 2245.0, 2378.0, 2520.0, 2670.0, 2828.0,
    2997.0, 3175.0, 3364.0, 3564.0, 3775.0, 4000.0, 4238.0, 4490.0, 4757.0, 5040.0, 5339.0, 5657.0, 5993.0, 6350.0, 6727.0,
    7127.0, 7551.0, 8000.0, 8476.0, 8980.0, 9514.0, 10080.0, 10680.0, 11310.0, 11990.0, 12700.0, 13450.0, 14250.0, 15100.0,
    16000.0, 16950.0, 17960.0, 19030.0, 20160.0,
];

/// RELEASE and DECAY: 160 points, 5 ms to 42.3 s (at 48 kHz, MAN p.194), evenly
/// spaced in log time: Sidecar's reading (the guide gives the ends only).
#[allow(clippy::approx_constant)] // 6.28 ms is a time, not 2 pi
pub const RELEASES: [f32; 160] = [
    5.0, 5.29, 5.6, 5.93, 6.28, 6.64, 7.03, 7.45, 7.88, 8.34, 8.83, 9.35, 9.89, 10.5, 11.1, 11.7, 12.4, 13.1, 13.9, 14.7, 15.6,
    16.5, 17.5, 18.5, 19.6, 20.7, 21.9, 23.2, 24.6, 26.0, 27.5, 29.2, 30.9, 32.7, 34.6, 36.6, 38.7, 41.0, 43.4, 46.0, 48.6, 51.5,
    54.5, 57.7, 61.1, 64.6, 68.4, 72.4, 76.7, 81.2, 85.9, 90.9, 96.2, 102.0, 108.0, 114.0, 121.0, 128.0, 135.0, 143.0, 152.0,
    161.0, 170.0, 180.0, 190.0, 202.0, 213.0, 226.0, 239.0, 253.0, 268.0, 284.0, 300.0, 318.0, 336.0, 356.0, 377.0, 399.0, 422.0,
    447.0, 473.0, 501.0, 530.0, 561.0, 594.0, 629.0, 666.0, 705.0, 746.0, 789.0, 836.0, 885.0, 936.0, 991.0, 1050.0, 1110.0,
    1180.0, 1240.0, 1320.0, 1390.0, 1480.0, 1560.0, 1650.0, 1750.0, 1850.0, 1960.0, 2080.0, 2200.0, 2330.0, 2460.0, 2610.0,
    2760.0, 2920.0, 3090.0, 3270.0, 3460.0, 3670.0, 3880.0, 4110.0, 4350.0, 4600.0, 4870.0, 5160.0, 5460.0, 5780.0, 6120.0,
    6470.0, 6850.0, 7250.0, 7680.0, 8130.0, 8600.0, 9110.0, 9640.0, 10200.0, 10800.0, 11400.0, 12100.0, 12800.0, 13600.0,
    14400.0, 15200.0, 16100.0, 17000.0, 18000.0, 19100.0, 20200.0, 21400.0, 22600.0, 24000.0, 25400.0, 26800.0, 28400.0, 30100.0,
    31800.0, 33700.0, 35700.0, 37800.0, 40000.0, 42300.0,
];

/// HOLD: 216 points, 0.02 ms to 1.96 s (at 48 kHz), evenly spaced in log time:
/// Sidecar's reading.
pub const HOLDS: [f32; 216] = [
    0.02, 0.0211, 0.0223, 0.0235, 0.0248, 0.0261, 0.0276, 0.0291, 0.0307, 0.0324, 0.0341, 0.036, 0.038, 0.0401, 0.0423, 0.0446,
    0.047, 0.0496, 0.0523, 0.0552, 0.0583, 0.0615, 0.0648, 0.0684, 0.0721, 0.0761, 0.0803, 0.0847, 0.0893, 0.0942, 0.0994, 0.105,
    0.111, 0.117, 0.123, 0.13, 0.137, 0.145, 0.152, 0.161, 0.17, 0.179, 0.189, 0.199, 0.21, 0.222, 0.234, 0.247, 0.26, 0.275,
    0.29, 0.305, 0.322, 0.34, 0.359, 0.378, 0.399, 0.421, 0.444, 0.469, 0.494, 0.521, 0.55, 0.58, 0.612, 0.646, 0.681, 0.719,
    0.758, 0.8, 0.843, 0.89, 0.939, 0.99, 1.04, 1.1, 1.16, 1.23, 1.29, 1.36, 1.44, 1.52, 1.6, 1.69, 1.78, 1.88, 1.98, 2.09, 2.21,
    2.33, 2.46, 2.59, 2.73, 2.88, 3.04, 3.21, 3.39, 3.57, 3.77, 3.97, 4.19, 4.42, 4.67, 4.92, 5.19, 5.48, 5.78, 6.1, 6.43, 6.78,
    7.16, 7.55, 7.96, 8.4, 8.86, 9.35, 9.86, 10.4, 11.0, 11.6, 12.2, 12.9, 13.6, 14.3, 15.1, 16.0, 16.8, 17.8, 18.7, 19.8, 20.8,
    22.0, 23.2, 24.5, 25.8, 27.2, 28.7, 30.3, 32.0, 33.7, 35.6, 37.5, 39.6, 41.8, 44.1, 46.5, 49.0, 51.7, 54.6, 57.6, 60.7, 64.0,
    67.6, 71.3, 75.2, 79.3, 83.7, 88.3, 93.1, 98.2, 104.0, 109.0, 115.0, 122.0, 128.0, 135.0, 143.0, 151.0, 159.0, 168.0, 177.0,
    187.0, 197.0, 208.0, 219.0, 231.0, 244.0, 257.0, 271.0, 286.0, 302.0, 318.0, 336.0, 354.0, 374.0, 394.0, 416.0, 439.0, 463.0,
    488.0, 515.0, 543.0, 573.0, 605.0, 638.0, 673.0, 710.0, 749.0, 790.0, 833.0, 879.0, 927.0, 978.0, 1030.0, 1090.0, 1150.0,
    1210.0, 1280.0, 1350.0, 1420.0, 1500.0, 1580.0, 1670.0, 1760.0, 1860.0, 1960.0,
];

/// The Q values (10 to 0.1 in 41 steps, MAN p.50). The presets use 33 of them
/// (MAN pp.56-60); 3.2, 2.5, 1.1, 0.8, 0.35, 0.32, 0.25 and 0.22 are Sidecar's
/// reading of the rest.
pub const Q_VALUES: [f32; 41] = [
    10.0, 9.0, 8.0, 7.0, 6.3, 5.6, 5.0, 4.5, 4.0, 3.5, 3.2, 2.8, 2.5, 2.2, 2.0, 1.8, 1.6, 1.4, 1.2, 1.1, 1.0, 0.9, 0.8, 0.7,
    0.63, 0.56, 0.5, 0.45, 0.4, 0.35, 0.32, 0.28, 0.25, 0.22, 0.2, 0.18, 0.16, 0.14, 0.12, 0.11, 0.1,
];

/// The L-MID and H-MID bands' Q, as the display shows it.
pub const MID_Q: &[&str] = &[
    "10", "9", "8", "7", "6.3", "5.6", "5", "4.5", "4", "3.5", "3.2", "2.8", "2.5", "2.2", "2", "1.8", "1.6", "1.4", "1.2",
    "1.1", "1", "0.9", "0.8", "0.7", "0.63", "0.56", "0.5", "0.45", "0.4", "0.35", "0.32", "0.28", "0.25", "0.22", "0.2", "0.18",
    "0.16", "0.14", "0.12", "0.11", "0.1",
];

/// LOW's: the Qs, then L.SHELF and HPF (turning Q past its end, MAN p.50).
pub const LOW_Q: &[&str] = &[
    "10", "9", "8", "7", "6.3", "5.6", "5", "4.5", "4", "3.5", "3.2", "2.8", "2.5", "2.2", "2", "1.8", "1.6", "1.4", "1.2",
    "1.1", "1", "0.9", "0.8", "0.7", "0.63", "0.56", "0.5", "0.45", "0.4", "0.35", "0.32", "0.28", "0.25", "0.22", "0.2", "0.18",
    "0.16", "0.14", "0.12", "0.11", "0.1", "L.SHELF", "HPF",
];

/// HIGH's: the Qs, then H.SHELF and LPF.
pub const HIGH_Q: &[&str] = &[
    "10", "9", "8", "7", "6.3", "5.6", "5", "4.5", "4", "3.5", "3.2", "2.8", "2.5", "2.2", "2", "1.8", "1.6", "1.4", "1.2",
    "1.1", "1", "0.9", "0.8", "0.7", "0.63", "0.56", "0.5", "0.45", "0.4", "0.35", "0.32", "0.28", "0.25", "0.22", "0.2", "0.18",
    "0.16", "0.14", "0.12", "0.11", "0.1", "H.SHELF", "LPF",
];

/// The outer bands' SHELF and filter choices (indexes into `LOW_Q` / `HIGH_Q`).
pub const SHELF: f32 = 41.0;
pub const FILTER: f32 = 42.0;

/// Q 0.7 (the reset's L-MID and H-MID: MAN p.51).
const Q07: f32 = 23.0;

/// The EQ's parameters: LOW, L-MID, H-MID and HIGH (frequency, gain, Q each),
/// and the HPF and LPF switches (the gain knob of a band set to HPF or LPF:
/// MAN p.50), as the EQ reset sets them (L.SHELF 125 Hz, peaks at 1 kHz and
/// 4 kHz Q 0.7, H.SHELF 10 kHz, all at 0 dB; MAN p.51); scene 0 is taken to be
/// the same (the guide doesn't say).
static EQ_PARAMS: [ParamSpec; 14] = [
    p("lf", "LOW F", "Hz", ParamKind::Values(&FREQS), 0.0, 119.0, 120, nearest(&FREQS, 125.0)),
    p("lg", "LOW G", "dB", ParamKind::Linear, -18.0, 18.0, 73, 0.0),
    p("lq", "LOW Q", "", ParamKind::Choice(LOW_Q), 0.0, 42.0, 43, SHELF),
    p("lmf", "L-MID F", "Hz", ParamKind::Values(&FREQS), 0.0, 119.0, 120, nearest(&FREQS, 1000.0)),
    p("lmg", "L-MID G", "dB", ParamKind::Linear, -18.0, 18.0, 73, 0.0),
    p("lmq", "L-MID Q", "", ParamKind::Choice(MID_Q), 0.0, 40.0, 41, Q07),
    p("hmf", "H-MID F", "Hz", ParamKind::Values(&FREQS), 0.0, 119.0, 120, nearest(&FREQS, 4000.0)),
    p("hmg", "H-MID G", "dB", ParamKind::Linear, -18.0, 18.0, 73, 0.0),
    p("hmq", "H-MID Q", "", ParamKind::Choice(MID_Q), 0.0, 40.0, 41, Q07),
    p("hf", "HIGH F", "Hz", ParamKind::Values(&FREQS), 0.0, 119.0, 120, nearest(&FREQS, 10000.0)),
    p("hg", "HIGH G", "dB", ParamKind::Linear, -18.0, 18.0, 73, 0.0),
    p("hq", "HIGH Q", "", ParamKind::Choice(HIGH_Q), 0.0, 42.0, 43, SHELF),
    p("hpf", "HPF", "", ParamKind::Toggle, 0.0, 1.0, 2, 1.0),
    p("lpf", "LPF", "", ParamKind::Toggle, 0.0, 1.0, 2, 1.0),
];

/// The 4-band EQ of the inputs, the tape and effect returns and STEREO.
pub static EQ: &[ModelSpec] = &[ModelSpec { token: "Y2EQ", name: "4-Band PEQ", category: ModelCategory::Eq, params: &EQ_PARAMS }];

/// The compressor's and expander's ratios (MAN p.194): oo is 1000:1.
pub const RATIOS: [f32; 16] = [1.0, 1.1, 1.3, 1.5, 1.7, 2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 6.0, 8.0, 10.0, 20.0, 1000.0];

/// The compander's: the same up to 20:1.
pub const COMPANDER_RATIOS: [f32; 15] = [1.0, 1.1, 1.3, 1.5, 1.7, 2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 6.0, 8.0, 10.0, 20.0];

/// The compressor's and expander's KNEE: hard, then 1-5.
pub const KNEES: &[&str] = &["HARD", "1", "2", "3", "4", "5"];

/// The compressor's parameters (MAN p.63), as scene 0 has them: off, flat
/// (0 dB, 1:1, 0 ms, +0 dB, hard, 5 ms: MAN-GS pp.55-56).
static CMP_PARAMS: [ParamSpec; 6] = [
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -54.0, 0.0, 55, 0.0),
    p("ratio", "RATIO", "", ParamKind::Values(&RATIOS), 0.0, 15.0, 16, 0.0),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 0.0),
    p("gain", "OUT GAIN", "dB", ParamKind::Linear, 0.0, 18.0, 37, 0.0),
    p("knee", "KNEE", "", ParamKind::Choice(KNEES), 0.0, 5.0, 6, 0.0),
    p("rel", "RELEASE", "ms", ParamKind::Values(&RELEASES), 0.0, 159.0, 160, 0.0),
];

/// The expander's (MAN p.65), on preset 2 A.Dr.BD's values (-23 dB, 1.7:1,
/// 1 ms, +3.5 dB, knee 2, 70 ms: MAN p.76).
static EXP_PARAMS: [ParamSpec; 6] = [
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -54.0, 0.0, 55, -23.0),
    p("ratio", "RATIO", "", ParamKind::Values(&RATIOS), 0.0, 15.0, 16, nearest(&RATIOS, 1.7)),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 1.0),
    p("gain", "OUT GAIN", "dB", ParamKind::Linear, 0.0, 18.0, 37, 3.5),
    p("knee", "KNEE", "", ParamKind::Choice(KNEES), 0.0, 5.0, 6, 2.0),
    p("rel", "RELEASE", "ms", ParamKind::Values(&RELEASES), 0.0, 159.0, 160, nearest(&RELEASES, 70.0)),
];

/// The compander's (MAN p.66): OUT GAIN -18..0 dB, WIDTH (how far under the
/// threshold the expansion starts) 1-90 dB. Hard on preset 4 A.Dr.BD's
/// values (-11 dB, 3.5:1, 1 ms, -1.5 dB, 7 dB, 192 ms), soft on preset 8
/// A.Dr.SN's (-8 dB, 1.7:1, 11 ms, 0 dB, 10 dB, 128 ms: MAN pp.76-77).
static CPH_PARAMS: [ParamSpec; 6] = [
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -54.0, 0.0, 55, -11.0),
    p("ratio", "RATIO", "", ParamKind::Values(&COMPANDER_RATIOS), 0.0, 14.0, 15, nearest(&COMPANDER_RATIOS, 3.5)),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 1.0),
    p("gain", "OUT GAIN", "dB", ParamKind::Linear, -18.0, 0.0, 37, -1.5),
    p("width", "WIDTH", "dB", ParamKind::Linear, 1.0, 90.0, 90, 7.0),
    p("rel", "RELEASE", "ms", ParamKind::Values(&RELEASES), 0.0, 159.0, 160, nearest(&RELEASES, 192.0)),
];

static CPS_PARAMS: [ParamSpec; 6] = [
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -54.0, 0.0, 55, -8.0),
    p("ratio", "RATIO", "", ParamKind::Values(&COMPANDER_RATIOS), 0.0, 14.0, 15, nearest(&COMPANDER_RATIOS, 1.7)),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 11.0),
    p("gain", "OUT GAIN", "dB", ParamKind::Linear, -18.0, 0.0, 37, 0.0),
    p("width", "WIDTH", "dB", ParamKind::Linear, 1.0, 90.0, 90, 10.0),
    p("rel", "RELEASE", "ms", ParamKind::Values(&RELEASES), 0.0, 159.0, 160, nearest(&RELEASES, 128.0)),
];

/// The gate's (MAN p.67), on preset 3 A.Dr.BD's values (-11 dB, -53 dB, 0 ms,
/// 1.93 ms, 400 ms: MAN p.76).
static GATE_PARAMS: [ParamSpec; 5] = [
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -54.0, 0.0, 55, -11.0),
    p("range", "RANGE", "dB", ParamKind::Linear, -70.0, 0.0, 71, -53.0),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 0.0),
    p("hld", "HOLD", "ms", ParamKind::Values(&HOLDS), 0.0, 215.0, 216, nearest(&HOLDS, 1.93)),
    p("dcy", "DECAY", "ms", ParamKind::Values(&RELEASES), 0.0, 159.0, 160, nearest(&RELEASES, 400.0)),
];

/// Ducking's, on preset 35 BGM Ducking's (-19 dB, -22 dB, 93 ms, 1.20 s, 6.32 s).
static DUCK_PARAMS: [ParamSpec; 5] = [
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -54.0, 0.0, 55, -19.0),
    p("range", "RANGE", "dB", ParamKind::Linear, -70.0, 0.0, 71, -22.0),
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, 93.0),
    p("hld", "HOLD", "ms", ParamKind::Values(&HOLDS), 0.0, 215.0, 216, nearest(&HOLDS, 1200.0)),
    p("dcy", "DECAY", "ms", ParamKind::Values(&RELEASES), 0.0, 159.0, 160, nearest(&RELEASES, 6320.0)),
];

/// The dynamics processor's six types (MAN pp.63-67), as its display tags them.
/// A strip's dynamics block holds any of them; its gate slot isn't used.
pub static DYN_MODELS: &[ModelSpec] = &[
    ModelSpec { token: "Y2CMP", name: "Compressor", category: ModelCategory::Compressor, params: &CMP_PARAMS },
    ModelSpec { token: "Y2EXP", name: "Expander", category: ModelCategory::Compressor, params: &EXP_PARAMS },
    ModelSpec { token: "Y2CPH", name: "Compander (H)", category: ModelCategory::Compressor, params: &CPH_PARAMS },
    ModelSpec { token: "Y2CPS", name: "Compander (S)", category: ModelCategory::Compressor, params: &CPS_PARAMS },
    ModelSpec { token: "Y2GAT", name: "Gate", category: ModelCategory::Gate, params: &GATE_PARAMS },
    ModelSpec { token: "Y2DUK", name: "Ducking", category: ModelCategory::Gate, params: &DUCK_PARAMS },
];

/// The 02R's models, as its profile names them; its two FX slots take its 17
/// effect types (`fx`).
pub static MODELS: ModelTables = ModelTables {
    dynamics: DYN_MODELS,
    aux_dynamics: &[],
    eq_channel: EQ,
    eq_bus: EQ,
    filter_tools: FILTER_TOOLS,
    fx: &fx::FX_MODELS,
    fx_premium_slots: 2,
    gate_default: "Y2GAT",
    comp_default: "Y2CMP",
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

    fn index(table: &[f32], v: f32) -> f32 {
        nearest(table, v)
    }

    #[test]
    fn the_02rs_tables() {
        // 1/12 octave steps, 21 Hz to 20.1 kHz; the presets' frequencies on them.
        assert!((FREQS[0] - 20.9).abs() < 0.1 && (FREQS[119] - 20159.0).abs() < 2.0);
        for (k, &f) in FREQS.iter().enumerate() {
            let exact = 1000.0 * 2f32.powf((k as f32 - 67.0) / 12.0);
            assert!((f / exact - 1.0).abs() < 0.001, "{f} Hz");
        }
        for f in [99.0, 265.0, 1059.0, 5339.0, 12699.0, 397.0, 4490.0, 35.08] {
            let n = index(&FREQS, f) as usize;
            assert!((FREQS[n] / f - 1.0).abs() < 0.005, "{f} Hz -> {}", FREQS[n]);
        }
        assert_eq!((Q_VALUES.len(), MID_Q.len(), LOW_Q.len(), HIGH_Q.len()), (41, 41, 43, 43));
        assert!(Q_VALUES.windows(2).all(|w| w[1] < w[0]));
        assert_eq!((RELEASES.len(), HOLDS.len()), (160, 216));
        assert_eq!((RELEASES[0], RELEASES[159]), (5.0, 42300.0));
        assert_eq!((HOLDS[0], HOLDS[215]), (0.02, 1960.0));
        let eq = &EQ[0];
        let v = values(eq, &[]);
        let p = Params::new(eq, &v[..eq.params.len()]);
        assert_eq!((p.f("lf"), p.text("lq"), p.f("lmf"), p.text("lmq"), p.f("hmf")), (125.0, "L.SHELF", 1000.0, "0.7", 4000.0));
        assert_eq!(p.text("hq"), "H.SHELF");
        assert!(eq.params.len() <= crate::wing::MAX_EQ_PARAMS);
        assert!(DYN_MODELS.iter().all(|m| m.params.len() <= crate::wing::MAX_DYN_PARAMS));
        // Scene 0's compressor is flat.
        let v = values(&DYN_MODELS[0], &[]);
        let c = Params::new(&DYN_MODELS[0], &v[..6]);
        assert_eq!((c.f("thr"), c.f("ratio"), c.f("rel"), c.text("knee")), (0.0, 1.0, 5.0, "HARD"));
    }

    #[test]
    fn the_02rs_eq_bands() {
        use crate::wing::eq::design;
        let eq = &EQ[0];
        let at = |set: &[(&str, f32)], f: f64| {
            let v = values(eq, set);
            design(eq, &v[..eq.params.len()], 48000.0, 1.0).response_db(f, 48000.0)
        };
        assert!(at(&[], 1000.0).abs() < 0.01, "flat at 0 dB");
        assert!((at(&[("lmg", 9.0), ("lmf", index(&FREQS, 500.0))], 500.0) - 9.0).abs() < 0.1, "a bell");
        assert!((at(&[("hmg", -12.5)], 4000.0) + 12.5).abs() < 0.1, "a cut, half a dB steps");
        // A narrow bell (Q 10) is down a lot an octave away; a wide one (Q 0.1) isn't.
        let narrow = at(&[("lmg", 12.0), ("lmq", 0.0)], 2000.0);
        let wide = at(&[("lmg", 12.0), ("lmq", 40.0)], 2000.0);
        assert!(narrow < 1.0 && wide > 6.0, "{narrow} {wide}");
        assert!((at(&[("lg", 6.0)], 30.0) - 6.0).abs() < 0.5, "the low shelf");
        assert!((at(&[("hg", -6.0)], 19000.0) + 6.0).abs() < 0.5, "the high shelf");
        // HPF and LPF: 12 dB/oct, the gain knob their switch.
        let hpf = at(&[("lq", FILTER), ("lf", index(&FREQS, 200.0))], 50.0);
        assert!(hpf < -20.0, "two octaves under a 12 dB/oct HPF: {hpf}");
        assert!(at(&[("lq", FILTER), ("hpf", 0.0)], 50.0).abs() < 0.01, "HPF off");
        assert!(at(&[("hq", FILTER), ("hf", index(&FREQS, 2000.0))], 8000.0) < -20.0, "the LPF");
    }

    #[test]
    fn the_02rs_dynamics_process() {
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
        let ratio = |r: f32| index(&RATIOS, r);
        // Scene 0's compressor (1:1) does nothing.
        assert!((run("Y2CMP", &[], 0.5) - 1.0).abs() < 0.01);
        // 10:1 from -30 dB turns a loud signal down; OUT GAIN makes up.
        let c = run("Y2CMP", &[("thr", -30.0), ("ratio", ratio(10.0))], 0.8);
        assert!(c < 0.2, "comp {c}");
        let made_up = run("Y2CMP", &[("thr", -30.0), ("ratio", ratio(10.0)), ("gain", 12.0)], 0.8);
        assert!((made_up / c - 3.98).abs() < 0.3, "12 dB of out gain: {}", made_up / c);
        // The expander turns a quiet signal further down, a loud one not.
        assert!(run("Y2EXP", &[("thr", -20.0), ("ratio", ratio(4.0)), ("gain", 0.0)], 0.01) < 0.05);
        assert!((run("Y2EXP", &[("thr", -40.0), ("gain", 0.0)], 0.5) - 1.0).abs() < 0.02);
        // The compander compresses over its threshold and expands well under it.
        let loud = run("Y2CPH", &[("thr", -30.0), ("ratio", index(&COMPANDER_RATIOS, 10.0)), ("gain", 0.0)], 0.8);
        assert!(loud < 0.2, "compressed: {loud}");
        let quiet = run("Y2CPH", &[("thr", -10.0), ("width", 10.0), ("gain", 0.0)], 0.003);
        assert!(quiet < 0.1, "expanded 5:1 under -20 dB: {quiet}");
        let soft = run("Y2CPS", &[("thr", -10.0), ("width", 10.0), ("gain", 0.0)], 0.003);
        assert!(soft > quiet && soft < 0.9, "expanded 1.5:1: {soft}");
        // The gate closes by its range under its threshold, and is open above it.
        let fast = index(&RELEASES, 5.0);
        assert!(run("Y2GAT", &[("thr", -20.0), ("range", -40.0), ("dcy", fast)], 0.01) < 0.02);
        assert!(run("Y2GAT", &[("thr", -40.0)], 0.5) > 0.95);
        // Ducking turns the signal down by its range while its key is over the threshold.
        let ducked = run("Y2DUK", &[("thr", -30.0), ("range", -20.0), ("att", 0.0)], 0.5);
        assert!((ducked - 0.1).abs() < 0.02, "ducked by 20 dB: {ducked}");
    }
}
