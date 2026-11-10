//! The DM2000's internal effects (V2 Owner's Manual, "Effects Parameters",
//! pp.310-331): its first 43 effect types and the multi-band dynamics, each a
//! model with the parameters and ranges the manual lists.
//!
//! The manual gives ranges, not steps: the steps are Sidecar's reading. The
//! algorithms are Sidecar's too: 42 of these types run on the 01V's devices
//! (`crate::y01v::fx`: the manual's list and parameter names are its, with wider
//! ranges and added sections), wrapped here for what the DM2000 adds:
//! the LSH / EQ / HSH sections of the modulation types, the HPF and LPF of the
//! delays, the reverbs' gate, AM DEPTH, and SYNC with NOTE and TEMPO (p.331). ST
//! REVERB and M.BAND DYNA have devices of their own here. All to be compared with
//! a console.
//!
//! Not modelled (on the panel, no effect): the reverbs' DENSITY, E/R DLY and E/R
//! BAL, early reflections' ER NUM. and TYPE, HQ.PITCH's and DUAL PITCH's MODE,
//! DUAL PITCH's PAN and its feedback, SOURCE = MIDI, Freeze's REC MODE, REC DLY,
//! TRG LVL, TRG MASK, PITCH, FINE and MIDI TRG, the Multi Filter's 24 dB/oct (the
//! 01V's filters are 12 dB/oct). TEMPO is held per effect, not shared by the eight
//! (a console's is one common value, p.331).
//!
//! Not done: the types that need more than a stereo slot (REVERB 5.1, OCTA REVERB,
//! AUTO PAN 5.1, CHORUS 5.1, FLANGE 5.1, SYMPHO. 5.1, COMP 5.1, COMPAND 5.1) and the
//! VCM Add-On types (Comp276/S, Comp260/S, Equalizer601, OpenDeck, REV-X Hall, Room
//! and Plate, and V2.2's): their circuits are proprietary.

use crate::biquad::{Biquad, BiquadCoeffs};
use crate::dm2000::FREQS;
use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec, Params};
use crate::promix::fx::{damping_hz, Stage};
use crate::util::db_to_gain;
use crate::wing::fx::{FxAlgo, FxContext};
use crate::wing::MAX_FX_PARAMS;
use crate::y02r::{nearest, COMPANDER_RATIOS, RATIOS, RELEASES};
use std::f32::consts::TAU;

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

const fn log(id: &'static str, label: &'static str, unit: &'static str, min: f32, max: f32, default: f32) -> ParamSpec {
    ParamSpec { id, label, unit, kind: ParamKind::Log, min, max, steps: 201, default }
}

const fn ch(id: &'static str, label: &'static str, items: &'static [&'static str], default: f32) -> ParamSpec {
    ParamSpec {
        id,
        label,
        unit: "",
        kind: ParamKind::Choice(items),
        min: 0.0,
        max: items.len() as f32 - 1.0,
        steps: items.len() as u32,
        default,
    }
}

const fn values(id: &'static str, label: &'static str, table: &'static [f32], unit: &'static str, default: usize) -> ParamSpec {
    ParamSpec {
        id,
        label,
        unit,
        kind: ParamKind::Values(table),
        min: 0.0,
        max: table.len() as f32 - 1.0,
        steps: table.len() as u32,
        default: default as f32,
    }
}

/// The frequencies from `from` Hz to `to` Hz of the EQ's table: a sub-range
/// its 1/12 octave series keeps (21.2 Hz-8.00 kHz and so on: p.310).
const fn range_of(from: f32, to: f32) -> &'static [f32] {
    let a = nearest(&FREQS, from) as usize;
    let b = nearest(&FREQS, to) as usize + 1;
    let all: &'static [f32] = &FREQS;
    all.split_at(b).0.split_at(a).1
}

const HPF_I8K: usize = nearest(&FREQS, 8000.0) as usize;
const LPF_I50: usize = nearest(&FREQS, 50.0) as usize;
const LPF_I16K: usize = nearest(&FREQS, 16000.0) as usize;
const HPF_LEN: usize = HPF_I8K + 2;
const LPF_LEN: usize = LPF_I16K - LPF_I50 + 2;

/// THRU (20 Hz, the first), then 21.2 Hz to 8.00 kHz.
static HPF_TABLE: [f32; HPF_LEN] = {
    let mut o = [20.0; HPF_LEN];
    let mut i = 1;
    while i < HPF_LEN {
        o[i] = FREQS[i - 1];
        i += 1;
    }
    o
};
/// 50.0 Hz to 16.0 kHz, then THRU (20 kHz).
static LPF_TABLE: [f32; LPF_LEN] = {
    let mut o = [20000.0; LPF_LEN];
    let mut i = 0;
    while i < LPF_LEN - 1 {
        o[i] = FREQS[LPF_I50 + i];
        i += 1;
    }
    o
};

const LSH_F: ParamSpec = values("lsf", "LSH F", range_of(21.2, 8000.0), "Hz", 16);
const LSH_G: ParamSpec = lin("lsg", "LSH G", "dB", -12.0, 12.0, 49, 0.0);
const EQ_F: ParamSpec = values("eqf", "EQ F", range_of(100.0, 8000.0), "Hz", 40);
const EQ_G: ParamSpec = lin("eqg", "EQ G", "dB", -12.0, 12.0, 49, 0.0);
const EQ_Q: ParamSpec = log("eqq", "EQ Q", "", 0.1, 10.0, 1.0);
const HSH_F: ParamSpec = values("hsf", "HSH F", range_of(50.0, 16000.0), "Hz", 60);
const HSH_G: ParamSpec = lin("hsg", "HSH G", "dB", -12.0, 12.0, 49, 0.0);

const HPF: ParamSpec = values("hpf", "HPF", &HPF_TABLE, "Hz", 0);
const LPF: ParamSpec = values("lpf", "LPF", &LPF_TABLE, "Hz", LPF_LEN - 1);

