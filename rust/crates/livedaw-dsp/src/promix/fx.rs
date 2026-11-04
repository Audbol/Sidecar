//! The ProMix 01's internal effects (Phase 5 step 4): the effect types of its
//! 30 preset programs (User's Guide pp.30, 37-44), each a model with the
//! parameters, ranges and steps the guide lists, on the first program of its
//! type's values.
//!
//! The algorithms are Sidecar's, on the WING's devices of the same kind where
//! there is one (as the X32's are): the reverbs on the WING's ROOM (REV TIME
//! its decay, HIGH RATIO its damping, DIFFUSION, INI DLY its pre-delay, LPF
//! and HPF its high and low cut; the room's size follows the time), chorus
//! and symphonic on its CHORUS, flange on its FLANGER, phasing on its PHASER,
//! tremolo and auto pan on its PANNER, pitch change on two of its PITCH; the
//! L-C-R, mono, two-stage and karaoke delays are a delay line of Sidecar's
//! own ([`Taps`]: taps, a feedback tap and a low-pass in the feedback for HIGH
//! RATIO). The combinations run their two parts in series, REV DEPTH mixing
//! the reverb against the part before it. Where a ProMix range is wider than
//! the WING device's (a 20 Hz chorus) the device's limit holds. Unmodelled:
//! the chorus' amplitude modulation (AM DEPTH, but tremolo's), the pitch
//! changers' feedback, auto pan's front / rear depth and direction. All to be
//! compared with a console.

use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec, Params};
use crate::wing::fx::{FxAlgo, FxContext};
use crate::wing::MAX_FX_PARAMS;

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
    p(id, label, unit, ParamKind::Linear, min, max, ((max - min) / step) as u32 + 1, default)
}

const fn int(id: &'static str, label: &'static str, unit: &'static str, min: f32, max: f32, default: f32) -> ParamSpec {
    p(id, label, unit, ParamKind::Int, min, max, (max - min) as u32 + 1, default)
}

/// LPF: 1.0-16 kHz in the EQ's 1/6 octave steps, then THRU (20 kHz).
pub const LPF_FREQS: &[f32] = &[
    1000.0, 1100.0, 1250.0, 1400.0, 1600.0, 1800.0, 2000.0, 2200.0, 2500.0, 2800.0, 3200.0, 3600.0, 4000.0, 4500.0, 5000.0,
    5600.0, 6300.0, 7000.0, 8000.0, 9000.0, 10000.0, 11000.0, 12000.0, 14000.0, 16000.0, 20000.0,
];

/// HPF: THRU (20 Hz), then 32 Hz-8 kHz in 1/6 octave steps.
pub const HPF_FREQS: &[f32] = &[
    20.0, 32.0, 36.0, 40.0, 45.0, 50.0, 56.0, 60.0, 70.0, 80.0, 90.0, 100.0, 110.0, 125.0, 140.0, 160.0, 180.0, 200.0, 220.0,
    250.0, 280.0, 315.0, 360.0, 400.0, 450.0, 500.0, 560.0, 630.0, 700.0, 800.0, 900.0, 1000.0, 1100.0, 1250.0, 1400.0, 1600.0,
    1800.0, 2000.0, 2200.0, 2500.0, 2800.0, 3200.0, 3600.0, 4000.0, 4500.0, 5000.0, 5600.0, 6300.0, 7000.0, 8000.0,
];

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
    lin("hr", "HIGH RATIO", "", 0.1, 1.0, 0.1, d)
}
const fn diffusion(d: f32) -> ParamSpec {
    int("dif", "DIFFUSION", "", 0.0, 10.0, d)
}
const fn ini_dly(max: f32, d: f32) -> ParamSpec {
    lin("idly", "INI DLY", "ms", 0.1, max, 0.1, d)
}
const fn lpf(d: f32) -> ParamSpec {
    p("lpf", "LPF", "Hz", ParamKind::Values(LPF_FREQS), 0.0, THRU_LPF, LPF_FREQS.len() as u32, d)
}
const fn hpf(d: f32) -> ParamSpec {
    p("hpf", "HPF", "Hz", ParamKind::Values(HPF_FREQS), 0.0, HPF_FREQS.len() as f32 - 1.0, HPF_FREQS.len() as u32, d)
}
const fn mod_freq(d: f32) -> ParamSpec {
    lin("mf", "MOD FREQ", "Hz", 0.1, 20.0, 0.05, d)
}
const fn depth(id: &'static str, label: &'static str, d: f32) -> ParamSpec {
    int(id, label, "%", 0.0, 100.0, d)
}
const fn mod_dly(max: f32, d: f32) -> ParamSpec {
    lin("mdl", "MOD DLY", "ms", 0.0, max, 0.1, d)
}
const fn fb_gain(id: &'static str, label: &'static str, d: f32) -> ParamSpec {
    int(id, label, "%", -99.0, 99.0, d)
}
const fn delay(id: &'static str, label: &'static str, max: f32, d: f32) -> ParamSpec {
    lin(id, label, "ms", 0.1, max, 0.1, d)
}

