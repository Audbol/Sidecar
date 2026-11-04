//! The 02R's internal effects (Phase 6 step 4): the effect types of its 40
//! preset programs (MAN pp.98-111), each a model with the parameters and
//! ranges the guide lists, on the first program of its type's values.
//!
//! The algorithms are Sidecar's, on the ProMix 01's where the types are the
//! same (`crate::promix::fx`: the WING's ROOM for the reverbs, CHORUS,
//! FLANGER, PHASER, PANNER and PITCH, and its delay line `Taps`); early
//! reflections run on the WING's AMBI, the gate and reverse gate reverbs on
//! its GATED and REVERSE; the echoes' L->R and R->L feed one line into the
//! other. The guide gives the ranges but not the steps: 0.1 ms, 0.1 s, 0.05 Hz
//! are Sidecar's reading of the presets' values. Unmodelled: the reverbs'
//! DENSITY, REV DLY and REV BAL, early reflections' TYPE beyond a room size
//! and ER NUM, the delays' LPF, HPF and INI DLY, the modulation types' EQ
//! (LSF, EQ, HSF), auto pan's F/R DEPTH and direction, phasing's stages. All
//! to be compared with a console.

use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec, Params};
use crate::promix::fx::{chorus, damping_hz, reverb, Stage, TapMode, Taps, HPF_FREQS, LPF_FREQS};
use crate::wing::fx::{FxAlgo, FxContext};

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

const fn lin(
    id: &'static str,
    label: &'static str,
    unit: &'static str,
    min: f32,
    max: f32,
    step: f32,
    default: f32,
) -> ParamSpec {
    p(id, label, unit, ParamKind::Linear, min, max, ((max - min) / step + 0.5) as u32 + 1, default)
}

const fn int(id: &'static str, label: &'static str, unit: &'static str, min: f32, max: f32, default: f32) -> ParamSpec {
    p(id, label, unit, ParamKind::Int, min, max, (max - min) as u32 + 1, default)
}

const fn index_of(table: &[f32], v: f32) -> f32 {
    let mut i = 0;
    while i < table.len() {
        if table[i] == v {
            return i as f32;
        }
        i += 1;
    }
    panic!("not in the table");
}

const THRU_LPF: f32 = LPF_FREQS.len() as f32 - 1.0;

const fn rev_time(d: f32) -> ParamSpec {
    lin("rt", "REV TIME", "s", 0.3, 30.0, 0.1, d)
}
const fn high_ratio(d: f32) -> ParamSpec {
    lin("hr", "HI.RATIO", "", 0.1, 1.0, 0.1, d)
}
const fn diffusion(d: f32) -> ParamSpec {
    int("dif", "DIFF.", "", 0.0, 10.0, d)
}
const fn ini_dly(max: f32, d: f32) -> ParamSpec {
    lin("idly", "INI.DLY", "ms", 0.1, max, 0.1, d)
}
const fn lpf(d: f32) -> ParamSpec {
    p("lpf", "LPF", "Hz", ParamKind::Values(LPF_FREQS), 0.0, THRU_LPF, LPF_FREQS.len() as u32, d)
}
const fn hpf(d: f32) -> ParamSpec {
    p("hpf", "HPF", "Hz", ParamKind::Values(HPF_FREQS), 0.0, HPF_FREQS.len() as f32 - 1.0, HPF_FREQS.len() as u32, d)
}
const fn mod_freq(d: f32) -> ParamSpec {
    lin("mf", "FREQ.", "Hz", 0.1, 40.0, 0.05, d)
}
const fn depth(id: &'static str, label: &'static str, d: f32) -> ParamSpec {
    int(id, label, "%", 0.0, 100.0, d)
}
const fn mod_dly(d: f32) -> ParamSpec {
    lin("mdl", "MOD.DLY", "ms", 0.0, 255.0, 0.1, d)
}
const fn fb_gain(id: &'static str, label: &'static str, d: f32) -> ParamSpec {
    int(id, label, "%", -99.0, 99.0, d)
}
const fn delay(id: &'static str, label: &'static str, max: f32, d: f32) -> ParamSpec {
    lin(id, label, "ms", 0.1, max, 0.1, d)
}
const fn room_size(d: f32) -> ParamSpec {
    lin("size", "ROOM SIZE", "", 0.1, 20.0, 0.1, d)
}
const fn liveness(d: f32) -> ParamSpec {
    int("live", "LIVENESS", "", 0.0, 10.0, d)
}
const fn er_num(d: f32) -> ParamSpec {
    int("ern", "ER NUM.", "", 1.0, 15.0, d)
}
/// The early reflections' patterns (MAN p.103).
pub const ER_TYPES: &[&str] = &["S-Hall", "L-Hall", "Random", "Reverse", "Plate", "Spring"];
const fn er_type(d: f32) -> ParamSpec {
    p("type", "TYPE", "", ParamKind::Choice(ER_TYPES), 0.0, 5.0, 6, d)
}
/// The modulation types' shelving and peaking EQ (MAN p.108).
const fn lsf_f(d: f32) -> ParamSpec {
    lin("lsff", "LSF F", "Hz", 32.0, 1000.0, 1.0, d)
}
const fn eq_f(d: f32) -> ParamSpec {
    lin("eqf", "EQ F", "Hz", 160.0, 8000.0, 1.0, d)
}
const fn hsf_f(d: f32) -> ParamSpec {
    lin("hsff", "HSF F", "Hz", 1000.0, 10000.0, 100.0, d)
}
const fn eq_g(id: &'static str, label: &'static str, d: f32) -> ParamSpec {
    int(id, label, "dB", -12.0, 12.0, d)
}

