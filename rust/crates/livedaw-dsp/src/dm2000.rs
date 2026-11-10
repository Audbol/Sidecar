//! The Yamaha DM2000 Version 2's processing models (V2 Owner's Manual, "OM",
//! pages as printed): the 4-band EQ with its TYPE I and TYPE II, the 31-band
//! GEQ, and the gate and compressor families. Clean-room: built from the
//! manual's description of the parameters, not from the console's firmware.
//!
//! The ranges and steps below are the manual's (pp.133-134, 345-346). What it
//! doesn't give, and what is therefore Sidecar's reading:
//!
//! - **The EQ's curves.** OM p.134 says only that TYPE I is the EQ type "used on
//!   legacy Yamaha digital" consoles and TYPE II "a newly developed algorithm";
//!   Yamaha's CL and QL manuals call the same switch an algorithm that "reduces
//!   interference between bands". TYPE I here is the 02R's curves (bilinear
//!   bells and shelves, a 12 dB/oct HPF and LPF). TYPE II is the same controls
//!   with each bell's own gain solved so that the finished response reaches the
//!   set gain at that band's frequency, the other bands' overlap taken out, on
//!   bells that keep their shape up to Nyquist.
//! - **The 120 frequencies.** The manual gives the ends (21.2 Hz, 20.0 kHz) and
//!   the step (1/12 octave), not the list. The list here is the rounded series
//!   its display and all 40 EQ presets use (pp.332-333: 21.2, 22.4, ... 1.06k,
//!   1.12k ...): twelve to the octave, it has exactly 120 values and every
//!   preset's frequency is on it.
//! - **Q.** 41 points from 10.0 to 0.10 (p.133): the presets use 33 of them, the
//!   same 33 as the 02R's; the other eight are the 02R's reading.
//! - **Hold, decay, release.** 216 and 160 points (p.345) with only their ends
//!   given: the 02R's reading, evenly spaced in log time at 48 kHz. The manual
//!   gives their ends at each sample rate and they follow it exactly (the time
//!   in samples is fixed): the dynamics scale these times by 48 kHz / the
//!   engine's rate. The display values are the 48 kHz ones.
//! - **KNEE, the compander's expansion** (the 02R's reading), the expander's
//!   range, the GEQ's bell width and its gain step (the display shows a tenth
//!   of a dB).
//!
//! Each dynamics model starts on its preset template (pp.333-334, printed at
//! 44.1 kHz, its times converted to the 48 kHz table).

use crate::biquad::BiquadCoeffs;
use crate::model::{ModelCategory, ModelSpec, ModelTables, ParamKind, ParamSpec, Params};
use crate::wing::eq::EqDesign;
use crate::wing::FILTER_TOOLS;
use crate::y02r::{nearest, COMPANDER_RATIOS, HOLDS, KNEES, Q_VALUES, RATIOS, RELEASES};
use std::f64::consts::FRAC_1_SQRT_2;

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

/// The EQ's 120 frequencies: 21.2 Hz to 20.0 kHz in 1/12 octave steps (p.345),
/// as the display writes them (Sidecar's reading of the list: see the module).
pub const FREQS: [f32; 120] = [
    21.2, 22.4, 23.6, 25.0, 26.5, 28.0, 30.0, 31.5, 33.5, 35.5, 37.5, 40.0, 42.5, 45.0, 47.5, 50.0, 53.0, 56.0, 60.0, 63.0, 67.0,
    71.0, 75.0, 80.0, 85.0, 90.0, 95.0, 100.0, 106.0, 112.0, 118.0, 125.0, 132.0, 140.0, 150.0, 160.0, 170.0, 180.0, 190.0,
    200.0, 212.0, 224.0, 236.0, 250.0, 265.0, 280.0, 300.0, 315.0, 335.0, 355.0, 375.0, 400.0, 425.0, 450.0, 475.0, 500.0, 530.0,
    560.0, 600.0, 630.0, 670.0, 710.0, 750.0, 800.0, 850.0, 900.0, 950.0, 1000.0, 1060.0, 1120.0, 1180.0, 1250.0, 1320.0, 1400.0,
    1500.0, 1600.0, 1700.0, 1800.0, 1900.0, 2000.0, 2120.0, 2240.0, 2360.0, 2500.0, 2650.0, 2800.0, 3000.0, 3150.0, 3350.0,
    3550.0, 3750.0, 4000.0, 4250.0, 4500.0, 4750.0, 5000.0, 5300.0, 5600.0, 6000.0, 6300.0, 6700.0, 7100.0, 7500.0, 8000.0,
    8500.0, 9000.0, 9500.0, 10000.0, 10600.0, 11200.0, 11800.0, 12500.0, 13200.0, 14000.0, 15000.0, 16000.0, 17000.0, 18000.0,
    19000.0, 20000.0,
];