const REV_TIME: ParamSpec = log("rev", "REV TIME", "s", 0.3, 99.0, 2.6);
const INI: ParamSpec = lin("idly", "INI.DLY", "ms", 0.0, 500.0, 5001, 20.0);
const HI: ParamSpec = lin("hr", "HI.RATIO", "", 0.1, 1.0, 10, 0.7);
const LO: ParamSpec = lin("lr", "LO.RATIO", "", 0.1, 2.4, 24, 1.0);
const DIFF: ParamSpec = lin("dif", "DIFF.", "", 0.0, 10.0, 11, 5.0);
const DENS: ParamSpec = lin("den", "DENSITY", "%", 0.0, 100.0, 101, 100.0);
const FBG: ParamSpec = lin("fbg", "FB.GAIN", "%", -99.0, 99.0, 199, 30.0);
const FREQ: ParamSpec = log("mf", "FREQ.", "Hz", 0.05, 40.0, 0.5);
const DEPTH: ParamSpec = lin("md", "DEPTH", "%", 0.0, 100.0, 101, 50.0);
const PM_DEPTH: ParamSpec = lin("md", "PM DEPTH", "%", 0.0, 100.0, 101, 50.0);
const AM_DEPTH: ParamSpec = lin("am", "AM DEPTH", "%", 0.0, 100.0, 101, 0.0);
const MDLY: ParamSpec = lin("mdl", "MOD.DLY", "ms", 0.0, 500.0, 5001, 5.0);
const WAVE2: ParamSpec = ch("wv", "WAVE", &["Sine", "Tri"], 0.0);
const WAVE3: ParamSpec = ch("wv", "WAVE", &["Sine", "Tri", "Square"], 0.0);
const DIR: ParamSpec = ch("dir", "DIR.", &["L<->R", "L->R", "L<-R", "Turn L", "Turn R"], 0.0);
const ER_TYPE: ParamSpec = ch("ty", "TYPE", &["S-Hall", "L-Hall", "Random", "Revers", "Plate", "Spring"], 0.0);
const SIZE: ParamSpec = lin("size", "ROOMSIZE", "", 0.1, 20.0, 200, 3.0);
const LIVE: ParamSpec = lin("live", "LIVENESS", "", 0.0, 10.0, 11, 5.0);
const ERN: ParamSpec = lin("ern", "ER NUM.", "", 1.0, 19.0, 19, 10.0);
const BAL: ParamSpec = lin("bal", "BALANCE", "%", 0.0, 100.0, 101, 50.0);
const FGL: ParamSpec = lin("fgl", "FB.G L", "%", -99.0, 99.0, 199, 30.0);
const FGR: ParamSpec = lin("fgr", "FB.G R", "%", -99.0, 99.0, 199, 30.0);
const SRC: ParamSpec = ch("src", "SOURCE", &["INPUT", "MIDI"], 0.0);
const SENSE: ParamSpec = lin("sns", "SENSE", "", 0.0, 100.0, 101, 50.0);
const UPDN: ParamSpec = ch("dir", "DIR.", &["UP", "DOWN"], 0.0);
const DECAY: ParamSpec = log("dcy", "DECAY", "ms", 5.0, 42300.0, 200.0);
const OFFS: ParamSpec = lin("off", "OFFSET", "", 0.0, 100.0, 101, 50.0);
const RESO: ParamSpec = lin("res", "RESO.", "", 0.0, 20.0, 21, 5.0);
const FTYPE: ParamSpec = ch("ty", "TYPE", &["LPF", "HPF", "BPF"], 0.0);
const LEVEL: ParamSpec = lin("lev", "LEVEL", "", 0.0, 100.0, 101, 100.0);
const STAGES: ParamSpec = ch("stg", "STAGE", &["2", "4", "6", "8", "10", "12", "14", "16"], 7.0);
const DST: ParamSpec = ch("dt", "DST TYPE", &["DST1", "DST2", "OVD1", "OVD2", "CRUNCH"], 0.0);
const DRIVE: ParamSpec = lin("drv", "DRIVE", "", 0.0, 100.0, 101, 50.0);
const MASTER: ParamSpec = lin("mst", "MASTER", "", 0.0, 100.0, 101, 50.0);
const TONE: ParamSpec = lin("ton", "TONE", "", -10.0, 10.0, 21, 0.0);
const NGATE: ParamSpec = lin("ng", "N.GATE", "", 0.0, 20.0, 21, 0.0);
const SYNC: ParamSpec =
    ParamSpec { id: "sync", label: "SYNC", unit: "", kind: ParamKind::Toggle, min: 0.0, max: 1.0, steps: 2, default: 0.0 };
/// TEMPO: the manual doesn't give its range (Sidecar's reading: 25-300 BPM).
const TEMPO: ParamSpec = lin("tmp", "TEMPO", "BPM", 25.0, 300.0, 276, 120.0);
const NOTE_NAMES: &[&str] =
    &["1/48", "1/24", "1/16", "1/12", "3/32", "1/8", "1/6", "3/16", "1/4", "3/8", "1/2", "3/4", "1/1", "2/1"];
/// NOTE's values (p.331, as beats of a whole note).
const NOTES: [f32; 14] = [
    1.0 / 48.0,
    1.0 / 24.0,
    1.0 / 16.0,
    1.0 / 12.0,
    3.0 / 32.0,
    1.0 / 8.0,
    1.0 / 6.0,
    3.0 / 16.0,
    0.25,
    3.0 / 8.0,
    0.5,
    0.75,
    1.0,
    2.0,
];
const fn note(id: &'static str, label: &'static str) -> ParamSpec {
    ch(id, label, NOTE_NAMES, 8.0)
}
const NOTE: ParamSpec = note("note", "NOTE");

static REV: [ParamSpec; 14] = [
    REV_TIME,
    INI,
    HI,
    LO,
    DIFF,
    DENS,
    lin("erd", "E/R DLY", "ms", 0.0, 100.0, 1001, 10.0),
    lin("erb", "E/R BAL.", "%", 0.0, 100.0, 101, 50.0),
    HPF,
    LPF,
    lin("gl", "GATE LVL", "dB", -61.0, 0.0, 62, -61.0),
    lin("att", "ATTACK", "ms", 0.0, 120.0, 121, 0.0),
    log("hld", "HOLD", "ms", 0.02, 1960.0, 10.0),
    DECAY,
];
static ER: [ParamSpec; 11] = [ER_TYPE, SIZE, LIVE, INI, DIFF, DENS, ERN, FBG, HI, HPF, LPF];
static GATE: [ParamSpec; 11] =
    [ch("ty", "TYPE", &["Type-A", "Type-B"], 0.0), SIZE, LIVE, INI, DIFF, DENS, HI, ERN, FBG, HPF, LPF];