/// Programs 1-14 (MAN p.98; program 1 REVERB HALL1).
static REV: [ParamSpec; 9] = [
    rev_time(2.8),
    high_ratio(0.8),
    diffusion(6.0),
    ini_dly(200.0, 40.0),
    int("dens", "DENSITY", "", 0.0, 3.0, 3.0),
    lpf(index_of(LPF_FREQS, 7000.0)),
    hpf(0.0),
    lin("rdly", "REV.DLY", "ms", 0.1, 85.0, 0.1, 18.0),
    depth("rbal", "REV.BAL", 50.0),
];

/// Program 15 CHORUS->REVERB (MAN p.102).
static CHO_REV: [ParamSpec; 10] = [
    mod_freq(4.5),
    depth("md", "DEPTH", 14.0),
    mod_dly(7.3),
    rev_time(1.7),
    high_ratio(0.8),
    diffusion(8.0),
    ini_dly(200.0, 30.0),
    lpf(index_of(LPF_FREQS, 12000.0)),
    hpf(index_of(HPF_FREQS, 40.0)),
    depth("rdep", "REV DEPTH", 60.0),
];

/// Programs 16-17 FLANGE->REVERB (MAN p.103).
static FLG_REV: [ParamSpec; 10] = [
    mod_freq(2.4),
    depth("md", "DEPTH", 68.0),
    fb_gain("fbg", "FB.GAIN", 37.0),
    mod_dly(254.4),
    rev_time(2.6),
    diffusion(8.0),
    ini_dly(200.0, 10.0),
    lpf(index_of(LPF_FREQS, 9000.0)),
    hpf(index_of(HPF_FREQS, 45.0)),
    depth("rdep", "REV DEPTH", 61.0),
];

/// Programs 18-19 EARLY REF. (MAN p.103; program 18 EARLY REF.1).
static EARLY: [ParamSpec; 10] = [
    er_type(1.0),
    room_size(2.5),
    liveness(2.0),
    diffusion(8.0),
    ini_dly(200.0, 23.7),
    er_num(15.0),
    high_ratio(0.8),
    fb_gain("fbg", "FB.GAIN", 0.0),
    lpf(index_of(LPF_FREQS, 10000.0)),
    hpf(0.0),
];

