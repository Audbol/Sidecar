//! The 01V's internal effects (Owner's Manual, "Effects Parameters", pp.138-159):
//! its 42 effect types, each a model with the parameters and ranges the manual
//! lists. The algorithms are Sidecar's, on the WING's devices of the same kind
//! and the ProMix 01's delay line (`crate::promix::fx`), with small filters and
//! waveshapers of their own where nothing matches: to be compared with a console.
//!
//! The manual gives ranges, not steps: the steps are Sidecar's reading. Kept on
//! the panel but not modelled: NOTE and TEMPO (the delays and rates are set
//! directly), the reverbs' gate, E/R DLY, E/R BAL and DENSITY, early reflections'
//! TYPE and ER NUM, the delays' HPF and LPF, pitch MODE, SOURCE = MIDI, and the
//! amp's and distortion's named types beyond their drive character.

use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec, Params};
use crate::promix::fx::{damping_hz, Stage, TapMode, Taps, HPF_FREQS, LPF_FREQS};
use crate::wing::fx::{FxAlgo, FxContext};
use std::f32::consts::{PI, TAU};

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

const HPF: ParamSpec = ParamSpec {
    id: "hpf",
    label: "HPF",
    unit: "Hz",
    kind: ParamKind::Values(HPF_FREQS),
    min: 0.0,
    max: HPF_FREQS.len() as f32 - 1.0,
    steps: HPF_FREQS.len() as u32,
    default: 0.0,
};
const LPF: ParamSpec = ParamSpec {
    id: "lpf",
    label: "LPF",
    unit: "Hz",
    kind: ParamKind::Values(LPF_FREQS),
    min: 0.0,
    max: LPF_FREQS.len() as f32 - 1.0,
    steps: LPF_FREQS.len() as u32,
    default: LPF_FREQS.len() as f32 - 1.0,
};

const REV_TIME: ParamSpec = log("rev", "REV TIME", "s", 0.3, 99.9, 2.6);
const INI: ParamSpec = lin("idly", "INI.DLY", "ms", 0.0, 500.0, 5001, 20.0);
const HI: ParamSpec = lin("hr", "HI.RATIO", "", 0.1, 1.0, 10, 0.7);
const LO: ParamSpec = lin("lr", "LO.RATIO", "", 0.1, 2.4, 24, 1.0);
const DIFF: ParamSpec = lin("dif", "DIFF.", "", 0.0, 10.0, 11, 5.0);
const DENS: ParamSpec = lin("den", "DENSITY", "%", 0.0, 100.0, 101, 100.0);
const FBG: ParamSpec = lin("fbg", "FB.GAIN", "%", -99.0, 99.0, 199, 30.0);
const FREQ: ParamSpec = log("mf", "FREQ.", "Hz", 0.05, 40.0, 0.5);
const DEPTH: ParamSpec = lin("md", "DEPTH", "%", 0.0, 100.0, 101, 50.0);
const MDLY: ParamSpec = lin("mdl", "MOD.DLY", "ms", 0.0, 500.0, 5001, 5.0);
const WAVE2: ParamSpec = ch("wv", "WAVE", &["Sine", "Tri"], 0.0);
const WAVE3: ParamSpec = ch("wv", "WAVE", &["Sine", "Tri", "Square"], 0.0);
const DIR: ParamSpec = ch("dir", "DIR.", &["L<->R", "L->R", "L<-R", "Turn L", "Turn R"], 0.0);
const ER_TYPE: ParamSpec = ch("ty", "TYPE", &["S-Hall", "L-Hall", "Random", "Revers", "Plate", "Spring"], 0.0);
const SIZE: ParamSpec = lin("size", "ROOMSIZE", "", 0.1, 20.0, 200, 3.0);
const LIVE: ParamSpec = lin("live", "LIVENESS", "", 0.0, 10.0, 11, 5.0);
const ERN: ParamSpec = lin("ern", "ER NUM.", "", 1.0, 19.0, 19, 10.0);
const BAL: ParamSpec = lin("bal", "BALANCE", "%", 0.0, 100.0, 101, 50.0);
const DL1: ParamSpec = lin("dl", "DELAY L", "ms", 0.0, 1000.0, 10001, 250.0);
const DR1: ParamSpec = lin("dr", "DELAY R", "ms", 0.0, 1000.0, 10001, 375.0);
const FBD1: ParamSpec = lin("fbd", "FB.DLY", "ms", 0.0, 1000.0, 10001, 500.0);
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
const STAGES: ParamSpec = ch("stg", "STAGE", &["2", "4", "8", "10", "12", "14", "16"], 6.0);
const DST: ParamSpec = ch("dt", "DST TYPE", &["DST1", "DST2", "OVD1", "OVD2", "CRUNCH"], 0.0);
const DRIVE: ParamSpec = lin("drv", "DRIVE", "", 0.0, 100.0, 101, 50.0);
const MASTER: ParamSpec = lin("mst", "MASTER", "", 0.0, 100.0, 101, 50.0);
const TONE: ParamSpec = lin("ton", "TONE", "", -10.0, 10.0, 21, 0.0);
const NGATE: ParamSpec = lin("ng", "N.GATE", "", 0.0, 20.0, 21, 0.0);

static REV: [ParamSpec; 14] = [
    REV_TIME,
    INI,
    HI,
    LO,
    DIFF,
    DENS,
    lin("erd", "E/R DLY", "ms", 0.0, 100.0, 1001, 10.0),
    lin("erb", "E/R BAL.", "%", 0.0, 100.0, 101, 50.0),
    lin("gl", "GATE LVL", "dB", -61.0, 0.0, 62, -61.0),
    lin("att", "ATTACK", "ms", 0.0, 120.0, 121, 0.0),
    log("hld", "HOLD", "ms", 0.02, 1960.0, 10.0),
    DECAY,
    HPF,
    LPF,
];
static ER: [ParamSpec; 11] = [ER_TYPE, SIZE, LIVE, INI, DIFF, DENS, ERN, FBG, HI, HPF, LPF];
static GATE: [ParamSpec; 11] =
    [ch("ty", "TYPE", &["Type-A", "Type-B"], 0.0), SIZE, LIVE, INI, DIFF, DENS, HI, ERN, FBG, HPF, LPF];
