//! The Allen & Heath dLive's processing blocks (`docs/UNIVERSAL_MIXER.md`): its
//! gate, ducker and expanders, its DEEP compressor models, its 4-band PEQ, its
//! four 28-band graphic EQs and its 12-band NEQ-12, from the dLive Firmware
//! Reference Guide V2.12 (the online chapters Processing Screen, DEEP Processing
//! and Processing specs, and their pictures of the models' panels). Clean-room:
//! built from the guide's description of the parameters and the controls its
//! pictures show, not from the console's firmware or Director software.
//!
//! They run on the EQ and dynamics DSP of [`crate::wing`] (`design` and
//! `configure` pick them by token), on any strip of a Universal console. The
//! guide states levels in dBu, with +18 dBu = 0 dBFS (the datasheets). Where it
//! is silent the choices are Sidecar's reading: the manual compressors' attack
//! and release ranges (the SQ's, its sister console), the ducker's and
//! expanders' threshold ranges (the gate's), the ballistics behind each model's
//! name, how far each GEQ type's bands widen, the NEQ-12's width range and its
//! other details listed at [`NEQ_PARAMS`], and every default value not shown in
//! a picture.
//!
//! The preamp models' controls come from the guide's panels; their ranges and
//! curves aren't published and are all Sidecar's reading (see `configure`'s
//! `DLVVALVE` and `DLVTUBE` in [`crate::wing::dynamics`]).
//!
//! The RackExtra FX are in [`crate::dlive_fx`]. Still to come: Dyn8,
//! the RackUltra FX and the console.

use crate::model::{ModelCategory, ModelSpec, ModelTables, ParamKind, ParamSpec};
use crate::sq::{self, PARALLEL_OFF_DB};

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

const fn model(token: &'static str, name: &'static str, category: ModelCategory, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name, category, params }
}

const LIN: ParamKind = ParamKind::Linear;
const LOG: ParamKind = ParamKind::Log;

/// Gate (Processing specs): THRESHOLD -72..+12 dBu, DEPTH 0..60 dB, ATTACK 50 us
/// to 300 ms, HOLD 10 ms to 5 s, RELEASE 10 ms to 1 s.
const GATE_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", LIN, -72.0, 12.0, 169, -30.0),
    p("range", "DEPTH", "dB", LIN, 0.0, 60.0, 61, 60.0),
    p("att", "ATTACK", "ms", LOG, 0.05, 300.0, 101, 1.0),
    p("hld", "HOLD", "ms", LOG, 10.0, 5000.0, 101, 50.0),
    p("rel", "RELEASE", "ms", LOG, 10.0, 1000.0, 101, 100.0),
];

/// The ducker: the gate's controls, starting as the guide's picture of it does.
const DUCK_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", LIN, -72.0, 12.0, 169, -7.0),
    p("range", "DEPTH", "dB", LIN, 0.0, 60.0, 61, 20.0),
    p("att", "ATTACK", "ms", LOG, 0.05, 300.0, 101, 30.0),
    p("hld", "HOLD", "ms", LOG, 10.0, 5000.0, 101, 184.0),
    p("rel", "RELEASE", "ms", LOG, 10.0, 1000.0, 101, 86.0),
];

/// The Dual Threshold Expander: fully open over UPPER, reaching DEPTH at LOWER;
/// EXP picks a Linear or Logarithmic response between them.
const DUAL_EXP_PARAMS: &[ParamSpec] = &[
    p("uthr", "UPPER", "dB", LIN, -72.0, 12.0, 169, -30.0),
    p("lthr", "LOWER", "dB", LIN, -72.0, 12.0, 169, -45.0),
    p("range", "DEPTH", "dB", LIN, 0.0, 60.0, 61, 30.0),
    p("att", "ATTACK", "ms", LOG, 0.05, 300.0, 101, 5.0),
    p("rel", "RELEASE", "ms", LOG, 10.0, 1000.0, 101, 200.0),
    p("exp", "EXP", "", ParamKind::Choice(&["LIN", "LOG"]), 0.0, 1.0, 2, 1.0),
];

/// The Source Expander: Threshold, Depth and its response speed.
const SOURCE_EXP_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", LIN, -72.0, 12.0, 169, -45.0),
    p("range", "DEPTH", "dB", LIN, 0.0, 60.0, 61, 12.0),
    p("spd", "SPEED", "", ParamKind::Choice(&["SLOW", "MEDIUM", "FAST"]), 0.0, 2.0, 3, 1.0),
];

/// Manual Peak and Manual RMS: the picture's Attack, Release, Soft Knee, Ratio,
/// Threshold (-46..+18 dBu) and Gain, and the compressor page's Parallel Path.
const MANUAL_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", LIN, -46.0, 18.0, 129, -21.0),
    p("ratio", "RATIO", "", ParamKind::Choice(sq::RATIO_NAMES), 0.0, 16.0, 17, 8.0),
    p("att", "ATTACK", "ms", LOG, 0.03, 300.0, 101, 4.6),
    p("rel", "RELEASE", "ms", LOG, 50.0, 2000.0, 101, 500.0),
    p("gain", "GAIN", "dB", LIN, 0.0, 18.0, 37, 0.0),
    p("soft", "SOFT KNEE", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    p("par", "PARALLEL", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    p("dry", "DRY", "dB", LIN, PARALLEL_OFF_DB, 0.0, 61, PARALLEL_OFF_DB),
    p("wet", "WET", "dB", LIN, PARALLEL_OFF_DB, 0.0, 61, 0.0),
];

/// 16T's panel: THRESHOLD, KNEE, COMPRESSION RATIO (past infinity to -5, -2 and
/// -1, where the output falls as the level rises), OUTPUT GAIN.
const T16_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", LIN, -46.0, 18.0, 129, 0.0),
    p(
        "ratio",
        "RATIO",
        "",
        ParamKind::Choice(&["1:1", "2:1", "3:1", "4:1", "6:1", "10:1", "INF", "-5", "-2", "-1"]),
        0.0,
        9.0,
        10,
        2.0,
    ),
    p("knee", "KNEE", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    p("out", "OUTPUT", "dB", LIN, -18.0, 18.0, 73, 0.0),
];

/// 16VU's panel: THRESHOLD, COMPRESSION, GAIN.
const VU16_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", LIN, -46.0, 18.0, 129, 0.0),
    p(
        "ratio",
        "COMPRESSION",
        "",
        ParamKind::Choice(&["1:1", "1.5:1", "2:1", "3:1", "4:1", "6:1", "10:1", "20:1", "INF"]),
        0.0,
        8.0,
        9,
        4.0,
    ),
    p("out", "GAIN", "dB", LIN, -18.0, 18.0, 73, 0.0),
];

/// Mighty's panel: THRESHOLD in dBv, RELEASE in seconds per 20 dB, OUTPUT, DETECTOR.
const MIGHTY_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", LIN, -30.0, 18.0, 97, 0.0),
    p("rel", "RELEASE", "s/20dB", LOG, 0.05, 5.0, 101, 0.5),
    p("out", "OUTPUT", "dB", LIN, -15.0, 15.0, 61, 0.0),
    p("det", "DETECTOR", "", ParamKind::Choice(&["PK", "AVG"]), 0.0, 1.0, 2, 1.0),
];

/// Opto's ratios, as its panel prints them.
const OPTO_RATIOS: [f32; 7] = [1.2, 1.5, 2.0, 3.0, 4.0, 6.0, 10.0];