/// Programs 20-21 GATE REVERB, REVERSE GATE (MAN p.104; program 20's values):
/// TYPE-A the reverse gate, TYPE-B the gate.
static GATE_REV: [ParamSpec; 10] = [
    p("type", "TYPE", "", ParamKind::Choice(&["Type-A", "Type-B"]), 0.0, 1.0, 2, 1.0),
    room_size(1.9),
    liveness(8.0),
    diffusion(7.0),
    ini_dly(200.0, 0.1),
    er_num(15.0),
    high_ratio(0.8),
    fb_gain("fbg", "FB.GAIN", 0.0),
    lpf(index_of(LPF_FREQS, 12000.0)),
    hpf(0.0),
];

/// Program 22 DELAY->EARLY REF (MAN p.105).
static DLY_ER: [ParamSpec; 10] = [
    delay("d1", "DELAY", 320.0, 256.0),
    fb_gain("fbg", "FB.GAIN", 43.0),
    high_ratio(0.3),
    er_type(2.0),
    room_size(6.8),
    liveness(4.0),
    diffusion(3.0),
    ini_dly(200.0, 8.0),
    er_num(12.0),
    depth("rdep", "DEPTH", 47.0),
];

/// Programs 23-24 DELAY L-C-R (MAN p.105; program 23's values).
static DLY_LCR: [ParamSpec; 10] = [
    delay("dl", "DLY(L)", 1000.0, 125.0),
    delay("dr", "DLY(R)", 1000.0, 250.0),
    delay("dc", "DLY(C)", 1000.0, 500.0),
    int("lc", "LEVEL(C)", "", -100.0, 100.0, 57.0),
    delay("fbd", "FB.DLY", 1000.0, 125.0),
    fb_gain("fbg", "FB.GAIN", 20.0),
    high_ratio(0.8),
    lpf(THRU_LPF),
    hpf(0.0),
    ini_dly(320.0, 0.1),
];

/// Program 25 CHORUS->DLY LCR (MAN p.106).
static CHO_DLY_LCR: [ParamSpec; 10] = [
    mod_freq(0.8),
    depth("md", "DEPTH", 24.0),
    mod_dly(5.9),
    delay("dl", "DLY(L)", 1000.0, 26.4),
    delay("dr", "DLY(R)", 1000.0, 33.2),
    delay("dc", "DLY(C)", 1000.0, 13.1),
    int("lc", "LEVEL(C)", "", -100.0, 100.0, 60.0),
    delay("fbd", "FB.DLY", 1000.0, 40.5),
    fb_gain("fbg", "FB.GAIN", -48.0),
    high_ratio(0.1),
];

/// Program 26 MONODLY->CHORUS (MAN p.106).
static MDLY_CHO: [ParamSpec; 10] = [
    delay("d1", "DELAY", 1000.0, 400.0),
    fb_gain("fbg", "FB.GAIN", 32.0),
    lpf(THRU_LPF),
    hpf(0.0),
    mod_freq(0.4),
    mod_dly(0.1),
    eq_f(315.0),
    eq_g("eqg", "EQ G", 0.0),
    depth("am", "AM DEPTH", 10.0),
    depth("pm", "PM DEPTH", 40.0),
];

/// Program 27 STEREO INI.DLY ECHO (MAN p.106).
static ST_ECHO: [ParamSpec; 9] = [
    delay("fdl", "FB.D(L)", 360.0, 220.0),
    fb_gain("fgl", "FB.G(L)", 40.0),
    delay("fdr", "FB.D(R)", 360.0, 220.0),
    fb_gain("fgr", "FB.G(R)", 40.0),
    high_ratio(0.4),
    lpf(THRU_LPF),
    hpf(0.0),
    delay("dl", "DLY(L)", 320.0, 220.0),
    delay("dr", "DLY(R)", 320.0, 110.0),
];

/// Programs 28-30 ECHO, CROSS ECHO, CROSS DELAY (MAN p.107; program 28's values).
static ECHO: [ParamSpec; 10] = [
    delay("dl", "DLY(L)", 500.0, 220.0),
    fb_gain("fgl", "FB.G(L)", 44.0),
    delay("dr", "DLY(R)", 500.0, 180.0),
    fb_gain("fgr", "FB.G(R)", -55.0),
    high_ratio(0.2),
    lpf(THRU_LPF),
    hpf(0.0),
    ini_dly(320.0, 0.1),
    depth("l2r", "L->R BAL", 0.0),
    depth("r2l", "R->L BAL", 0.0),
];