/// Programs 1-11 (p.37; program 1 REVERB HALL 1).
static REV: [ParamSpec; 6] =
    [rev_time(2.8), high_ratio(0.8), diffusion(6.0), ini_dly(200.0, 40.0), lpf(index_of(LPF_FREQS, 7000.0)), hpf(0.0)];

/// Program 12 CHORUS->REVERB (p.39).
static CHO_REV: [ParamSpec; 10] = [
    mod_freq(0.8),
    depth("md", "MOD DEPTH", 40.0),
    mod_dly(24.0, 1.3),
    rev_time(2.4),
    high_ratio(0.7),
    diffusion(7.0),
    ini_dly(139.0, 30.0),
    lpf(index_of(LPF_FREQS, 6300.0)),
    hpf(0.0),
    depth("rdep", "REV DEPTH", 24.0),
];

/// Program 13 FLANGE->REVERB (p.40).
static FLG_REV: [ParamSpec; 10] = [
    mod_freq(1.4),
    depth("md", "MOD DEPTH", 22.0),
    fb_gain("fbg", "FB GAIN", 45.0),
    mod_dly(15.5, 13.0),
    rev_time(2.4),
    diffusion(8.0),
    ini_dly(160.0, 26.0),
    lpf(index_of(LPF_FREQS, 4500.0)),
    hpf(index_of(HPF_FREQS, 45.0)),
    depth("rdep", "REV DEPTH", 30.0),
];

/// Program 14 DELAY L-C-R (p.40).
static DLY_LCR: [ParamSpec; 7] = [
    delay("dl", "DLY L", 661.0, 250.0),
    delay("dr", "DLY R", 661.0, 500.0),
    delay("dc", "DLY C", 661.0, 125.0),
    int("lc", "LEVEL C", "", 0.0, 100.0, 70.0),
    delay("fbd", "FB DLY", 661.0, 500.0),
    fb_gain("fbg", "FB GAIN", 40.0),
    high_ratio(0.8),
];

/// Program 15 MONO DLY->CHORUS (p.40).
static MDLY_CHO: [ParamSpec; 6] = [
    delay("d1", "DELAY", 618.0, 400.0),
    fb_gain("fbg", "FB GAIN", 32.0),
    high_ratio(0.6),
    mod_freq(0.4),
    depth("md", "MOD DEPTH", 10.0),
    mod_dly(24.0, 0.1),
];

/// Program 16 CHORUS->DLY LCR (p.41).
static CHO_DLY_LCR: [ParamSpec; 10] = [
    mod_freq(0.8),
    depth("md", "MOD DEPTH", 24.0),
    mod_dly(24.0, 5.9),
    delay("dl", "DLY L", 618.0, 26.4),
    delay("dr", "DLY R", 618.0, 33.2),
    delay("dc", "DLY C", 618.0, 13.1),
    int("lc", "LEVEL C", "", 0.0, 100.0, 60.0),
    delay("fbd", "FB DLY", 618.0, 40.5),
    fb_gain("fbg", "FB GAIN", -48.0),
    high_ratio(0.1),
];

/// Program 17 DELAY->CHORUS (p.41).
static DLY_CHO: [ParamSpec; 8] = [
    delay("d1", "DLY 1", 618.0, 250.0),
    delay("d2", "DLY 2", 618.0, 500.0),
    delay("fbd", "FB DLY", 618.0, 500.0),
    fb_gain("fbg", "FB GAIN", 33.0),
    high_ratio(0.7),
    mod_freq(1.2),
    depth("md", "MOD DEPTH", 25.0),
    mod_dly(24.0, 10.0),
];