/// Opto's panel: THRESHOLD, RATIO, GAIN, ATTACK and RECOVERY, TRANSIENT and BURN.
const OPTO_PARAMS: &[ParamSpec] = &[
    p("thr", "THRESHOLD", "dB", LIN, -46.0, 18.0, 129, 0.0),
    p("ratio", "RATIO", "", ParamKind::Values(&OPTO_RATIOS), 0.0, 6.0, 7, 3.0),
    p("out", "GAIN", "dB", LIN, -18.0, 18.0, 73, 0.0),
    p("att", "ATTACK", "", ParamKind::Choice(&["FAST", "MED", "SLOW"]), 0.0, 2.0, 3, 1.0),
    p("rec", "RECOVERY", "", ParamKind::Choice(&["FAST", "MED", "SLOW"]), 0.0, 2.0, 3, 1.0),
    p("trans", "TRANSIENT", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    p("burn", "BURN", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
];

/// OptTronik's panel: GAIN, PEAK REDUCTION, LIMIT / COMPRESS, EMPHASIS, UNIT A or B.
const OPTTRONIK_PARAMS: &[ParamSpec] = &[
    p("gain", "GAIN", "", LIN, 0.0, 100.0, 101, 50.0),
    p("peak", "PEAK REDUCTION", "", LIN, 0.0, 100.0, 101, 40.0),
    p("mode", "MODE", "", ParamKind::Choice(&["COMPRESS", "LIMIT"]), 0.0, 1.0, 2, 0.0),
    p("emph", "EMPHASIS", "", ParamKind::Choice(&["FLAT", "80", "80/5K"]), 0.0, 2.0, 3, 0.0),
    p("unit", "UNIT", "", ParamKind::Choice(&["A", "B"]), 0.0, 1.0, 2, 0.0),
];

/// Peak Limiter 76's panel: INPUT and OUTPUT, ATTACK and RELEASE 1-7, the four
/// ratio keys and ALL, UNIT 1 (the modern silver) or 2 (the black vintage), GAIN LINK.
const L76_PARAMS: &[ParamSpec] = &[
    p("in", "INPUT", "dB", LIN, -48.0, 0.0, 49, -24.0),
    p("out", "OUTPUT", "dB", LIN, -48.0, 0.0, 49, -24.0),
    p("att", "ATTACK", "", LIN, 1.0, 7.0, 61, 4.0),
    p("rel", "RELEASE", "", LIN, 1.0, 7.0, 61, 4.0),
    p("ratio", "RATIO", "", ParamKind::Choice(&["4", "8", "12", "20", "ALL"]), 0.0, 4.0, 5, 0.0),
    p("unit", "UNIT", "", ParamKind::Choice(&["1", "2"]), 0.0, 1.0, 2, 0.0),
    p("link", "GAIN LINK", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
];

/// Bus's attack positions and ratios, as its panel prints them.
const BUS_ATTACKS: [f32; 6] = [0.1, 0.3, 1.0, 3.0, 10.0, 30.0];
const BUS_RATIOS: [f32; 4] = [1.5, 2.0, 4.0, 10.0];

/// Bus's panel: ATTACK (ms), RELEASE (s or Auto), THRESHOLD, RATIO, MAKE-UP.
const BUS_PARAMS: &[ParamSpec] = &[
    p("att", "ATTACK", "ms", ParamKind::Values(&BUS_ATTACKS), 0.0, 5.0, 6, 4.0),
    p("rel", "RELEASE", "s", ParamKind::Choice(&[".1", ".3", ".6", "1.2", "AUTO"]), 0.0, 4.0, 5, 4.0),
    p("thr", "THRESHOLD", "dB", LIN, -15.0, 15.0, 61, 0.0),
    p("ratio", "RATIO", "", ParamKind::Values(&BUS_RATIOS), 0.0, 3.0, 4, 2.0),
    p("gain", "MAKE-UP", "dB", LIN, -5.0, 15.0, 41, 0.0),
];

/// CompStortion's panel: INPUT, ATTACK, RELEASE and OUTPUT 0-10, its RATIO modes
/// (10:1 is its opto mode), the DETECTOR's HP and band-pass, DISTORTION A-D.
const COMPSTORTION_PARAMS: &[ParamSpec] = &[
    p("in", "INPUT", "", LIN, 0.0, 10.0, 101, 5.0),
    p("att", "ATTACK", "", LIN, 0.0, 10.0, 101, 5.0),
    p("rel", "RELEASE", "", LIN, 0.0, 10.0, 101, 5.0),
    p("out", "OUTPUT", "", LIN, 0.0, 10.0, 101, 5.0),
    p("ratio", "RATIO", "", ParamKind::Choice(&["2:1", "3:1", "4:1", "6:1", "10:1", "20:1", "SMASH", "BRIT"]), 0.0, 7.0, 8, 1.0),
    p("hp", "HP", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    p("bp", "BP", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    p("dist", "DISTORTION", "", ParamKind::Choice(&["A", "B", "C", "D"]), 0.0, 3.0, 4, 0.0),
];

/// Dual-Stage Valve's panel: Stage-1 and its HARMONICS, Stage-2 (or Off) with its
/// BIAS and HiDrive, HF LIFT (marked 0 to 6) and OUTPUT. On/Off is the block's own
/// key. Sidecar's reading: HARMONICS and BIAS run 0 to 100, OUTPUT +-18 dB (the
/// other models' output gain), and a new one is a single triode, gently driven.
const VALVE_PARAMS: &[ParamSpec] = &[
    p("s1", "STAGE-1", "", ParamKind::Choice(&["TRIODE", "PENTODE"]), 0.0, 1.0, 2, 0.0),
    p("harm", "HARMONICS", "", LIN, 0.0, 100.0, 101, 30.0),
    p("s2", "STAGE-2", "", ParamKind::Choice(&["OFF", "TRIODE", "PENTODE"]), 0.0, 2.0, 3, 0.0),
    p("bias", "BIAS", "", LIN, 0.0, 100.0, 101, 50.0),
    p("hidrv", "HIDRIVE", "", ParamKind::Toggle, 0.0, 1.0, 2, 0.0),
    p("hf", "HF LIFT", "", LIN, 0.0, 6.0, 61, 0.0),
    p("out", "OUTPUT", "dB", LIN, -18.0, 18.0, 73, 0.0),
];

/// Tube Stage's panel: a rotary mode selector, Drive and Level (On/Off is the
/// block's key). The guide doesn't name the selector's positions: three neutral
/// numbers, and what each sounds like, are Sidecar's reading (1: a triode, 2: a
/// pentode, 3: the one into the other). Drive 0 to 100 and Level +-18 dB too.
const TUBE_PARAMS: &[ParamSpec] = &[
    p("mode", "MODE", "", ParamKind::Choice(&["1", "2", "3"]), 0.0, 2.0, 3, 0.0),
    p("drive", "DRIVE", "", LIN, 0.0, 100.0, 101, 30.0),
    p("level", "LEVEL", "dB", LIN, -18.0, 18.0, 73, 0.0),
];

/// The preamp model a new dLive channel's preamp block holds (Sidecar's reading).
pub const PREAMP_DEFAULT: &str = "DLVVALVE";

/// Whether `token` is one of the Preamp page's models: on a dLive, those go in the
/// preamp block alone (`ProfileSpec::may_hold`).
pub fn is_preamp(token: &str) -> bool {
    matches!(token, "DLVVALVE" | "DLVTUBE")
}

/// The gate slot's and the compressor slot's models and, last, the preamp block's.
/// The Library of each block holds a Ducker, so there is one for each slot.
pub static DYN_MODELS: &[ModelSpec] = &[
    model("DLVGATE", "Gate", ModelCategory::Gate, GATE_PARAMS),
    model("DLVDUCK", "Ducker", ModelCategory::Gate, DUCK_PARAMS),
    model("DLVDTEX", "Dual Threshold Expander", ModelCategory::Gate, DUAL_EXP_PARAMS),
    model("DLVSRCX", "Source Expander", ModelCategory::Gate, SOURCE_EXP_PARAMS),
    model("DLVPEAK", "Manual Peak", ModelCategory::Compressor, MANUAL_PARAMS),
    model("DLVRMS", "Manual RMS", ModelCategory::Compressor, MANUAL_PARAMS),
    model("DLV16T", "16T", ModelCategory::Compressor, T16_PARAMS),
    model("DLV16VU", "16VU", ModelCategory::Compressor, VU16_PARAMS),
    model("DLVDUCKC", "Ducker", ModelCategory::Compressor, DUCK_PARAMS),
    model("DLVMIGHT", "Mighty Compressor", ModelCategory::Compressor, MIGHTY_PARAMS),
    model("DLVOPTO", "Opto", ModelCategory::Compressor, OPTO_PARAMS),
    model("DLVOTRON", "OptTronik", ModelCategory::Compressor, OPTTRONIK_PARAMS),
    model("DLV76", "Peak Limiter 76", ModelCategory::Compressor, L76_PARAMS),
    model("DLVBUS", "Bus", ModelCategory::Compressor, BUS_PARAMS),
    model("DLVSTORT", "CompStortion", ModelCategory::Compressor, COMPSTORTION_PARAMS),
    model("DLVVALVE", "Dual-Stage Valve", ModelCategory::Special, VALVE_PARAMS),
    model("DLVTUBE", "Tube Stage", ModelCategory::Special, TUBE_PARAMS),
];

/// The NEQ-12's bands (Processing Screen, shown three tabs of 4).
pub const NEQ_BANDS: usize = 12;
/// A band's controls: In, Frequency, Gain, Width.
const NEQ_BAND_CONTROLS: usize = 4;
/// The NEQ-12's overall In, in its parameter order.
pub const NEQ_IN: usize = 0;
/// The lowest band's shape, in its parameter order.
pub const NEQ_LF_SHAPE: usize = 1;
/// The highest band's shape, in its parameter order.
pub const NEQ_HF_SHAPE: usize = 2;
const NEQ_HEAD: usize = 3;
/// The NEQ-12's parameters: the overall In, the two outer shapes and the bands'.
pub const NEQ_PARAM_COUNT: usize = NEQ_HEAD + NEQ_BANDS * NEQ_BAND_CONTROLS;

/// Where band `band`'s In sits in the NEQ-12's parameter order; its Frequency,
/// Gain and Width follow it. By index, so the audio thread's design reads the
/// 51 parameters without searching them by name.
pub const fn neq_band(band: usize) -> usize {
    NEQ_HEAD + band * NEQ_BAND_CONTROLS
}

/// The outer bands' shapes (the guide's): a shelf, a bell, or a low or high
/// cut at 12, 24 or 48 dB/octave.
pub const NEQ_SHAPES: &[&str] = &["SHELF", "BELL", "CUT 12", "CUT 24", "CUT 48"];
pub const NEQ_SHELF: usize = 0;
pub const NEQ_BELL: usize = 1;
/// The first of the three cuts; the others follow it in slope order.
pub const NEQ_CUT: usize = 2;

const NEQ_IDS: [[&str; NEQ_BAND_CONTROLS]; NEQ_BANDS] = [
    ["li", "lf", "lg", "lq"],
    ["2i", "2f", "2g", "2q"],
    ["3i", "3f", "3g", "3q"],
    ["4i", "4f", "4g", "4q"],
    ["5i", "5f", "5g", "5q"],
    ["6i", "6f", "6g", "6q"],
    ["7i", "7f", "7g", "7q"],
    ["8i", "8f", "8g", "8q"],
    ["9i", "9f", "9g", "9q"],
    ["10i", "10f", "10g", "10q"],
    ["11i", "11f", "11g", "11q"],
    ["hi", "hf", "hg", "hq"],
];
const NEQ_LABELS: [[&str; NEQ_BAND_CONTROLS]; NEQ_BANDS] = [
    ["LF IN", "LF F", "LF G", "LF W"],
    ["2 IN", "2 F", "2 G", "2 W"],
    ["3 IN", "3 F", "3 G", "3 W"],
    ["4 IN", "4 F", "4 G", "4 W"],
    ["5 IN", "5 F", "5 G", "5 W"],
    ["6 IN", "6 F", "6 G", "6 W"],
    ["7 IN", "7 F", "7 G", "7 W"],
    ["8 IN", "8 F", "8 G", "8 W"],
    ["9 IN", "9 F", "9 G", "9 W"],
    ["10 IN", "10 F", "10 G", "10 W"],
    ["11 IN", "11 F", "11 G", "11 W"],
    ["HF IN", "HF F", "HF G", "HF W"],
];

/// Where the bands start: spread evenly, by ratio, across the audio band.
/// Sidecar's reading (the guide doesn't give them).
const NEQ_HZ: [f32; NEQ_BANDS] = [40.0, 70.0, 120.0, 200.0, 350.0, 600.0, 1000.0, 1800.0, 3000.0, 5000.0, 9000.0, 16000.0];

const fn neq_params() -> [ParamSpec; NEQ_PARAM_COUNT] {
    let mut out = [p("in", "IN", "", ParamKind::Toggle, 0.0, 1.0, 2, 1.0); NEQ_PARAM_COUNT];
    out[NEQ_LF_SHAPE] = p("lm", "LF SHAPE", "", ParamKind::Choice(NEQ_SHAPES), 0.0, 4.0, 5, NEQ_SHELF as f32);
    out[NEQ_HF_SHAPE] = p("hm", "HF SHAPE", "", ParamKind::Choice(NEQ_SHAPES), 0.0, 4.0, 5, NEQ_SHELF as f32);
    let mut b = 0;
    while b < NEQ_BANDS {
        let (at, ids, labels) = (neq_band(b), NEQ_IDS[b], NEQ_LABELS[b]);
        out[at] = p(ids[0], labels[0], "", ParamKind::Toggle, 0.0, 1.0, 2, 1.0);
        out[at + 1] = p(ids[1], labels[1], "Hz", LOG, sq::FREQ.0, sq::FREQ.1, sq::FREQ.2, NEQ_HZ[b]);
        out[at + 2] = p(ids[2], labels[2], "dB", LIN, sq::GAIN.0, sq::GAIN.1, sq::GAIN.2, 0.0);
        out[at + 3] = p(ids[3], labels[3], "oct", LOG, sq::WIDTH.0, sq::WIDTH.1, sq::WIDTH.2, 1.0);
        b += 1;
    }
    out
}

/// The NEQ-12 (Processing Screen, Processing specs): an overall In, then 12
/// bands of In, Frequency, Gain (+-15 dB) and Width. Sidecar's reading, where
/// the guide is silent: the frequency and width ranges (the 4-band PEQ's, 20 Hz
/// to 20 kHz and 1.5 to 1/9 octave, unpublished here); constant-width bells (the
/// set width at any gain); first-order shelves, the PEQ's; Butterworth cuts;
/// every band In, the outer bands on their shelves and every band 1 octave wide
/// at the start.
pub static NEQ_PARAMS: [ParamSpec; NEQ_PARAM_COUNT] = neq_params();

/// The EQs: the 4-band PEQ (the SQ's ranges: 20 Hz..20 kHz, +-15 dB, 1.5 to 1/9
/// octave, Baxandall shelves here), the 28-band GEQ's four types and the NEQ-12.
pub static EQ: [ModelSpec; 6] = [
    model("DLVPEQ", "PEQ", ModelCategory::Eq, &sq::PEQ_PARAMS),
    model("DLVGEQC", "GEQ Constant-Q", ModelCategory::Eq, &sq::GEQ_PARAMS),
    model("DLVGEQP", "GEQ Proportional-Q", ModelCategory::Eq, &sq::GEQ_PARAMS),
    model("DLVGEQD", "GEQ Digi-Q", ModelCategory::Eq, &sq::GEQ_PARAMS),
    model("DLVGEQH", "GEQ Hybrid", ModelCategory::Eq, &sq::GEQ_PARAMS),
    model("DLVNEQ12", "NEQ-12", ModelCategory::Eq, &NEQ_PARAMS),
];

/// The input channels' EQ: the 4-band PEQ alone.
pub static EQ_INPUT: [ModelSpec; 1] = [EQ[0]];

/// What a dLive's blocks may hold. Its FX slots run the 20 RackExtra models
/// ([`crate::dlive_fx`]); all 16 engines are alike, so every slot may hold any of
/// them. Manual Peak as the compressor's first model is Sidecar's reading.
pub static MODELS: ModelTables = ModelTables {
    dynamics: DYN_MODELS,
    aux_dynamics: &[],
    eq_channel: &EQ_INPUT,
    eq_bus: &EQ,
    filter_tools: crate::wing::FILTER_TOOLS,
    fx: &crate::dlive_fx::FX_MODELS,
    fx_premium_first: 0,
    fx_premium_slots: 16,
    gate_default: "DLVGATE",
    comp_default: "DLVPEAK",
};

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::model_index;
    use crate::wing::dynamics::{SidechainSettings, WingDynamics, XoverSettings};
    use crate::wing::eq::design;
    use crate::wing::{MAX_DYN_PARAMS, MAX_EQ_PARAMS};
    use std::f32::consts::{PI, SQRT_2};

    fn defaults(spec: &ModelSpec) -> Vec<f32> {
        let mut v = vec![0.0; spec.params.len()];
        spec.defaults_into(&mut v);
        v
    }

    /// The output RMS over the last half of 1 s of a 1 kHz tone at `level`.
    fn run(token: &str, set: &[(&str, f32)], level: f32) -> f32 {
        let m = model_index(DYN_MODELS, token).unwrap();
        let spec = &DYN_MODELS[m];
        let mut v = defaults(spec);
        for (id, x) in set {
            v[spec.param_index(id).unwrap()] = *x;
        }
        let sr = 48000.0;
        let mut d = WingDynamics::default();
        d.set_table(DYN_MODELS);
        d.prepare(sr as f64);
        let (mut sum, mut n) = (0.0f32, 0usize);
        for b in 0..(48000 / 256) {
            let mut x: Vec<f32> = (0..256).map(|i| level * (2.0 * PI * 1000.0 * (b * 256 + i) as f32 / sr).sin()).collect();
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
    fn the_blocks_have_the_guides_ranges_and_fit_the_engine() {
        let range = |ps: &[ParamSpec], id: &str| {
            let s = ps.iter().find(|s| s.id == id).unwrap();
            (s.min, s.max)
        };
        assert_eq!(range(GATE_PARAMS, "thr"), (-72.0, 12.0));
        assert_eq!(range(GATE_PARAMS, "range"), (0.0, 60.0));
        assert_eq!(range(GATE_PARAMS, "att"), (0.05, 300.0));
        assert_eq!(range(GATE_PARAMS, "hld"), (10.0, 5000.0));
        assert_eq!(range(GATE_PARAMS, "rel"), (10.0, 1000.0));
        assert_eq!(range(MANUAL_PARAMS, "thr"), (-46.0, 18.0));
        assert!(DYN_MODELS.iter().all(|m| m.params.len() <= MAX_DYN_PARAMS));
        assert!(EQ.iter().all(|m| m.params.len() <= MAX_EQ_PARAMS));
        for m in DYN_MODELS.iter().chain(EQ.iter()) {
            for s in m.params {
                assert!(s.default >= s.min.min(s.max) && s.default <= s.max.max(s.min), "{} {}", m.token, s.id);
            }
        }
    }

    #[test]
    fn every_dynamics_model_runs_on_its_defaults() {
        for m in DYN_MODELS {
            let out = run(m.token, &[], 0.25);
            assert!(out.is_finite() && out > 0.0 && out < 1.5, "{}: {out}", m.token);
        }
    }

    #[test]
    fn levels_are_in_dbu_with_18_dbu_at_full_scale() {
        // A tone peaking at -12 dBFS is +6 dBu: over a +3 dBu threshold, under +12.
        let level = 0.25;
        let dry = level / SQRT_2;
        let under = run("DLVPEAK", &[("thr", 12.0), ("ratio", 16.0)], level);
        assert!((under / dry - 1.0).abs() < 0.03, "{}", under / dry);
        let over = run("DLVPEAK", &[("thr", 3.0), ("ratio", 16.0)], level);
        assert!(over < dry * 0.85, "{}", over / dry);
    }

    #[test]
    fn manual_compressors_and_their_parallel_path() {
        let level = 0.5;
        let dry = level / SQRT_2;
        for token in ["DLVPEAK", "DLVRMS"] {
            let hard = run(token, &[("thr", -30.0), ("ratio", 16.0)], level);
            assert!(hard < dry * 0.2, "{token}: {hard}");
            let dry_only =
                run(token, &[("thr", -30.0), ("ratio", 16.0), ("par", 1.0), ("dry", 0.0), ("wet", PARALLEL_OFF_DB)], level);
            assert!((dry_only / dry - 1.0).abs() < 0.03, "{token}: {}", dry_only / dry);
        }
    }

    #[test]
    fn the_gate_closes_by_its_depth_and_the_expanders_by_theirs() {
        let quiet = 0.01; // about -40 dBFS, -22 dBu
        let open = run("DLVGATE", &[("thr", -60.0)], quiet);
        assert!((open / (quiet / SQRT_2) - 1.0).abs() < 0.03, "{open}");
        let closed = run("DLVGATE", &[("thr", 0.0), ("range", 20.0)], quiet);
        assert!((closed / open - 0.1).abs() < 0.02, "{}", closed / open);

        // Fully open over UPPER; DEPTH under LOWER.
        let up = run("DLVDTEX", &[("uthr", -40.0), ("lthr", -50.0), ("range", 30.0)], quiet);
        assert!((up / open - 1.0).abs() < 0.05, "{}", up / open);
        let down = run("DLVDTEX", &[("uthr", 6.0), ("lthr", 0.0), ("range", 30.0)], quiet);
        assert!((down / open - 0.0316).abs() < 0.02, "{}", down / open);
        // Between the thresholds: part of the way down, LOG and LIN alike.
        let log = run("DLVDTEX", &[("uthr", -12.0), ("lthr", -32.0), ("range", 30.0), ("exp", 1.0)], quiet);
        let lin = run("DLVDTEX", &[("uthr", -12.0), ("lthr", -32.0), ("range", 30.0), ("exp", 0.0)], quiet);
        assert!(log < open * 0.9 && log > down, "{}", log / open);
        assert!(lin < open && lin > down, "{}", lin / open);

        let src = run("DLVSRCX", &[("thr", 0.0), ("range", 12.0), ("spd", 2.0)], quiet);
        assert!((src / open - 0.25).abs() < 0.05, "{}", src / open);
    }

    #[test]
    fn the_16t_goes_past_infinity() {
        let (soft, hot) = (0.2, 0.8);
        let at = |ratio: f32, level: f32| run("DLV16T", &[("thr", -20.0), ("ratio", ratio), ("knee", 0.0)], level);
        // 2:1: louder in, louder out.
        assert!(at(1.0, hot) > at(1.0, soft) * 1.5);
        // INF: about the same out.
        assert!((at(6.0, hot) / at(6.0, soft) - 1.0).abs() < 0.2, "{}", at(6.0, hot) / at(6.0, soft));
        // -1: louder in, quieter out.
        assert!(at(9.0, hot) < at(9.0, soft) * 0.6, "{}", at(9.0, hot) / at(9.0, soft));
    }

    #[test]
    fn the_character_compressors_respond_to_their_controls() {
        let level = 0.5;
        let dry = level / SQRT_2;
        for (token, set) in [
            ("DLV16VU", vec![("thr", -30.0), ("ratio", 8.0)]),
            ("DLVMIGHT", vec![("thr", -30.0)]),
            ("DLVOPTO", vec![("thr", -30.0), ("ratio", 6.0)]),
            ("DLVOTRON", vec![("peak", 100.0), ("mode", 1.0)]),
            ("DLVBUS", vec![("thr", -15.0), ("ratio", 3.0)]),
        ] {
            assert!(run(token, &set, level) < dry * 0.75, "{token}: {}", run(token, &set, level) / dry);
        }
        // The 76: more input, more limiting; GAIN LINK keeps the level roughly where it was.
        let plain = run("DLV76", &[("in", -24.0), ("out", -24.0)], 0.05);
        let driven = run("DLV76", &[("in", -6.0), ("out", -24.0)], 0.05);
        let linked = run("DLV76", &[("in", -6.0), ("out", -24.0), ("link", 1.0)], 0.05);
        assert!(driven > plain * 1.2, "{}", driven / plain);
        assert!(linked < driven * 0.5, "{}", linked / driven);
        // CompStortion's SMASH holds the level like a brick wall.
        let (a, b) = (run("DLVSTORT", &[("ratio", 6.0)], 0.5), run("DLVSTORT", &[("ratio", 6.0)], 0.8));
        assert!((b / a - 1.0).abs() < 0.2, "{}", b / a);
    }

    fn eq(token: &str) -> &'static ModelSpec {
        &EQ[model_index(&EQ, token).unwrap()]
    }

    #[test]
    fn the_peq_shelves_are_baxandall_and_its_bells_tighten_with_gain() {
        let sr = 96000.0;
        let spec = eq("DLVPEQ");
        let at = |id: &str| spec.param_index(id).unwrap();
        let mut v = defaults(spec);
        assert_eq!(design(spec, &v, sr, 1.0).len, 0, "flat: no filters at all");
        v[at("lf")] = 200.0;
        v[at("lg")] = 12.0;
        let d = design(spec, &v, sr, 1.0);
        assert!((d.response_db(10.0, sr) - 12.0).abs() < 0.3, "{}", d.response_db(10.0, sr));
        assert!((d.response_db(200.0, sr) - 6.0).abs() < 0.3, "half way at its frequency: {}", d.response_db(200.0, sr));
        // First order: still lifting an octave and more above its frequency.
        assert!(d.response_db(800.0, sr) > 0.6, "{}", d.response_db(800.0, sr));

        let bell = |g: f32| {
            let mut v = defaults(spec);
            v[at("1f")] = 1000.0;
            v[at("1g")] = g;
            v[at("1q")] = 1.0;
            design(spec, &v, sr, 1.0)
        };
        // How much of the bell's peak remains an octave off.
        let share = |g: f32| bell(g).response_db(2000.0, sr) / g as f64;
        assert!(share(15.0) < share(3.0), "{} {}", share(15.0), share(3.0));
    }

    #[test]
    fn the_geq_types_differ_as_the_guide_says() {
        let sr = 96000.0;
        let band = 15; // 1 kHz
        let peak = |token: &str, g: f32| {
            let s = eq(token);
            let mut v = defaults(s);
            v[band] = g;
            design(s, &v, sr, 1.0)
        };
        for token in ["DLVGEQC", "DLVGEQP", "DLVGEQD", "DLVGEQH"] {
            assert!((peak(token, 12.0).response_db(1000.0, sr) - 12.0).abs() < 0.6, "{token}");
            assert_eq!(design(eq(token), &defaults(eq(token)), sr, 1.0).len, 0, "{token}: flat");
        }
        let off = |d: crate::wing::eq::EqDesign, g: f64| d.response_db(1400.0, sr) / g;
        // Constant-Q: the same shape at any level.
        assert!((off(peak("DLVGEQC", 12.0), 12.0) - off(peak("DLVGEQC", 3.0), 3.0)).abs() < 0.05);
        // Proportional-Q: wide when small, tight when large.
        assert!(off(peak("DLVGEQP", 3.0), 3.0) > off(peak("DLVGEQP", 12.0), 12.0) + 0.05);
        // Hybrid: the boost proportional, the cut constant.
        assert!((off(peak("DLVGEQH", 3.0), 3.0) - off(peak("DLVGEQP", 3.0), 3.0)).abs() < 1e-6);
        assert!((off(peak("DLVGEQH", -3.0), -3.0) - off(peak("DLVGEQC", -3.0), -3.0)).abs() < 1e-6);

        // Digi-Q: a slider pattern comes out closer to the sliders.
        let s = |token: &str| {
            let spec = eq(token);
            let mut v = defaults(spec);
            v[14] = 6.0;
            v[15] = 6.0;
            v[16] = 6.0;
            design(spec, &v, sr, 1.0).response_db(1000.0, sr)
        };
        assert!((s("DLVGEQD") - 6.0).abs() < (s("DLVGEQC") - 6.0).abs(), "{} {}", s("DLVGEQD"), s("DLVGEQC"));
    }

    fn neq() -> &'static ModelSpec {
        eq("DLVNEQ12")
    }

    /// NEQ-12 parameter values: its defaults, changed by `(index, value)`.
    fn neq_values(set: &[(usize, f32)]) -> Vec<f32> {
        let mut v = defaults(neq());
        for &(i, x) in set {
            v[i] = x;
        }
        v
    }

    fn neq_design(set: &[(usize, f32)], sr: f64) -> crate::wing::eq::EqDesign {
        design(neq(), &neq_values(set), sr, 1.0)
    }

    /// Indices of band `b`'s In, Frequency, Gain and Width.
    fn neq_controls(b: usize) -> (usize, usize, usize, usize) {
        let i = neq_band(b);
        (i, i + 1, i + 2, i + 3)
    }

    #[test]
    fn the_neq12_has_the_guides_controls_ranges_and_defaults() {
        let s = neq();
        assert_eq!(NEQ_PARAM_COUNT, 51, "48 band controls, two shapes, the overall In");
        assert_eq!(s.params.len(), NEQ_PARAM_COUNT);
        assert!(s.params.len() <= MAX_EQ_PARAMS, "fits the engine's EQ block");
        let mut ids: Vec<&str> = s.params.iter().map(|p| p.id).collect();
        ids.sort_unstable();
        ids.dedup();
        assert_eq!(ids.len(), NEQ_PARAM_COUNT, "every control has its own id");

        assert_eq!((s.params[NEQ_IN].kind, s.params[NEQ_IN].default), (ParamKind::Toggle, 1.0));
        for shape in [NEQ_LF_SHAPE, NEQ_HF_SHAPE] {
            let p = &s.params[shape];
            assert_eq!(p.kind, ParamKind::Choice(NEQ_SHAPES));
            assert_eq!((p.lo(), p.hi(), p.steps, p.default), (0.0, 4.0, 5, NEQ_SHELF as f32));
        }
        assert_eq!(NEQ_SHAPES, ["SHELF", "BELL", "CUT 12", "CUT 24", "CUT 48"]);

        let mut below: f32 = 0.0;
        for b in 0..NEQ_BANDS {
            let (i, f, g, w) = neq_controls(b);
            let (on, f, g, w) = (&s.params[i], &s.params[f], &s.params[g], &s.params[w]);
            assert_eq!((on.kind, on.default), (ParamKind::Toggle, 1.0), "band {b}");
            assert_eq!((f.kind, f.lo(), f.hi(), f.unit), (ParamKind::Log, 20.0, 20000.0, "Hz"), "band {b}");
            assert_eq!((g.kind, g.lo(), g.hi(), g.default, g.unit), (ParamKind::Linear, -15.0, 15.0, 0.0, "dB"), "band {b}");
            assert_eq!((w.kind, w.lo(), w.hi(), w.default, w.unit), (ParamKind::Log, 1.0 / 9.0, 1.5, 1.0, "oct"), "band {b}");
            assert!(f.default >= 1.4 * below, "band {b} spread from the one below");
            assert!(f.default >= f.lo() && f.default <= f.hi());
            below = f.default;
        }
        assert!(s.params[neq_controls(0).1].default < 63.0, "reaches the low end");
        assert!(s.params[neq_controls(NEQ_BANDS - 1).1].default > 12000.0, "and the top");
        for (shape, id, band) in [(NEQ_LF_SHAPE, "lm", 0), (NEQ_HF_SHAPE, "hm", NEQ_BANDS - 1)] {
            assert_eq!(s.param_index(id), Some(shape));
            assert_eq!(s.params[neq_controls(band).1].id, if band == 0 { "lf" } else { "hf" });
        }
    }

    #[test]
    fn the_neq12_is_flat_at_0_db_whatever_its_shapes() {
        let sr = 48000.0;
        for (lf, hf) in [(NEQ_SHELF, NEQ_SHELF), (NEQ_BELL, NEQ_BELL), (NEQ_SHELF, NEQ_BELL)] {
            let d = neq_design(&[(NEQ_LF_SHAPE, lf as f32), (NEQ_HF_SHAPE, hf as f32)], sr);
            assert_eq!(d.len, 0, "no filters for flat bands");
            for f in [20.0, 100.0, 1000.0, 10000.0, 20000.0] {
                assert_eq!(d.response_db(f, sr), 0.0);
            }
        }
        // ... and through the DSP: nothing changes.
        let mut x: Vec<f32> = (0..512).map(|i| (i as f32 * 0.1).sin() * 0.5).collect();
        let dry = x.clone();
        let mut processor = crate::wing::eq::WingEq::default();
        processor.prepare(sr);
        let model = model_index(&EQ, "DLVNEQ12").unwrap();
        processor.process(&mut [&mut x[..]], &EQ, model, true, 100.0, &neq_values(&[]));
        assert_eq!(x, dry);
    }

    #[test]
    fn an_neq12_bell_lands_on_its_centre_and_width_at_any_gain() {
        let sr = 96000.0;
        let w: f32 = 2.0 / 3.0;
        for b in 0..NEQ_BANDS {
            let (_, f, g, width) = neq_controls(b);
            for gain in [15.0, 9.0, 3.0, -3.0, -9.0, -15.0] {
                let mut set = vec![(f, 1000.0), (g, gain), (width, w)];
                if b == 0 {
                    set.push((NEQ_LF_SHAPE, NEQ_BELL as f32));
                } else if b == NEQ_BANDS - 1 {
                    set.push((NEQ_HF_SHAPE, NEQ_BELL as f32));
                }
                let d = neq_design(&set, sr);
                assert_eq!(d.len, 1, "band {b}");
                let got = d.response_db(1000.0, sr);
                assert!((got - gain as f64).abs() < 0.05, "band {b} at {gain} dB: {got}");
                // Half the boost, in dB, half the width away either side.
                let up = 1000.0 * 2f64.powf(0.5 * w as f64);
                for edge in [up, 1000.0 * 1000.0 / up] {
                    let at = d.response_db(edge, sr);
                    assert!((at - gain as f64 * 0.5).abs() < 0.12, "band {b} at {gain} dB, {edge} Hz: {at}");
                }
            }
        }
        // The narrowest and the widest width, still the set boost.
        let (_, f, g, width) = neq_controls(5);
        for w in [1.5, 1.0 / 9.0] {
            let d = neq_design(&[(f, 5000.0), (g, 12.0), (width, w)], sr);
            assert!((d.response_db(5000.0, sr) - 12.0).abs() < 0.05, "width {w}");
        }
    }

    #[test]
    fn neq12_shelves_are_first_order_at_both_ends() {
        let sr = 96000.0;
        let (_, f, g, _) = neq_controls(0);
        let (_, fh, gh, _) = neq_controls(NEQ_BANDS - 1);
        let low = neq_design(&[(f, 200.0), (g, 12.0), (NEQ_LF_SHAPE, NEQ_SHELF as f32)], sr);
        assert_eq!(low.len, 1);
        assert!((low.response_db(10.0, sr) - 12.0).abs() < 0.3);
        assert!((low.response_db(200.0, sr) - 6.0).abs() < 0.2, "half way at its frequency");
        assert!(low.response_db(800.0, sr) > 0.6, "first order: still lifting two octaves up");
        assert!(low.response_db(20000.0, sr).abs() < 0.1);
        let high = neq_design(&[(fh, 8000.0), (gh, -9.0), (NEQ_HF_SHAPE, NEQ_SHELF as f32)], sr);
        assert_eq!(high.len, 1);
        assert!((high.response_db(40000.0, sr) + 9.0).abs() < 0.3);
        assert!((high.response_db(8000.0, sr) + 4.5).abs() < 0.2, "half way at its frequency");
        assert!(high.response_db(100.0, sr).abs() < 0.1);
        // A band set to bell is a bell: back to flat on either side.
        let bell = neq_design(&[(f, 200.0), (g, 12.0), (NEQ_LF_SHAPE, NEQ_BELL as f32)], sr);
        assert!(bell.response_db(20.0, sr) < 1.0, "a bell, not a shelf: {}", bell.response_db(20.0, sr));
    }

    #[test]
    fn neq12_cuts_are_butterworth_at_each_slope_ignoring_the_gain() {
        let sr = 96000.0;
        let (_, lf, lg, _) = neq_controls(0);
        let (_, hf, hg, _) = neq_controls(NEQ_BANDS - 1);
        for (offset, sections, slope) in [(0, 1, 12.0), (1, 2, 24.0), (2, 4, 48.0)] {
            let shape = (NEQ_CUT + offset) as f32;
            // Low cut: -3 dB at its frequency; the slope below it; untouched above.
            let low = neq_design(&[(lf, 2000.0), (lg, 15.0), (NEQ_LF_SHAPE, shape)], sr);
            assert_eq!(low.len, sections, "the low cut's sections");
            assert!((low.response_db(2000.0, sr) + 3.0).abs() < 0.1, "{}", low.response_db(2000.0, sr));
            let db = low.response_db(500.0, sr) - low.response_db(250.0, sr);
            assert!((db - slope).abs() < slope * 0.06, "low cut {slope} dB/oct: {db}");
            assert!(low.response_db(10000.0, sr).abs() < 0.1, "pass band");
            // High cut: -3 dB at its frequency; the slope above it; untouched below.
            let high = neq_design(&[(hf, 500.0), (hg, -15.0), (NEQ_HF_SHAPE, shape)], sr);
            assert_eq!(high.len, sections, "the high cut's sections");
            assert!((high.response_db(500.0, sr) + 3.0).abs() < 0.1, "{}", high.response_db(500.0, sr));
            let db = high.response_db(1000.0, sr) - high.response_db(2000.0, sr);
            assert!((db - slope).abs() < slope * 0.06, "high cut {slope} dB/oct: {db}");
            assert!(high.response_db(100.0, sr).abs() < 0.1, "pass band");
        }
    }

    #[test]
    fn neq12_band_in_and_overall_in_bypass() {
        let sr = 48000.0;
        let (bell_in, f, g, w) = neq_controls(4);
        let (_, f2, g2, _) = neq_controls(7);
        let (cut_in, _, _, _) = neq_controls(0);
        let (hf_in, hf, hg, _) = neq_controls(NEQ_BANDS - 1);
        let both = [(f, 1000.0), (g, 12.0), (w, 1.0), (f2, 4000.0), (g2, -6.0)];
        let with = |extra: &[(usize, f32)]| neq_design(&[&both[..], extra].concat(), sr);

        // A band out leaves the other bands alone.
        let d = with(&[]);
        assert_eq!(d.len, 2);
        assert!(d.response_db(1000.0, sr) > 10.0);
        let out = with(&[(bell_in, 0.0)]);
        assert_eq!(out.len, 1, "the band's filter is gone");
        let alone = neq_design(&[(f2, 4000.0), (g2, -6.0)], sr);
        assert_eq!(out.response_db(1000.0, sr), alone.response_db(1000.0, sr));
        assert!(out.response_db(1000.0, sr).abs() < 1.0);

        // A cut's own In takes the whole cut out, the shelf's too.
        let cut = [(NEQ_LF_SHAPE, (NEQ_CUT + 2) as f32)];
        assert_eq!(neq_design(&cut, sr).len, 4);
        assert_eq!(neq_design(&[&cut[..], &[(cut_in, 0.0)]].concat(), sr).len, 0);
        let shelf = [(hf, 8000.0), (hg, 9.0), (NEQ_HF_SHAPE, NEQ_SHELF as f32)];
        assert_eq!(neq_design(&shelf, sr).len, 1);
        assert_eq!(neq_design(&[&shelf[..], &[(hf_in, 0.0)]].concat(), sr).len, 0);

        // The overall In takes everything out, cuts included.
        let overall = neq_design(&[&both[..], &cut[..], &[(NEQ_IN, 0.0)]].concat(), sr);
        assert_eq!(overall.len, 0);
        for x in [100.0, 1000.0, 4000.0] {
            assert_eq!(overall.response_db(x, sr), 0.0);
        }
        // Through the DSP: audio untouched while the overall In is off.
        let mut samples: Vec<f32> = (0..2048).map(|i| (i as f32 * 0.13).sin() * 0.5).collect();
        let dry = samples.clone();
        let mut processor = crate::wing::eq::WingEq::default();
        processor.prepare(sr);
        let model = model_index(&EQ, "DLVNEQ12").unwrap();
        let v = neq_values(&[&both[..], &cut[..], &[(NEQ_IN, 0.0)]].concat());
        processor.process(&mut [&mut samples[..]], &EQ, model, true, 100.0, &v);
        assert_eq!(samples, dry);
    }

    #[test]
    fn neq12_stereo_processing_stays_finite_at_its_extremes() {
        use crate::wing::eq::{WingEq, MAX_SECTIONS};
        let model = model_index(&EQ, "DLVNEQ12").unwrap();
        // The biggest design there can be: 48 dB/oct cuts on both outer bands, ten bells.
        let cuts = [(NEQ_LF_SHAPE, 4.0), (NEQ_HF_SHAPE, 4.0)];
        let mut max = neq_values(&cuts);
        for b in 1..NEQ_BANDS - 1 {
            max[neq_controls(b).2] = 12.0;
        }
        let d = design(neq(), &max, 48000.0, 1.0);
        assert_eq!(d.len, 18);
        assert!(d.len <= MAX_SECTIONS);

        for sr in [32000.0, 44100.0, 48000.0, 96000.0, 192000.0] {
            let mut p = WingEq::default();
            p.prepare(sr);
            let mut v = defaults(neq());
            let mut t = 0usize;
            let mut block = |p: &mut WingEq, v: &[f32], blocks: usize| {
                for _ in 0..blocks {
                    let mut l: Vec<f32> =
                        (0..64).map(|i| if (t + i) % 97 == 0 { 1.0 } else { ((t + i) as f32 * 0.31).sin() }).collect();
                    let mut r: Vec<f32> = (0..64).map(|i| -((t + i) as f32 * 0.07).sin()).collect();
                    p.process(&mut [&mut l[..], &mut r[..]], &EQ, model, true, 100.0, v);
                    t += 64;
                    assert!(l.iter().chain(&r).all(|x| x.is_finite()), "non-finite at {sr} Hz");
                }
            };
            block(&mut p, &v, 8);
            // Everything at once at the top of the band, then at the bottom, then the cuts.
            for (hz, gain, width) in
                [(20000.0, 15.0, 1.0 / 9.0), (20000.0, -15.0, 1.5), (20.0, 15.0, 1.5), (20.0, -15.0, 1.0 / 9.0)]
            {
                for b in 0..NEQ_BANDS {
                    let (_, f, g, w) = neq_controls(b);
                    v[f] = hz;
                    v[g] = gain;
                    v[w] = width;
                }
                for (lf, hf) in [(NEQ_BELL, NEQ_BELL), (NEQ_SHELF, NEQ_SHELF), (NEQ_CUT + 2, NEQ_CUT + 2), (NEQ_CUT, NEQ_CUT + 1)]
                {
                    v[NEQ_LF_SHAPE] = lf as f32;
                    v[NEQ_HF_SHAPE] = hf as f32;
                    block(&mut p, &v, 24);
                }
            }
            // Cuts crossing each other, a run with every In toggled, then back to flat.
            v[neq_controls(0).1] = 20000.0;
            v[neq_controls(NEQ_BANDS - 1).1] = 20.0;
            block(&mut p, &v, 16);
            for b in 0..NEQ_BANDS {
                v[neq_band(b)] = 0.0;
                block(&mut p, &v, 2);
                v[neq_band(b)] = 1.0;
                v[NEQ_IN] = 1.0 - v[NEQ_IN];
            }
            block(&mut p, &defaults(neq()), 64);
        }
    }

    // ---- the preamp models, measured -------------------------------------------------

    const PREAMP_SR: f32 = 48000.0;
    /// 0.25 s to settle, then 0.25 s measured: a whole number of cycles of any
    /// frequency that's a multiple of 4 Hz.
    const SETTLE: usize = 12000;
    const MEASURED: usize = 12000;

    fn preamp_values(token: &str, set: &[(&str, f32)]) -> (usize, Vec<f32>) {
        let m = model_index(DYN_MODELS, token).unwrap();
        let spec = &DYN_MODELS[m];
        let mut v = defaults(spec);
        for (id, x) in set {
            v[spec.param_index(id).unwrap()] = *x;
        }
        (m, v)
    }

    /// `input` (of the sample's index) through the model, the slot `on`, in 256-sample blocks.
    fn preamp_run(token: &str, set: &[(&str, f32)], on: bool, len: usize, input: impl Fn(usize) -> f32) -> Vec<f32> {
        let (m, v) = preamp_values(token, set);
        let mut d = WingDynamics::default();
        d.set_table(DYN_MODELS);
        d.prepare(PREAMP_SR as f64);
        let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
        let mut out = Vec::with_capacity(len);
        while out.len() < len {
            let start = out.len();
            let mut x: Vec<f32> = (start..(start + 256).min(len)).map(&input).collect();
            d.process_slot(&mut [&mut x[..]], None, m, &v, &sc, &xo, 1.0, 0.0, on);
            out.extend_from_slice(&x);
        }
        out
    }

    /// What the model makes of a sine at `level`, once settled.
    fn preamp_tone(token: &str, set: &[(&str, f32)], level: f32, freq: f32) -> Vec<f32> {
        let tone = |i: usize| level * (2.0 * std::f64::consts::PI * freq as f64 * i as f64 / PREAMP_SR as f64).sin() as f32;
        preamp_run(token, set, true, SETTLE + MEASURED, tone)[SETTLE..].to_vec()
    }

    /// The amplitude of `y` at `freq`, by correlating it against a sine and a cosine there.
    fn correlate(y: &[f32], freq: f64) -> f32 {
        let (mut c, mut s) = (0.0f64, 0.0f64);
        for (i, v) in y.iter().enumerate() {
            let w = 2.0 * std::f64::consts::PI * freq * i as f64 / PREAMP_SR as f64;
            c += *v as f64 * w.cos();
            s += *v as f64 * w.sin();
        }
        ((c * c + s * s).sqrt() * 2.0 / y.len() as f64) as f32
    }

    /// Harmonics 1 to `count` of a 1 kHz measurement.
    fn harmonics(y: &[f32], count: usize) -> Vec<f32> {
        (1..=count).map(|k| correlate(y, 1000.0 * k as f64)).collect()
    }

    /// Distortion of a 1 kHz measurement: harmonics 2 to 10 against the fundamental.
    fn thd(y: &[f32]) -> f32 {
        let h = harmonics(y, 10);
        h[1..].iter().map(|x| x * x).sum::<f32>().sqrt() / h[0]
    }

    fn at_1k(token: &str, set: &[(&str, f32)], level: f32) -> Vec<f32> {
        preamp_tone(token, set, level, 1000.0)
    }

    #[test]
    fn the_preamp_models_are_the_dlives_special_ones_and_have_its_controls() {
        for (token, name, ids) in [
            ("DLVVALVE", "Dual-Stage Valve", vec!["s1", "harm", "s2", "bias", "hidrv", "hf", "out"]),
            ("DLVTUBE", "Tube Stage", vec!["mode", "drive", "level"]),
        ] {
            let m = &DYN_MODELS[model_index(DYN_MODELS, token).unwrap()];
            assert_eq!((m.name, m.category), (name, ModelCategory::Special));
            assert_eq!(m.params.iter().map(|p| p.id).collect::<Vec<_>>(), ids);
            assert!(is_preamp(token));
        }
        assert!(!is_preamp("DLVGATE") && !is_preamp("DLV16T"));
        // At the end of the table: every other model keeps its index.
        assert_eq!(&DYN_MODELS[DYN_MODELS.len() - 2].token, &"DLVVALVE");
        let valve = &DYN_MODELS[model_index(DYN_MODELS, "DLVVALVE").unwrap()];
        let param = |id: &str| valve.params.iter().find(|p| p.id == id).unwrap();
        assert_eq!((param("hf").min, param("hf").max), (0.0, 6.0));
        assert_eq!(param("s2").display(0.0), "OFF");
        // Three neutral modes on the rotary selector.
        let tube = &DYN_MODELS[model_index(DYN_MODELS, "DLVTUBE").unwrap()];
        let mode = tube.params.iter().find(|p| p.id == "mode").unwrap();
        assert_eq!((0..3).map(|i| mode.display(i as f32)).collect::<Vec<_>>(), ["1", "2", "3"]);
    }

    #[test]
    fn off_passes_the_signal_unchanged() {
        let tone = |i: usize| 0.7 * (0.13 * i as f32).sin();
        for token in ["DLVVALVE", "DLVTUBE"] {
            let out = preamp_run(token, &[], false, 4096, tone);
            assert!(out.iter().enumerate().all(|(i, y)| *y == tone(i)), "{token}: off is exactly the input");
        }
    }

    #[test]
    fn switched_off_it_fades_out_and_then_passes_the_signal_unchanged() {
        let tone = |i: usize| 0.7 * (0.13 * i as f32).sin();
        let (m, v) = preamp_values("DLVVALVE", &[("harm", 100.0), ("out", -12.0)]);
        let mut d = WingDynamics::default();
        d.set_table(DYN_MODELS);
        d.prepare(PREAMP_SR as f64);
        let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
        let block = |d: &mut WingDynamics, start: usize, n: usize, on: bool| {
            let mut x: Vec<f32> = (start..start + n).map(tone).collect();
            d.process_slot(&mut [&mut x[..]], None, m, &v, &sc, &xo, 1.0, 0.0, on);
            x
        };
        let on = block(&mut d, 0, 9600, true);
        assert!(on.iter().enumerate().skip(4800).any(|(i, y)| (y - tone(i)).abs() > 0.01), "on, it colours the signal");
        block(&mut d, 9600, 960, false); // 20 ms: past the 5 ms fade
        assert!(!d.is_running(false));
        let after = block(&mut d, 10560, 2400, false);
        assert!(after.iter().enumerate().all(|(i, y)| *y == tone(10560 + i)));
    }

    #[test]
    fn a_triode_leans_even_and_a_pentode_odd() {
        for level in [0.06, 0.25, 0.5, 1.0] {
            for harm in [10.0, 30.0, 60.0, 100.0] {
                let triode = harmonics(&at_1k("DLVVALVE", &[("s1", 0.0), ("harm", harm)], level), 3);
                assert!(triode[1] > triode[2] * 3.0, "triode at {level}, {harm}: {triode:?}");
                let pentode = harmonics(&at_1k("DLVVALVE", &[("s1", 1.0), ("harm", harm)], level), 3);
                assert!(pentode[2] > pentode[1] * 3.0, "pentode at {level}, {harm}: {pentode:?}");
            }
        }
        // And the same through two stages of a kind.
        let two = |kind: f32, set: &[(&str, f32)]| {
            let mut s = vec![("s1", kind), ("s2", 1.0 + kind), ("harm", 40.0), ("bias", 60.0)];
            s.extend_from_slice(set);
            harmonics(&at_1k("DLVVALVE", &s, 0.25), 3)
        };
        let t = two(0.0, &[]);
        assert!(t[1] > t[2], "two triodes: {t:?}");
        let p = two(1.0, &[]);
        assert!(p[2] > p[1], "two pentodes: {p:?}");
    }

    #[test]
    fn more_harmonics_more_distortion() {
        for (kind, name) in [(0.0, "triode"), (1.0, "pentode")] {
            let at = |harm: f32| thd(&at_1k("DLVVALVE", &[("s1", kind), ("harm", harm)], 0.25));
            let mut last = 0.0;
            for harm in [0.0, 20.0, 40.0, 60.0, 80.0, 100.0] {
                let d = at(harm);
                assert!(d > last, "{name} at HARMONICS {harm}: {d}, was {last}");
                last = d;
            }
            assert!(at(0.0) < 0.01, "{name} barely driven: {}", at(0.0));
            assert!(at(100.0) > 0.2, "{name} hard driven: {}", at(100.0));
        }
    }

    #[test]
    fn more_bias_more_overdrive_and_hidrive_raises_it() {
        for (kind, name) in [(1.0, "triode"), (2.0, "pentode")] {
            let at = |bias: f32, hi: f32| {
                thd(&at_1k("DLVVALVE", &[("s1", 0.0), ("harm", 0.0), ("s2", kind), ("bias", bias), ("hidrv", hi)], 0.25))
            };
            let (clean, a, b, c) = (at(0.0, 0.0), at(40.0, 0.0), at(70.0, 0.0), at(100.0, 0.0));
            assert!(clean < a && a < b && b <= c * 1.001, "{name}: {clean} {a} {b} {c}");
            assert!(clean < 0.01, "{name} under the break-up, all but clean: {clean}");
            assert!(c > 0.2, "{name} overdriven: {c}");
            // HiDrive: more, and an overdrive that doesn't wait for the knee.
            assert!(at(40.0, 1.0) > a * 1.5, "{name}: {} over {a}", at(40.0, 1.0));
            assert!(at(0.0, 1.0) > clean * 3.0, "{name}: {} over {clean}", at(0.0, 1.0));
        }
    }

    #[test]
    fn hf_lift_raises_the_high_frequencies_alone() {
        // A quiet tone, barely driven: the shelf alone.
        let at = |hf: f32, freq: f32| {
            correlate(&preamp_tone("DLVVALVE", &[("s1", 0.0), ("harm", 0.0), ("hf", hf)], 0.02, freq), freq as f64)
        };
        let lift = |hf: f32, freq: f32| 20.0 * (at(hf, freq) / at(0.0, freq)).log10();
        for (f, most) in [(100.0, 0.05), (200.0, 0.05), (1000.0, 0.2), (2000.0, 0.7)] {
            let db = lift(6.0, f);
            assert!(db.abs() < most, "{f} Hz moved by {db} dB");
        }
        for (f, least) in [(10000.0, 3.0), (16000.0, 4.0), (20000.0, 5.0)] {
            let db = lift(6.0, f);
            assert!(db > least && db < 6.1, "{f} Hz: {db} dB");
        }
        // Half way up the marks, half the lift.
        let (half, full) = (lift(3.0, 16000.0), lift(6.0, 16000.0));
        assert!(half > 1.0 && half < full * 0.7, "{half} of {full} dB");
    }

    #[test]
    fn output_and_level_change_the_level() {
        let level = |token: &str, id: &str, x: f32| correlate(&at_1k(token, &[(id, x)], 0.1), 1000.0);
        for (token, id) in [("DLVVALVE", "out"), ("DLVTUBE", "level")] {
            let (down, flat, up) = (level(token, id, -12.0), level(token, id, 0.0), level(token, id, 12.0));
            assert!((20.0 * (up / flat).log10() - 12.0).abs() < 0.05, "{token}: {}", 20.0 * (up / flat).log10());
            assert!((20.0 * (down / flat).log10() + 12.0).abs() < 0.05, "{token}: {}", 20.0 * (down / flat).log10());
        }
    }

    #[test]
    fn a_new_preamp_keeps_a_normal_level_about_where_it_was() {
        for token in ["DLVVALVE", "DLVTUBE"] {
            for level in [0.03, 0.126, 0.5] {
                let g = correlate(&at_1k(token, &[], level), 1000.0) / level;
                assert!((g - 1.0).abs() < 0.1, "{token} at {level}: gain {g}");
            }
        }
    }

    #[test]
    fn stage_2_off_is_a_single_stage() {
        let run = |set: &[(&str, f32)]| at_1k("DLVVALVE", set, 0.25);
        let single = run(&[("s2", 0.0), ("bias", 10.0), ("hidrv", 0.0)]);
        assert_eq!(single, run(&[("s2", 0.0), ("bias", 90.0), ("hidrv", 1.0)]), "off, BIAS and HiDrive do nothing");
        // Stage 2 in, the same stage 1: a different signal, and more distorted.
        let two = run(&[("s2", 1.0), ("bias", 90.0), ("hidrv", 1.0)]);
        assert!(thd(&two) > thd(&single) * 2.0, "{} over {}", thd(&two), thd(&single));
    }

    #[test]
    fn the_tube_stages_three_modes_differ() {
        let h = |mode: f32| harmonics(&at_1k("DLVTUBE", &[("mode", mode), ("drive", 60.0)], 0.25), 3);
        let (a, b, c) = (h(0.0), h(1.0), h(2.0));
        assert!(a[1] > a[2] * 3.0, "1 leans even: {a:?}");
        assert!(b[2] > b[1] * 3.0, "2 leans odd: {b:?}");
        assert!(c[1] > c[0] * 0.01 && c[2] > c[0] * 0.01, "3 has both: {c:?}");
        for m in [0.0, 1.0, 2.0] {
            let set = |d: f32| [("mode", m), ("drive", d)];
            let (lo, hi) = (thd(&at_1k("DLVTUBE", &set(0.0), 0.25)), thd(&at_1k("DLVTUBE", &set(100.0), 0.25)));
            assert!(hi > lo * 5.0, "mode {m}: Drive took it from {lo} to {hi}");
        }
    }

    #[test]
    fn strong_drive_does_not_alias() {
        // 7 kHz, hard into the pentode. Harmonics 5, 7, 9 and 11 fold back to these frequencies.
        let set = [("s1", 1.0), ("harm", 100.0)];
        let y = preamp_tone("DLVVALVE", &set, 0.25, 7000.0);
        let fundamental = correlate(&y, 7000.0);
        for alias in [13000.0, 1000.0, 15000.0, 19000.0] {
            let db = 20.0 * (correlate(&y, alias) / fundamental).log10();
            assert!(db < -45.0, "{alias} Hz is {db} dB");
        }
        // Harmonics that really are in the band are there: the tone is distorted.
        assert!(correlate(&y, 21000.0) / fundamental > 0.1);
    }

    #[test]
    fn every_control_at_its_extremes_stays_finite_and_bounded() {
        let noise = |i: usize| {
            let x = (i as u64).wrapping_mul(1103515245).wrapping_add(12345) % 65536;
            x as f32 / 32768.0 - 1.0
        };
        let tone = |f: f32, level: f32| move |i: usize| level * (2.0 * std::f32::consts::PI * f * i as f32 / PREAMP_SR).sin();
        type Input = Box<dyn Fn(usize) -> f32>;
        let inputs: [(&str, Input); 5] = [
            ("silence", Box::new(|_| 0.0)),
            ("full scale 100 Hz", Box::new(tone(100.0, 1.0))),
            ("full scale 10 kHz", Box::new(tone(10000.0, 1.0))),
            ("noise", Box::new(noise)),
            ("over full scale", Box::new(tone(440.0, 4.0))),
        ];
        let check = |token: &str, set: &[(&str, f32)]| {
            for (name, input) in &inputs {
                let y = preamp_run(token, set, true, 4800, input);
                assert!(y.iter().all(|v| v.is_finite()), "{token} {set:?}, {name}: not finite");
                // Past the 5 ms it takes to fade in from the dry signal.
                let peak = y[240..].iter().fold(0.0f32, |m, v| m.max(v.abs()));
                // Under the OUTPUT and the shelf a stage stays within +-2 (`valve_limit`).
                let out = set.iter().find(|(id, _)| *id == "out" || *id == "level").map_or(0.0, |x| x.1);
                let hf = set.iter().find(|(id, _)| *id == "hf").map_or(0.0, |x| x.1);
                let bound = 2.6 * crate::util::db_to_gain(out) * crate::util::db_to_gain(hf).powi(2);
                assert!(peak <= bound, "{token} {set:?}, {name}: peak {peak}, bound {bound}");
            }
        };
        for s1 in [0.0, 1.0] {
            for harm in [0.0, 100.0] {
                for s2 in [0.0, 1.0, 2.0] {
                    for (bias, hi) in [(0.0, 0.0), (0.0, 1.0), (100.0, 0.0), (100.0, 1.0)] {
                        for (hf, out) in [(0.0, 0.0), (6.0, 18.0), (6.0, -18.0)] {
                            check(
                                "DLVVALVE",
                                &[
                                    ("s1", s1),
                                    ("harm", harm),
                                    ("s2", s2),
                                    ("bias", bias),
                                    ("hidrv", hi),
                                    ("hf", hf),
                                    ("out", out),
                                ],
                            );
                        }
                    }
                }
            }
        }
        for mode in [0.0, 1.0, 2.0] {
            for drive in [0.0, 100.0] {
                for level in [-18.0, 0.0, 18.0] {
                    check("DLVTUBE", &[("mode", mode), ("drive", drive), ("level", level)]);
                }
            }
        }
    }

    #[test]
    fn stereo_runs_its_two_sides_independently() {
        let (m, v) = preamp_values("DLVVALVE", &[("s1", 0.0), ("harm", 70.0)]);
        let mut d = WingDynamics::default();
        d.set_table(DYN_MODELS);
        d.prepare(PREAMP_SR as f64);
        let (sc, xo) = (SidechainSettings::default(), XoverSettings::default());
        let left = |i: usize| 0.5 * (0.2 * i as f32).sin();
        let right = |i: usize| 0.2 * (0.05 * i as f32).sin();
        let (mut l_out, mut r_out) = (vec![], vec![]);
        for b in 0..40 {
            let mut l: Vec<f32> = (b * 256..(b + 1) * 256).map(left).collect();
            let mut r: Vec<f32> = (b * 256..(b + 1) * 256).map(right).collect();
            d.process(&mut [&mut l[..], &mut r[..]], None, m, &v, &sc, &xo, 1.0, 0.0);
            l_out.extend_from_slice(&l);
            r_out.extend_from_slice(&r);
        }
        let solo = |f: &dyn Fn(usize) -> f32| preamp_run("DLVVALVE", &[("s1", 0.0), ("harm", 70.0)], true, l_out.len(), f);
        assert_eq!(solo(&left), l_out, "each side as if alone");
        assert_eq!(solo(&right), r_out);
    }
}
