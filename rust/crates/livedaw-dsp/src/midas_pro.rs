//! The Midas PRO2's processing models (PRO2 Live Audio System Owner's Manual,
//! DOC02-DL2 Issue A, "OM", pages as printed; a few facts from the PRO X User
//! Manual, "PXM", where the PRO2's manual is silent and the family agrees).
//! Clean-room: built only from those public manuals' words, tables and block
//! diagrams, never from a console, its firmware or its software, and no code here
//! derives from any of them.
//!
//! The ranges, control names and the order of the blocks are the manual's. What
//! it describes only in words, and what is therefore Sidecar's own reading:
//!
//! - **The bells.** OM p.292 gives gain, width in octaves and the frequency
//!   ranges. Width to Q is the ordinary constant-Q relation; the curve is a
//!   cramping-free bell, so a treble band still means something near the top.
//! - **The shelves** (OM pp.292-294, words only: no equation, no plot). Soft and
//!   Warm, "a very gentle gradient", are a first-order shelf. Classic, "much
//!   steeper" with "some undershoot", is a second-order shelf with a Q that dips
//!   the far side about 1 dB ([`CLASSIC_Q`]). Bright and Deep, "slightly steeper"
//!   with no undershoot, are a 6th-order Butterworth shelf: monotonic and
//!   narrower in transition. None of this has been compared with a console.
//! - **The filter slopes** on a band are Butterworth.
//! - **The compressor's five envelopes** (OM pp.290-292, words only:
//!   `wing::dynamics`, "MPCMP"): every time constant beyond the set ATTACK and
//!   RELEASE, the RMS window, how far the soft knee slows an attack, Vintage's
//!   slow onset and its harmonics, Shimmer's damping.
//! - **dBu.** Thresholds are in dBu; Sidecar puts 0 dBFS at +21 dBu, the
//!   outputs' maximum and the meters' top (OM pp.313, 317; [`DBU_AT_FS`]).
//! - **The ratio's end.** OM p.250 calls the end of the knob infinity and p.315
//!   25:1; it is 25:1 here.
//! - **The GEQ's Q** (OM p.124: proportional or constant, no curve); its notches'
//!   width. **Every default** not on a panel drawing.
//! - **The soft clip** (MPCMPO only): PXM p.206 describes it after the make-up
//!   gain, adding "progressive gain reduction". OM p.13 lists a "soft clip
//!   limiter" on the outputs but shows no control; the switch and the level's
//!   range are Sidecar's, and the curve (+6 dB of ceiling over its threshold, the
//!   manual's 3 to 6 dB of headroom) is too.
//!
//! The side chain filter (IN, FREQ 50 Hz-15 kHz, WIDTH 1/3, 1 or 2 octaves, OM
//! p.255) is the dynamics slot's own key filter, on BP (Sidecar's reading of its
//! Q for a width: [`SC_WIDTH_Q`]).

use crate::biquad::BiquadCoeffs;
use crate::model::{ModelCategory, ModelSpec, ModelTables, ParamKind, ParamSpec, Params};
use crate::wing::eq::EqDesign;
use crate::wing::FILTER_TOOLS;
use std::f64::consts::FRAC_1_SQRT_2;

pub mod fx;

/// The dBu that is 0 dBFS (Sidecar's reading: the module's notes).
pub const DBU_AT_FS: f32 = 21.0;

/// A level in dBu, in dBFS.
pub fn dbfs(dbu: f32) -> f32 {
    dbu - DBU_AT_FS
}

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

const fn log(id: &'static str, label: &'static str, unit: &'static str, min: f32, max: f32, default: f32) -> ParamSpec {
    p(id, label, unit, ParamKind::Log, min, max, 201, default)
}

const fn choice(id: &'static str, label: &'static str, items: &'static [&'static str], default: f32) -> ParamSpec {
    p(id, label, "", ParamKind::Choice(items), 0.0, items.len() as f32 - 1.0, items.len() as u32, default)
}