static MONO: [ParamSpec; 5] = [lin("d", "DELAY", "ms", 0.0, 2730.0, 27301, 500.0), FBG, HI, HPF, LPF];
static STEREO: [ParamSpec; 7] = [
    lin("dl", "DELAY L", "ms", 0.0, 1350.0, 13501, 250.0),
    FGL,
    lin("dr", "DELAY R", "ms", 0.0, 1350.0, 13501, 375.0),
    FGR,
    HI,
    HPF,
    LPF,
];
static MOD_DLY: [ParamSpec; 7] = [lin("d", "DELAY", "ms", 0.0, 2725.0, 27251, 500.0), FBG, FREQ, DEPTH, HI, HPF, LPF];
static LCR: [ParamSpec; 11] = [
    lin("dl", "DELAY L", "ms", 0.0, 2730.0, 27301, 250.0),
    lin("ll", "LEVEL L", "%", -100.0, 100.0, 201, 100.0),
    lin("dc", "DELAY C", "ms", 0.0, 2730.0, 27301, 500.0),
    lin("lc", "LEVEL C", "%", -100.0, 100.0, 201, 100.0),
    lin("dr", "DELAY R", "ms", 0.0, 2730.0, 27301, 375.0),
    lin("lvr", "LEVEL R", "%", -100.0, 100.0, 201, 100.0),
    lin("fbd", "FB.DLY", "ms", 0.0, 2730.0, 27301, 500.0),
    FBG,
    HI,
    HPF,
    LPF,
];
static ECHO: [ParamSpec; 11] = [
    lin("dl", "DELAY L", "ms", 0.0, 1350.0, 13501, 250.0),
    FGL,
    lin("dr", "DELAY R", "ms", 0.0, 1350.0, 13501, 375.0),
    FGR,
    lin("fdl", "FB.D L", "ms", 0.0, 1350.0, 13501, 250.0),
    lin("l2r", "L->R FB.G", "%", -99.0, 99.0, 199, 20.0),
    lin("fdr", "FB.D R", "ms", 0.0, 1350.0, 13501, 375.0),
    lin("r2l", "R->L FB.G", "%", -99.0, 99.0, 199, 20.0),
    HI,
    HPF,
    LPF,
];
static CHORUS: [ParamSpec; 4] = [FREQ, DEPTH, MDLY, WAVE2];
static FLANGE: [ParamSpec; 5] = [FREQ, DEPTH, MDLY, FBG, WAVE2];
static PHASER: [ParamSpec; 5] = [FREQ, DEPTH, FBG, OFFS, STAGES];
static AUTOPAN: [ParamSpec; 4] = [FREQ, DEPTH, DIR, WAVE3];
static TREMOLO: [ParamSpec; 3] = [FREQ, DEPTH, WAVE3];
static HQ_PITCH: [ParamSpec; 5] = [
    lin("pit", "PITCH", "st", -12.0, 12.0, 25, 0.0),
    lin("fin", "FINE", "ct", -50.0, 50.0, 101, 0.0),
    lin("mode", "MODE", "", 1.0, 10.0, 10, 5.0),
    lin("d", "DELAY", "ms", 0.0, 1000.0, 10001, 0.0),
    FBG,
];
static DUAL_PITCH: [ParamSpec; 11] = [
    lin("ptl", "PITCH L", "st", -24.0, 24.0, 49, -1.0),
    lin("fnl", "FINE L", "ct", -50.0, 50.0, 101, 0.0),
    lin("ll", "LEVEL L", "%", -100.0, 100.0, 201, 100.0),
    lin("ptr", "PITCH R", "st", -24.0, 24.0, 49, 1.0),
    lin("fnr", "FINE R", "ct", -50.0, 50.0, 101, 0.0),
    lin("lvr", "LEVEL R", "%", -100.0, 100.0, 201, 100.0),
    DL1,
    FGL,
    DR1,
    FGR,
    lin("mode", "MODE", "", 1.0, 10.0, 10, 5.0),
];
static ROTARY: [ParamSpec; 8] = [
    ch("rot", "ROTATE", &["STOP", "START"], 1.0),
    ch("spd", "SPEED", &["SLOW", "FAST"], 0.0),
    lin("drv", "DRIVE", "", 0.0, 100.0, 101, 0.0),
    lin("acc", "ACCEL", "", 0.0, 10.0, 11, 5.0),
    lin("lo", "LOW", "", 0.0, 100.0, 101, 50.0),
    lin("hi", "HIGH", "", 0.0, 100.0, 101, 50.0),
    log("slo", "SLOW", "Hz", 0.05, 10.0, 0.8),
    log("fst", "FAST", "Hz", 0.05, 10.0, 6.0),
];
static RING: [ParamSpec; 4] = [
    ch("src", "SOURCE", &["OSC", "SELF"], 0.0),
    lin("osc", "OSC FREQ", "Hz", 0.0, 3000.0, 30001, 440.0),
    log("fmf", "FM FREQ", "Hz", 0.05, 40.0, 1.0),
    lin("fmd", "FM DEPTH", "%", 0.0, 100.0, 101, 0.0),
];
static MOD_FILTER: [ParamSpec; 7] = [FREQ, DEPTH, FTYPE, OFFS, RESO, lin("ph", "PHASE", "deg", 0.0, 354.38, 64, 0.0), LEVEL];
static DISTORTION: [ParamSpec; 5] = [DST, DRIVE, MASTER, TONE, NGATE];
static AMP: [ParamSpec; 12] = [
    ch("at", "AMP TYPE", &["STK-M1", "STK-M2", "THRASH", "MIDBST", "CMB-PG", "CMB-VR", "CMB-DX", "CMB-TW", "MINI", "FLAT"], 0.0),
    DST,
    NGATE,
    DRIVE,
    MASTER,
    lin("cab", "CAB DEP", "%", 0.0, 100.0, 101, 100.0),
    lin("bas", "BASS", "", 0.0, 100.0, 101, 50.0),
    lin("mid", "MIDDLE", "", 0.0, 100.0, 101, 50.0),
    lin("tre", "TREBLE", "", 0.0, 100.0, 101, 50.0),
    log("eqf", "EQ F", "Hz", 99.0, 8000.0, 1000.0),
    lin("eqg", "EQ G", "dB", -12.0, 12.0, 49, 0.0),
    log("eqq", "EQ Q", "", 0.1, 10.0, 1.0),
];
static DYNA_FILTER: [ParamSpec; 8] = [SRC, SENSE, FTYPE, OFFS, RESO, LEVEL, UPDN, DECAY];
static DYNA_FLANGE: [ParamSpec; 6] = [SRC, SENSE, FBG, OFFS, UPDN, DECAY];
static DYNA_PHASER: [ParamSpec; 7] = [SRC, SENSE, FBG, OFFS, UPDN, STAGES, DECAY];
static REV_CHO: [ParamSpec; 11] = [REV_TIME, INI, HI, DENS, FREQ, DEPTH, MDLY, WAVE2, BAL, HPF, LPF];
static REV_FLG: [ParamSpec; 12] = [REV_TIME, INI, HI, DENS, FREQ, DEPTH, MDLY, FBG, WAVE2, BAL, HPF, LPF];
static REV_PAN: [ParamSpec; 11] = [REV_TIME, INI, HI, DENS, FREQ, DEPTH, DIR, WAVE3, BAL, HPF, LPF];
static DLY_ER: [ParamSpec; 12] = [DL1, FBG, DR1, HI, FBD1, BAL, ER_TYPE, SIZE, LIVE, INI, DENS, ERN];
static DLY_REV: [ParamSpec; 12] = [
    DL1,
    FBG,
    DR1,
    lin("dh", "DLY HI", "", 0.1, 1.0, 10, 0.7),
    FBD1,
    BAL,
    REV_TIME,
    INI,
    lin("hr", "REV HI", "", 0.1, 1.0, 10, 0.7),
    DENS,
    HPF,
    LPF,
];
static DIST_DLY: [ParamSpec; 11] =
    [DST, DRIVE, MASTER, TONE, NGATE, BAL, lin("d", "DELAY", "ms", 0.0, 2725.0, 27251, 500.0), FBG, HI, FREQ, DEPTH];
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
    lin("st", "START", "ms", 0.0, 2970.0, 2971, 0.0),
    lin("en", "END", "ms", 0.0, 2970.0, 2971, 2970.0),
    lin("lop", "LOOP", "ms", 0.0, 2970.0, 2971, 0.0),
    lin("num", "LOOP NUM", "", 0.0, 100.0, 101, 0.0),
    ch("mode", "PLY MODE", &["MOMENT", "CONTI."], 1.0),
];