/// L-MID's and H-MID's Q, as the display writes it (p.133): 10.0 to 0.10.
pub const MID_Q: &[&str] = &[
    "10.0", "9.00", "8.00", "7.00", "6.30", "5.60", "5.00", "4.50", "4.00", "3.50", "3.20", "2.80", "2.50", "2.20", "2.00",
    "1.80", "1.60", "1.40", "1.20", "1.10", "1.00", "0.90", "0.80", "0.70", "0.63", "0.56", "0.50", "0.45", "0.40", "0.35",
    "0.32", "0.28", "0.25", "0.22", "0.20", "0.18", "0.16", "0.14", "0.12", "0.11", "0.10",
];

/// LOW's Q in the manual's order (p.133): HPF, the 41 Qs, L.SHELF.
pub const LOW_Q: &[&str] = &[
    "HPF", "10.0", "9.00", "8.00", "7.00", "6.30", "5.60", "5.00", "4.50", "4.00", "3.50", "3.20", "2.80", "2.50", "2.20",
    "2.00", "1.80", "1.60", "1.40", "1.20", "1.10", "1.00", "0.90", "0.80", "0.70", "0.63", "0.56", "0.50", "0.45", "0.40",
    "0.35", "0.32", "0.28", "0.25", "0.22", "0.20", "0.18", "0.16", "0.14", "0.12", "0.11", "0.10", "L.SHELF",
];

/// HIGH's: LPF, the 41 Qs, H.SHELF.
pub const HIGH_Q: &[&str] = &[
    "LPF", "10.0", "9.00", "8.00", "7.00", "6.30", "5.60", "5.00", "4.50", "4.00", "3.50", "3.20", "2.80", "2.50", "2.20",
    "2.00", "1.80", "1.60", "1.40", "1.20", "1.10", "1.00", "0.90", "0.80", "0.70", "0.63", "0.56", "0.50", "0.45", "0.40",
    "0.35", "0.32", "0.28", "0.25", "0.22", "0.20", "0.18", "0.16", "0.14", "0.12", "0.11", "0.10", "H.SHELF",
];

/// LOW's and HIGH's filter and shelf positions (indexes into `LOW_Q` / `HIGH_Q`).
pub const FILTER: f32 = 0.0;
pub const SHELF: f32 = 42.0;

/// The EQ's TYPE (p.134).
pub const EQ_TYPES: &[&str] = &["TYPE I", "TYPE II"];

/// Q 0.70 in `MID_Q`.
const Q07: f32 = 23.0;

/// A band's frequency and gain (-18 to +18 dB in 0.1 dB steps: p.133).
const fn freq(id: &'static str, label: &'static str, hz: f32) -> ParamSpec {
    p(id, label, "Hz", ParamKind::Values(&FREQS), 0.0, 119.0, 120, nearest(&FREQS, hz))
}
const fn gain(id: &'static str, label: &'static str) -> ParamSpec {
    p(id, label, "dB", ParamKind::Linear, -18.0, 18.0, 361, 0.0)
}