/// Programs 18-19 KARAOKE ECHO (p.41).
static ECHO: [ParamSpec; 5] = [
    delay("dl", "DLY L", 322.0, 220.0),
    fb_gain("fbl", "FB GAIN L", 40.0),
    delay("dr", "DLY R", 322.0, 223.0),
    fb_gain("fbr", "FB GAIN R", 40.0),
    high_ratio(0.4),
];

/// Programs 20-22 ST.PITCH CHANGE (p.42).
static PITCH: [ParamSpec; 10] = [
    int("pit", "PITCH", "", -12.0, 12.0, 0.0),
    int("f1", "FINE 1", "", -50.0, 50.0, 10.0),
    int("f2", "FINE 2", "", -50.0, 50.0, -10.0),
    int("o1", "OUT LVL 1", "", -100.0, 100.0, 100.0),
    int("o2", "OUT LVL 2", "", -100.0, 100.0, 100.0),
    int("p1", "PAN 1", "", -100.0, 100.0, -100.0),
    int("p2", "PAN 2", "", -100.0, 100.0, 100.0),
    fb_gain("fb1", "FB GAIN 1", 28.0),
    fb_gain("fb2", "FB GAIN 2", -28.0),
    delay("fbd", "FB DLY", 223.0, 25.0),
];

/// Programs 23-24 CHORUS (p.43; MOD DLY's range is misprinted as -99...+99%:
/// 0.0-50.0 ms is Sidecar's reading of the presets' 12.1 and 38.0 ms).
static CHORUS: [ParamSpec; 4] =
    [mod_freq(0.6), depth("am", "AM DEPTH", 49.0), depth("pm", "PM DEPTH", 35.0), mod_dly(50.0, 12.1)];

/// Program 25 SYMPHONIC (p.43).
static SYMPHONIC: [ParamSpec; 3] = [mod_freq(0.6), depth("md", "MOD DEPTH", 8.0), mod_dly(40.0, 2.4)];

/// Programs 26-27 FLANGE (p.43).
static FLANGE: [ParamSpec; 4] =
    [mod_freq(1.2), depth("md", "MOD DEPTH", 25.0), fb_gain("fbg", "FB GAIN", 45.0), mod_dly(15.0, 14.6)];

/// Program 28 PHASING (p.44).
static PHASING: [ParamSpec; 4] = [
    mod_freq(0.5),
    depth("md", "MOD DEPTH", 58.0),
    fb_gain("fbg", "FB GAIN", 47.0),
    int("ofs", "PHASE OFST", "", 0.0, 100.0, 24.0),
];

/// Program 29 TREMOLO (p.44).
static TREMOLO: [ParamSpec; 3] = [mod_freq(2.8), depth("am", "AM DEPTH", 86.0), depth("pm", "PM DEPTH", 0.0)];

/// Program 30 AUTO PAN (p.44).
static AUTO_PAN: [ParamSpec; 4] = [
    mod_freq(0.8),
    depth("lr", "L/R DEPTH", 98.0),
    depth("fr", "F/R DEPTH", 15.0),
    p("dir", "DIRECTION", "", ParamKind::Choice(&["LR", "TURN L", "TURN R"]), 0.0, 2.0, 3, 0.0),
];

const fn fx(token: &'static str, name: &'static str, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name, category: ModelCategory::FxStandard, params }
}