const fn fx(token: &'static str, name: &'static str, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name, category: ModelCategory::FxStandard, params }
}

/// PITCH and FREEZE types run only on Effect 2 (Owner's Manual p.119):
/// premium devices, whose slots are `MODELS`' Effect 2.
const fn pitch(token: &'static str, name: &'static str, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name, category: ModelCategory::FxPremium, params }
}

/// The 01V's FX table: an empty slot, a plugin (the WING's EXT), then its 42 types.
pub static FX_MODELS: [ModelSpec; 44] = [
    crate::wing::FX_MODELS[0],
    crate::wing::FX_MODELS[1],
    fx("O1HALL", "Reverb Hall", &REV),
    fx("O1ROOM", "Reverb Room", &REV),
    fx("O1STAGE", "Reverb Stage", &REV),
    fx("O1PLATE", "Reverb Plate", &REV),
    fx("O1ER", "Early Ref.", &ER),
    fx("O1GATE", "Gate Reverb", &GATE),
    fx("O1REVG", "Reverse Gate", &GATE),
    fx("O1MONO", "Mono Delay", &MONO),
    fx("O1STDLY", "Stereo Delay", &STEREO),
    fx("O1MODDL", "Mod.Delay", &MOD_DLY),
    fx("O1LCR", "Delay LCR", &LCR),
    fx("O1ECHO", "Echo", &ECHO),
    fx("O1CHO", "Chorus", &CHORUS),
    fx("O1FLG", "Flange", &FLANGE),
    fx("O1SYMP", "Symphonic", &CHORUS),
    fx("O1PHAS", "Phaser", &PHASER),
    fx("O1APAN", "Auto Pan", &AUTOPAN),
    fx("O1TREM", "Tremolo", &TREMOLO),
    pitch("O1HQP", "HQ.Pitch", &HQ_PITCH),
    pitch("O1DUALP", "Dual Pitch", &DUAL_PITCH),
    fx("O1ROT", "Rotary", &ROTARY),
    fx("O1RING", "Ring Mod.", &RING),
    fx("O1MODF", "Mod.Filter", &MOD_FILTER),
    fx("O1DIST", "Distortion", &DISTORTION),
    fx("O1AMP", "Amp Simulate", &AMP),
    fx("O1DYNF", "Dyna.Filter", &DYNA_FILTER),
    fx("O1DYNFL", "Dyna.Flange", &DYNA_FLANGE),
    fx("O1DYNPH", "Dyna.Phaser", &DYNA_PHASER),
    fx("O1RPCHO", "Rev+Chorus", &REV_CHO),
    fx("O1RSCHO", "Rev->Chorus", &REV_CHO),
    fx("O1RPFLG", "Rev+Flange", &REV_FLG),
    fx("O1RSFLG", "Rev->Flange", &REV_FLG),
    fx("O1RPSYM", "Rev+Sympho.", &REV_CHO),
    fx("O1RSSYM", "Rev->Sympho.", &REV_CHO),
    fx("O1RSPAN", "Rev->Pan", &REV_PAN),
    fx("O1DPER", "Delay+ER.", &DLY_ER),
    fx("O1DSER", "Delay->ER.", &DLY_ER),
    fx("O1DPREV", "Delay+Rev", &DLY_REV),
    fx("O1DSREV", "Delay->Rev", &DLY_REV),
    fx("O1DSTDL", "Dist->Delay", &DIST_DLY),
    fx("O1MULTI", "Multi.Filter", &MULTI),
    pitch("O1FRZ", "Freeze", &FREEZE),
];

/// Whether `token` is one of the 01V's effect types.
pub fn is_01v(token: &str) -> bool {
    token.starts_with("O1") && FX_MODELS.iter().any(|m| m.token == token)
}

/// A state-variable filter (Zavalishin's topology-preserving form).
#[derive(Clone, Copy, Default)]
struct Svf {
    s1: f32,
    s2: f32,
}

impl Svf {
    /// (low, band, high) for `x` at frequency coefficient `g` and damping `k`.
    fn run(&mut self, x: f32, g: f32, k: f32) -> (f32, f32, f32) {
        let hp = (x - (k + g) * self.s1 - self.s2) / (1.0 + g * (k + g));
        let v1 = g * hp;
        let bp = v1 + self.s1;
        self.s1 = bp + v1;
        let v2 = g * bp;
        let lp = v2 + self.s2;
        self.s2 = lp + v2;
        (lp, bp, hp)
    }
}

fn coef(hz: f32, sr: f32) -> f32 {
    (PI * hz.clamp(10.0, 0.45 * sr) / sr).tan()
}

/// RESO. 0-20 as the filter's damping (2: no peak, 0.1: a sharp one).
fn damping(reso: f32) -> f32 {
    2.0 * (0.05f32).powf(reso.clamp(0.0, 20.0) / 20.0)
}

/// OFFSET 0-100 as a frequency, 40 Hz to 12 kHz.
fn offset_hz(offset: f32) -> f32 {
    40.0 * 300f32.powf(offset.clamp(0.0, 100.0) * 0.01)
}

fn pick(kind: usize, (lp, bp, hp): (f32, f32, f32)) -> f32 {
    match kind {
        0 => lp,
        1 => hp,
        _ => bp,
    }
}

fn wave(kind: f32, phase: f32) -> f32 {
    let p = phase.rem_euclid(1.0);
    match kind as usize {
        0 => (TAU * p).sin(),
        1 => 1.0 - 4.0 * (p - 0.5).abs(),
        _ => {
            if p < 0.5 {
                1.0
            } else {
                -1.0
            }
        }
    }
}

/// A soft-clipping stage: the five DST TYPEs as a drive range and a hardness.
#[derive(Clone, Copy, Default)]
struct Shaper {
    gain: f32,
    hard: f32,
    out: f32,
    gate: f32,
    env: f32,
    tone: Svf,
    tone_g: f32,
    tone_mix: f32,
}