static MONO: [ParamSpec; 8] = [lin("d", "DELAY", "ms", 0.0, 2730.0, 27301, 500.0), FBG, HI, HPF, LPF, SYNC, NOTE, TEMPO];
static STEREO: [ParamSpec; 11] = [
    lin("dl", "DELAY L", "ms", 0.0, 1350.0, 13501, 250.0),
    lin("dr", "DELAY R", "ms", 0.0, 1350.0, 13501, 375.0),
    FGL,
    FGR,
    HI,
    HPF,
    LPF,
    SYNC,
    note("notel", "NOTE L"),
    note("noter", "NOTE R"),
    TEMPO,
];
static MOD_DLY: [ParamSpec; 12] = [
    lin("d", "DELAY", "ms", 0.0, 2725.0, 27251, 500.0),
    FBG,
    HI,
    FREQ,
    DEPTH,
    WAVE2,
    HPF,
    LPF,
    SYNC,
    note("dnote", "DLY.NOTE"),
    note("mnote", "MOD.NOTE"),
    TEMPO,
];
static LCR: [ParamSpec; 17] = [
    lin("dl", "DELAY L", "ms", 0.0, 2730.0, 27301, 250.0),
    lin("dc", "DELAY C", "ms", 0.0, 2730.0, 27301, 500.0),
    lin("dr", "DELAY R", "ms", 0.0, 2730.0, 27301, 375.0),
    lin("fbd", "FB.DLY", "ms", 0.0, 2730.0, 27301, 500.0),
    lin("ll", "LEVEL L", "%", -100.0, 100.0, 201, 100.0),
    lin("lc", "LEVEL C", "%", -100.0, 100.0, 201, 100.0),
    lin("lvr", "LEVEL R", "%", -100.0, 100.0, 201, 100.0),
    FBG,
    HI,
    HPF,
    LPF,
    SYNC,
    note("notel", "NOTE L"),
    note("notec", "NOTE C"),
    note("noter", "NOTE R"),
    note("notef", "NOTE FB"),
    TEMPO,
];
static ECHO: [ParamSpec; 17] = [
    lin("dl", "DELAY L", "ms", 0.0, 1350.0, 13501, 250.0),
    lin("dr", "DELAY R", "ms", 0.0, 1350.0, 13501, 375.0),
    lin("fdl", "FB.DLY L", "ms", 0.0, 1350.0, 13501, 250.0),
    lin("fdr", "FB.DLY R", "ms", 0.0, 1350.0, 13501, 375.0),
    FGL,
    FGR,
    lin("l2r", "L->R FBG", "%", -99.0, 99.0, 199, 20.0),
    lin("r2l", "R->L FBG", "%", -99.0, 99.0, 199, 20.0),
    HI,
    HPF,
    LPF,
    SYNC,
    note("notel", "NOTE L"),
    note("noter", "NOTE R"),
    note("notefl", "NOTE FBL"),
    note("notefr", "NOTE FBR"),
    TEMPO,
];
static CHORUS: [ParamSpec; 15] =
    [FREQ, AM_DEPTH, PM_DEPTH, MDLY, WAVE2, LSH_F, LSH_G, EQ_F, EQ_G, EQ_Q, HSH_F, HSH_G, SYNC, NOTE, TEMPO];
static FLANGE: [ParamSpec; 15] = [FREQ, DEPTH, MDLY, FBG, WAVE2, LSH_F, LSH_G, EQ_F, EQ_G, EQ_Q, HSH_F, HSH_G, SYNC, NOTE, TEMPO];
static SYMPHONIC: [ParamSpec; 14] = [FREQ, DEPTH, MDLY, WAVE2, LSH_F, LSH_G, EQ_F, EQ_G, EQ_Q, HSH_F, HSH_G, SYNC, NOTE, TEMPO];
static PHASER: [ParamSpec; 13] = [
    FREQ,
    DEPTH,
    FBG,
    OFFS,
    lin("ph", "PHASE", "deg", 0.0, 354.38, 64, 0.0),
    STAGES,
    LSH_F,
    LSH_G,
    HSH_F,
    HSH_G,
    SYNC,
    NOTE,
    TEMPO,
];
static AUTOPAN: [ParamSpec; 14] = [FREQ, DEPTH, DIR, WAVE3, LSH_F, LSH_G, EQ_F, EQ_G, EQ_Q, HSH_F, HSH_G, SYNC, NOTE, TEMPO];
static TREMOLO: [ParamSpec; 13] = [FREQ, DEPTH, WAVE3, LSH_F, LSH_G, EQ_F, EQ_G, EQ_Q, HSH_F, HSH_G, SYNC, NOTE, TEMPO];
static HQ_PITCH: [ParamSpec; 8] = [
    lin("pit", "PITCH", "st", -12.0, 12.0, 25, 0.0),
    lin("fin", "FINE", "ct", -50.0, 50.0, 101, 0.0),
    lin("d", "DELAY", "ms", 0.0, 1000.0, 10001, 0.0),
    FBG,
    lin("mode", "MODE", "", 1.0, 10.0, 10, 5.0),
    SYNC,
    NOTE,
    TEMPO,
];
static DUAL_PITCH: [ParamSpec; 17] = [
    lin("ptl", "PITCH 1", "st", -24.0, 24.0, 49, -1.0),
    lin("fnl", "FINE 1", "ct", -50.0, 50.0, 101, 0.0),
    lin("ll", "LEVEL 1", "%", -100.0, 100.0, 201, 100.0),
    lin("pan1", "PAN 1", "", -63.0, 63.0, 127, -63.0),
    lin("dl", "DELAY 1", "ms", 0.0, 1000.0, 10001, 0.0),
    lin("fgl", "FB.G 1", "%", -99.0, 99.0, 199, 0.0),
    lin("ptr", "PITCH 2", "st", -24.0, 24.0, 49, 1.0),
    lin("fnr", "FINE 2", "ct", -50.0, 50.0, 101, 0.0),
    lin("lvr", "LEVEL 2", "%", -100.0, 100.0, 201, 100.0),
    lin("pan2", "PAN 2", "", -63.0, 63.0, 127, 63.0),
    lin("dr", "DELAY 2", "ms", 0.0, 1000.0, 10001, 0.0),
    lin("fgr", "FB.G 2", "%", -99.0, 99.0, 199, 0.0),
    lin("mode", "MODE", "", 1.0, 10.0, 10, 5.0),
    SYNC,
    note("notel", "NOTE 1"),
    note("noter", "NOTE 2"),
    TEMPO,
];
static ROTARY: [ParamSpec; 8] = [
    ch("rot", "ROTATE", &["STOP", "START"], 1.0),
    ch("spd", "SPEED", &["SLOW", "FAST"], 0.0),
    log("slo", "SLOW", "Hz", 0.05, 10.0, 0.8),
    log("fst", "FAST", "Hz", 0.05, 10.0, 6.0),
    lin("drv", "DRIVE", "", 0.0, 100.0, 101, 0.0),
    lin("acc", "ACCEL", "", 0.0, 10.0, 11, 5.0),
    lin("lo", "LOW", "", 0.0, 100.0, 101, 50.0),
    lin("hi", "HIGH", "", 0.0, 100.0, 101, 50.0),
];
static RING: [ParamSpec; 7] = [
    ch("src", "SOURCE", &["OSC", "SELF"], 0.0),
    lin("osc", "OSC FREQ", "Hz", 0.0, 5000.0, 50001, 440.0),
    log("fmf", "FM FREQ.", "Hz", 0.05, 40.0, 1.0),
    lin("fmd", "FM DEPTH", "%", 0.0, 100.0, 101, 0.0),
    SYNC,
    note("fnote", "NOTE FM"),
    TEMPO,
];
static MOD_FILTER: [ParamSpec; 10] =
    [FREQ, DEPTH, lin("ph", "PHASE", "deg", 0.0, 354.38, 64, 0.0), FTYPE, OFFS, RESO, LEVEL, SYNC, NOTE, TEMPO];