/// The ProMix 01's FX table: an empty slot, a plugin (the WING's EXT), then
/// its 15 effect types.
pub static FX_MODELS: [ModelSpec; 17] = [
    crate::wing::FX_MODELS[0],
    crate::wing::FX_MODELS[1],
    fx("PMREV", "Reverb", &REV),
    fx("PMCHREV", "Chorus->Reverb", &CHO_REV),
    fx("PMFLREV", "Flange->Reverb", &FLG_REV),
    fx("PMDLCR", "Delay L-C-R", &DLY_LCR),
    fx("PMMDCHO", "Mono Dly->Chorus", &MDLY_CHO),
    fx("PMCHDLY", "Chorus->Dly LCR", &CHO_DLY_LCR),
    fx("PMDLCHO", "Delay->Chorus", &DLY_CHO),
    fx("PMECHO", "Karaoke Echo", &ECHO),
    fx("PMPITCH", "St.Pitch Change", &PITCH),
    fx("PMCHO", "Chorus", &CHORUS),
    fx("PMSYMP", "Symphonic", &SYMPHONIC),
    fx("PMFLG", "Flange", &FLANGE),
    fx("PMPHAS", "Phasing", &PHASING),
    fx("PMTREM", "Tremolo", &TREMOLO),
    fx("PMAPAN", "Auto Pan", &AUTO_PAN),
];

/// The ProMix effect types' tokens.
pub fn is_promix(token: &str) -> bool {
    token.starts_with("PM") && FX_MODELS.iter().any(|m| m.token == token)
}

/// One of the WING's devices and its control values (the 02R's effects run
/// them too: `crate::y02r::fx`).
pub(crate) struct Stage {
    pub(crate) dev: Box<dyn FxAlgo>,
    spec: &'static ModelSpec,
    values: [f32; MAX_FX_PARAMS],
}

impl Stage {
    pub(crate) fn new(wing: &str, ctx: FxContext) -> Option<Self> {
        let spec = &crate::wing::FX_MODELS[crate::wing::fx::model_index(wing)?];
        let mut values = [0.0; MAX_FX_PARAMS];
        spec.defaults_into(&mut values);
        let mut s = Self { dev: crate::wing::fx::create(wing, ctx)?, spec, values };
        // A send effect: wet only.
        s.set("mix", 100.0);
        Some(s)
    }

    /// Sets a control, held within the device's range.
    pub(crate) fn set(&mut self, id: &str, v: f32) {
        if let Some(i) = self.spec.param_index(id) {
            let ps = &self.spec.params[i];
            self.values[i] = v.clamp(ps.lo(), ps.hi());
        }
    }

    pub(crate) fn apply(&mut self) {
        self.dev.set_params(&Params::new(self.spec, &self.values));
    }
}

/// HIGH RATIO (0.1-1.0) as a damping frequency: 1.35 kHz to 20 kHz on a log
/// scale (Sidecar's reading).
pub fn damping_hz(ratio: f32) -> f32 {
    1000.0 * 20f32.powf(ratio.clamp(0.1, 1.0))
}

/// The delay types' structure.
#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum TapMode {
    /// L, R and C taps of one line fed by the mono sum, a feedback tap.
    Lcr,
    /// One tap, fed back.
    Mono,
    /// Two taps (L, R) of one line, a feedback tap.
    Two,
    /// Two lines (L, R), each fed back.
    Echo,
}

/// Sidecar's delay for the ProMix's delay types: up to 661 ms per line (the
/// 02R's: 1 s), a one-pole low-pass in the feedback (HIGH RATIO); Echo's two
/// lines may also feed each other (the 02R's L->R and R->L).
pub(crate) struct Taps {
    mode: TapMode,
    line: [Vec<f32>; 2],
    pos: usize,
    sr: f32,
    // Delays in samples: L, R, C, feedback (or L / R feedback in Echo).
    d: [usize; 4],
    level_c: f32,
    fb: [f32; 2],
    lp_coef: f32,
    lp: [f32; 2],
    /// Echo: L's feedback into R's line, R's into L's (0..1).
    cross: [f32; 2],
}

impl Taps {
    fn new(mode: TapMode, ctx: FxContext) -> Self {
        Self::with_length(mode, ctx, 0.7)
    }

    /// Lines of `seconds`.
    pub(crate) fn with_length(mode: TapMode, ctx: FxContext, seconds: f32) -> Self {
        let len = (seconds * ctx.sample_rate) as usize + ctx.max_block + 2;
        Taps {
            mode,
            line: [vec![0.0; len], vec![0.0; len]],
            pos: 0,
            sr: ctx.sample_rate,
            d: [1; 4],
            level_c: 0.0,
            fb: [0.0; 2],
            lp_coef: 0.0,
            lp: [0.0; 2],
            cross: [0.0; 2],
        }
    }