impl Shaper {
    fn set(&mut self, p: &Params, sr: f32) {
        let (range, hard) = match p.text("dt") {
            "DST1" => (40.0, 0.9),
            "DST2" => (60.0, 1.0),
            "OVD1" => (18.0, 0.4),
            "OVD2" => (28.0, 0.55),
            _ => (24.0, 0.7), // CRUNCH
        };
        self.gain = 1.0 + range * (p.f("drv") * 0.01).powi(2);
        self.hard = hard;
        self.out = (p.f("mst") * 0.01).powi(2) / self.gain.sqrt().max(1.0) * 2.0;
        self.gate = if p.spec.param_index("ng").is_some() { 0.0002 * p.f("ng").powi(2) } else { 0.0 };
        let tone = if p.spec.param_index("ton").is_some() { p.f("ton") } else { 0.0 };
        self.tone_g = coef(2500.0, sr);
        self.tone_mix = (tone + 10.0) * 0.05;
    }

    fn run(&mut self, x: f32) -> f32 {
        self.env = x.abs().max(self.env * 0.9995);
        if self.env < self.gate {
            return 0.0;
        }
        let d = x * self.gain;
        let y = (1.0 - self.hard) * (d / (1.0 + d.abs())) + self.hard * d.clamp(-1.0, 1.0);
        let (lp, _, hp) = self.tone.run(y, self.tone_g, 1.4);
        (lp * (1.0 - self.tone_mix) + hp * self.tone_mix + 0.5 * y) * self.out
    }
}

/// What the WING devices and delay line of a type are, and how they combine.
#[derive(Clone, Copy, PartialEq)]
enum Shape {
    /// The first part, then the second, on all of the signal.
    Series,
    /// The first part, and the second on a copy of the input, by BALANCE.
    Parallel,
    /// The first part, then the second mixed in by BALANCE.
    SeriesBalance,
}

/// The types built from WING devices and the delay line.
struct Chain {
    token: &'static str,
    /// Run in order, after `taps` when `taps_first`.
    stages: Vec<Stage>,
    taps: Option<Taps>,
    taps_first: bool,
    shape: Shape,
    bal: f32,
    dist: Option<[Shaper; 2]>,
    /// DYNA.FLANGE, DYNA.PHASER: the envelope that sweeps the device.
    env: f32,
    env_rel: f32,
    sense: f32,
    down: bool,
    sr: f32,
    a: Vec<f32>,
    b: Vec<f32>,
}

fn wave_index(p: &Params) -> f32 {
    if p.spec.param_index("wv").is_some() {
        p.f("wv")
    } else {
        0.0
    }
}

fn reverb(s: &mut Stage, p: &Params) {
    let rt = p.f("rev");
    s.set("pdel", p.f("idly"));
    s.set("dcy", rt);
    s.set("size", 8.0 + 2.0 * rt);
    s.set("damp", damping_hz(p.f("hr")));
    if p.spec.param_index("lr").is_some() {
        s.set("mult", p.f("lr"));
    }
    if p.spec.param_index("dif").is_some() {
        s.set("diff", p.f("dif") * 10.0);
    }
    s.set("hc", p.f("lpf"));
    s.set("lc", p.f("hpf"));
}

fn early(s: &mut Stage, p: &Params) {
    s.set("pdel", p.f("idly"));
    s.set("size", 2.0 + 4.9 * p.f("size"));
    s.set("dcy", 0.2 + 0.3 * p.f("live"));
    if p.spec.param_index("dif").is_some() {
        s.set("diff", 10.0 * p.f("dif"));
    }
    if p.spec.param_index("hr").is_some() {
        s.set("damp", damping_hz(p.f("hr")));
    }
    if p.spec.param_index("lpf").is_some() {
        s.set("hc", p.f("lpf"));
        s.set("lc", p.f("hpf"));
    }
}

fn chorus(s: &mut Stage, p: &Params, spread: f32) {
    s.set("spd", p.f("mf"));
    s.set("depl", p.f("md"));
    s.set("depr", p.f("md"));
    s.set("dlyl", 5.0 + p.f("mdl"));
    s.set("dlyr", 5.0 + p.f("mdl"));
    s.set("sprd", spread);
    s.set("wave", wave_index(p));
}

fn flange(s: &mut Stage, p: &Params) {
    s.set("spd", p.f("mf"));
    s.set("depl", p.f("md"));
    s.set("depr", p.f("md"));
    s.set("dlyl", p.f("mdl"));
    s.set("dlyr", p.f("mdl"));
    s.set("feed", p.f("fbg"));
    s.set("wave", wave_index(p));
}

fn panner(s: &mut Stage, p: &Params, phase: f32) {
    s.set("spd", p.f("mf"));
    s.set("depth", p.f("md"));
    s.set("phase", phase);
    s.set("wave", wave_index(p));
}

fn pan_phase(p: &Params) -> f32 {
    match p.text("dir") {
        "L<->R" => 180.0,
        "L->R" => 120.0,
        "L<-R" => 240.0,
        "Turn L" => 90.0,
        _ => 270.0, // Turn R
    }
}

impl Chain {
    fn new(token: &'static str, ctx: FxContext) -> Option<Self> {
        let st = |w: &str| Stage::new(w, ctx);
        let taps = |mode| Some(Taps::with_length(mode, ctx, 2.75));
        use Shape::{Parallel, Series, SeriesBalance};
        let (stages, taps, taps_first, shape, dist) = match token {
            "O1HALL" => (vec![st("HALL")?], None, false, Series, false),
            "O1ROOM" => (vec![st("ROOM")?], None, false, Series, false),
            "O1STAGE" => (vec![st("CHAMBER")?], None, false, Series, false),
            "O1PLATE" => (vec![st("PLATE")?], None, false, Series, false),
            "O1ER" => (vec![st("AMBI")?], None, false, Series, false),
            "O1GATE" => (vec![st("GATED")?], None, false, Series, false),
            "O1REVG" => (vec![st("REVERSE")?], None, false, Series, false),
            "O1MONO" => (vec![], taps(TapMode::Mono), false, Series, false),
            "O1STDLY" | "O1ECHO" => (vec![], taps(TapMode::Echo), false, Series, false),
            "O1MODDL" => (vec![st("CHORUS")?], taps(TapMode::Mono), true, Series, false),
            "O1LCR" => (vec![], taps(TapMode::Lcr), false, Series, false),
            "O1CHO" | "O1SYMP" => (vec![st("CHORUS")?], None, false, Series, false),
            "O1FLG" | "O1DYNFL" => (vec![st("FLANGER")?], None, false, Series, false),
            "O1PHAS" | "O1DYNPH" => (vec![st("PHASER")?], None, false, Series, false),
            "O1APAN" | "O1TREM" => (vec![st("PANNER")?], None, false, Series, false),
            "O1HQP" => (vec![st("PITCH")?], taps(TapMode::Mono), false, Series, false),
            "O1DUALP" => (vec![st("PITCH")?, st("PITCH")?], None, false, Parallel, false),
            "O1ROT" => (vec![st("ROTARY")?], None, false, Series, false),
            "O1RPCHO" | "O1RPSYM" => (vec![st("ROOM")?, st("CHORUS")?], None, false, Parallel, false),
            "O1RSCHO" | "O1RSSYM" => (vec![st("ROOM")?, st("CHORUS")?], None, false, SeriesBalance, false),
            "O1RPFLG" => (vec![st("ROOM")?, st("FLANGER")?], None, false, Parallel, false),
            "O1RSFLG" => (vec![st("ROOM")?, st("FLANGER")?], None, false, SeriesBalance, false),
            "O1RSPAN" => (vec![st("ROOM")?, st("PANNER")?], None, false, SeriesBalance, false),
            "O1DPER" => (vec![st("AMBI")?], taps(TapMode::Two), true, Parallel, false),
            "O1DSER" => (vec![st("AMBI")?], taps(TapMode::Two), true, SeriesBalance, false),
            "O1DPREV" => (vec![st("ROOM")?], taps(TapMode::Two), true, Parallel, false),
            "O1DSREV" => (vec![st("ROOM")?], taps(TapMode::Two), true, SeriesBalance, false),
            "O1DSTDL" => (vec![st("CHORUS")?], taps(TapMode::Mono), true, SeriesBalance, true),
            _ => return None,
        };
        let n = ctx.max_block;
        Some(Chain {
            token,
            stages,
            taps,
            taps_first,
            shape,
            bal: 0.5,
            dist: dist.then(<[Shaper; 2]>::default),
            env: 0.0,
            env_rel: 0.999,
            sense: 1.0,
            down: false,
            sr: ctx.sample_rate,
            a: vec![0.0; n],
            b: vec![0.0; n],
        })
    }