/// Programs 31-33 and 36 CHORUS, BROAD CHORUS, TREMOLO (MAN p.108; program
/// 31's values).
static CHORUS: [ParamSpec; 10] = [
    mod_freq(0.6),
    depth("am", "AM DEPTH", 49.0),
    depth("pm", "PM DEPTH", 35.0),
    lsf_f(220.0),
    eq_g("lsfg", "LSF G", 3.0),
    mod_dly(12.1),
    eq_f(560.0),
    eq_g("eqg", "EQ G", -1.0),
    hsf_f(4000.0),
    eq_g("hsfg", "HSF G", 1.0),
];

/// Program 34 FLANGE (MAN p.109).
static FLANGE: [ParamSpec; 10] = [
    mod_freq(1.2),
    depth("md", "DEPTH", 25.0),
    fb_gain("fbg", "FB.GAIN", 45.0),
    lsf_f(63.0),
    eq_g("lsfg", "LSF G", 0.0),
    mod_dly(14.6),
    eq_f(630.0),
    eq_g("eqg", "EQ G", 2.0),
    hsf_f(3200.0),
    eq_g("hsfg", "HSF G", 0.0),
];

/// Program 35 SYMPHONIC (MAN p.109).
static SYMPHONIC: [ParamSpec; 9] = [
    mod_freq(0.6),
    depth("md", "DEPTH", 8.0),
    mod_dly(2.4),
    lsf_f(63.0),
    eq_g("lsfg", "LSF G", 0.0),
    eq_f(500.0),
    eq_g("eqg", "EQ G", 0.0),
    hsf_f(6300.0),
    eq_g("hsfg", "HSF G", -2.0),
];

/// Program 37 AUTO PAN (MAN p.110).
static AUTO_PAN: [ParamSpec; 10] = [
    mod_freq(0.5),
    depth("lr", "L/R DEPTH", 98.0),
    depth("fr", "F/R DEPTH", 15.0),
    lsf_f(140.0),
    eq_g("lsfg", "LSF G", 4.0),
    p("dir", "DIR.", "", ParamKind::Choice(&["L<->R", "L-->R", "L<--R", "Turn L", "Turn R"]), 0.0, 4.0, 5, 0.0),
    eq_f(630.0),
    eq_g("eqg", "EQ G", 3.0),
    hsf_f(3200.0),
    eq_g("hsfg", "HSF G", 0.0),
];

/// Program 38 PHASING (MAN p.110).
static PHASING: [ParamSpec; 9] = [
    mod_freq(1.3),
    depth("md", "DEPTH", 52.0),
    fb_gain("fbg", "FB.GAIN", 68.0),
    int("ofs", "OFFSET", "", 0.0, 100.0, 32.0),
    p("stg", "STAGE", "", ParamKind::Choice(&["4", "6", "8"]), 0.0, 2.0, 3, 0.0),
    lsf_f(250.0),
    eq_g("lsfg", "LSF G", 2.0),
    hsf_f(3600.0),
    eq_g("hsfg", "HSF G", 1.0),
];

/// Programs 39-40 ST.PITCH CHANGE, VOCAL DOUBLER (MAN p.110; program 39's values).
static PITCH: [ParamSpec; 10] = [
    int("pit", "PITCH", "", -12.0, 12.0, 0.0),
    int("f1", "FINE(1)", "", -50.0, 50.0, 20.0),
    int("f2", "FINE(2)", "", -50.0, 50.0, -20.0),
    int("o1", "OUT(1)", "", -100.0, 100.0, 100.0),
    int("o2", "OUT(2)", "", -100.0, 100.0, 100.0),
    int("p1", "PAN(1)", "", -100.0, 100.0, -100.0),
    int("p2", "PAN(2)", "", -100.0, 100.0, 100.0),
    fb_gain("fb1", "FB.G(1)", 28.0),
    fb_gain("fb2", "FB.G(2)", -28.0),
    delay("fbd", "DELAY", 255.0, 25.0),
];

const fn fx(token: &'static str, name: &'static str, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name, category: ModelCategory::FxStandard, params }
}