    fn samples(&self, ms: f32) -> usize {
        ((ms * 0.001 * self.sr) as usize).clamp(1, self.line[0].len() - 1)
    }

    pub(crate) fn set(&mut self, delays_ms: [f32; 4], level_c: f32, fb: [f32; 2], high_ratio: f32) {
        self.d = delays_ms.map(|ms| self.samples(ms));
        self.level_c = level_c;
        self.fb = fb.map(|g| (g * 0.01).clamp(-0.99, 0.99));
        self.lp_coef = (-2.0 * std::f32::consts::PI * damping_hz(high_ratio) / self.sr).exp();
    }

    /// Echo's cross feeds: L into R, R into L (0..1).
    pub(crate) fn set_cross(&mut self, l_to_r: f32, r_to_l: f32) {
        self.cross = [l_to_r.clamp(0.0, 1.0), r_to_l.clamp(0.0, 1.0)];
    }

    fn read(&self, ch: usize, d: usize) -> f32 {
        let len = self.line[ch].len();
        self.line[ch][(self.pos + len - d) % len]
    }

    pub(crate) fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let len = self.line[0].len();
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let (inl, inr) = (*l, *r);
            let mono = 0.5 * (inl + inr);
            let damp = |lp: &mut f32, x: f32, c: f32| {
                *lp = x + c * (*lp - x);
                *lp
            };
            match self.mode {
                TapMode::Echo => {
                    let mut back = [0.0; 2];
                    for (ch, b) in back.iter_mut().enumerate() {
                        let y = self.read(ch, self.d[ch]);
                        *b = self.fb[ch] * damp(&mut self.lp[ch], y, self.lp_coef);
                    }
                    // Each line gets its own feedback, and the other's crossed over.
                    self.line[0][self.pos] = inl + back[0] + self.cross[1] * back[1];
                    self.line[1][self.pos] = inr + back[1] + self.cross[0] * back[0];
                    *l = self.read(0, self.d[0]);
                    *r = self.read(1, self.d[1]);
                }
                _ => {
                    let back = self.read(0, if self.mode == TapMode::Mono { self.d[0] } else { self.d[3] });
                    let f = damp(&mut self.lp[0], back, self.lp_coef);
                    self.line[0][self.pos] = mono + self.fb[0] * f;
                    let (a, b) = (self.read(0, self.d[0]), self.read(0, self.d[1]));
                    match self.mode {
                        TapMode::Lcr => {
                            let c = self.level_c * self.read(0, self.d[2]);
                            *l = a + c;
                            *r = b + c;
                        }
                        TapMode::Mono => {
                            *l = a;
                            *r = a;
                        }
                        _ => {
                            *l = a;
                            *r = b;
                        }
                    }
                }
            }
            self.pos = (self.pos + 1) % len;
        }
    }

    pub(crate) fn reset(&mut self) {
        self.line.iter_mut().for_each(|b| b.fill(0.0));
        self.lp = [0.0; 2];
    }
}

/// A ProMix effect type's algorithm.
struct PmFx {
    token: &'static str,
    /// The WING devices it runs: modulation or pitch first, the reverb last.
    stages: Vec<Stage>,
    taps: Option<Taps>,
    /// Combinations: REV DEPTH (0..1); ST.PITCH: the two voices' gains and pans.
    rev_depth: f32,
    voices: [(f32, f32); 2],
    scratch: [Vec<f32>; 4],
}

/// Sets the WING ROOM's controls from a ProMix reverb's.
pub(crate) fn reverb(stage: &mut Stage, p: &Params) {
    let rt = p.f("rt");
    stage.set("dcy", rt);
    stage.set("size", 8.0 + 2.0 * rt);
    stage.set("damp", damping_hz(p.f("hr")));
    stage.set("diff", if p.spec.param_index("dif").is_some() { 10.0 * p.f("dif") } else { 70.0 });
    stage.set("pdel", p.f("idly"));
    stage.set("hc", p.f("lpf"));
    stage.set("lc", p.f("hpf"));
}