    /// Sets the sweep of a dynamic type from the envelope (0-1).
    fn sweep(&mut self) {
        let e = (self.env * self.sense).min(1.0);
        let v = if self.down { 1.0 - e } else { e };
        let s = &mut self.stages[0];
        match self.token {
            "O1DYNFL" => {
                let ms = 0.5 + 6.0 * v;
                s.set("dlyl", ms);
                s.set("dlyr", ms);
                s.apply();
            }
            _ => {
                s.set("range", 100.0 * v);
                s.apply();
            }
        }
    }
}

impl FxAlgo for Chain {
    fn set_params(&mut self, p: &Params) {
        let f = |id: &str| p.f(id);
        if p.spec.param_index("bal").is_some() {
            self.bal = 0.01 * f("bal");
        }
        let token = self.token;
        if let Some(d) = &mut self.dist {
            for s in d.iter_mut() {
                s.set(p, self.sr);
            }
        }
        if let Some(t) = &mut self.taps {
            match token {
                "O1MONO" => t.set([f("d"); 4], 0.0, [f("fbg"); 2], f("hr")),
                "O1MODDL" | "O1DSTDL" => t.set([f("d"); 4], 0.0, [f("fbg"); 2], f("hr")),
                "O1STDLY" => t.set([f("dl"), f("dr"), 1.0, 1.0], 0.0, [f("fgl"), f("fgr")], f("hr")),
                "O1ECHO" => {
                    t.set([f("fdl"), f("fdr"), 1.0, 1.0], 0.0, [f("fgl"), f("fgr")], f("hr"));
                    t.set_cross(0.01 * f("l2r"), 0.01 * f("r2l"));
                }
                "O1LCR" => t.set([f("dl"), f("dr"), f("dc"), f("fbd")], 0.01 * f("lc"), [f("fbg"); 2], f("hr")),
                "O1HQP" => t.set([f("d"); 4], 0.0, [f("fbg"); 2], 1.0),
                "O1DPER" | "O1DSER" => t.set([f("dl"), f("dr"), 1.0, f("fbd")], 0.0, [f("fbg"); 2], f("hr")),
                _ => t.set([f("dl"), f("dr"), 1.0, f("fbd")], 0.0, [f("fbg"); 2], f("dh")),
            }
        }
        match token {
            "O1HALL" | "O1ROOM" | "O1STAGE" | "O1PLATE" => reverb(&mut self.stages[0], p),
            "O1ER" | "O1DPER" | "O1DSER" => early(&mut self.stages[0], p),
            "O1GATE" | "O1REVG" => {
                let s = &mut self.stages[0];
                s.set("pdel", f("idly"));
                s.set("dcy", 100.0 + 40.0 * f("size"));
                s.set("diff", 10.0 * f("dif"));
                s.set("lc", f("hpf"));
            }
            "O1DPREV" | "O1DSREV" => reverb(&mut self.stages[0], p),
            "O1CHO" => chorus(&mut self.stages[0], p, 50.0),
            "O1SYMP" => chorus(&mut self.stages[0], p, 100.0),
            "O1MODDL" => {
                let s = &mut self.stages[0];
                s.set("spd", f("mf"));
                s.set("depl", f("md"));
                s.set("depr", f("md"));
                s.set("dlyl", 5.0);
                s.set("dlyr", 5.0);
            }
            "O1FLG" => flange(&mut self.stages[0], p),
            "O1PHAS" => {
                let s = &mut self.stages[0];
                s.set("spd", f("mf"));
                s.set("depth", f("md"));
                s.set("range", f("off"));
                s.set("stg", [2.0, 4.0, 8.0, 10.0, 12.0, 14.0, 16.0][f("stg") as usize]);
                s.set("reso", f("fbg").abs());
            }
            "O1APAN" => panner(&mut self.stages[0], p, pan_phase(p)),
            "O1TREM" => panner(&mut self.stages[0], p, 0.0),
            "O1HQP" => {
                let s = &mut self.stages[0];
                s.set("semi", f("pit"));
                s.set("cent", f("fin"));
                s.set("dly", 0.0);
            }
            "O1DUALP" => {
                let (a, b) = self.stages.split_at_mut(1);
                a[0].set("semi", f("ptl"));
                a[0].set("cent", f("fnl"));
                a[0].set("dly", f("dl"));
                b[0].set("semi", f("ptr"));
                b[0].set("cent", f("fnr"));
                b[0].set("dly", f("dr"));
                self.bal = 0.5 * (1.0 + (f("ll") - f("lvr")) * 0.005);
            }
            "O1ROT" => {
                let s = &mut self.stages[0];
                s.set("sw", if f("rot") < 0.5 { 0.0 } else { 1.0 + f("spd") });
                s.set("lo", if f("spd") > 0.5 { f("fst") } else { f("slo") });
                s.set("hi", if f("spd") > 0.5 { f("fst") } else { f("slo") });
                s.set("dist", f("drv"));
                s.set("dac", f("acc") * 10.0);
                s.set("hac", f("acc") * 10.0);
            }
            "O1DSTDL" => {
                let s = &mut self.stages[0];
                s.set("spd", f("mf"));
                s.set("depl", f("md"));
                s.set("depr", f("md"));
            }
            "O1DYNFL" => {
                let s = &mut self.stages[0];
                s.set("feed", f("fbg"));
                s.set("depl", 0.0);
                s.set("depr", 0.0);
                self.sense = (f("sns") * 0.08).max(0.05);
                self.down = f("dir") > 0.5;
                self.env_rel = (-1.0 / (0.001 * f("dcy") * self.sr)).exp();
            }
            "O1DYNPH" => {
                let s = &mut self.stages[0];
                s.set("depth", 0.0);
                s.set("reso", f("fbg").abs());
                s.set("stg", [2.0, 4.0, 8.0, 10.0, 12.0, 14.0, 16.0][f("stg") as usize]);
                self.sense = (f("sns") * 0.08).max(0.05);
                self.down = f("dir") > 0.5;
                self.env_rel = (-1.0 / (0.001 * f("dcy") * self.sr)).exp();
            }
            // Delay line only: set above.
            "O1MONO" | "O1STDLY" | "O1ECHO" | "O1LCR" => {}
            _ => {
                // REV+ and REV->: the reverb, then the modulation device.
                let (a, b) = self.stages.split_at_mut(1);
                reverb(&mut a[0], p);
                match token {
                    "O1RPCHO" | "O1RSCHO" => chorus(&mut b[0], p, 50.0),
                    "O1RPSYM" | "O1RSSYM" => chorus(&mut b[0], p, 100.0),
                    "O1RPFLG" | "O1RSFLG" => flange(&mut b[0], p),
                    _ => panner(&mut b[0], p, pan_phase(p)),
                }
            }
        }
        for s in &mut self.stages {
            s.apply();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len().min(self.a.len());
        let (left, right) = (&mut left[..n], &mut right[..n]);
        if matches!(self.token, "O1DYNFL" | "O1DYNPH") {
            let mut peak = 0.0f32;
            for (l, r) in left.iter().zip(right.iter()) {
                peak = peak.max(l.abs().max(r.abs()));
            }
            let rel = self.env_rel.powi(n as i32);
            self.env = peak.max(self.env * rel);
            self.sweep();
        }
        if let Some(d) = &mut self.dist {
            for (l, r) in left.iter_mut().zip(right.iter_mut()) {
                *l = d[0].run(*l);
                *r = d[1].run(*r);
            }
        }
        if self.token == "O1DSTDL" {
            // The distortion, then its delayed and modulated copy by DLY BAL.
            self.a[..n].copy_from_slice(left);
            self.b[..n].copy_from_slice(right);
            if let Some(t) = &mut self.taps {
                t.process(&mut self.a[..n], &mut self.b[..n]);
            }
            self.stages[0].dev.process(&mut self.a[..n], &mut self.b[..n]);
            for i in 0..n {
                left[i] += self.bal * (self.a[i] - left[i]);
                right[i] += self.bal * (self.b[i] - right[i]);
            }
            return;
        }
        match self.shape {
            Shape::Series => {
                if self.taps_first {
                    if let Some(t) = &mut self.taps {
                        t.process(left, right);
                    }
                }
                for s in &mut self.stages {
                    s.dev.process(left, right);
                }
                if !self.taps_first {
                    if let Some(t) = &mut self.taps {
                        t.process(left, right);
                    }
                }
            }
            Shape::Parallel => {
                self.a[..n].copy_from_slice(left);
                self.b[..n].copy_from_slice(right);
                // The first path: the delay line, or the first device.
                if let Some(t) = &mut self.taps {
                    t.process(left, right);
                    self.stages[0].dev.process(&mut self.a[..n], &mut self.b[..n]);
                } else {
                    self.stages[0].dev.process(left, right);
                    self.stages[1].dev.process(&mut self.a[..n], &mut self.b[..n]);
                }
                for i in 0..n {
                    left[i] = self.bal * left[i] + (1.0 - self.bal) * self.a[i];
                    right[i] = self.bal * right[i] + (1.0 - self.bal) * self.b[i];
                }
            }
            Shape::SeriesBalance => {
                if let Some(t) = &mut self.taps {
                    t.process(left, right);
                } else {
                    self.stages[0].dev.process(left, right);
                }
                self.a[..n].copy_from_slice(left);
                self.b[..n].copy_from_slice(right);
                let last = self.stages.len() - 1;
                self.stages[last].dev.process(&mut self.a[..n], &mut self.b[..n]);
                for i in 0..n {
                    left[i] = self.bal * left[i] + (1.0 - self.bal) * self.a[i];
                    right[i] = self.bal * right[i] + (1.0 - self.bal) * self.b[i];
                }
            }
        }
    }

    fn reset(&mut self) {
        for s in &mut self.stages {
            s.dev.reset();
        }
        if let Some(t) = &mut self.taps {
            t.reset();
        }
        self.env = 0.0;
        if let Some(d) = &mut self.dist {
            *d = Default::default();
        }
        // The shapers' settings come back with the next parameters.
    }
}

/// The types that are filters, a ring modulator, a waveshaper or a sampler.
struct Small {
    token: &'static str,
    sr: f32,
    filters: [[Svf; 3]; 2],
    g: [f32; 3],
    k: [f32; 3],
    kind: [usize; 3],
    level: [f32; 3],
    rate: f32,
    depth: f32,
    wave: f32,
    offset: f32,
    phase: f32,
    stereo_phase: f32,
    osc: f32,
    self_ring: bool,
    dist: [Shaper; 2],
    cab: f32,
    tone: [f32; 3],
    env: f32,
    env_rel: f32,
    sense: f32,
    down: bool,
    eq: [Svf; 2],
    eq_g: f32,
    eq_k: f32,
    eq_gain: f32,
    // Freeze: 2.97 s of stereo sample memory.
    mem: [Vec<f32>; 2],
    write: usize,
    rec: bool,
    play: bool,
    read: f32,
    start: f32,
    end: f32,
    loop_at: f32,
}

impl Small {
    fn new(token: &'static str, ctx: FxContext) -> Option<Self> {
        if !matches!(token, "O1RING" | "O1MODF" | "O1DIST" | "O1AMP" | "O1DYNF" | "O1MULTI" | "O1FRZ") {
            return None;
        }
        let mem = if token == "O1FRZ" { (ctx.sample_rate * 2.97).ceil() as usize } else { 0 };
        Some(Small {
            token,
            sr: ctx.sample_rate,
            filters: Default::default(),
            g: [0.1; 3],
            k: [1.4; 3],
            kind: [0; 3],
            level: [1.0; 3],
            rate: 1.0,
            depth: 0.0,
            wave: 0.0,
            offset: 1000.0,
            phase: 0.0,
            stereo_phase: 0.0,
            osc: 440.0,
            self_ring: false,
            dist: Default::default(),
            cab: 1.0,
            tone: [1.0; 3],
            env: 0.0,
            env_rel: 0.999,
            sense: 1.0,
            down: false,
            eq: Default::default(),
            eq_g: 0.1,
            eq_k: 1.0,
            eq_gain: 0.0,
            mem: [vec![0.0; mem], vec![0.0; mem]],
            write: 0,
            rec: false,
            play: false,
            read: 0.0,
            start: 0.0,
            end: 1.0,
            loop_at: 0.0,
        })
    }
}

impl FxAlgo for Small {
    fn set_params(&mut self, p: &Params) {
        let f = |id: &str| p.f(id);
        match self.token {
            "O1RING" => {
                self.self_ring = f("src") > 0.5;
                self.osc = f("osc");
                self.rate = f("fmf");
                self.depth = 0.01 * f("fmd");
            }
            "O1MODF" => {
                self.rate = f("mf");
                self.depth = 0.01 * f("md");
                self.kind[0] = f("ty") as usize;
                self.offset = offset_hz(f("off"));
                self.k[0] = damping(f("res"));
                self.stereo_phase = f("ph") / 360.0;
                self.level[0] = 0.01 * f("lev");
            }
            "O1DYNF" => {
                self.sense = (f("sns") * 0.08).max(0.05);
                self.kind[0] = f("ty") as usize;
                self.offset = offset_hz(f("off"));
                self.k[0] = damping(f("res"));
                self.level[0] = 0.01 * f("lev");
                self.down = f("dir") > 0.5;
                self.env_rel = (-1.0 / (0.001 * f("dcy") * self.sr)).exp();
            }
            "O1MULTI" => {
                for (i, (t, fr, l, r)) in
                    [("t1", "f1", "l1", "r1"), ("t2", "f2", "l2", "r2"), ("t3", "f3", "l3", "r3")].iter().enumerate()
                {
                    // The type's order here is HPF, LPF, BPF.
                    self.kind[i] = [1, 0, 2][f(t) as usize];
                    self.g[i] = coef(f(fr), self.sr);
                    self.level[i] = 0.01 * f(l);
                    self.k[i] = damping(f(r));
                }
            }
            "O1DIST" | "O1AMP" => {
                for s in &mut self.dist {
                    s.set(p, self.sr);
                }
                if self.token == "O1AMP" {
                    self.cab = 0.01 * f("cab");
                    self.tone = [f("bas") * 0.02, f("mid") * 0.02, f("tre") * 0.02];
                    self.eq_g = coef(f("eqf"), self.sr);
                    self.eq_k = 1.0 / f("eqq").max(0.1);
                    self.eq_gain = 10f32.powf(f("eqg") / 20.0) - 1.0;
                    self.g[1] = coef(5000.0, self.sr);
                    self.g[2] = coef(120.0, self.sr);
                }
            }
            _ => {
                let was = self.rec;
                self.rec = f("rec") > 0.5;
                if self.rec && !was {
                    self.write = 0;
                }
                let playing = f("ply") > 0.5;
                if playing && !self.play {
                    self.read = f("st") * 0.001 * self.sr;
                }
                self.play = playing;
                self.start = f("st") * 0.001 * self.sr;
                self.end = (f("en") * 0.001 * self.sr).max(self.start + 1.0);
                self.loop_at = f("lop") * 0.001 * self.sr;
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        match self.token {
            "O1RING" => {
                for (l, r) in left.iter_mut().zip(right.iter_mut()) {
                    if self.self_ring {
                        let m = 0.5 * (*l + *r);
                        *l *= m;
                        *r *= m;
                    } else {
                        let fm = 1.0 + self.depth * (TAU * self.stereo_phase).sin();
                        self.stereo_phase = (self.stereo_phase + self.rate / self.sr).fract();
                        let m = (TAU * self.phase).sin();
                        self.phase = (self.phase + self.osc * fm / self.sr).fract();
                        *l *= m;
                        *r *= m;
                    }
                }
            }
            "O1MODF" => {
                let (rate, depth, offset, kind, k, level, sr, w, sp) = (
                    self.rate,
                    self.depth,
                    self.offset,
                    self.kind[0],
                    self.k[0],
                    self.level[0],
                    self.sr,
                    self.wave,
                    self.stereo_phase,
                );
                for (l, r) in left.iter_mut().zip(right.iter_mut()) {
                    self.phase = (self.phase + rate / sr).fract();
                    let hz = |ph: f32| offset * 2f32.powf(3.0 * depth * wave(w, ph));
                    *l = level * pick(kind, self.filters[0][0].run(*l, coef(hz(self.phase), sr), k));
                    *r = level * pick(kind, self.filters[1][0].run(*r, coef(hz(self.phase + sp), sr), k));
                }
            }
            "O1DYNF" => {
                let (offset, kind, k, level, sr, sense, down, rel) =
                    (self.offset, self.kind[0], self.k[0], self.level[0], self.sr, self.sense, self.down, self.env_rel);
                for (l, r) in left.iter_mut().zip(right.iter_mut()) {
                    self.env = (l.abs().max(r.abs())).max(self.env * rel);
                    let e = (self.env * sense).min(1.0) * if down { -1.0 } else { 1.0 };
                    let g = coef(offset * 2f32.powf(3.0 * e), sr);
                    *l = level * pick(kind, self.filters[0][0].run(*l, g, k));
                    *r = level * pick(kind, self.filters[1][0].run(*r, g, k));
                }
            }
            "O1MULTI" => {
                for (l, r) in left.iter_mut().zip(right.iter_mut()) {
                    let (mut a, mut b) = (0.0, 0.0);
                    for i in 0..3 {
                        a += self.level[i] * pick(self.kind[i], self.filters[0][i].run(*l, self.g[i], self.k[i]));
                        b += self.level[i] * pick(self.kind[i], self.filters[1][i].run(*r, self.g[i], self.k[i]));
                    }
                    *l = a;
                    *r = b;
                }
            }
            "O1DIST" => {
                for (l, r) in left.iter_mut().zip(right.iter_mut()) {
                    *l = self.dist[0].run(*l);
                    *r = self.dist[1].run(*r);
                }
            }
            "O1AMP" => {
                let (cab, tone, eg, ek, eg0) = (self.cab, self.tone, self.eq_g, self.eq_k, self.eq_gain);
                let (lp, hp) = (self.g[1], self.g[2]);
                for (ch, s) in [(0, left), (1, right)] {
                    for x in s.iter_mut() {
                        let y = self.dist[ch].run(*x);
                        // BASS, MIDDLE and TREBLE (Sidecar's three bands), then the EQ.
                        let (bl, _, _) = self.filters[ch][0].run(y, hp * 2.0, 1.4);
                        let (_, _, th) = self.filters[ch][1].run(y, lp, 1.4);
                        let m = y - bl - th;
                        let t = tone[0] * bl + tone[1] * m + tone[2] * th;
                        let (_, band, _) = self.eq[ch].run(t, eg, ek);
                        let t = t + eg0 * band;
                        // CAB DEP: a speaker's roll-off, by this much.
                        let (cl, _, _) = self.filters[ch][2].run(t, lp, 1.0);
                        *x = t + cab * (cl - t);
                    }
                }
            }
            _ => {
                let n = self.mem[0].len();
                if n == 0 {
                    return;
                }
                for (l, r) in left.iter_mut().zip(right.iter_mut()) {
                    if self.rec && self.write < n {
                        self.mem[0][self.write] = *l;
                        self.mem[1][self.write] = *r;
                        self.write += 1;
                    }
                    if self.play {
                        let at = (self.read as usize).min(n - 1);
                        *l = self.mem[0][at];
                        *r = self.mem[1][at];
                        self.read += 1.0;
                        if self.read >= self.end.min(n as f32) {
                            self.read = self.loop_at.max(self.start);
                        }
                    } else if !self.rec {
                        *l = 0.0;
                        *r = 0.0;
                    }
                }
            }
        }
    }

    fn reset(&mut self) {
        self.filters = Default::default();
        self.eq = Default::default();
        self.env = 0.0;
        self.phase = 0.0;
        self.read = self.start;
        self.write = 0;
        for m in &mut self.mem {
            m.fill(0.0);
        }
    }
}

/// Creates the device of 01V effect type `token`; `None` for other tokens.
pub fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    let spec = FX_MODELS.iter().find(|m| m.token == token && token.starts_with("O1"))?;
    if let Some(s) = Small::new(spec.token, ctx) {
        return Some(Box::new(s));
    }
    Some(Box::new(Chain::new(spec.token, ctx)?))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wing::MAX_FX_PARAMS;

    fn device(token: &str) -> (Box<dyn FxAlgo>, usize) {
        let ctx = FxContext { sample_rate: 48000.0, max_block: 512 };
        let model = crate::model::model_index(&FX_MODELS, token).unwrap();
        (create(token, ctx).unwrap(), model)
    }

    /// `n` samples of a 440 Hz tone through the type on its defaults, `set` applied.
    fn render(token: &str, set: &[(&str, f32)], n: usize) -> (Vec<f32>, Vec<f32>) {
        let (mut dev, model) = device(token);
        let spec = &FX_MODELS[model];
        let mut values = [0.0; MAX_FX_PARAMS];
        spec.defaults_into(&mut values);
        for (id, v) in set {
            values[spec.param_index(id).unwrap()] = *v;
        }
        dev.set_params(&Params::new(spec, &values));
        let (mut l, mut r) = (vec![0.0f32; n], vec![0.0f32; n]);
        for i in 0..n {
            l[i] = 0.25 * (TAU * 440.0 * i as f32 / 48000.0).sin();
            r[i] = l[i];
        }
        for (a, b) in l.chunks_mut(256).zip(r.chunks_mut(256)) {
            dev.process(a, b);
        }
        (l, r)
    }

    fn rms(x: &[f32]) -> f32 {
        (x.iter().map(|v| v * v).sum::<f32>() / x.len().max(1) as f32).sqrt()
    }

    #[test]
    fn the_table_has_every_type() {
        assert_eq!(FX_MODELS.len(), 44);
        assert_eq!((FX_MODELS[0].token, FX_MODELS[1].token), ("NONE", "EXT"));
        for m in &FX_MODELS[2..] {
            assert!(m.token.starts_with("O1"), "{}", m.token);
            assert!(is_01v(m.token));
            assert!(!m.params.is_empty());
            assert!(m.params.len() <= MAX_FX_PARAMS, "{}", m.token);
            let mut ids: Vec<_> = m.params.iter().map(|p| p.id).collect();
            ids.sort_unstable();
            ids.dedup();
            assert_eq!(ids.len(), m.params.len(), "{} has a repeated id", m.token);
        }
    }

    #[test]
    fn every_type_runs_and_stays_finite() {
        for m in &FX_MODELS[2..] {
            let set: &[(&str, f32)] = if m.token == "O1FRZ" { &[("ply", 0.0), ("rec", 1.0)] } else { &[] };
            let (l, r) = render(m.token, set, 48000);
            assert!(l.iter().chain(r.iter()).all(|v| v.is_finite()), "{}", m.token);
            assert!(l.iter().chain(r.iter()).all(|v| v.abs() < 8.0), "{} blew up", m.token);
            // All but the sampler's silence and a type that hasn't started pass something.
            let tail = &l[l.len() / 2..];
            assert!(rms(tail) > 1.0e-5, "{} gave silence", m.token);
        }
    }

    #[test]
    fn distortion_adds_harmonics_with_drive() {
        let harmonic = |x: &[f32]| {
            // Energy left once the fundamental is taken out, at 3 x 440 Hz.
            let (mut re, mut im) = (0.0, 0.0);
            for (i, v) in x.iter().enumerate() {
                let w = TAU * 1320.0 * i as f32 / 48000.0;
                re += v * w.cos();
                im += v * w.sin();
            }
            (re * re + im * im).sqrt() / x.len() as f32
        };
        let clean = render("O1DIST", &[("drv", 0.0), ("mst", 100.0)], 24000).0;
        let driven = render("O1DIST", &[("drv", 100.0), ("mst", 100.0)], 24000).0;
        assert!(harmonic(&driven[4800..]) > 3.0 * harmonic(&clean[4800..]));
    }

    #[test]
    fn a_ring_modulator_moves_the_tone() {
        let (l, _) = render("O1RING", &[("osc", 1000.0), ("fmd", 0.0)], 24000);
        let at = |hz: f32| {
            let (mut re, mut im) = (0.0, 0.0);
            for (i, v) in l[2400..].iter().enumerate() {
                let w = TAU * hz * i as f32 / 48000.0;
                re += v * w.cos();
                im += v * w.sin();
            }
            (re * re + im * im).sqrt()
        };
        assert!(at(1440.0) > 10.0 * at(440.0), "sum {} vs input {}", at(1440.0), at(440.0));
    }

    #[test]
    fn a_low_pass_multi_filter_cuts_a_tone_above_it() {
        let (l, _) =
            render("O1MULTI", &[("t1", 1.0), ("f1", 100.0), ("l1", 100.0), ("l2", 0.0), ("l3", 0.0), ("r1", 0.0)], 24000);
        let (open, _) =
            render("O1MULTI", &[("t1", 1.0), ("f1", 16000.0), ("l1", 100.0), ("l2", 0.0), ("l3", 0.0), ("r1", 0.0)], 24000);
        assert!(rms(&l[4800..]) < 0.2 * rms(&open[4800..]));
    }

    #[test]
    fn freeze_plays_back_what_it_recorded() {
        let (mut dev, model) = device("O1FRZ");
        let spec = &FX_MODELS[model];
        let mut values = [0.0; MAX_FX_PARAMS];
        spec.defaults_into(&mut values);
        let set = |values: &mut [f32; MAX_FX_PARAMS], id: &str, v: f32| values[spec.param_index(id).unwrap()] = v;
        set(&mut values, "rec", 1.0);
        dev.set_params(&Params::new(spec, &values));
        let (mut l, mut r) = (vec![0.5f32; 4800], vec![0.5f32; 4800]);
        dev.process(&mut l[..256], &mut r[..256]);
        set(&mut values, "rec", 0.0);
        set(&mut values, "en", 2.0);
        set(&mut values, "ply", 1.0);
        dev.set_params(&Params::new(spec, &values));
        let (mut a, mut b) = (vec![0.0f32; 256], vec![0.0f32; 256]);
        dev.process(&mut a, &mut b);
        assert!(a.iter().all(|v| (*v - 0.5).abs() < 1.0e-6), "{:?}", &a[..4]);
    }
}