static DISTORTION: [ParamSpec; 5] = [DST, DRIVE, MASTER, TONE, NGATE];
static AMP: [ParamSpec; 12] = [
    ch("at", "AMP TYPE", &["STK-M1", "STK-M2", "THRASH", "MIDBST", "CMB-PG", "CMB-VR", "CMB-DX", "CMB-TW", "MINI", "FLAT"], 0.0),
    DST,
    DRIVE,
    MASTER,
    lin("bas", "BASS", "", 0.0, 100.0, 101, 50.0),
    lin("mid", "MIDDLE", "", 0.0, 100.0, 101, 50.0),
    lin("tre", "TREBLE", "", 0.0, 100.0, 101, 50.0),
    lin("cab", "CAB DEP", "%", 0.0, 100.0, 101, 100.0),
    log("eqf", "EQ F", "Hz", 100.0, 8000.0, 1000.0),
    lin("eqg", "EQ G", "dB", -12.0, 12.0, 49, 0.0),
    log("eqq", "EQ Q", "", 0.1, 10.0, 1.0),
    NGATE,
];
static DYNA_FILTER: [ParamSpec; 8] = [SRC, SENSE, UPDN, DECAY, FTYPE, OFFS, RESO, LEVEL];
static DYNA_FLANGE: [ParamSpec; 13] = [SRC, SENSE, UPDN, DECAY, OFFS, FBG, LSH_F, LSH_G, EQ_F, EQ_G, EQ_Q, HSH_F, HSH_G];
static DYNA_PHASER: [ParamSpec; 11] = [SRC, SENSE, UPDN, DECAY, OFFS, FBG, STAGES, LSH_F, LSH_G, HSH_F, HSH_G];
static REV_CHO: [ParamSpec; 16] =
    [REV_TIME, INI, HI, DIFF, DENS, HPF, LPF, BAL, FREQ, AM_DEPTH, PM_DEPTH, MDLY, WAVE2, SYNC, NOTE, TEMPO];
static REV_FLG: [ParamSpec; 16] =
    [REV_TIME, INI, HI, DIFF, DENS, HPF, LPF, BAL, FREQ, DEPTH, MDLY, FBG, WAVE2, SYNC, NOTE, TEMPO];
static REV_SYM: [ParamSpec; 15] = [REV_TIME, INI, HI, DIFF, DENS, HPF, LPF, BAL, FREQ, DEPTH, MDLY, WAVE2, SYNC, NOTE, TEMPO];
static REV_PAN: [ParamSpec; 15] = [REV_TIME, INI, HI, DIFF, DENS, HPF, LPF, BAL, FREQ, DEPTH, DIR, WAVE3, SYNC, NOTE, TEMPO];
static DLY_ER: [ParamSpec; 20] = [
    lin("dl", "DELAY L", "ms", 0.0, 1000.0, 10001, 250.0),
    lin("dr", "DELAY R", "ms", 0.0, 1000.0, 10001, 375.0),
    lin("fbd", "FB.DLY", "ms", 0.0, 1000.0, 10001, 500.0),
    FBG,
    HI,
    HPF,
    LPF,
    BAL,
    ER_TYPE,
    SIZE,
    LIVE,
    INI,
    DIFF,
    DENS,
    ERN,
    SYNC,
    note("notel", "NOTE L"),
    note("noter", "NOTE R"),
    note("notef", "NOTE FB"),
    TEMPO,
];
static DLY_REV: [ParamSpec; 18] = [
    lin("dl", "DELAY L", "ms", 0.0, 1000.0, 10001, 250.0),
    lin("dr", "DELAY R", "ms", 0.0, 1000.0, 10001, 375.0),
    lin("fbd", "FB.DLY", "ms", 0.0, 1000.0, 10001, 500.0),
    FBG,
    lin("dh", "DELAY HI", "", 0.1, 1.0, 10, 0.7),
    HPF,
    LPF,
    BAL,
    REV_TIME,
    INI,
    lin("hr", "REV HI", "", 0.1, 1.0, 10, 0.7),
    DIFF,
    DENS,
    SYNC,
    note("notel", "NOTE L"),
    note("noter", "NOTE R"),
    note("notef", "NOTE FB"),
    TEMPO,
];
static DIST_DLY: [ParamSpec; 15] = [
    DST,
    DRIVE,
    MASTER,
    TONE,
    NGATE,
    lin("d", "DELAY", "ms", 0.0, 2725.0, 27251, 500.0),
    FBG,
    HI,
    FREQ,
    DEPTH,
    BAL,
    SYNC,
    note("dnote", "DLY.NOTE"),
    note("mnote", "MOD.NOTE"),
    TEMPO,
];
static MULTI: [ParamSpec; 12] = [
    ch("t1", "TYPE 1", &["HPF", "LPF", "BPF"], 0.0),
    ch("t2", "TYPE 2", &["HPF", "LPF", "BPF"], 2.0),
    ch("t3", "TYPE 3", &["HPF", "LPF", "BPF"], 1.0),
    log("f1", "FREQ. 1", "Hz", 28.0, 16000.0, 200.0),
    log("f2", "FREQ. 2", "Hz", 28.0, 16000.0, 1000.0),
    log("f3", "FREQ. 3", "Hz", 28.0, 16000.0, 5000.0),
    lin("l1", "LEVEL 1", "", 0.0, 100.0, 101, 100.0),
    lin("l2", "LEVEL 2", "", 0.0, 100.0, 101, 100.0),
    lin("l3", "LEVEL 3", "", 0.0, 100.0, 101, 100.0),
    lin("r1", "RESO. 1", "", 0.0, 20.0, 21, 5.0),
    lin("r2", "RESO. 2", "", 0.0, 20.0, 21, 5.0),
    lin("r3", "RESO. 3", "", 0.0, 20.0, 21, 5.0),
];
static FREEZE: [ParamSpec; 7] = [
    ch("rec", "REC", &["OFF", "ON"], 0.0),
    ch("ply", "PLAY", &["OFF", "ON"], 0.0),
    lin("st", "START", "ms", 0.0, 5458.3, 54584, 0.0),
    lin("en", "END", "ms", 0.0, 5458.3, 54584, 5458.3),
    lin("lop", "LOOP", "ms", 0.0, 5458.3, 54584, 0.0),
    lin("num", "LOOP NUM", "", 0.0, 100.0, 101, 0.0),
    ch("mode", "PLY MODE", &["MOMENT", "CONTI.", "INPUT"], 1.0),
];
static ST_REV: [ParamSpec; 10] = [
    REV_TIME,
    ch("rty", "REV TYPE", &["Hall", "Room", "Stage", "Plate"], 0.0),
    lin("idly", "INI.DLY", "ms", 0.0, 100.0, 1001, 20.0),
    HI,
    LO,
    DIFF,
    DENS,
    lin("erb", "E/R BAL.", "%", 0.0, 100.0, 101, 50.0),
    HPF,
    LPF,
];