/// Sets the WING CHORUS's controls (both sides alike, spread wide for SYMPHONIC).
pub(crate) fn chorus(stage: &mut Stage, p: &Params, depth_id: &str, spread: f32) {
    stage.set("spd", p.f("mf"));
    let d = p.f(depth_id);
    stage.set("depl", d);
    stage.set("depr", d);
    stage.set("dlyl", 5.0 + p.f("mdl"));
    stage.set("dlyr", 5.0 + p.f("mdl"));
    stage.set("sprd", spread);
}

impl PmFx {
    fn new(token: &'static str, ctx: FxContext) -> Option<Self> {
        let stage = |w: &str| Stage::new(w, ctx);
        let (stages, taps) = match token {
            "PMREV" => (vec![stage("ROOM")?], None),
            "PMCHREV" => (vec![stage("CHORUS")?, stage("ROOM")?], None),
            "PMFLREV" => (vec![stage("FLANGER")?, stage("ROOM")?], None),
            "PMDLCR" => (vec![], Some(Taps::new(TapMode::Lcr, ctx))),
            "PMMDCHO" => (vec![stage("CHORUS")?], Some(Taps::new(TapMode::Mono, ctx))),
            "PMCHDLY" => (vec![stage("CHORUS")?], Some(Taps::new(TapMode::Lcr, ctx))),
            "PMDLCHO" => (vec![stage("CHORUS")?], Some(Taps::new(TapMode::Two, ctx))),
            "PMECHO" => (vec![], Some(Taps::new(TapMode::Echo, ctx))),
            "PMPITCH" => (vec![stage("PITCH")?, stage("PITCH")?], None),
            "PMCHO" | "PMSYMP" => (vec![stage("CHORUS")?], None),
            "PMFLG" => (vec![stage("FLANGER")?], None),
            "PMPHAS" => (vec![stage("PHASER")?], None),
            "PMTREM" | "PMAPAN" => (vec![stage("PANNER")?], None),
            _ => return None,
        };
        let n = ctx.max_block;
        Some(PmFx {
            token,
            stages,
            taps,
            rev_depth: 1.0,
            voices: [(1.0, -100.0), (1.0, 100.0)],
            scratch: [vec![0.0; n], vec![0.0; n], vec![0.0; n], vec![0.0; n]],
        })
    }
}