/// The 4-band EQ's parameters in the EQ Edit page's order (p.134): TYPE, then Q,
/// F and G of each band, as the initial data has them (p.133: L.SHELF 125 Hz,
/// bells at 1.00 kHz and 4.00 kHz Q 0.70, H.SHELF 10.0 kHz, TYPE I, 0 dB), and
/// the HPF and LPF switches (LOW's and HIGH's gain knob with Q on HPF or LPF).
pub const EQ_PARAMS: [ParamSpec; 15] = [
    p("type", "TYPE", "", ParamKind::Choice(EQ_TYPES), 0.0, 1.0, 2, 0.0),
    p("lq", "LOW Q", "", ParamKind::Choice(LOW_Q), 0.0, 42.0, 43, SHELF),
    freq("lf", "LOW F", 125.0),
    gain("lg", "LOW G"),
    p("lmq", "L-MID Q", "", ParamKind::Choice(MID_Q), 0.0, 40.0, 41, Q07),
    freq("lmf", "L-MID F", 1000.0),
    gain("lmg", "L-MID G"),
    p("hmq", "H-MID Q", "", ParamKind::Choice(MID_Q), 0.0, 40.0, 41, Q07),
    freq("hmf", "H-MID F", 4000.0),
    gain("hmg", "H-MID G"),
    p("hq", "HIGH Q", "", ParamKind::Choice(HIGH_Q), 0.0, 42.0, 43, SHELF),
    freq("hf", "HIGH F", 10000.0),
    gain("hg", "HIGH G"),
    p("hpf", "HPF", "", ParamKind::Toggle, 0.0, 1.0, 2, 1.0),
    p("lpf", "LPF", "", ParamKind::Toggle, 0.0, 1.0, 2, 1.0),
];

/// The 4-band EQ of every Input and Output Channel.
pub const EQ_MODEL: ModelSpec = ModelSpec { token: "DMEQ", name: "4-Band PEQ", category: ModelCategory::Eq, params: &EQ_PARAMS };

/// The GEQ's 31 bands (p.184: 20.0 Hz to 20.0 kHz; the fader assignment's 24
/// bands end at 4.0 kHz and start at 100 Hz: the third-octave series).
pub const GEQ_HZ: [f32; 31] = [
    20.0, 25.0, 31.5, 40.0, 50.0, 63.0, 80.0, 100.0, 125.0, 160.0, 200.0, 250.0, 315.0, 400.0, 500.0, 630.0, 800.0, 1000.0,
    1250.0, 1600.0, 2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0, 10000.0, 12500.0, 16000.0, 20000.0,
];

/// LIMIT (p.183).
pub const GEQ_LIMITS: &[&str] = &["+/-15 dB", "+/-12 dB", "+/-6 dB", "-24 dB"];

/// A band's bell: a third of an octave wide (Sidecar's reading: the manual
/// shows the curve, p.183, but gives no width).
const GEQ_Q: f64 = 4.32;

macro_rules! geq_band {
    ($id:literal, $label:literal) => {
        p($id, $label, "dB", ParamKind::Linear, -24.0, 15.0, 391, 0.0)
    };
}

/// The GEQ's parameters: LIMIT, then the 31 bands, 0 dB (FLAT). The band's
/// range is LIMIT's widest; the design holds a band within LIMIT.
pub const GEQ_PARAMS: [ParamSpec; 32] = [
    p("lim", "LIMIT", "", ParamKind::Choice(GEQ_LIMITS), 0.0, 3.0, 4, 0.0),
    geq_band!("1", "20"),
    geq_band!("2", "25"),
    geq_band!("3", "31.5"),
    geq_band!("4", "40"),
    geq_band!("5", "50"),
    geq_band!("6", "63"),
    geq_band!("7", "80"),
    geq_band!("8", "100"),
    geq_band!("9", "125"),
    geq_band!("10", "160"),
    geq_band!("11", "200"),
    geq_band!("12", "250"),
    geq_band!("13", "315"),
    geq_band!("14", "400"),
    geq_band!("15", "500"),
    geq_band!("16", "630"),
    geq_band!("17", "800"),
    geq_band!("18", "1k"),
    geq_band!("19", "1.25k"),
    geq_band!("20", "1.6k"),
    geq_band!("21", "2k"),
    geq_band!("22", "2.5k"),
    geq_band!("23", "3.15k"),
    geq_band!("24", "4k"),
    geq_band!("25", "5k"),
    geq_band!("26", "6.3k"),
    geq_band!("27", "8k"),
    geq_band!("28", "10k"),
    geq_band!("29", "12.5k"),
    geq_band!("30", "16k"),
    geq_band!("31", "20k"),
];

/// A GEQ (six on a console: pp.18, 183). It goes on the Output Channels only.
pub const GEQ_MODEL: ModelSpec =
    ModelSpec { token: "DMGEQ", name: "31-Band GEQ", category: ModelCategory::Eq, params: &GEQ_PARAMS };