/// The 02R's FX table: an empty slot, a plugin (the WING's EXT), then its 17
/// effect types.
pub static FX_MODELS: [ModelSpec; 19] = [
    crate::wing::FX_MODELS[0],
    crate::wing::FX_MODELS[1],
    fx("Y2REV", "Reverb", &REV),
    fx("Y2CHREV", "Chorus->Reverb", &CHO_REV),
    fx("Y2FLREV", "Flange->Reverb", &FLG_REV),
    fx("Y2ER", "Early Ref.", &EARLY),
    fx("Y2GATE", "Gate Reverb", &GATE_REV),
    fx("Y2DLER", "Delay->Early Ref", &DLY_ER),
    fx("Y2DLCR", "Delay L-C-R", &DLY_LCR),
    fx("Y2CHDLY", "Chorus->Dly LCR", &CHO_DLY_LCR),
    fx("Y2MDCHO", "MonoDly->Chorus", &MDLY_CHO),
    fx("Y2STECHO", "Stereo Ini.Dly Echo", &ST_ECHO),
    fx("Y2ECHO", "Echo", &ECHO),
    fx("Y2CHO", "Chorus", &CHORUS),
    fx("Y2FLG", "Flange", &FLANGE),
    fx("Y2SYMP", "Symphonic", &SYMPHONIC),
    fx("Y2APAN", "Auto Pan", &AUTO_PAN),
    fx("Y2PHAS", "Phasing", &PHASING),
    fx("Y2PITCH", "St.Pitch Change", &PITCH),
];

/// The 02R effect types' tokens.
pub fn is_02r(token: &str) -> bool {
    token.starts_with("Y2") && FX_MODELS.iter().any(|m| m.token == token)
}

/// An 02R effect type's algorithm.
struct Y2Fx {
    token: &'static str,
    /// The WING devices it runs, in order.
    stages: Vec<Stage>,
    taps: Option<Taps>,
    /// The part after the taps mixed in by this much (DELAY->EARLY REF's DEPTH),
    /// or the reverb after the modulation (REV DEPTH); 1: all of it.
    depth: f32,
    /// GATE REVERB's type: 0 TYPE-A (reverse), 1 TYPE-B.
    gate: usize,
    voices: [(f32, f32); 2],
    scratch: [Vec<f32>; 4],
}

/// Sets the WING AMBI's controls from early reflections' (ROOM SIZE the room's
/// size, LIVENESS its decay, INI.DLY its pre-delay: Sidecar's reading).
fn early(stage: &mut Stage, p: &Params) {
    stage.set("pdel", p.f("idly"));
    stage.set("size", 2.0 + 4.9 * p.f("size"));
    stage.set("dcy", 0.2 + 0.3 * p.f("live"));
    stage.set("diff", 10.0 * p.f("dif"));
    stage.set("damp", damping_hz(p.f("hr")));
    stage.set("hc", p.f("lpf"));
    stage.set("lc", p.f("hpf"));
}

impl Y2Fx {
    fn new(token: &'static str, ctx: FxContext) -> Option<Self> {
        let stage = |w: &str| Stage::new(w, ctx);
        let taps = |mode| Some(Taps::with_length(mode, ctx, 1.05));
        let (stages, taps) = match token {
            "Y2REV" => (vec![stage("ROOM")?], None),
            "Y2CHREV" => (vec![stage("CHORUS")?, stage("ROOM")?], None),
            "Y2FLREV" => (vec![stage("FLANGER")?, stage("ROOM")?], None),
            "Y2ER" => (vec![stage("AMBI")?], None),
            "Y2GATE" => (vec![stage("REVERSE")?, stage("GATED")?], None),
            "Y2DLER" => (vec![stage("AMBI")?], taps(TapMode::Mono)),
            "Y2DLCR" => (vec![], taps(TapMode::Lcr)),
            "Y2CHDLY" => (vec![stage("CHORUS")?], taps(TapMode::Lcr)),
            "Y2MDCHO" => (vec![stage("CHORUS")?, stage("PANNER")?], taps(TapMode::Mono)),
            "Y2STECHO" | "Y2ECHO" => (vec![], taps(TapMode::Echo)),
            "Y2CHO" => (vec![stage("CHORUS")?, stage("PANNER")?], None),
            "Y2SYMP" => (vec![stage("CHORUS")?], None),
            "Y2FLG" => (vec![stage("FLANGER")?], None),
            "Y2PHAS" => (vec![stage("PHASER")?], None),
            "Y2APAN" => (vec![stage("PANNER")?], None),
            "Y2PITCH" => (vec![stage("PITCH")?, stage("PITCH")?], None),
            _ => return None,
        };
        let n = ctx.max_block;
        Some(Y2Fx {
            token,
            stages,
            taps,
            depth: 1.0,
            gate: 1,
            voices: [(1.0, -100.0), (1.0, 100.0)],
            scratch: [vec![0.0; n], vec![0.0; n], vec![0.0; n], vec![0.0; n]],
        })
    }
}