const fn release(id: &'static str, label: &'static str, ms: f32) -> ParamSpec {
    ParamSpec {
        id,
        label,
        unit: "ms",
        kind: ParamKind::Values(&RELEASES),
        min: 0.0,
        max: 159.0,
        steps: 160,
        default: nearest(&RELEASES, ms),
    }
}

/// The band gains' ratio: 1:1 to 20:1, the compressors' table (the manual gives
/// the ends only).
static MBAND_RATIOS: [f32; 15] = COMPANDER_RATIOS;

static MBAND: [ParamSpec; 24] = [
    lin("lgn", "LOW GAIN", "dB", -96.0, 12.0, 1081, 0.0),
    lin("mgn", "MID GAIN", "dB", -96.0, 12.0, 1081, 0.0),
    lin("hgn", "HI.GAIN", "dB", -96.0, 12.0, 1081, 0.0),
    lin("prs", "PRESENCE", "", -10.0, 10.0, 21, 0.0),
    lin("cth", "CMP.THRE", "dB", -24.0, 0.0, 241, -12.0),
    values("crt", "CMP.RAT", &MBAND_RATIOS, "", 5),
    lin("cat", "CMP.ATK", "ms", 0.0, 120.0, 121, 10.0),
    release("crl", "CMP.REL", 200.0),
    ch("ckn", "CMP.KNEE", &["0", "1", "2", "3", "4", "5"], 2.0),
    lin("lku", "LOOKUP", "ms", 0.0, 100.0, 1001, 0.0),
    ch("cby", "CMP.BYP", &["OFF", "ON"], 0.0),
    values("xlm", "L-M XOVR", range_of(21.2, 8000.0), "Hz", 53),
    values("xmh", "M-H XOVR", range_of(21.2, 8000.0), "Hz", 77),
    ch("slp", "SLOPE", &["-6 dB", "-12 dB"], 1.0),
    lin("cei", "CEILING", "dB", -6.1, 0.0, 62, -6.1),
    lin("eth", "EXP.THRE", "dB", -54.0, -24.0, 301, -40.0),
    values("ert", "EXP.RAT", &RATIOS, "", 5),
    release("erl", "EXP.REL", 200.0),
    ch("eby", "EXP.BYP", &["OFF", "ON"], 1.0),
    lin("lth", "LIM.THRE", "dB", -12.0, 0.0, 121, -3.0),
    lin("lat", "LIM.ATK", "ms", 0.0, 120.0, 121, 1.0),
    release("lrl", "LIM.REL", 200.0),
    ch("lby", "LIM.BYP", &["OFF", "ON"], 1.0),
    ch("lkn", "LIM.KNEE", &["0", "1", "2", "3", "4", "5"], 2.0),
];

const fn fx(token: &'static str, name: &'static str, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name, category: ModelCategory::FxStandard, params }
}

/// The DM2000's FX table: an empty slot, a plugin (the WING's EXT), then its types.
pub static FX_MODELS: [ModelSpec; 46] = [
    crate::wing::FX_MODELS[0],
    crate::wing::FX_MODELS[1],
    fx("DMHALL", "Reverb Hall", &REV),
    fx("DMROOM", "Reverb Room", &REV),
    fx("DMSTAGE", "Reverb Stage", &REV),
    fx("DMPLATE", "Reverb Plate", &REV),
    fx("DMER", "Early Ref.", &ER),
    fx("DMGREV", "Gate Reverb", &GATE),
    fx("DMRGATE", "Reverse Gate", &GATE),
    fx("DMMONO", "Mono Delay", &MONO),
    fx("DMSTDLY", "Stereo Delay", &STEREO),
    fx("DMMODDL", "Mod.Delay", &MOD_DLY),
    fx("DMLCR", "Delay LCR", &LCR),
    fx("DMECHO", "Echo", &ECHO),
    fx("DMCHO", "Chorus", &CHORUS),
    fx("DMFLG", "Flange", &FLANGE),
    fx("DMSYMP", "Symphonic", &SYMPHONIC),
    fx("DMPHAS", "Phaser", &PHASER),
    fx("DMAPAN", "Auto Pan", &AUTOPAN),
    fx("DMTREM", "Tremolo", &TREMOLO),
    fx("DMHQP", "HQ.Pitch", &HQ_PITCH),
    fx("DMDUALP", "Dual Pitch", &DUAL_PITCH),
    fx("DMROT", "Rotary", &ROTARY),
    fx("DMRING", "Ring Mod.", &RING),
    fx("DMMODF", "Mod.Filter", &MOD_FILTER),
    fx("DMDIST", "Distortion", &DISTORTION),
    fx("DMAMP", "Amp Simulate", &AMP),
    fx("DMDYNF", "Dyna.Filter", &DYNA_FILTER),
    fx("DMDYNFL", "Dyna.Flange", &DYNA_FLANGE),
    fx("DMDYNPH", "Dyna.Phaser", &DYNA_PHASER),
    fx("DMRPCHO", "Rev+Chorus", &REV_CHO),
    fx("DMRSCHO", "Rev->Chorus", &REV_CHO),
    fx("DMRPFLG", "Rev+Flange", &REV_FLG),
    fx("DMRSFLG", "Rev->Flange", &REV_FLG),
    fx("DMRPSYM", "Rev+Sympho.", &REV_SYM),
    fx("DMRSSYM", "Rev->Sympho.", &REV_SYM),
    fx("DMRSPAN", "Rev->Pan", &REV_PAN),
    fx("DMDPER", "Delay+ER.", &DLY_ER),
    fx("DMDSER", "Delay->ER.", &DLY_ER),
    fx("DMDPREV", "Delay+Rev", &DLY_REV),
    fx("DMDSREV", "Delay->Rev", &DLY_REV),
    fx("DMDSTDL", "Dist->Delay", &DIST_DLY),
    fx("DMMULTI", "Multi.Filter", &MULTI),
    fx("DMFRZ", "Freeze", &FREEZE),
    fx("DMSTREV", "Stereo Reverb", &ST_REV),
    fx("DMMBAND", "M.Band Dyna.", &MBAND),
];

/// How a type runs on the 01V's device.
struct Def {
    token: &'static str,
    o1: &'static str,
    /// Runs the wrapper's HPF and LPF (the 01V's delays have none).
    filters: bool,
    /// BALANCE reads the other way round here (the manual's parallel types: 0% all
    /// of the first part).
    invert_balance: bool,
    /// NOTE ids and the delay or FREQ. id each sets when SYNC is on.
    sync: &'static [(&'static str, &'static str)],
}