/// The Input Channels' EQ.
pub static EQ_CHANNEL: [ModelSpec; 1] = [EQ_MODEL];
/// The Output Channels' EQ, and their GEQ.
pub static EQ_BUS: [ModelSpec; 2] = [EQ_MODEL, GEQ_MODEL];

/// The time `ms`, printed at 44.1 kHz (the presets), at the tables' 48 kHz.
const fn at_48k(ms: f32) -> f32 {
    ms * 44.1 / 48.0
}

const fn thr(default: f32) -> ParamSpec {
    p("thr", "THRESHOLD", "dB", ParamKind::Linear, -54.0, 0.0, 541, default)
}
const fn att(default: f32) -> ParamSpec {
    p("att", "ATTACK", "ms", ParamKind::Linear, 0.0, 120.0, 121, default)
}
const fn release(default_ms_44k1: f32) -> ParamSpec {
    p("rel", "RELEASE", "ms", ParamKind::Values(&RELEASES), 0.0, 159.0, 160, nearest(&RELEASES, at_48k(default_ms_44k1)))
}

/// COMP's (p.345), on preset 1 "Comp" (-8 dB, 2.5:1, 60 ms, +0.0 dB, knee 2, 250 ms: p.334).
static CMP_PARAMS: [ParamSpec; 6] = [
    thr(-8.0),
    p("ratio", "RATIO", "", ParamKind::Values(&RATIOS), 0.0, 15.0, 16, nearest(&RATIOS, 2.5)),
    att(60.0),
    p("gain", "OUT GAIN", "dB", ParamKind::Linear, 0.0, 18.0, 181, 0.0),
    p("knee", "KNEE", "", ParamKind::Choice(KNEES), 0.0, 5.0, 6, 2.0),
    release(250.0),
];

/// EXPAND's (p.345), on preset 2 "Expand" (-23 dB, 1.7:1, 1 ms, +3.5 dB, knee 2, 70 ms).
static EXP_PARAMS: [ParamSpec; 6] = [
    thr(-23.0),
    p("ratio", "RATIO", "", ParamKind::Values(&RATIOS), 0.0, 15.0, 16, nearest(&RATIOS, 1.7)),
    att(1.0),
    p("gain", "OUT GAIN", "dB", ParamKind::Linear, 0.0, 18.0, 181, 3.5),
    p("knee", "KNEE", "", ParamKind::Choice(KNEES), 0.0, 5.0, 6, 2.0),
    release(70.0),
];

/// COMPAND-H's (p.346: 16 ratios with infinity, OUT GAIN -18 to 0 dB, WIDTH
/// 1-90 dB), on preset 3 "Compander (H)" (-10 dB, 3.5:1, 1 ms, 0 dB, 6 dB, 250 ms).
static CPH_PARAMS: [ParamSpec; 6] = [
    thr(-10.0),
    p("ratio", "RATIO", "", ParamKind::Values(&RATIOS), 0.0, 15.0, 16, nearest(&RATIOS, 3.5)),
    att(1.0),
    p("gain", "OUT GAIN", "dB", ParamKind::Linear, -18.0, 0.0, 181, 0.0),
    p("width", "WIDTH", "dB", ParamKind::Linear, 1.0, 90.0, 90, 6.0),
    release(250.0),
];

/// COMPAND-S's (p.346: 15 ratios, no infinity), on preset 4 "Compander (S)"
/// (-8 dB, 4:1, 25 ms, 0 dB, 24 dB, 180 ms).
static CPS_PARAMS: [ParamSpec; 6] = [
    thr(-8.0),
    p("ratio", "RATIO", "", ParamKind::Values(&COMPANDER_RATIOS), 0.0, 14.0, 15, nearest(&COMPANDER_RATIOS, 4.0)),
    att(25.0),
    p("gain", "OUT GAIN", "dB", ParamKind::Linear, -18.0, 0.0, 181, 0.0),
    p("width", "WIDTH", "dB", ParamKind::Linear, 1.0, 90.0, 90, 24.0),
    release(180.0),
];