impl FxAlgo for PmFx {
    fn set_params(&mut self, p: &Params) {
        let f = |id: &str| p.f(id);
        match self.token {
            "PMREV" => reverb(&mut self.stages[0], p),
            "PMCHREV" | "PMFLREV" => {
                if self.token == "PMCHREV" {
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
                self.rev_depth = 0.01 * f("rdep");
            }
            "PMDLCR" | "PMCHDLY" => {
                if self.token == "PMCHDLY" {
                    chorus(&mut self.stages[0], p, "md", 50.0);
                }
                let t = self.taps.as_mut().expect("taps");
                t.set([f("dl"), f("dr"), f("dc"), f("fbd")], 0.01 * f("lc"), [f("fbg"); 2], f("hr"));
            }
            "PMMDCHO" => {
                chorus(&mut self.stages[0], p, "md", 50.0);
                let t = self.taps.as_mut().expect("taps");
                t.set([f("d1"); 4], 0.0, [f("fbg"); 2], f("hr"));
            }
            "PMDLCHO" => {
                chorus(&mut self.stages[0], p, "md", 50.0);
                let t = self.taps.as_mut().expect("taps");
                t.set([f("d1"), f("d2"), 1.0, f("fbd")], 0.0, [f("fbg"); 2], f("hr"));
            }
            "PMECHO" => {
                let t = self.taps.as_mut().expect("taps");
                t.set([f("dl"), f("dr"), 1.0, 1.0], 0.0, [f("fbl"), f("fbr")], f("hr"));
            }
            "PMPITCH" => {
                for (k, (fine, out, pan)) in [("f1", "o1", "p1"), ("f2", "o2", "p2")].into_iter().enumerate() {
                    let s = &mut self.stages[k];
                    s.set("semi", f("pit"));
                    s.set("cent", f(fine));
                    s.set("dly", 0.0);
                    self.voices[k] = (0.01 * f(out), f(pan));
                }
            }
            "PMCHO" => chorus(&mut self.stages[0], p, "pm", 50.0),
            "PMSYMP" => chorus(&mut self.stages[0], p, "md", 100.0),
            "PMFLG" => {
                let s = &mut self.stages[0];
                s.set("spd", f("mf"));
                s.set("depl", f("md"));
                s.set("depr", f("md"));
                s.set("dlyl", f("mdl"));
                s.set("dlyr", f("mdl"));
                s.set("feed", f("fbg"));
            }
            "PMPHAS" => {
                let s = &mut self.stages[0];
                s.set("spd", f("mf"));
                s.set("depth", f("md"));
                s.set("reso", 0.8 * f("fbg").abs());
                s.set("phase", 1.8 * f("ofs"));
            }
            "PMTREM" | "PMAPAN" => {
                let s = &mut self.stages[0];
                s.set("spd", f("mf"));
                s.set("depth", f(if self.token == "PMTREM" { "am" } else { "lr" }));
                // A tremolo moves both sides together, an auto pan against each other.
                s.set("phase", if self.token == "PMTREM" { 0.0 } else { 180.0 });
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
            "PMCHREV" | "PMFLREV" => {
                self.stages[0].dev.process(left, right);
                let [a, b, ..] = &mut self.scratch;
                a[..n].copy_from_slice(left);
                b[..n].copy_from_slice(right);
                self.stages[1].dev.process(&mut a[..n], &mut b[..n]);
                let d = self.rev_depth;
                for i in 0..n {
                    left[i] += d * (a[i] - left[i]);
                    right[i] += d * (b[i] - right[i]);
                }
            }
            "PMPITCH" => {
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
                let taps_first = matches!(self.token, "PMMDCHO" | "PMDLCHO");
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

/// Creates the device of ProMix effect type `token`; `None` for other tokens.
pub fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    let spec = FX_MODELS.iter().find(|m| m.token == token && token.starts_with("PM"))?;
    Some(Box::new(PmFx::new(spec.token, ctx)?))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_promix01s_effect_types() {
        assert_eq!(FX_MODELS.len(), 17);
        assert_eq!((FX_MODELS[0].token, FX_MODELS[1].token), ("NONE", "EXT"));
        for m in &FX_MODELS[2..] {
            assert!(m.params.len() <= 10, "{}: at most 10 parameters (CC 406-445)", m.token);
            for ps in m.params {
                assert!((ps.lo()..=ps.hi()).contains(&ps.default), "{} {}", m.token, ps.id);
                assert!(ps.steps <= 16384, "{} {}: fits 14 bits", m.token, ps.id);
            }
            crate::wing::fx::tests::check_device(m.token);
        }
        let rev = &FX_MODELS[2];
        assert_eq!(rev.params.iter().find(|p| p.id == "rt").unwrap().steps, 298, "0.3-30.0 s in 0.1 s");
        assert_eq!(LPF_FREQS.len(), 26);
        assert_eq!(HPF_FREQS[0], 20.0, "THRU");
    }

    #[test]
    fn the_delays_repeat_where_they_are_set() {
        let ctx = FxContext { sample_rate: 48000.0, max_block: 64 };
        let model = crate::model::model_index(&FX_MODELS, "PMDLCR").unwrap();
        let mut dev = crate::wing::fx::FxDevice::new_in(&FX_MODELS, model, ctx).unwrap();
        let mut values = [0.0; MAX_FX_PARAMS];
        FX_MODELS[model].defaults_into(&mut values);
        // L at 10 ms, R at 20 ms, C at 5 ms (level 0), no feedback.
        for (id, v) in [("dl", 10.0), ("dr", 20.0), ("dc", 5.0), ("lc", 0.0), ("fbg", 0.0)] {
            values[FX_MODELS[model].param_index(id).unwrap()] = v;
        }
        let (mut l, mut r) = (vec![0.0f32; 2048], vec![0.0f32; 2048]);
        l[0] = 1.0;
        r[0] = 1.0;
        for (cl, cr) in l.chunks_mut(64).zip(r.chunks_mut(64)) {
            dev.process(&values, cl, cr);
        }
        let peak = |x: &[f32]| x.iter().enumerate().fold((0, 0.0f32), |m, (i, v)| if v.abs() > m.1 { (i, v.abs()) } else { m }).0;
        assert_eq!((peak(&l), peak(&r)), (480, 960));
    }
}