// ---- EQ ------------------------------------------------------------------------------

/// The treble band's SHAPE (OM p.292: Bell is its parametric default).
pub const TREBLE_SHAPES: &[&str] = &["Bell", "Soft", "Classic", "Bright"];
/// The bass band's.
pub const BASS_SHAPES: &[&str] = &["Bell", "Warm", "Classic", "Deep"];
/// Band 1's on the outputs (OM p.293).
pub const BAND1_SHAPES: &[&str] = &["Bell", "Warm", "HP 6", "HP 12"];
/// Band 2's (OM p.294).
pub const BAND2_SHAPES: &[&str] = &["Bell", "HP 24"];
/// Band 6's.
pub const BAND6_SHAPES: &[&str] = &["Bell", "Soft", "LP 6", "LP 12"];

const fn gain(id: &'static str, label: &'static str) -> ParamSpec {
    p(id, label, "dB", ParamKind::Linear, -16.0, 16.0, 321, 0.0)
}

const fn width(id: &'static str, label: &'static str) -> ParamSpec {
    log(id, label, "oct", 0.1, 3.0, 1.0)
}

/// The input EQ's parameters, in the manual's order (OM p.292: 16 Hz-400 Hz bass,
/// 80 Hz-2 kHz lo mid, 320 Hz-8 kHz hi mid, 1-25 kHz treble, +/-16 dB, width 0.1 to
/// 3 octaves, all parametric until switched; the starting frequencies are
/// Sidecar's).
pub const EQ4_PARAMS: [ParamSpec; 14] = [
    choice("ts", "TREBLE SHAPE", TREBLE_SHAPES, 0.0),
    log("tf", "TREBLE FREQ", "Hz", 1000.0, 25000.0, 8000.0),
    gain("tg", "TREBLE GAIN"),
    width("tw", "TREBLE WIDTH"),
    log("hf", "HI MID FREQ", "Hz", 320.0, 8000.0, 2000.0),
    gain("hg", "HI MID GAIN"),
    width("hw", "HI MID WIDTH"),
    log("lf", "LO MID FREQ", "Hz", 80.0, 2000.0, 400.0),
    gain("lg", "LO MID GAIN"),
    width("lw", "LO MID WIDTH"),
    choice("bs", "BASS SHAPE", BASS_SHAPES, 0.0),
    log("bf", "BASS FREQ", "Hz", 16.0, 400.0, 100.0),
    gain("bg", "BASS GAIN"),
    width("bw", "BASS WIDTH"),
];

/// The 4-band EQ of the input channels and aux returns.
pub const EQ4_MODEL: ModelSpec =
    ModelSpec { token: "MPEQ4", name: "4-Band PEQ", category: ModelCategory::Eq, params: &EQ4_PARAMS };

const fn f6(id: &'static str, label: &'static str, default: f32) -> ParamSpec {
    log(id, label, "Hz", 16.0, 25000.0, default)
}

/// The output EQ's parameters (OM p.293: six bands, 16 Hz-25 kHz; the starting
/// frequencies are Sidecar's).
pub const EQ6_PARAMS: [ParamSpec; 21] = [
    choice("1s", "BAND 1 SHAPE", BAND1_SHAPES, 0.0),
    f6("1f", "BAND 1 FREQ", 63.0),
    gain("1g", "BAND 1 GAIN"),
    width("1w", "BAND 1 WIDTH"),
    choice("2s", "BAND 2 SHAPE", BAND2_SHAPES, 0.0),
    f6("2f", "BAND 2 FREQ", 160.0),
    gain("2g", "BAND 2 GAIN"),
    width("2w", "BAND 2 WIDTH"),
    f6("3f", "BAND 3 FREQ", 500.0),
    gain("3g", "BAND 3 GAIN"),
    width("3w", "BAND 3 WIDTH"),
    f6("4f", "BAND 4 FREQ", 1250.0),
    gain("4g", "BAND 4 GAIN"),
    width("4w", "BAND 4 WIDTH"),
    f6("5f", "BAND 5 FREQ", 4000.0),
    gain("5g", "BAND 5 GAIN"),
    width("5w", "BAND 5 WIDTH"),
    choice("6s", "BAND 6 SHAPE", BAND6_SHAPES, 0.0),
    f6("6f", "BAND 6 FREQ", 10000.0),
    gain("6g", "BAND 6 GAIN"),
    width("6w", "BAND 6 WIDTH"),
];