const fn def(
    token: &'static str,
    o1: &'static str,
    filters: bool,
    invert_balance: bool,
    sync: &'static [(&'static str, &'static str)],
) -> Def {
    Def { token, o1, filters, invert_balance, sync }
}

const MF: &[(&str, &str)] = &[("note", "mf")];
const LRF: &[(&str, &str)] = &[("notel", "dl"), ("noter", "dr"), ("notef", "fbd")];

static DEFS: [Def; 42] = [
    def("DMHALL", "O1HALL", false, false, &[]),
    def("DMROOM", "O1ROOM", false, false, &[]),
    def("DMSTAGE", "O1STAGE", false, false, &[]),
    def("DMPLATE", "O1PLATE", false, false, &[]),
    def("DMER", "O1ER", false, false, &[]),
    def("DMGREV", "O1GATE", false, false, &[]),
    def("DMRGATE", "O1REVG", false, false, &[]),
    def("DMMONO", "O1MONO", true, false, &[("note", "d")]),
    def("DMSTDLY", "O1STDLY", true, false, &[("notel", "dl"), ("noter", "dr")]),
    def("DMMODDL", "O1MODDL", true, false, &[("dnote", "d"), ("mnote", "mf")]),
    def("DMLCR", "O1LCR", true, false, &[("notel", "dl"), ("notec", "dc"), ("noter", "dr"), ("notef", "fbd")]),
    def("DMECHO", "O1ECHO", true, false, &[("notel", "dl"), ("noter", "dr"), ("notefl", "fdl"), ("notefr", "fdr")]),
    def("DMCHO", "O1CHO", false, false, MF),
    def("DMFLG", "O1FLG", false, false, MF),
    def("DMSYMP", "O1SYMP", false, false, MF),
    def("DMPHAS", "O1PHAS", false, false, MF),
    def("DMAPAN", "O1APAN", false, false, MF),
    def("DMTREM", "O1TREM", false, false, MF),
    def("DMHQP", "O1HQP", false, false, &[("note", "d")]),
    def("DMDUALP", "O1DUALP", false, false, &[("notel", "dl"), ("noter", "dr")]),
    def("DMROT", "O1ROT", false, false, &[]),
    def("DMRING", "O1RING", false, false, &[("fnote", "fmf")]),
    def("DMMODF", "O1MODF", false, false, MF),
    def("DMDIST", "O1DIST", false, false, &[]),
    def("DMAMP", "O1AMP", false, false, &[]),
    def("DMDYNF", "O1DYNF", false, false, &[]),
    def("DMDYNFL", "O1DYNFL", false, false, &[]),
    def("DMDYNPH", "O1DYNPH", false, false, &[]),
    def("DMRPCHO", "O1RPCHO", false, true, MF),
    def("DMRSCHO", "O1RSCHO", false, false, MF),
    def("DMRPFLG", "O1RPFLG", false, true, MF),
    def("DMRSFLG", "O1RSFLG", false, false, MF),
    def("DMRPSYM", "O1RPSYM", false, true, MF),
    def("DMRSSYM", "O1RSSYM", false, false, MF),
    def("DMRSPAN", "O1RSPAN", false, false, MF),
    def("DMDPER", "O1DPER", false, true, LRF),
    def("DMDSER", "O1DSER", false, false, LRF),
    def("DMDPREV", "O1DPREV", false, true, LRF),
    def("DMDSREV", "O1DSREV", false, false, LRF),
    def("DMDSTDL", "O1DSTDL", false, false, &[("dnote", "d"), ("mnote", "mf")]),
    def("DMMULTI", "O1MULTI", false, false, &[]),
    def("DMFRZ", "O1FRZ", false, false, &[]),
];

/// Whether `token` is one of the DM2000's effect types.
pub fn is_dm2000(token: &str) -> bool {
    token.starts_with("DM") && FX_MODELS.iter().any(|m| m.token == token)
}

/// The filters after a type's device: at most HPF, LPF, LSH, EQ and HSH.
struct Post {
    sections: [Biquad; 5],
    live: usize,
    sr: f64,
}

impl Post {
    fn new(sr: f32) -> Self {
        Self { sections: Default::default(), live: 0, sr: sr as f64 }
    }

    fn clear(&mut self) {
        self.live = 0;
    }

    fn push(&mut self, c: BiquadCoeffs) {
        if self.live < self.sections.len() {
            self.sections[self.live].set_coefficients(c);
            self.live += 1;
        }
    }