impl FxAlgo for Y2Fx {
    fn set_params(&mut self, p: &Params) {
        let f = |id: &str| p.f(id);
        // A tremolo (PANNER, both sides together) by AM DEPTH.
        let tremolo = |s: &mut Stage| {
            s.set("spd", f("mf"));
            s.set("depth", f("am"));
            s.set("phase", 0.0);
        };
        match self.token {
            "Y2REV" => reverb(&mut self.stages[0], p),
            "Y2CHREV" | "Y2FLREV" => {
                if self.token == "Y2CHREV" {
                    chorus(&mut self.stages[0], p, "md", 50.0);
                } else {
                    let s = &mut self.stages[0];
                    s.set("spd", f("mf"));
                    s.set("depl", f("md"));
                    s.set("depr", f("md"));
                    s.set("dlyl", f("mdl"));
                    s.set("dlyr", f("mdl"));
                    s.set("feed", f("fbg"));
                }
                reverb(&mut self.stages[1], p);
                self.depth = 0.01 * f("rdep");
            }
            "Y2ER" => early(&mut self.stages[0], p),
            "Y2GATE" => {
                self.gate = p.choice("type").min(1);
                for s in &mut self.stages {
                    s.set("pdel", f("idly"));
                    s.set("dcy", 0.14 + 0.086 * f("live"));
                }
            }
            "Y2DLER" => {
                early(&mut self.stages[0], p);
                let t = self.taps.as_mut().expect("taps");
                t.set([f("d1"); 4], 0.0, [f("fbg"); 2], f("hr"));
                self.depth = 0.01 * f("rdep");
            }
            "Y2DLCR" | "Y2CHDLY" => {
                if self.token == "Y2CHDLY" {
                    chorus(&mut self.stages[0], p, "md", 50.0);
                }
                let t = self.taps.as_mut().expect("taps");
                t.set([f("dl"), f("dr"), f("dc"), f("fbd")], 0.01 * f("lc"), [f("fbg"); 2], f("hr"));
            }
            "Y2MDCHO" => {
                chorus(&mut self.stages[0], p, "pm", 50.0);
                tremolo(&mut self.stages[1]);
                let t = self.taps.as_mut().expect("taps");
                t.set([f("d1"); 4], 0.0, [f("fbg"); 2], 1.0);
            }
            "Y2STECHO" => {
                let t = self.taps.as_mut().expect("taps");
                t.set([f("fdl"), f("fdr"), 1.0, 1.0], 0.0, [f("fgl"), f("fgr")], f("hr"));
            }
            "Y2ECHO" => {
                let t = self.taps.as_mut().expect("taps");
                t.set([f("dl"), f("dr"), 1.0, 1.0], 0.0, [f("fgl"), f("fgr")], f("hr"));
                t.set_cross(0.01 * f("l2r"), 0.01 * f("r2l"));
            }
            "Y2CHO" => {
                chorus(&mut self.stages[0], p, "pm", 50.0);
                tremolo(&mut self.stages[1]);
            }
            "Y2SYMP" => chorus(&mut self.stages[0], p, "md", 100.0),
            "Y2FLG" => {
                let s = &mut self.stages[0];
                s.set("spd", f("mf"));
                s.set("depl", f("md"));
                s.set("depr", f("md"));
                s.set("dlyl", f("mdl"));
                s.set("dlyr", f("mdl"));
                s.set("feed", f("fbg"));
            }
            "Y2PHAS" => {
                let s = &mut self.stages[0];
                s.set("spd", f("mf"));
                s.set("depth", f("md"));
                s.set("reso", 0.8 * f("fbg").abs());
                s.set("phase", 1.8 * f("ofs"));
            }
            "Y2APAN" => {
                let s = &mut self.stages[0];
                s.set("spd", f("mf"));
                s.set("depth", f("lr"));
                s.set("phase", 180.0);
            }
            "Y2PITCH" => {
                for (k, (fine, out, pan)) in [("f1", "o1", "p1"), ("f2", "o2", "p2")].into_iter().enumerate() {
                    let s = &mut self.stages[k];
                    s.set("semi", f("pit"));
                    s.set("cent", f(fine));
                    s.set("dly", 0.0);
                    self.voices[k] = (0.01 * f(out), f(pan));
                }
            }
            _ => {}
        }
        for s in &mut self.stages {
            s.apply();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len().min(self.scratch[0].len());
        let (left, right) = (&mut left[..n], &mut right[..n]);
        match self.token {
            "Y2CHREV" | "Y2FLREV" | "Y2DLER" => {
                // The first part, then the reverb or early reflections mixed in by DEPTH.
                if let Some(t) = &mut self.taps {
                    t.process(left, right);
                } else {
                    self.stages[0].dev.process(left, right);
                }
                let last = self.stages.len() - 1;
                let [a, b, ..] = &mut self.scratch;
                a[..n].copy_from_slice(left);
                b[..n].copy_from_slice(right);
                self.stages[last].dev.process(&mut a[..n], &mut b[..n]);
                let d = self.depth;
                for i in 0..n {
                    left[i] += d * (a[i] - left[i]);
                    right[i] += d * (b[i] - right[i]);
                }
            }
            "Y2GATE" => self.stages[self.gate].dev.process(left, right),
            "Y2PITCH" => {
                let [a, b, c, e] = &mut self.scratch;
                for i in 0..n {
                    let mono = 0.5 * (left[i] + right[i]);
                    a[i] = mono;
                    b[i] = mono;
                    c[i] = mono;
                    e[i] = mono;
                }
                self.stages[0].dev.process(&mut a[..n], &mut b[..n]);
                self.stages[1].dev.process(&mut c[..n], &mut e[..n]);
                let gains = self.voices.map(|(g, pan)| {
                    let x = (pan.clamp(-100.0, 100.0) + 100.0) / 200.0 * std::f32::consts::FRAC_PI_2;
                    (g * x.cos(), g * x.sin())
                });
                for i in 0..n {
                    let (v1, v2) = (0.5 * (a[i] + b[i]), 0.5 * (c[i] + e[i]));
                    left[i] = gains[0].0 * v1 + gains[1].0 * v2;
                    right[i] = gains[0].1 * v1 + gains[1].1 * v2;
                }
            }
            _ => {
                // The delay first where it leads (MONODLY->CHORUS), else last.
                let taps_first = self.token == "Y2MDCHO";
                if taps_first {
                    if let Some(t) = &mut self.taps {
                        t.process(left, right);
                    }
                }
                for s in &mut self.stages {
                    s.dev.process(left, right);
                }
                if !taps_first {
                    if let Some(t) = &mut self.taps {
                        t.process(left, right);
                    }
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
    }
}

/// Creates the device of 02R effect type `token`; `None` for other tokens.
pub fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    let spec = FX_MODELS.iter().find(|m| m.token == token && token.starts_with("Y2"))?;
    Some(Box::new(Y2Fx::new(spec.token, ctx)?))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wing::MAX_FX_PARAMS;

    #[test]
    fn the_02rs_effect_types() {
        assert_eq!(FX_MODELS.len(), 19);
        assert_eq!((FX_MODELS[0].token, FX_MODELS[1].token), ("NONE", "EXT"));
        for m in &FX_MODELS[2..] {
            assert!(m.params.len() <= 10, "{}", m.token);
            for ps in m.params {
                assert!((ps.lo()..=ps.hi()).contains(&ps.default), "{} {}", m.token, ps.id);
            }
            crate::wing::fx::tests::check_device(m.token);
        }
        let rev = &FX_MODELS[2];
        assert_eq!(rev.params.iter().find(|p| p.id == "rt").unwrap().steps, 298, "0.3-30.0 s in 0.1 s");
        assert!(is_02r("Y2ECHO") && !is_02r("PMECHO"));
    }

    fn render(token: &str, set: &[(&str, f32)], n: usize) -> (Vec<f32>, Vec<f32>) {
        let ctx = FxContext { sample_rate: 48000.0, max_block: 64 };
        let model = crate::model::model_index(&FX_MODELS, token).unwrap();
        let mut dev = crate::wing::fx::FxDevice::new_in(&FX_MODELS, model, ctx).unwrap();
        let mut values = [0.0; MAX_FX_PARAMS];
        FX_MODELS[model].defaults_into(&mut values);
        for (id, v) in set {
            values[FX_MODELS[model].param_index(id).unwrap()] = *v;
        }
        let (mut l, mut r) = (vec![0.0f32; n], vec![0.0f32; n]);
        l[0] = 1.0;
        r[0] = 1.0;
        for (cl, cr) in l.chunks_mut(64).zip(r.chunks_mut(64)) {
            dev.process(&values, cl, cr);
        }
        (l, r)
    }

    fn peak(x: &[f32]) -> usize {
        x.iter().enumerate().fold((0, 0.0f32), |m, (i, v)| if v.abs() > m.1 { (i, v.abs()) } else { m }).0
    }

    #[test]
    fn the_delays_repeat_where_they_are_set_up_to_a_second() {
        // L at 10 ms, R at 900 ms, C off, no feedback.
        let (l, r) = render("Y2DLCR", &[("dl", 10.0), ("dr", 900.0), ("lc", 0.0), ("fbg", 0.0)], 48000);
        assert_eq!((peak(&l), peak(&r)), (480, 43200));
    }

    #[test]
    fn a_cross_echo_feeds_one_side_into_the_other() {
        // Only L has input; R's line hears L's repeats through L->R.
        let ctx = FxContext { sample_rate: 48000.0, max_block: 64 };
        let model = crate::model::model_index(&FX_MODELS, "Y2ECHO").unwrap();
        let mut dev = crate::wing::fx::FxDevice::new_in(&FX_MODELS, model, ctx).unwrap();
        let mut values = [0.0; MAX_FX_PARAMS];
        FX_MODELS[model].defaults_into(&mut values);
        let spec = &FX_MODELS[model];
        for (id, v) in [("dl", 10.0), ("dr", 15.0), ("fgl", 50.0), ("fgr", 0.0), ("hr", 1.0), ("l2r", 100.0), ("r2l", 0.0)] {
            values[spec.param_index(id).unwrap()] = v;
        }
        let (mut l, mut r) = (vec![0.0f32; 4800], vec![0.0f32; 4800]);
        l[0] = 1.0;
        for (cl, cr) in l.chunks_mut(64).zip(r.chunks_mut(64)) {
            dev.process(&values, cl, cr);
        }
        assert!(r.iter().any(|x| x.abs() > 0.1), "L crossed into R");
        // Without the cross feed R stays silent.
        values[spec.param_index("l2r").unwrap()] = 0.0;
        dev.reset();
        let (mut l, mut r) = (vec![0.0f32; 4800], vec![0.0f32; 4800]);
        l[0] = 1.0;
        for (cl, cr) in l.chunks_mut(64).zip(r.chunks_mut(64)) {
            dev.process(&values, cl, cr);
        }
        assert!(r.iter().all(|x| x.abs() < 1e-6));
    }

    #[test]
    fn the_reverbs_ring() {
        for token in ["Y2REV", "Y2ER", "Y2GATE", "Y2CHREV", "Y2DLER"] {
            let (l, _) = render(token, &[], 96000);
            let energy: f32 = l[4800..].iter().map(|x| x * x).sum();
            assert!(energy > 1e-4, "{token}: {energy}");
        }
    }
}