/// GATE's (p.345), on preset 1 "Gate" (-26 dB, -56 dB, 0 ms, 2.56 ms, 331 ms: p.333).
static GATE_PARAMS: [ParamSpec; 5] = [
    thr(-26.0),
    p("range", "RANGE", "dB", ParamKind::Linear, -70.0, 0.0, 71, -56.0),
    att(0.0),
    p("hld", "HOLD", "ms", ParamKind::Values(&HOLDS), 0.0, 215.0, 216, nearest(&HOLDS, at_48k(2.56))),
    p("dcy", "DECAY", "ms", ParamKind::Values(&RELEASES), 0.0, 159.0, 160, nearest(&RELEASES, at_48k(331.0))),
];

/// DUCKING's, on preset 2 "Ducking" (-19 dB, -22 dB, 93 ms, 1.20 s, 6.32 s).
static DUCK_PARAMS: [ParamSpec; 5] = [
    thr(-19.0),
    p("range", "RANGE", "dB", ParamKind::Linear, -70.0, 0.0, 71, -22.0),
    att(93.0),
    p("hld", "HOLD", "ms", ParamKind::Values(&HOLDS), 0.0, 215.0, 216, nearest(&HOLDS, at_48k(1200.0))),
    p("dcy", "DECAY", "ms", ParamKind::Values(&RELEASES), 0.0, 159.0, 160, nearest(&RELEASES, at_48k(6320.0))),
];

/// The gate family (the Input Channels' gate slot) and the compressor family
/// (every channel's compressor), as the manual's TYPE names them (pp.85, 137).
pub static DYN_MODELS: &[ModelSpec] = &[
    ModelSpec { token: "DMCMP", name: "Comp", category: ModelCategory::Compressor, params: &CMP_PARAMS },
    ModelSpec { token: "DMEXP", name: "Expand", category: ModelCategory::Compressor, params: &EXP_PARAMS },
    ModelSpec { token: "DMCPH", name: "Compand-H", category: ModelCategory::Compressor, params: &CPH_PARAMS },
    ModelSpec { token: "DMCPS", name: "Compand-S", category: ModelCategory::Compressor, params: &CPS_PARAMS },
    ModelSpec { token: "DMGAT", name: "Gate", category: ModelCategory::Gate, params: &GATE_PARAMS },
    ModelSpec { token: "DMDUK", name: "Ducking", category: ModelCategory::Gate, params: &DUCK_PARAMS },
];

/// Whether dynamics model `token` belongs in a gate slot (GATE, DUCKING); the
/// others are the compressor's types.
pub fn is_gate(token: &str) -> bool {
    matches!(token, "DMGAT" | "DMDUK")
}

/// The DM2000's models. Effect types run on any of the eight processors (the
/// premium slots, kept for Effects 1-2's 8-in 8-out surround types, are empty
/// until those exist).
pub static MODELS: ModelTables = ModelTables {
    dynamics: DYN_MODELS,
    aux_dynamics: &[],
    eq_channel: &EQ_CHANNEL,
    eq_bus: &EQ_BUS,
    filter_tools: FILTER_TOOLS,
    fx: &fx::FX_MODELS,
    fx_premium_first: 0,
    fx_premium_slots: 2,
    gate_default: "DMGAT",
    comp_default: "DMCMP",
};

fn bell(sr: f64, f: f32, q: f32, g: f64, type2: bool) -> BiquadCoeffs {
    if type2 {
        BiquadCoeffs::peak_decramped(sr, f as f64, q as f64, g)
    } else {
        BiquadCoeffs::peak(sr, f as f64, q as f64, g)
    }
}

/// What a band does, without its gain: a bell of this Q, a shelf or a filter.
#[derive(Clone, Copy)]
enum Band {
    Off,
    Bell(f32, f32),
    LowShelf(f32),
    HighShelf(f32),
    Cut(BiquadCoeffs),
}

fn outer_band(p: &Params, q_id: &str, freq_id: &str, sw_id: &str, sr: f64, low: bool) -> Band {
    let f = p.f(freq_id);
    match p.choice(q_id) as f32 {
        x if x == FILTER => {
            if p.on(sw_id) {
                let hz = f as f64;
                Band::Cut(if low {
                    BiquadCoeffs::high_pass(sr, hz, FRAC_1_SQRT_2)
                } else {
                    BiquadCoeffs::low_pass(sr, hz, FRAC_1_SQRT_2)
                })
            } else {
                Band::Off
            }
        }
        x if x == SHELF => {
            if low {
                Band::LowShelf(f)
            } else {
                Band::HighShelf(f)
            }
        }
        x => Band::Bell(f, Q_VALUES[(x as usize).clamp(1, 41) - 1]),
    }
}