    fn set(&mut self, p: &Params, filters: bool) {
        self.clear();
        let sr = self.sr;
        let (hpf, lpf) = (p.f("hpf"), p.f("lpf"));
        if filters && p.spec.param_index("hpf").is_some() && hpf > 20.5 {
            self.push(BiquadCoeffs::high_pass(sr, hpf as f64, std::f64::consts::FRAC_1_SQRT_2));
        }
        if filters && p.spec.param_index("lpf").is_some() && lpf < 19000.0 {
            self.push(BiquadCoeffs::low_pass(sr, lpf as f64, std::f64::consts::FRAC_1_SQRT_2));
        }
        if p.spec.param_index("lsf").is_some() {
            let g = p.f("lsg") as f64;
            if g.abs() > 1e-3 {
                self.push(BiquadCoeffs::low_shelf(sr, p.f("lsf") as f64, 0.707, g));
            }
        }
        if p.spec.param_index("eqf").is_some() && p.spec.param_index("eqq").is_some() {
            let g = p.f("eqg") as f64;
            if g.abs() > 1e-3 {
                self.push(BiquadCoeffs::peak(sr, p.f("eqf") as f64, p.f("eqq") as f64, g));
            }
        }
        if p.spec.param_index("hsf").is_some() {
            let g = p.f("hsg") as f64;
            if g.abs() > 1e-3 {
                self.push(BiquadCoeffs::high_shelf(sr, p.f("hsf") as f64, 0.707, g));
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for s in &mut self.sections[..self.live] {
            s.process(left, 0);
            s.process(right, 1);
        }
    }

    fn reset(&mut self) {
        for s in &mut self.sections {
            s.reset();
        }
    }
}

/// The reverbs' gate (p.310: GATE LVL, ATTACK, HOLD, DECAY): the input's level
/// opens it past GATE LVL and it closes after HOLD, DECAY long (Sidecar's
/// reading of how).
#[derive(Default)]
struct OutGate {
    level: f32,
    attack: f32,
    hold: usize,
    decay: f32,
    env: f32,
    counter: usize,
    on: bool,
}

impl OutGate {
    fn set(&mut self, p: &Params, sr: f32) {
        if p.spec.param_index("gl").is_none() {
            self.on = false;
            return;
        }
        let lvl = p.f("gl");
        self.on = lvl > -60.5;
        self.level = db_to_gain(lvl);
        self.attack = (-1.0 / (0.001 * p.f("att").max(0.05) * sr)).exp();
        self.hold = (p.f("hld") * 0.001 * sr) as usize;
        self.decay = (-1.0 / (0.001 * p.f("dcy").max(1.0) * sr)).exp();
    }

    fn process(&mut self, trigger: f32, left: &mut [f32], right: &mut [f32]) {
        if !self.on {
            return;
        }
        let open = trigger >= self.level;
        if open {
            self.counter = self.hold;
        }
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            if open {
                self.env = 1.0 - (1.0 - self.env) * self.attack;
            } else if self.counter > 0 {
                self.counter -= 1;
            } else {
                self.env *= self.decay;
            }
            *l *= self.env;
            *r *= self.env;
        }
    }
}

/// An effect type on the 01V's device, with what the DM2000 adds.
struct Voice {
    def: &'static Def,
    inner: Box<dyn FxAlgo>,
    post: Post,
    gate: OutGate,
    am_depth: f32,
    am_inc: f32,
    am_phase: f32,
    sr: f32,
}

impl FxAlgo for Voice {
    fn set_params(&mut self, p: &Params) {
        let mut v = [0.0; MAX_FX_PARAMS];
        let n = p.values.len().min(MAX_FX_PARAMS);
        v[..n].copy_from_slice(&p.values[..n]);
        if p.on("sync") {
            let tempo = p.f("tmp").max(1.0);
            for &(note, target) in self.def.sync {
                if let Some(i) = p.spec.param_index(target) {
                    let beats = NOTES[p.choice(note).min(NOTES.len() - 1)];
                    let time = beats * 4.0 * 60.0 / tempo;
                    // p.331's equation for FREQ. and DELAY alike; a delay's in ms.
                    let x = if matches!(target, "mf" | "fmf") { time } else { time * 1000.0 };
                    v[i] = p.spec.params[i].sanitise(x);
                }
            }
        }
        if self.def.invert_balance {
            if let Some(i) = p.spec.param_index("bal") {
                v[i] = 100.0 - v[i];
            }
        }
        let inner = Params::new(p.spec, &v[..n]);
        self.inner.set_params(&inner);
        self.post.set(&inner, self.def.filters);
        self.gate.set(&inner, self.sr);
        self.am_depth = if p.spec.param_index("am").is_some() { 0.01 * p.f("am") } else { 0.0 };
        self.am_inc = inner.f("mf").max(0.0) / self.sr;
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let trigger = left.iter().chain(right.iter()).fold(0.0f32, |m, x| m.max(x.abs()));
        self.inner.process(left, right);
        self.post.process(left, right);
        if self.gate.on {
            self.gate.process(trigger, left, right);
        }
        if self.am_depth > 0.0 {
            for (l, r) in left.iter_mut().zip(right.iter_mut()) {
                self.am_phase = (self.am_phase + self.am_inc).fract();
                let g = 1.0 - self.am_depth * (0.5 + 0.5 * (TAU * self.am_phase).sin());
                *l *= g;
                *r *= g;
            }
        }
    }

    fn reset(&mut self) {
        self.inner.reset();
        self.post.reset();
        self.gate.env = 0.0;
        self.gate.counter = 0;
        self.am_phase = 0.0;
    }
}

/// ST REVERB (p.326): the WING's reverb of its REV TYPE.
struct StReverb {
    stages: Vec<Stage>,
    kind: usize,
}

impl StReverb {
    fn new(ctx: FxContext) -> Option<Self> {
        let stages =
            vec![Stage::new("HALL", ctx)?, Stage::new("ROOM", ctx)?, Stage::new("CHAMBER", ctx)?, Stage::new("PLATE", ctx)?];
        Some(Self { stages, kind: 0 })
    }
}

impl FxAlgo for StReverb {
    fn set_params(&mut self, p: &Params) {
        self.kind = p.choice("rty").min(3);
        let rt = p.f("rev");
        let s = &mut self.stages[self.kind];
        s.set("pdel", p.f("idly"));
        s.set("dcy", rt);
        s.set("size", 8.0 + 2.0 * rt);
        s.set("damp", damping_hz(p.f("hr")));
        s.set("mult", p.f("lr"));
        s.set("diff", p.f("dif") * 10.0);
        s.set("hc", p.f("lpf"));
        s.set("lc", p.f("hpf"));
        s.apply();
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        self.stages[self.kind].dev.process(left, right);
    }

    fn reset(&mut self) {
        for s in &mut self.stages {
            s.dev.reset();
        }
    }
}

/// One band's envelope and gain stages of M.BAND DYNA.
#[derive(Clone, Copy, Default)]
struct BandEnv {
    comp: f32,
    expand: f32,
    limit: f32,
    gr_db: f32,
}

/// M.BAND DYNA (p.329): three bands split by two crossovers (a low-pass, a
/// high-pass, the mid what's left: the bands add back to the input), each with a
/// compressor, an expander and a limiter, and their gains; a ceiling. The
/// expander's knee, PRESENCE's dB per step and the look-up are Sidecar's reading.
struct MBandDyna {
    sr: f32,
    lo: [Biquad; 2],
    hi: [Biquad; 2],
    // Linear gain of each band after dynamics.
    gain: [f32; 3],
    comp: Comp,
    exp: Exp,
    lim: Lim,
    presence_db: f32,
    env: [BandEnv; 3],
    meter_gr: f32,
    ceil: Option<f32>,
    /// The look-up: a delay of the signal.
    line: [Vec<f32>; 2],
    write: usize,
    delay: usize,
}

#[derive(Default, Clone, Copy)]
struct Comp {
    on: bool,
    thr: f32,
    ratio: f32,
    att: f32,
    rel: f32,
    knee: f32,
}

#[derive(Default, Clone, Copy)]
struct Exp {
    on: bool,
    thr: f32,
    ratio: f32,
    rel: f32,
}

#[derive(Default, Clone, Copy)]
struct Lim {
    on: bool,
    thr: f32,
    att: f32,
    rel: f32,
    knee: f32,
}

/// Gain reduction (dB, >= 0) of a compressor curve at input level `db`.
fn comp_gr(db: f32, thr: f32, ratio: f32, knee: f32) -> f32 {
    let over = db - thr;
    let slope = 1.0 - 1.0 / ratio.max(1.0);
    if knee > 0.0 && over.abs() <= 0.5 * knee {
        slope * (over + 0.5 * knee).powi(2) / (2.0 * knee)
    } else if over > 0.0 {
        slope * over
    } else {
        0.0
    }
}

impl MBandDyna {
    fn new(ctx: FxContext) -> Self {
        let n = (ctx.sample_rate * 0.1).ceil() as usize + 1;
        Self {
            sr: ctx.sample_rate,
            lo: Default::default(),
            hi: Default::default(),
            gain: [1.0; 3],
            comp: Comp::default(),
            exp: Exp::default(),
            lim: Lim::default(),
            presence_db: 0.0,
            env: [BandEnv::default(); 3],
            meter_gr: 0.0,
            ceil: None,
            line: [vec![0.0; n], vec![0.0; n]],
            write: 0,
            delay: 0,
        }
    }