/// The 6-band EQ of the auxes, matrices and masters.
pub const EQ6_MODEL: ModelSpec =
    ModelSpec { token: "MPEQ6", name: "6-Band PEQ", category: ModelCategory::Eq, params: &EQ6_PARAMS };

/// The GEQ's 31 bands, the ISO third-octave centres (OM p.124).
pub const GEQ_HZ: [f32; 31] = [
    20.0, 25.0, 31.5, 40.0, 50.0, 63.0, 80.0, 100.0, 125.0, 160.0, 200.0, 250.0, 315.0, 400.0, 500.0, 630.0, 800.0, 1000.0,
    1250.0, 1600.0, 2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0, 10000.0, 12500.0, 16000.0, 20000.0,
];

/// Q: proportional or constant (OM p.125).
pub const GEQ_Q_MODES: &[&str] = &["PROP", "CON"];
/// The high and low pass filters' slopes (OM p.124).
pub const GEQ_SLOPES: &[&str] = &["6", "12"];

macro_rules! geq_band {
    ($id:literal, $label:literal) => {
        p($id, $label, "dB", ParamKind::Linear, -12.0, 12.0, 241, 0.0)
    };
}

/// The GEQ's parameters: its Q, the high and low pass (20-500 Hz, 2-20 kHz, 6 or 12
/// dB/oct), the two notches (20 Hz-2 kHz, 200 Hz-20 kHz), the 31 bands (OM p.124;
/// +/-12 dB: PXM p.221).
pub const GEQ_PARAMS: [ParamSpec; 42] = [
    choice("q", "Q", GEQ_Q_MODES, 0.0),
    p("hon", "HPF IN", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    log("hf", "HPF", "Hz", 20.0, 500.0, 20.0),
    choice("hs", "HPF SLOPE", GEQ_SLOPES, 1.0),
    p("lon", "LPF IN", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    log("lf", "LPF", "Hz", 2000.0, 20000.0, 20000.0),
    choice("ls", "LPF SLOPE", GEQ_SLOPES, 1.0),
    p("n1on", "NOTCH 1 IN", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    log("n1f", "NOTCH 1", "Hz", 20.0, 2000.0, 200.0),
    p("n2on", "NOTCH 2 IN", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    log("n2f", "NOTCH 2", "Hz", 200.0, 20000.0, 2000.0),
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

/// A GEQ: replaces the outputs' PEQ (OM p.13).
pub const GEQ_MODEL: ModelSpec =
    ModelSpec { token: "MPGEQ", name: "31-Band GEQ", category: ModelCategory::Eq, params: &GEQ_PARAMS };

/// The input channels' and aux returns' EQ.
pub static EQ_CHANNEL: [ModelSpec; 1] = [EQ4_MODEL];
/// The outputs': the PEQ, or the GEQ in its place.
pub static EQ_BUS: [ModelSpec; 2] = [EQ6_MODEL, GEQ_MODEL];

// ---- dynamics ------------------------------------------------------------------------

/// KNEE (OM p.291).
pub const KNEES: &[&str] = &["Hard", "Medium", "Soft"];
pub const KNEES_DB: [f32; 3] = [4.0, 12.0, 40.0];
/// MODE on the inputs (OM p.250) ...
pub const INPUT_MODES: &[&str] = &["Corrective", "Adaptive", "Creative", "Vintage"];
/// ... and on the outputs (p.273): Shimmer is theirs alone.
pub const OUTPUT_MODES: &[&str] = &["Corrective", "Adaptive", "Creative", "Vintage", "Shimmer"];

/// The side chain filter's WIDTH (OM p.315), as a band pass's Q.
pub const SC_WIDTH_Q: [f32; 3] = [4.318, 1.414, 0.667];

const THR: ParamSpec = p("thr", "THRESHOLD", "dBu", ParamKind::Linear, -50.0, 25.0, 751, 0.0);
const RATIO: ParamSpec = log("ratio", "RATIO", "", 1.0, 25.0, 3.0);
const ATT: ParamSpec = log("att", "ATTACK", "ms", 0.2, 20.0, 5.0);
const REL: ParamSpec = log("rel", "RELEASE", "ms", 50.0, 3000.0, 300.0);
const KNEE: ParamSpec = choice("knee", "KNEE", KNEES, 1.0);
const GAIN: ParamSpec = p("gain", "GAIN", "dB", ParamKind::Linear, 0.0, 24.0, 241, 0.0);

static CMP_PARAMS: [ParamSpec; 7] = [choice("mode", "MODE", INPUT_MODES, 0.0), THR, RATIO, ATT, REL, KNEE, GAIN];
static CMPO_PARAMS: [ParamSpec; 9] = [
    choice("mode", "MODE", OUTPUT_MODES, 0.0),
    THR,
    RATIO,
    ATT,
    REL,
    KNEE,
    GAIN,
    p("clip", "SOFT CLIP", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    p("clipl", "CLIP LEVEL", "dBu", ParamKind::Linear, 0.0, 21.0, 211, 18.0),
];

/// The gate (OM pp.254, 315: threshold -50 to +25 dBu, attack 20 us-20 ms, hold 5 ms-2
/// s, range -oo (100 dB) to 0; RELEASE 2 ms-2 s as p.315 has it, p.254 says 5 ms).
static GATE_PARAMS: [ParamSpec; 5] = [
    p("thr", "THRESHOLD", "dBu", ParamKind::Linear, -50.0, 25.0, 751, -30.0),
    log("att", "ATTACK", "ms", 0.02, 20.0, 1.0),
    log("hld", "HOLD", "ms", 5.0, 2000.0, 50.0),
    log("rel", "RELEASE", "ms", 2.0, 2000.0, 200.0),
    p("range", "RANGE", "dB", ParamKind::Linear, -100.0, 0.0, 101, -60.0),
];

pub static DYN_MODELS: &[ModelSpec] = &[
    ModelSpec { token: "MPCMP", name: "Compressor", category: ModelCategory::Compressor, params: &CMP_PARAMS },
    ModelSpec { token: "MPCMPO", name: "Output Compressor", category: ModelCategory::Compressor, params: &CMPO_PARAMS },
    ModelSpec { token: "MPGAT", name: "Gate", category: ModelCategory::Gate, params: &GATE_PARAMS },
];

/// Whether dynamics model `token` belongs in a gate slot.
pub fn is_gate(token: &str) -> bool {
    token == "MPGAT"
}

/// Whether compressor `token` is one the outputs hold (the other is the inputs').
pub fn is_output_compressor(token: &str) -> bool {
    token == "MPCMPO"
}

/// The PRO2's models.
pub static MODELS: ModelTables = ModelTables {
    dynamics: DYN_MODELS,
    aux_dynamics: &[],
    eq_channel: &EQ_CHANNEL,
    eq_bus: &EQ_BUS,
    filter_tools: FILTER_TOOLS,
    fx: &fx::FX_MODELS,
    fx_premium_first: 0,
    fx_premium_slots: 0,
    gate_default: "MPGAT",
    comp_default: "MPCMP",
};

// ---- the EQ's curves ---------------------------------------------------------------

/// Classic's Q: the far side of the shelf dips about 1 dB on a 12 dB boost (Sidecar's
/// reading of "some undershoot", OM p.293).
pub const CLASSIC_Q: f64 = 1.1;

/// Q of the two sections of a 4th-order Butterworth filter (band 2's high pass).
const BW4_Q: [f64; 2] = [0.5412, 1.3066];
/// Q of the three sections of a 6th-order Butterworth shelf (Bright, Deep).
const BW6_Q: [f64; 3] = [0.5176, FRAC_1_SQRT_2, 1.9319];

/// A bandwidth in octaves as a Q.
fn q_of(octaves: f32) -> f64 {
    let n = 2f64.powf(octaves as f64);
    n.sqrt() / (n - 1.0)
}

fn bell(sr: f64, f: f32, octaves: f32, g: f64) -> BiquadCoeffs {
    BiquadCoeffs::peak_decramped(sr, f as f64, q_of(octaves), g)
}

#[derive(Clone, Copy, PartialEq)]
enum Shelf {
    Gentle,
    Classic,
    Steep,
}

/// A shelf, its frequency kept under Nyquist on a 48 kHz engine (the console runs
/// at 96 kHz, so 25 kHz is real on it).
fn push_shelf(d: &mut EqDesign, sr: f64, high: bool, kind: Shelf, f: f32, g: f64) {
    let hz = (f as f64).min(0.45 * sr);
    let at = |q: f64, g: f64| {
        if high {
            BiquadCoeffs::high_shelf(sr, hz, q, g)
        } else {
            BiquadCoeffs::low_shelf(sr, hz, q, g)
        }
    };
    match kind {
        Shelf::Gentle => d.push(if high { BiquadCoeffs::high_shelf_1(sr, hz, g) } else { BiquadCoeffs::low_shelf_1(sr, hz, g) }),
        Shelf::Classic => d.push(at(CLASSIC_Q, g)),
        Shelf::Steep => {
            for q in BW6_Q {
                d.push(at(q, g / 3.0));
            }
        }
    }
}

/// One band of the input EQ: its bell, or (SHAPE not 0) its shelf.
fn push_outer(d: &mut EqDesign, sr: f64, p: &Params, high: bool, ids: [&str; 4], k: f32) {
    let g = (p.f(ids[2]) * k) as f64;
    if g.abs() <= 1e-3 {
        return;
    }
    let f = p.f(ids[1]);
    match p.choice(ids[0]) {
        0 => d.push(bell(sr, f, p.f(ids[3]), g)),
        1 => push_shelf(d, sr, high, Shelf::Gentle, f, g),
        2 => push_shelf(d, sr, high, Shelf::Classic, f, g),
        _ => push_shelf(d, sr, high, Shelf::Steep, f, g),
    }
}

/// A parametric band's three controls.
fn push_bell(d: &mut EqDesign, sr: f64, p: &Params, ids: [&str; 3], k: f32) {
    let g = (p.f(ids[1]) * k) as f64;
    if g.abs() > 1e-3 {
        d.push(bell(sr, p.f(ids[0]), p.f(ids[2]), g));
    }
}

/// The 4-band input EQ (OM p.292).
pub(crate) fn design_eq4(p: &Params, sr: f64, k: f32, d: &mut EqDesign) {
    push_outer(d, sr, p, false, ["bs", "bf", "bg", "bw"], k);
    push_bell(d, sr, p, ["lf", "lg", "lw"], k);
    push_bell(d, sr, p, ["hf", "hg", "hw"], k);
    push_outer(d, sr, p, true, ["ts", "tf", "tg", "tw"], k);
}

/// The 6-band output EQ (OM pp.293-294): band 1 shelf or high pass, band 2 a 24
/// dB/oct high pass (which takes band 1 out), band 6 shelf or low pass.
pub(crate) fn design_eq6(p: &Params, sr: f64, k: f32, d: &mut EqDesign) {
    let hp24 = p.choice("2s") == 1;
    if !hp24 {
        match p.choice("1s") {
            0 => push_bell(d, sr, p, ["1f", "1g", "1w"], k),
            1 => {
                let g = (p.f("1g") * k) as f64;
                if g.abs() > 1e-3 {
                    push_shelf(d, sr, false, Shelf::Gentle, p.f("1f"), g);
                }
            }
            2 => d.push(BiquadCoeffs::high_pass_1(sr, p.f("1f") as f64)),
            _ => d.push(BiquadCoeffs::high_pass(sr, p.f("1f") as f64, FRAC_1_SQRT_2)),
        }
    }
    if hp24 {
        for q in BW4_Q {
            d.push(BiquadCoeffs::high_pass(sr, p.f("2f") as f64, q));
        }
    } else {
        push_bell(d, sr, p, ["2f", "2g", "2w"], k);
    }
    push_bell(d, sr, p, ["3f", "3g", "3w"], k);
    push_bell(d, sr, p, ["4f", "4g", "4w"], k);
    push_bell(d, sr, p, ["5f", "5g", "5w"], k);
    let top = (p.f("6f") as f64).min(0.45 * sr);
    match p.choice("6s") {
        0 => push_bell(d, sr, p, ["6f", "6g", "6w"], k),
        1 => {
            let g = (p.f("6g") * k) as f64;
            if g.abs() > 1e-3 {
                push_shelf(d, sr, true, Shelf::Gentle, p.f("6f"), g);
            }
        }
        2 => d.push(BiquadCoeffs::low_pass_1(sr, top)),
        _ => d.push(BiquadCoeffs::low_pass(sr, top, FRAC_1_SQRT_2)),
    }
}

/// A third of an octave, and how much narrower a proportional-Q band is at no gain
/// than at full (Sidecar's reading of "reduce the bandwidth" for small gains, OM
/// p.140; three times).
const GEQ_Q: f32 = 4.318;
const GEQ_PROP_NARROW: f32 = 3.0;
const GEQ_RANGE_DB: f32 = 12.0;

/// The notches' Q (Sidecar's reading).
const NOTCH_Q: f64 = 8.0;

/// A GEQ band's Q at `g` dB.
pub fn geq_q(proportional: bool, g: f32) -> f32 {
    if proportional {
        GEQ_Q * (1.0 + (GEQ_PROP_NARROW - 1.0) * (1.0 - (g.abs() / GEQ_RANGE_DB).min(1.0)))
    } else {
        GEQ_Q
    }
}

/// The 31-band GEQ (OM pp.124-125).
pub(crate) fn design_geq(p: &Params, sr: f64, k: f32, d: &mut EqDesign) {
    let prop = p.choice("q") == 0;
    if p.on("hon") {
        let f = p.f("hf") as f64;
        d.push(if p.choice("hs") == 0 {
            BiquadCoeffs::high_pass_1(sr, f)
        } else {
            BiquadCoeffs::high_pass(sr, f, FRAC_1_SQRT_2)
        });
    }
    if p.on("lon") {
        let f = (p.f("lf") as f64).min(0.45 * sr);
        d.push(if p.choice("ls") == 0 { BiquadCoeffs::low_pass_1(sr, f) } else { BiquadCoeffs::low_pass(sr, f, FRAC_1_SQRT_2) });
    }
    if p.on("n1on") {
        d.push(BiquadCoeffs::notch(sr, p.f("n1f") as f64, NOTCH_Q));
    }
    if p.on("n2on") {
        d.push(BiquadCoeffs::notch(sr, (p.f("n2f") as f64).min(0.45 * sr), NOTCH_Q));
    }
    const FIRST_BAND: usize = 11;
    for (i, &f) in GEQ_HZ.iter().enumerate() {
        let g = p.values.get(i + FIRST_BAND).copied().unwrap_or(0.0).clamp(-GEQ_RANGE_DB, GEQ_RANGE_DB) * k;
        if g.abs() > 1e-3 {
            d.push(BiquadCoeffs::peak(sr, f as f64, geq_q(prop, g) as f64, g as f64));
        }
    }
}