fn mid_band(p: &Params, q_id: &str, freq_id: &str) -> Band {
    Band::Bell(p.f(freq_id), Q_VALUES[p.choice(q_id).min(40)])
}

impl Band {
    fn coeffs(self, sr: f64, g: f64, type2: bool) -> Option<BiquadCoeffs> {
        let live = g.abs() > 1e-3;
        match self {
            Band::Off => None,
            Band::Bell(f, q) => live.then(|| bell(sr, f, q, g, type2)),
            Band::LowShelf(f) => live.then(|| BiquadCoeffs::low_shelf(sr, f as f64, FRAC_1_SQRT_2, g)),
            Band::HighShelf(f) => live.then(|| BiquadCoeffs::high_shelf(sr, f as f64, FRAC_1_SQRT_2, g)),
            Band::Cut(c) => Some(c),
        }
    }

    /// The frequency a bell's gain is held at (the shelves and filters keep
    /// their gain as set).
    fn bell_at(self) -> Option<f64> {
        match self {
            Band::Bell(f, _) => Some(f as f64),
            _ => None,
        }
    }
}

/// The DM2000's 4-band EQ, TYPE I or TYPE II (the module's notes).
pub(crate) fn design_eq(p: &Params, sr: f64, k: f32, d: &mut EqDesign) {
    let type2 = p.choice("type") == 1;
    let bands = [
        outer_band(p, "lq", "lf", "hpf", sr, true),
        mid_band(p, "lmq", "lmf"),
        mid_band(p, "hmq", "hmf"),
        outer_band(p, "hq", "hf", "lpf", sr, false),
    ];
    let k = k as f64;
    let set = [p.f("lg") as f64 * k, p.f("lmg") as f64 * k, p.f("hmg") as f64 * k, p.f("hg") as f64 * k];
    let mut gains = set;
    if type2 {
        solve_band_gains(&bands, &set, &mut gains, sr);
    }
    for (band, g) in bands.iter().zip(gains) {
        if let Some(c) = band.coeffs(sr, g, type2) {
            d.push(c);
        }
    }
}

/// The gains that give each bell its set level at its frequency, the other
/// bands' overlap taken out: a few rounds of relaxation. Bands at 0 dB and the
/// shelves and filters stay as they are.
fn solve_band_gains(bands: &[Band; 4], set: &[f64; 4], gains: &mut [f64; 4], sr: f64) {
    for _ in 0..4 {
        let mut sections = [None; 4];
        for (i, band) in bands.iter().enumerate() {
            sections[i] = band.coeffs(sr, gains[i], true);
        }
        for i in 0..4 {
            let (Some(at), true) = (bands[i].bell_at(), set[i].abs() > 1e-3) else { continue };
            let total: f64 = sections.iter().flatten().map(|c| 20.0 * c.magnitude_at(at, sr).max(1e-9).log10()).sum();
            gains[i] = (gains[i] + 0.8 * (set[i] - total)).clamp(-24.0, 24.0);
            sections[i] = bands[i].coeffs(sr, gains[i], true);
        }
    }
}

/// The DM2000's 31-band GEQ: 1/3 octave bells; a band at 0 dB costs nothing,
/// and LIMIT holds each band within its range.
pub(crate) fn design_geq(p: &Params, sr: f64, k: f32, d: &mut EqDesign) {
    let (lo, hi): (f32, f32) = match p.choice("lim") {
        1 => (-12.0, 12.0),
        2 => (-6.0, 6.0),
        3 => (-24.0, 0.0),
        _ => (-15.0, 15.0),
    };
    for (i, &f) in GEQ_HZ.iter().enumerate() {
        let g = p.values.get(i + 1).copied().unwrap_or(0.0).clamp(lo, hi) * k;
        if g.abs() > 1e-3 {
            d.push(bell(sr, f, GEQ_Q as f32, g as f64, false));
        }
    }
}