    /// Runs one band's envelope on level `db` (with its PRESENCE shift `shift`
    /// dB on the compressor's threshold).
    fn band_gain(env: &mut BandEnv, db: f32, shift: f32, comp: Comp, exp: Exp, lim: Lim) -> f32 {
        let gr_comp = if comp.on { comp_gr(db, comp.thr + shift, comp.ratio, comp.knee) } else { 0.0 };
        let coef = if gr_comp > env.comp { comp.att } else { comp.rel };
        env.comp = gr_comp + (env.comp - gr_comp) * coef;

        let gr_exp = if exp.on && db < exp.thr { (exp.thr - db) * (exp.ratio.min(30.0) - 1.0) } else { 0.0 };
        let coef = if gr_exp > env.expand { comp.att } else { exp.rel };
        env.expand = gr_exp + (env.expand - gr_exp) * coef;

        let gr_lim = if lim.on { comp_gr(db, lim.thr, 1000.0, lim.knee) } else { 0.0 };
        let coef = if gr_lim > env.limit { lim.att } else { lim.rel };
        env.limit = gr_lim + (env.limit - gr_lim) * coef;
        env.gr_db = env.comp + env.expand + env.limit;
        db_to_gain(-env.gr_db)
    }
}

impl FxAlgo for MBandDyna {
    fn set_params(&mut self, p: &Params) {
        let t = 48000.0 / self.sr;
        let coef = |ms: f32| (-1.0 / (0.001 * ms.max(0.05) * self.sr)).exp();
        self.gain = [db_to_gain(p.f("lgn")), db_to_gain(p.f("mgn")), db_to_gain(p.f("hgn"))];
        self.presence_db = p.f("prs") * 0.5;
        self.comp = Comp {
            on: p.choice("cby") == 0,
            thr: p.f("cth"),
            ratio: p.f("crt"),
            att: coef(p.f("cat")),
            rel: coef(p.f("crl") * t),
            knee: p.choice("ckn") as f32 * 2.0,
        };
        self.exp = Exp { on: p.choice("eby") == 0, thr: p.f("eth"), ratio: p.f("ert"), rel: coef(p.f("erl") * t) };
        self.lim = Lim {
            on: p.choice("lby") == 0,
            thr: p.f("lth"),
            att: coef(p.f("lat")),
            rel: coef(p.f("lrl") * t),
            knee: p.choice("lkn") as f32 * 2.0,
        };
        self.ceil = if p.f("cei") > -6.05 { Some(db_to_gain(p.f("cei"))) } else { None };
        self.delay = ((p.f("lku") * 0.001 * self.sr) as usize).min(self.line[0].len() - 1);
        let sr = self.sr as f64;
        let (lm, mh) = (p.f("xlm") as f64, p.f("xmh") as f64);
        let (lo, hi) = if p.choice("slp") == 0 {
            (BiquadCoeffs::low_pass_1(sr, lm), BiquadCoeffs::high_pass_1(sr, mh))
        } else {
            (
                BiquadCoeffs::low_pass(sr, lm, std::f64::consts::FRAC_1_SQRT_2),
                BiquadCoeffs::high_pass(sr, mh, std::f64::consts::FRAC_1_SQRT_2),
            )
        };
        for b in &mut self.lo {
            b.set_coefficients(lo);
        }
        for b in &mut self.hi {
            b.set_coefficients(hi);
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = self.line[0].len();
        let mut worst = 0.0f32;
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let (dry_l, dry_r) = (*l, *r);
            let read = (self.write + n - self.delay) % n;
            self.line[0][self.write] = dry_l;
            self.line[1][self.write] = dry_r;
            self.write = (self.write + 1) % n;
            let (dl, dr) = (self.line[0][read], self.line[1][read]);
            // The level from the undelayed bands, the gain on the delayed ones.
            let lo = (self.lo[0].tick(dl, 0), self.lo[1].tick(dr, 1));
            let hi = (self.hi[0].tick(dl, 0), self.hi[1].tick(dr, 1));
            let mid = (dl - lo.0 - hi.0, dr - lo.1 - hi.1);
            let bands = [lo, mid, hi];
            let mut sum = (0.0f32, 0.0f32);
            for (i, band) in bands.iter().enumerate() {
                let peak = band.0.abs().max(band.1.abs()).max(1e-6);
                let db = 20.0 * peak.log10();
                // PRESENCE: positive lowers the high band's threshold and raises
                // the low band's (p.329).
                let shift = match i {
                    0 => self.presence_db,
                    2 => -self.presence_db,
                    _ => 0.0,
                };
                let g = Self::band_gain(&mut self.env[i], db, shift, self.comp, self.exp, self.lim) * self.gain[i];
                sum.0 += band.0 * g;
                sum.1 += band.1 * g;
                worst = worst.min(-self.env[i].gr_db);
            }
            if let Some(c) = self.ceil {
                sum.0 = sum.0.clamp(-c, c);
                sum.1 = sum.1.clamp(-c, c);
            }
            *l = sum.0;
            *r = sum.1;
        }
        self.meter_gr = worst;
    }

    fn reset(&mut self) {
        for b in self.lo.iter_mut().chain(self.hi.iter_mut()) {
            b.reset();
        }
        self.env = [BandEnv::default(); 3];
        self.meter_gr = 0.0;
        for l in &mut self.line {
            l.fill(0.0);
        }
    }

    fn meter(&self) -> f32 {
        self.meter_gr.min(0.0)
    }
}

/// Creates the device of DM2000 effect type `token`; `None` for other tokens.
pub fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    if !is_dm2000(token) {
        return None;
    }
    match token {
        "DMSTREV" => return Some(Box::new(StReverb::new(ctx)?)),
        "DMMBAND" => return Some(Box::new(MBandDyna::new(ctx))),
        _ => {}
    }
    let def = DEFS.iter().find(|d| d.token == token)?;
    let inner = crate::y01v::fx::create(def.o1, ctx)?;
    Some(Box::new(Voice {
        def,
        inner,
        post: Post::new(ctx.sample_rate),
        gate: OutGate::default(),
        am_depth: 0.0,
        am_inc: 0.0,
        am_phase: 0.0,
        sr: ctx.sample_rate,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_type_passes_the_fx_checks() {
        // Freeze is silent until something is recorded and played (as the 01V's).
        for m in FX_MODELS[2..].iter().filter(|m| m.token != "DMFRZ") {
            crate::wing::fx::tests::check_device(m.token);
        }
    }
}
