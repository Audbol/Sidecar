//! The PRO2's internal effects (Owner's Manual, Chapter 16, pp.127-142, "OM"; the
//! DN780's programs from the PRO X manual's Appendix D, pp.223-226, the same
//! virtual unit). Each model has the controls and ranges the manual lists, so far as
//! it gives them.
//!
//! The manual gives controls, not code: **every algorithm here is Sidecar's own**,
//! written from the manual's description of what each device does. Clean-room as
//! the rest of `midas_pro`. In particular the DN780 is a plain feedback delay
//! network and a set of tap delays that follow the unit's published controls and
//! program notes; it is not a model of the unit's algorithms. To be compared with a
//! console.
//!
//! Steps and defaults the manual doesn't give are Sidecar's reading (the panels'
//! pictures on pp.130-140 give the defaults that are readable). So are: the delay's
//! note values under BPM Sync and its modulation depth; the shelves' corner
//! frequencies; the phaser's stages list, its sweep names and range; the pitch
//! shifter's window range; COARSE read as semitones (the manual says "whole tones",
//! and FINE in hundredths of one: read as a whole number of semitones); the 3-band
//! compressor's "partially adaptive" time constants; the dynamic EQ's attack and
//! release, its LIMIT, and its proportional Q curve.
//!
//! Not done: SQ1 Dynamics (8 inputs and 8 outputs, a stereo slot can't hold it; its
//! ranges are in an operator's manual that wasn't available), the dynamic EQ's 2x2
//! and 4x1 routings (the same reason: it's one stereo chain here), the 3-band's Soft
//! Link, the meters and the DN780's momentary mute on a ROOMSIZE change.

#![allow(clippy::needless_range_loop)] // parallel arrays of lines, gains and filters

use crate::biquad::{Biquad, BiquadCoeffs};
use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec, Params};
use crate::util::{db_to_gain, gain_to_db};
use crate::wing::fx::blocks::{fast_tanh, Allpass, DelayLine, OnePole};
use crate::wing::fx::{FxAlgo, FxContext};
use std::f32::consts::{FRAC_PI_2, PI, TAU};

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

const fn int(id: &'static str, label: &'static str, min: f32, max: f32, default: f32) -> ParamSpec {
    ParamSpec { id, label, unit: "", kind: ParamKind::Int, min, max, steps: (max - min) as u32 + 1, default }
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

const fn sw(id: &'static str, label: &'static str, default: f32) -> ParamSpec {
    ParamSpec { id, label, unit: "", kind: ParamKind::Toggle, min: 0.0, max: 1.0, steps: 2, default }
}

const fn pct(id: &'static str, label: &'static str, min: f32, default: f32) -> ParamSpec {
    lin(id, label, "%", min, 100.0, (100.0 - min) as u32 + 1, default)
}

const HI_EQ: ParamSpec = lin("hi", "HI EQ", "dB", -12.0, 12.0, 49, 0.0);
const LO_EQ: ParamSpec = lin("lo", "LO EQ", "dB", -12.0, 12.0, 49, 0.0);
const GAIN: ParamSpec = lin("gain", "GAIN", "dB", -20.0, 20.0, 81, 0.0);
const MIX: ParamSpec = pct("mix", "MIX", 0.0, 100.0);

/// The delay's modes (OM p.130).
const DELAY_MODES: &[&str] = &["Single", "Dual", "Ping-Pong"];
const DELAY_RANGES: &[&str] = &["1-25 ms", "10-200 ms", "80-1600 ms"];
const DELAY_RANGES_MS: [(f32, f32); 3] = [(1.0, 25.0), (10.0, 200.0), (80.0, 1600.0)];
const MODELS_DA: &[&str] = &["Digital", "Analogue"];
/// Delay times under BPM Sync, in beats (Sidecar's list: "ms or note duration").
const NOTE_NAMES: &[&str] = &["1/16", "1/8T", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2"];
const NOTE_BEATS: [f32; 9] = [0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0];

/// The delay (OM pp.130-131: the panel's starting times, 366.4 ms at 150 bpm).
static DELAY: [ParamSpec; 24] = [
    ch("mode", "MODE", DELAY_MODES, 0.0),
    ch("rng", "RANGE", DELAY_RANGES, 2.0),
    log("dl", "LEFT TIME", "ms", 1.0, 1600.0, 366.4),
    log("dr", "RIGHT TIME", "ms", 1.0, 1600.0, 366.4),
    sw("lnk", "LINK", 1.0),
    sw("bpm", "BPM SYNC", 0.0),
    ch("nl", "LEFT NOTE", NOTE_NAMES, 5.0),
    ch("nr", "RIGHT NOTE", NOTE_NAMES, 5.0),
    lin("tmp", "TEMPO", "bpm", 60.0, 240.0, 1801, 150.0),
    lin("pl", "LEFT PAN", "", -100.0, 100.0, 201, -100.0),
    lin("pr", "RIGHT PAN", "", -100.0, 100.0, 201, 100.0),
    lin("ll", "LEFT LEVEL", "dB", -60.0, 10.0, 141, 0.0),
    lin("lr", "RIGHT LEVEL", "dB", -60.0, 10.0, 141, 0.0),
    pct("fl", "LEFT FEEDBACK", -100.0, 25.0),
    pct("fr", "RIGHT FEEDBACK", -100.0, 25.0),
    pct("bl", "LEFT BLEND", 0.0, 0.0),
    pct("br", "RIGHT BLEND", 0.0, 0.0),
    pct("dpl", "LEFT DEPTH", 0.0, 0.0),
    pct("dpr", "RIGHT DEPTH", 0.0, 0.0),
    log("rt", "RATE", "Hz", 0.001, 10.0, 0.7),
    lin("hfd", "HF DEPTH", "", 0.0, 100.0, 101, 50.0),
    log("hf", "HF DAMPING", "Hz", 2000.0, 24000.0, 12000.0),
    log("lf", "LF DAMPING", "Hz", 10.0, 200.0, 20.0),
    ch("md", "MODEL", MODELS_DA, 0.0),
];

static DELAY_FULL: [ParamSpec; 28] = {
    let mut o = [HI_EQ; 28];
    let mut i = 0;
    while i < DELAY.len() {
        o[i] = DELAY[i];
        i += 1;
    }
    o[24] = HI_EQ;
    o[25] = LO_EQ;
    o[26] = GAIN;
    o[27] = MIX;
    o
};

/// The DN780's programs (the panel, OM p.132).
pub const DN780_PROGRAMS: &[&str] = &[
    "Hall",
    "Plate",
    "Chamber",
    "Room",
    "Alive",
    "Non-Linear",
    "Reverse",
    "Delay",
    "ADT",
    "Multitap Echo",
    "Sound-On-Sound",
    "Infinite Room",
];

/// The Virtual DN780 (OM pp.132-133; pattern 0-9 and pre-delay to 2 s as Appendix D
/// of the PRO X manual has them; the starting panel from p.132's picture).
static DN780: [ParamSpec; 13] = [
    ch("alg", "PROGRAM", DN780_PROGRAMS, 0.0),
    lin("pre", "PRE DELAY", "ms", 0.0, 2000.0, 2001, 40.0),
    int("pat", "PATTERN", 0.0, 9.0, 8.0),
    int("lvl", "LEVEL", 0.0, 9.0, 5.0),
    log("dec", "DECAY", "s", 0.1, 18.0, 1.6),
    int("lf", "LF", -7.0, 7.0, 0.0),
    int("hf", "HF", -7.0, 7.0, 5.0),
    log("size", "ROOM SIZE", "m", 8.0, 90.0, 8.0),
    MIX,
    sw("st", "ST", 0.0),
    lin("in", "INPUT LEVEL", "dB", -4.0, 6.0, 101, 0.0),
    sw("imt", "INPUT MUTE", 0.0),
    sw("rmt", "REV MUTE", 0.0),
];

/// The flanger (OM pp.134-135: 5, 0.7 Hz, 90, 10 kHz, 140 Hz at top dead centre).
static FLANGER: [ParamSpec; 14] = [
    log("dly", "DELAY", "ms", 0.1, 10.0, 5.0),
    pct("fb", "FEEDBACK", -100.0, 0.0),
    GAIN,
    sw("inv", "INVERT", 0.0),
    pct("mix", "MIX", 0.0, 50.0),
    HI_EQ,
    LO_EQ,
    log("hf", "HF", "Hz", 1000.0, 20000.0, 10000.0),
    log("lf", "LF", "Hz", 20.0, 1000.0, 140.0),
    pct("dep", "DEPTH", 0.0, 50.0),
    sw("tz", "THRU ZERO", 0.0),
    lin("spr", "SPREAD", "deg", 0.0, 180.0, 181, 90.0),
    pct("shp", "SHAPE", 0.0, 0.0),
    log("rt", "RATE", "Hz", 0.01, 50.0, 0.7),
];

const PHASER_MODES: &[&str] = &["Single", "Dual Series", "Dual Parallel", "Linked Series", "Linked Parallel"];
const PHASER_STAGES: &[&str] = &["2", "4", "6", "8", "10", "12"];
const PHASER_SWEEPS: &[&str] = &["Sine", "Triangle", "Log"];

/// The phaser (OM pp.135-136: the panel shows SINGLE, LOG and 4 stages).
static PHASER: [ParamSpec; 14] = [
    lin("r1", "RATE 1", "Hz", 0.0, 40.0, 401, 0.7),
    log("m1", "MANUAL 1", "Hz", 500.0, 24000.0, 3500.0),
    lin("r2", "RATE 2", "Hz", 0.0, 40.0, 401, 0.7),
    log("m2", "MANUAL 2", "Hz", 500.0, 24000.0, 3500.0),
    HI_EQ,
    LO_EQ,
    GAIN,
    pct("fb", "FEEDBACK", -100.0, 0.0),
    pct("spin", "SPIN", 0.0, 50.0),
    ch("stg", "STAGES", PHASER_STAGES, 1.0),
    ch("swp", "SWEEP", PHASER_SWEEPS, 2.0),
    ch("mode", "MODE", PHASER_MODES, 0.0),
    pct("dep", "DEPTH", 0.0, 50.0),
    pct("bld", "BLEND", 0.0, 50.0),
];

/// The pitch shifter's two channels and what they share (OM p.137: 22, 50, -20, 6k
/// and 50 at top dead centre).
static PITCH: [ParamSpec; 21] = [
    int("c1", "COARSE 1", -12.0, 12.0, 0.0),
    int("f1", "FINE 1", -100.0, 100.0, 10.0),
    log("p1", "PREDELAY 1", "ms", 1.0, 500.0, 22.0),
    log("d1", "FB DELAY 1", "ms", 1.0, 500.0, 22.0),
    pct("b1", "FEEDBACK 1", 0.0, 0.0),
    lin("l1", "LEVEL 1", "dB", -40.0, -10.0, 301, -20.0),
    lin("n1", "PAN 1", "", -100.0, 100.0, 201, -50.0),
    int("c2", "COARSE 2", -12.0, 12.0, 0.0),
    int("f2", "FINE 2", -100.0, 100.0, -10.0),
    log("p2", "PREDELAY 2", "ms", 1.0, 500.0, 22.0),
    log("d2", "FB DELAY 2", "ms", 1.0, 500.0, 22.0),
    pct("b2", "FEEDBACK 2", 0.0, 0.0),
    lin("l2", "LEVEL 2", "dB", -40.0, -10.0, 301, -20.0),
    lin("n2", "PAN 2", "", -100.0, 100.0, 201, 50.0),
    log("hf", "HF DAMP", "Hz", 2000.0, 20000.0, 6000.0),
    log("lf", "LF DAMP", "Hz", 20.0, 400.0, 40.0),
    HI_EQ,
    LO_EQ,
    lin("out", "OUT LVL", "dB", -20.0, 20.0, 81, 0.0),
    pct("mix", "MIX", 0.0, 50.0),
    log("win", "WIN SZ", "ms", 10.0, 100.0, 40.0),
];

macro_rules! comp_band {
    ($t:literal, $r:literal, $g:literal, $l:literal, $a:literal) => {
        [
            lin($t, concat!($t, " THRESHOLD"), "dB", -50.0, 25.0, 751, 0.0),
            log($r, concat!($r, " RATIO"), "", 1.0, 25.0, 2.0),
            lin($g, concat!($g, " MAKE-UP"), "dB", 0.0, 24.0, 241, 0.0),
            log($l, concat!($l, " RELEASE"), "ms", 50.0, 3000.0, 300.0),
            log($a, concat!($a, " ATTACK"), "ms", 0.2, 50.0, 10.0),
        ]
    };
}

const fn splice3(a: [ParamSpec; 5], b: [ParamSpec; 5], c: [ParamSpec; 5]) -> [ParamSpec; 18] {
    let mut o = [HI_EQ; 18];
    let mut i = 0;
    while i < 5 {
        o[i] = a[i];
        o[i + 5] = b[i];
        o[i + 10] = c[i];
        i += 1;
    }
    o[15] = log("x1", "LO MID FREQ", "Hz", 40.0, 1000.0, 200.0);
    o[16] = log("x2", "HI MID FREQ", "Hz", 640.0, 16000.0, 3000.0);
    o[17] = lin("kn", "GLOBAL KNEE", "dB", 4.0, 40.0, 37, 12.0);
    o
}

/// The 3-band compressor (OM p.139; the parameters are named by band in the labels).
static THREE_BAND: [ParamSpec; 18] = splice3(
    comp_band!("LT", "LR", "LG", "LL", "LA"),
    comp_band!("MT", "MR", "MG", "ML", "MA"),
    comp_band!("HT", "HR", "HG", "HL", "HA"),
);

const DEQ_TYPES: &[&str] = &["Bell", "Low Shelf", "High Shelf", "Full Band"];
const DEQ_FIELDS: usize = 11;

macro_rules! deq_band {
    ($i:literal, $f:literal) => {
        [
            sw(concat!("in", $i), concat!("IN ", $i), 0.0),
            sw(concat!("ls", $i), concat!("LSN ", $i), 0.0),
            ch(concat!("ty", $i), concat!("TYPE ", $i), DEQ_TYPES, 0.0),
            log(concat!("w", $i), concat!("WIDTH ", $i), "oct", 0.1, 3.0, 1.0),
            sw(concat!("fx", $i), concat!("FIXED ", $i), 0.0),
            lin(concat!("r", $i), concat!("RATIO ", $i), "", -100.0, 100.0, 201, -40.0),
            sw(concat!("fr", $i), concat!("FAST REL ", $i), 0.0),
            log(concat!("f", $i), concat!("FREQ ", $i), "Hz", 20.0, 20000.0, $f),
            sw(concat!("be", $i), concat!("BELOW ", $i), 0.0),
            lin(concat!("t", $i), concat!("THRESHOLD ", $i), "dB", -60.0, 20.0, 161, -20.0),
            lin(concat!("g", $i), concat!("LIMIT ", $i), "dB", 0.0, 24.0, 97, 12.0),
        ]
    };
}

const fn splice4(
    a: [ParamSpec; DEQ_FIELDS],
    b: [ParamSpec; DEQ_FIELDS],
    c: [ParamSpec; DEQ_FIELDS],
    d: [ParamSpec; DEQ_FIELDS],
) -> [ParamSpec; 44] {
    let mut o = [HI_EQ; 44];
    let mut i = 0;
    while i < DEQ_FIELDS {
        o[i] = a[i];
        o[i + DEQ_FIELDS] = b[i];
        o[i + 2 * DEQ_FIELDS] = c[i];
        o[i + 3 * DEQ_FIELDS] = d[i];
        i += 1;
    }
    o
}

/// The dynamic EQ (OM pp.140-142), one stereo chain of its four bands.
static DYN_EQ: [ParamSpec; 44] =
    splice4(deq_band!("1", 100.0), deq_band!("2", 500.0), deq_band!("3", 2000.0), deq_band!("4", 8000.0));

const fn device(token: &'static str, name: &'static str, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name, category: ModelCategory::FxStandard, params }
}

/// The effects: NONE and EXT, then the eight of the manual's list less SQ1.
pub static FX_MODELS: [ModelSpec; 9] = [
    crate::wing::FX_MODELS[0],
    crate::wing::FX_MODELS[1],
    device("MPDLY", "Delay", &DELAY_FULL),
    device("MPDN780", "Virtual DN780", &DN780),
    device("MPFLG", "Flanger", &FLANGER),
    device("MPPHS", "Phaser", &PHASER),
    device("MPPIT", "Pitch Shifter", &PITCH),
    device("MP3BC", "3-Band Compressor", &THREE_BAND),
    device("MPDEQ", "Dynamic EQ", &DYN_EQ),
];

pub fn is_pro2(token: &str) -> bool {
    token.starts_with("MP") && FX_MODELS.iter().any(|m| m.token == token)
}

/// Saturates anything over 1 towards 2, so feedback settles (loops and tails).
#[inline]
fn limit(x: f32) -> f32 {
    let a = x.abs();
    if a <= 1.0 {
        x
    } else {
        (1.0 + fast_tanh(a - 1.0)).copysign(x)
    }
}

/// Left and right gains for a pan of -1..1, equal power (Sidecar's reading).
fn pan_gains(p: f32) -> (f32, f32) {
    let a = (p.clamp(-1.0, 1.0) + 1.0) * 0.25 * PI;
    (a.cos(), a.sin())
}

/// The effect output's two shelves (Sidecar's corners: 250 Hz, 6 kHz) and level.
struct Output {
    lo: Biquad,
    hi: Biquad,
    sr: f64,
    gain: f32,
    busy: bool,
}

impl Output {
    fn new(sr: f32) -> Self {
        Self { lo: Biquad::default(), hi: Biquad::default(), sr: sr as f64, gain: 1.0, busy: false }
    }

    fn set(&mut self, lo: f32, hi: f32, gain_db: f32) {
        self.lo.set_coefficients(BiquadCoeffs::low_shelf(self.sr, 250.0, 0.707, lo as f64));
        self.hi.set_coefficients(BiquadCoeffs::high_shelf(self.sr, 6000.0, 0.707, hi as f64));
        self.gain = db_to_gain(gain_db);
        self.busy = lo.abs() > 1e-3 || hi.abs() > 1e-3;
    }

    #[inline]
    fn tick(&mut self, x: f32, ch: usize) -> f32 {
        let y = if self.busy { self.hi.tick(self.lo.tick(x, ch), ch) } else { x };
        // The panels' gains can stack up past any headroom: a stop at +30 dBFS.
        (y * self.gain).clamp(-32.0, 32.0)
    }

    fn reset(&mut self) {
        self.lo.reset();
        self.hi.reset();
    }
}

// ---- delay ----------------------------------------------------------------------------

/// The delay's LFO moves the time by this much at depth 100 (Sidecar's reading).
const DELAY_DEPTH_MS: f32 = 2.0;

struct Delay {
    sr: f32,
    lines: [DelayLine; 2],
    time: [f32; 2],
    target: [f32; 2],
    feedback: [f32; 2],
    blend: [f32; 2],
    depth: [f32; 2],
    pans: [(f32, f32); 2],
    levels: [f32; 2],
    phase: f32,
    inc: f32,
    hf: [OnePole; 2],
    lf: [OnePole; 2],
    hf_hz: f32,
    hf_depth: f32,
    analogue: bool,
    mode: usize,
    mix: f32,
    out: Output,
    tick_n: usize,
    primed: bool,
}

impl Delay {
    fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let max = (1.7 * sr) as usize + 4;
        Self {
            sr,
            lines: [DelayLine::new(max), DelayLine::new(max)],
            time: [sr * 0.1; 2],
            target: [sr * 0.1; 2],
            feedback: [0.0; 2],
            blend: [0.0; 2],
            depth: [0.0; 2],
            pans: [(1.0, 0.0), (0.0, 1.0)],
            levels: [1.0; 2],
            phase: 0.0,
            inc: 0.0,
            hf: [OnePole::new(12000.0, sr); 2],
            lf: [OnePole::new(20.0, sr); 2],
            hf_hz: 12000.0,
            hf_depth: 0.0,
            analogue: false,
            mode: 0,
            mix: 1.0,
            out: Output::new(sr),
            tick_n: 0,
            primed: false,
        }
    }
}

impl FxAlgo for Delay {
    fn set_params(&mut self, p: &Params) {
        self.mode = p.choice("mode");
        let (lo, hi) = DELAY_RANGES_MS[p.choice("rng").min(2)];
        let beat = 60000.0 / p.f("tmp");
        let bpm = p.on("bpm");
        let ms = |t: &str, n: &str| {
            let value = if bpm { beat * NOTE_BEATS[p.choice(n).min(8)] } else { p.f(t) };
            value.clamp(lo, hi)
        };
        let dl = ms("dl", "nl");
        let dr = if p.on("lnk") { dl } else { ms("dr", "nr") };
        self.target = [dl * 0.001 * self.sr, dr * 0.001 * self.sr];
        if !self.primed {
            self.primed = true;
            self.time = self.target;
        }
        self.feedback = [p.f("fl") * 0.01, p.f("fr") * 0.01];
        self.blend = [p.f("bl") * 0.01, p.f("br") * 0.01];
        self.depth = [p.f("dpl") * 0.01, p.f("dpr") * 0.01];
        self.pans = [pan_gains(p.f("pl") * 0.01), pan_gains(p.f("pr") * 0.01)];
        let level = |id: &str| if p.f(id) <= -60.0 { 0.0 } else { db_to_gain(p.f(id)) };
        self.levels = [level("ll"), level("lr")];
        self.inc = p.f("rt") / self.sr;
        self.hf_hz = p.f("hf");
        self.hf_depth = p.f("hfd") * 0.01;
        self.lf.iter_mut().for_each(|f| f.set(p.f("lf"), self.sr));
        self.analogue = p.choice("md") == 1;
        self.mix = p.f("mix") * 0.01;
        self.out.set(p.f("lo"), p.f("hi"), p.f("gain"));
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let depth_n = DELAY_DEPTH_MS * 0.001 * self.sr;
        for i in 0..left.len() {
            let (l, r) = (left[i], right[i]);
            self.phase += self.inc;
            if self.phase >= 1.0 {
                self.phase -= 1.0;
            }
            let lfo = (TAU * self.phase).sin();
            if self.tick_n & 15 == 0 {
                // The damping filter moves with the LFO (HF Depth).
                let top = self.hf_hz * (0.5 * self.hf_depth * lfo).exp2();
                let top = if self.analogue { top.min(6000.0) } else { top };
                self.hf.iter_mut().for_each(|f| f.set(top, self.sr));
            }
            self.tick_n = self.tick_n.wrapping_add(1);
            let mut y = [0.0f32; 2];
            for k in 0..2 {
                self.time[k] += (self.target[k] - self.time[k]) * 0.0015;
                let t = (self.time[k] + lfo * self.depth[k] * depth_n).max(1.0);
                y[k] = self.lines[k].tap_hermite(t);
            }
            let mix_in = 0.5 * (l + r);
            let inputs: [f32; 2] = match self.mode {
                2 => [mix_in, 0.0],
                _ => [l, r],
            };
            for k in 0..2 {
                let o = 1 - k;
                let cross = if self.mode == 2 { 1.0 } else { self.blend[k] };
                let back = (1.0 - cross) * y[k] + cross * y[o];
                let back = self.hf[k].lp(back);
                let back = self.lf[k].hp(back);
                let mut x = inputs[k] + self.feedback[k] * back;
                if self.analogue {
                    x = fast_tanh(x * 1.1) / 1.1;
                }
                self.lines[k].push(limit(x));
            }
            let (a, b) = (y[0] * self.levels[0], y[1] * self.levels[1]);
            let wet_l = self.out.tick(a * self.pans[0].0 + b * self.pans[1].0, 0);
            let wet_r = self.out.tick(a * self.pans[0].1 + b * self.pans[1].1, 1);
            left[i] = l + (wet_l - l) * self.mix;
            right[i] = r + (wet_r - r) * self.mix;
        }
    }

    fn reset(&mut self) {
        self.lines.iter_mut().for_each(DelayLine::clear);
        self.hf.iter_mut().for_each(OnePole::reset);
        self.lf.iter_mut().for_each(OnePole::reset);
        self.out.reset();
        self.time = self.target;
        self.phase = 0.0;
    }
}

// ---- Virtual DN780 ---------------------------------------------------------------------

const REV_LINES: usize = 8;
/// The lines' lengths against the room (distinct, so the modes spread).
const REV_RATIOS: [f32; REV_LINES] = [1.0, 1.13, 1.27, 1.39, 1.51, 1.62, 1.79, 1.93];
/// How much of the room each family's network uses: Hall, Plate, Chamber, Room.
const REV_SCALE: [f32; 4] = [1.0, 0.6, 0.8, 0.5];
const ER_TIMES: [f32; 16] = [0.11, 0.19, 0.24, 0.31, 0.37, 0.43, 0.48, 0.53, 0.58, 0.64, 0.69, 0.75, 0.81, 0.87, 0.93, 1.0];
const MAX_TAPS: usize = 48;
/// The longest delay of the long line (the Sound-On-Sound loop, the Delay program).
const LONG_MAX_S: f32 = 2.2;

#[derive(Clone, Copy, Default)]
struct Tap {
    delay: usize,
    left: f32,
    right: f32,
}

struct Dn780 {
    sr: f32,
    pre: [DelayLine; 2],
    pre_n: usize,
    diff: [Allpass; 4],
    lines: Vec<DelayLine>,
    len: [usize; REV_LINES],
    g: [f32; REV_LINES],
    g_lf: [f32; REV_LINES],
    low: [OnePole; REV_LINES],
    damp: [OnePole; REV_LINES],
    er: DelayLine,
    er_taps: [Tap; 16],
    er_n: usize,
    er_gain: f32,
    long: [DelayLine; 2],
    taps: [Tap; MAX_TAPS],
    n: usize,
    loop_n: usize,
    loop_fb: f32,
    loop_lp: [OnePole; 2],
    alg: usize,
    in_gain: f32,
    in_scale: f32,
    mix: f32,
    stereo: bool,
    in_mute: bool,
    rev_mute: bool,
}

impl Dn780 {
    fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let line_max = (0.56 * sr) as usize + 8;
        let ap = |ms: f32, g: f32| Allpass::new((ms * 0.001 * sr) as usize + 4, (ms * 0.001 * sr).max(1.0), g);
        Self {
            sr,
            pre: [DelayLine::new((2.1 * sr) as usize), DelayLine::new((2.1 * sr) as usize)],
            pre_n: 1,
            diff: [ap(4.77, 0.7), ap(3.59, 0.7), ap(5.31, 0.7), ap(3.13, 0.7)],
            lines: (0..REV_LINES).map(|_| DelayLine::new(line_max)).collect(),
            len: [1000; REV_LINES],
            g: [0.7; REV_LINES],
            g_lf: [0.7; REV_LINES],
            low: [OnePole::new(300.0, sr); REV_LINES],
            damp: [OnePole::new(6000.0, sr); REV_LINES],
            er: DelayLine::new((0.2 * sr) as usize),
            er_taps: [Tap::default(); 16],
            er_n: 8,
            er_gain: 0.3,
            long: [DelayLine::new((LONG_MAX_S * sr) as usize), DelayLine::new((LONG_MAX_S * sr) as usize)],
            taps: [Tap::default(); MAX_TAPS],
            n: 0,
            loop_n: 0,
            loop_fb: 0.0,
            loop_lp: [OnePole::new(12000.0, sr); 2],
            alg: 0,
            in_gain: 1.0,
            in_scale: 1.0,
            mix: 1.0,
            stereo: false,
            in_mute: false,
            rev_mute: false,
        }
    }

    fn samples(&self, ms: f32) -> usize {
        ((ms * 0.001 * self.sr) as usize).max(1)
    }

    /// The network's lengths and gains for a reverberation program (`rev` of 0..3,
    /// or Infinite Room: the Room's lengths, no decay).
    fn set_reverb(&mut self, p: &Params, kind: usize, infinite: bool) {
        let size = p.f("size");
        let base = (size / 343.0) * self.sr * REV_SCALE[kind];
        let t60 = p.f("dec");
        let lf_mult = (p.f("lf") / 4.0).exp2();
        let hf_mult = (p.f("hf") / 4.0).exp2();
        for i in 0..REV_LINES {
            let len = (base * REV_RATIOS[i]).max(16.0) as usize;
            self.len[i] = len;
            let seconds = len as f32 / self.sr;
            if infinite {
                self.g[i] = 1.0;
                self.g_lf[i] = 1.0;
                self.damp[i].set(18000.0, self.sr);
                continue;
            }
            self.g[i] = 10f32.powf(-3.0 * seconds / t60);
            self.g_lf[i] = 10f32.powf(-3.0 * seconds / (t60 * lf_mult));
            // The HF key lengthens or shortens the top's decay: a one-pole whose
            // corner rides with it (Sidecar's mapping).
            self.damp[i].set(7000.0 * hf_mult.sqrt() * (t60 / 2.0).powf(0.25).min(2.0), self.sr);
        }
    }

    /// The early reflections: PATTERN sets how many, LEVEL how close.
    fn set_reflections(&mut self, p: &Params) {
        self.er_n = (6.0 + p.f("pat")).min(16.0) as usize;
        self.er_gain = 0.1 + 0.09 * p.f("lvl");
        let span = (0.4 * p.f("size") / 343.0).min(0.19);
        for i in 0..16 {
            let delay = self.samples(span * 1000.0 * ER_TIMES[i]);
            let sign = if i % 3 == 1 { -1.0 } else { 1.0 };
            let g = (1.0 - i as f32 / 20.0) * sign;
            self.er_taps[i] =
                Tap { delay, left: if i % 2 == 0 { g } else { 0.4 * g }, right: if i % 2 == 0 { 0.4 * g } else { g } };
        }
    }

    /// Alive, Non-Linear and Reverse: a burst of taps whose shape is the program's.
    fn set_burst(&mut self, p: &Params) {
        let n = (8 + 4 * p.f("pat") as usize).min(MAX_TAPS);
        // DECAY's knob, from its 1 to its 12 (PRO X manual p.225: "not calibrated").
        let steps = 1.0 + 11.0 * normalised_decay(p.f("dec"));
        let length_ms = 40.0 * steps;
        let lf = (p.f("lf") * 0.05).clamp(-0.3, 0.3);
        let scale = 2.0 / (n as f32).sqrt();
        let start = p.f("pre").min(990.0);
        for i in 0..n {
            let x = (i as f32 + 0.5) / n as f32;
            let env = match self.alg {
                4 => (-3.0 * x).exp(),
                5 => {
                    if x < 0.85 {
                        1.0
                    } else {
                        ((1.0 - x) / 0.15).max(0.0)
                    }
                }
                _ => 0.08 + 0.92 * x * x,
            };
            let sign = if (i * 7) % 5 < 2 { -1.0 } else { 1.0 };
            let jitter = ((i * 37) % 11) as f32 / 11.0 - 0.5;
            let delay = self.samples(start + 1.0 + length_ms * (x + jitter * 0.5 / n as f32));
            let g = env * sign * scale;
            self.taps[i] = Tap { delay, left: g * (1.0 + lf), right: if i % 2 == 0 { -g * (1.0 - lf) } else { g * (1.0 - lf) } };
        }
        self.n = n;
        self.loop_n = 0;
        self.loop_fb = 0.0;
        self.in_scale = 1.0;
    }

    /// The Delay, ADT, Multitap Echo and Sound-On-Sound programs (PRO X manual pp.223-224).
    fn set_special(&mut self, p: &Params) {
        let pre = p.f("pre");
        let dec = normalised_decay(p.f("dec"));
        let pattern = p.f("pat").max(1.0);
        self.loop_n = 0;
        self.loop_fb = 0.0;
        self.in_scale = 1.0;
        match self.alg {
            7 => {
                let d = self.samples(pre.min(2000.0));
                self.taps[0] = Tap { delay: d, left: 1.0, right: 1.0 };
                self.n = 1;
            }
            8 => {
                // Pattern 1 two voices ... 5 eight; left and right different taps.
                let voices = (2.0 * pattern.min(5.0)) as usize;
                let d = pre.clamp(1.0, 127.0);
                for k in 0..voices.min(MAX_TAPS) {
                    let at = d * (1.0 + 0.13 * k as f32 / voices as f32);
                    let left = if k % 2 == 0 { 0.7 } else { 0.0 };
                    let right = if k % 2 == 0 { 0.0 } else { 0.7 };
                    self.taps[k] = Tap { delay: self.samples(at), left, right };
                }
                self.n = voices.min(MAX_TAPS);
            }
            9 => {
                // Pattern 1 two repeats ... 9 eight; DECAY the regeneration.
                let repeats = (2.0 + (pattern - 1.0) * 0.75).round() as usize;
                let d = pre.clamp(1.0, 990.0);
                for k in 0..repeats.min(MAX_TAPS) {
                    let at = d * (k as f32 + 1.0) / repeats as f32;
                    let left = if k % 2 == 0 { 0.7 } else { 0.25 };
                    let right = if k % 2 == 0 { 0.25 } else { 0.7 };
                    self.taps[k] = Tap { delay: self.samples(at), left, right };
                }
                self.n = repeats.min(MAX_TAPS);
                self.loop_n = self.samples(d);
                self.loop_fb = 0.95 * dec;
                let top = 3000.0 * (p.f("hf") / 3.0).exp2();
                self.loop_lp.iter_mut().for_each(|f| f.set(top, self.sr));
            }
            _ => {
                // Sound-On-Sound: a loop of PRE DELAY, LEVEL into it, DECAY keeps it.
                let d = self.samples(pre.clamp(1.0, 2000.0));
                self.taps[0] = Tap { delay: d, left: 1.0, right: 1.0 };
                self.n = 1;
                self.loop_n = d;
                self.loop_fb = dec.min(1.0);
                self.in_scale = p.f("lvl") / 9.0;
                self.loop_lp.iter_mut().for_each(|f| f.set(20000.0, self.sr));
            }
        }
    }

    #[inline]
    fn reverb_tick(&mut self, xl: f32, xr: f32) -> (f32, f32) {
        self.pre[0].push(xl);
        self.pre[1].push(xr);
        let dl = self.pre[0].tap(self.pre_n);
        let dr = self.pre[1].tap(self.pre_n);
        let dl = self.diff[0].tick(dl);
        let dl = self.diff[1].tick(dl);
        let dr = self.diff[2].tick(dr);
        let dr = self.diff[3].tick(dr);
        let mut y = [0.0f32; REV_LINES];
        let mut sum = 0.0;
        for i in 0..REV_LINES {
            let out = self.lines[i].tap(self.len[i] - 1);
            let low = self.low[i].lp(out);
            let v = (out - low) * self.g[i] + low * self.g_lf[i];
            let v = self.damp[i].lp(v);
            y[i] = v;
            sum += v;
        }
        let k = sum * 0.25;
        let mut wet = (0.0, 0.0);
        for i in 0..REV_LINES {
            let inject = if i % 2 == 0 { dl } else { dr };
            let sign = if i < 2 || (4..6).contains(&i) { 1.0 } else { -1.0 };
            self.lines[i].push(limit(y[i] - k + inject * 0.5));
            if i % 2 == 0 {
                wet.0 += sign * y[i];
            } else {
                wet.1 += sign * y[i];
            }
        }
        (wet.0 * 0.4, wet.1 * 0.4)
    }

    #[inline]
    fn reflections(&mut self, x: f32) -> (f32, f32) {
        self.er.push(x);
        let mut l = 0.0;
        let mut r = 0.0;
        for t in &self.er_taps[..self.er_n] {
            let v = self.er.tap(t.delay);
            l += v * t.left;
            r += v * t.right;
        }
        (l * self.er_gain, r * self.er_gain)
    }

    #[inline]
    fn special_tick(&mut self, xl: f32, xr: f32) -> (f32, f32) {
        let mut l = 0.0;
        let mut r = 0.0;
        for t in &self.taps[..self.n] {
            l += self.long[0].tap(t.delay) * t.left;
            r += self.long[1].tap(t.delay) * t.right;
        }
        let back = if self.loop_n > 0 {
            let a = self.long[0].tap(self.loop_n);
            let b = self.long[1].tap(self.loop_n);
            (self.loop_lp[0].lp(a) * self.loop_fb, self.loop_lp[1].lp(b) * self.loop_fb)
        } else {
            (0.0, 0.0)
        };
        self.long[0].push(limit(xl + back.0));
        self.long[1].push(limit(xr + back.1));
        (l, r)
    }
}

/// DECAY's position 0..1, for the programs that read it as a plain number (PRO X
/// manual: 0 to 99 on Sound-On-Sound, 1 to 12 on Alive and the others).
fn normalised_decay(seconds: f32) -> f32 {
    ((seconds / 0.1).ln() / (180.0f32).ln()).clamp(0.0, 1.0)
}

impl FxAlgo for Dn780 {
    fn set_params(&mut self, p: &Params) {
        self.alg = p.choice("alg").min(DN780_PROGRAMS.len() - 1);
        self.mix = p.f("mix") * 0.01;
        self.in_gain = db_to_gain(p.f("in"));
        self.in_mute = p.on("imt");
        self.rev_mute = p.on("rmt");
        self.stereo = p.on("st");
        self.pre_n = self.samples(p.f("pre").min(1990.0));
        self.in_scale = 1.0;
        match self.alg {
            0..=3 => {
                let kind = self.alg;
                self.set_reverb(p, kind, false);
                self.set_reflections(p);
            }
            4..=6 => self.set_burst(p),
            7..=10 => self.set_special(p),
            _ => {
                self.set_reverb(p, 3, true);
                self.in_scale = p.f("lvl") / 9.0;
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for i in 0..left.len() {
            let (l, r) = (left[i], right[i]);
            let g = if self.in_mute { 0.0 } else { self.in_gain * self.in_scale };
            let mono = 0.5 * (l + r);
            let (xl, xr) = if self.stereo { (l * g, r * g) } else { (mono * g, mono * g) };
            let (a, b) = match self.alg {
                0..=3 => {
                    let (a, b) = self.reverb_tick(xl, xr);
                    let (el, er) = self.reflections(0.5 * (xl + xr));
                    (a + el, b + er)
                }
                4..=10 => self.special_tick(xl, xr),
                _ => self.reverb_tick(xl, xr),
            };
            let (a, b) = if self.rev_mute { (0.0, 0.0) } else { (a, b) };
            left[i] = l + (a - l) * self.mix;
            right[i] = r + (b - r) * self.mix;
        }
    }

    fn reset(&mut self) {
        self.pre.iter_mut().for_each(DelayLine::clear);
        self.diff.iter_mut().for_each(Allpass::clear);
        self.lines.iter_mut().for_each(DelayLine::clear);
        self.low.iter_mut().for_each(OnePole::reset);
        self.damp.iter_mut().for_each(OnePole::reset);
        self.er.clear();
        self.long.iter_mut().for_each(DelayLine::clear);
        self.loop_lp.iter_mut().for_each(OnePole::reset);
    }
}

// ---- flanger ----------------------------------------------------------------------------

struct Flanger {
    sr: f32,
    lines: [DelayLine; 2],
    hf: [OnePole; 2],
    lf: [OnePole; 2],
    prev: [f32; 2],
    delay: f32,
    fb: f32,
    invert: bool,
    mix: f32,
    depth: f32,
    thru_zero: bool,
    spread: f32,
    shape: f32,
    phase: f32,
    inc: f32,
    out: Output,
}

impl Flanger {
    fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let max = (0.03 * sr) as usize + 8;
        Self {
            sr,
            lines: [DelayLine::new(max), DelayLine::new(max)],
            hf: [OnePole::new(10000.0, sr); 2],
            lf: [OnePole::new(140.0, sr); 2],
            prev: [0.0; 2],
            delay: 240.0,
            fb: 0.0,
            invert: false,
            mix: 0.5,
            depth: 0.5,
            thru_zero: false,
            spread: 0.25,
            shape: 0.0,
            phase: 0.0,
            inc: 0.0,
            out: Output::new(sr),
        }
    }

    /// Triangle towards exponential (Sidecar's shape): -1..1.
    #[inline]
    fn wave(&self, phase: f32) -> f32 {
        let p = phase.rem_euclid(1.0);
        let u = 2.0 * (p - 0.5).abs();
        if self.shape <= 0.001 {
            2.0 * u - 1.0
        } else {
            let k = 4.0 * self.shape;
            2.0 * ((k * u).exp() - 1.0) / (k.exp() - 1.0) - 1.0
        }
    }
}

impl FxAlgo for Flanger {
    fn set_params(&mut self, p: &Params) {
        self.delay = (p.f("dly") * 0.001 * self.sr).max(1.0);
        self.fb = p.f("fb") * 0.01 * 0.98;
        self.invert = p.on("inv");
        self.mix = p.f("mix") * 0.01;
        self.depth = p.f("dep") * 0.01;
        self.thru_zero = p.on("tz");
        self.spread = p.f("spr") / 360.0;
        self.shape = p.f("shp") * 0.01;
        self.inc = p.f("rt") / self.sr;
        self.hf.iter_mut().for_each(|f| f.set(p.f("hf"), self.sr));
        self.lf.iter_mut().for_each(|f| f.set(p.f("lf"), self.sr));
        self.out.set(p.f("lo"), p.f("hi"), p.f("gain"));
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for i in 0..left.len() {
            let x = [left[i], right[i]];
            self.phase += self.inc;
            if self.phase >= 1.0 {
                self.phase -= 1.0;
            }
            let lfo = [self.wave(self.phase), self.wave(self.phase + self.spread)];
            let mut output = [0.0f32; 2];
            for k in 0..2 {
                let moved = self.lf[k].hp(self.hf[k].lp(self.prev[k]));
                self.lines[k].push(limit(x[k] + self.fb * moved));
                let t = self.delay * (1.0 + self.depth * lfo[k]);
                let modulated = self.lines[k].tap_hermite(t.max(1.0));
                let fixed = self.lines[k].tap(self.delay as usize);
                let sign = if self.invert { -1.0 } else { 1.0 };
                let wet = if self.thru_zero { 0.5 * (fixed + sign * modulated) } else { sign * modulated };
                self.prev[k] = modulated;
                let dry = x[k];
                output[k] = dry + (self.out.tick(wet, k) - dry) * self.mix;
            }
            left[i] = output[0];
            right[i] = output[1];
        }
    }

    fn reset(&mut self) {
        self.lines.iter_mut().for_each(DelayLine::clear);
        self.hf.iter_mut().for_each(OnePole::reset);
        self.lf.iter_mut().for_each(OnePole::reset);
        self.prev = [0.0; 2];
        self.out.reset();
        self.phase = 0.0;
    }
}

// ---- phaser ------------------------------------------------------------------------------

const PHASER_MAX_STAGES: usize = 12;

#[derive(Clone, Copy, Default)]
struct AllPassChain {
    z: [f32; PHASER_MAX_STAGES],
    last: f32,
}

impl AllPassChain {
    #[inline]
    fn tick(&mut self, x: f32, a: f32, stages: usize, fb: f32) -> f32 {
        let mut y = x + fb * self.last;
        for z in &mut self.z[..stages] {
            let out = a * y + *z;
            *z = y - a * out;
            y = out;
        }
        self.last = limit(y);
        y
    }
}

struct Phaser {
    sr: f32,
    chains: [[AllPassChain; 2]; 2],
    phase: [f32; 2],
    inc: [f32; 2],
    manual: [f32; 2],
    a: [[f32; 2]; 2],
    depth: f32,
    spin: f32,
    fb: f32,
    stages: usize,
    sweep: usize,
    mode: usize,
    blend: f32,
    n: usize,
    out: Output,
}

impl Phaser {
    fn new(ctx: FxContext) -> Self {
        Self {
            sr: ctx.sample_rate,
            chains: [[AllPassChain::default(); 2]; 2],
            phase: [0.0; 2],
            inc: [0.0; 2],
            manual: [3500.0; 2],
            a: [[0.0; 2]; 2],
            depth: 0.5,
            spin: 0.25,
            fb: 0.0,
            stages: 4,
            sweep: 2,
            mode: 0,
            blend: 0.5,
            n: 0,
            out: Output::new(ctx.sample_rate),
        }
    }

    #[inline]
    fn wave(&self, phase: f32) -> f32 {
        let p = phase.rem_euclid(1.0);
        match self.sweep {
            0 => (TAU * p).sin(),
            1 => 1.0 - 4.0 * (p - 0.5).abs(),
            _ => {
                let u = 2.0 * (p - 0.5).abs();
                2.0 * u * u - 1.0
            }
        }
    }

    /// All-pass coefficient for frequency `hz`.
    #[inline]
    fn coef(&self, hz: f32) -> f32 {
        let t = (PI * hz.clamp(30.0, 0.45 * self.sr) / self.sr).tan();
        (t - 1.0) / (t + 1.0)
    }

    fn sweep_coefs(&mut self) {
        let linked = self.mode >= 3;
        for p in 0..2 {
            let source = if linked { 0 } else { p };
            for side in 0..2 {
                let at = self.phase[source] + if side == 1 { self.spin } else { 0.0 };
                let hz = self.manual[p] * (self.wave(at) * self.depth * 2.0).exp2();
                self.a[p][side] = self.coef(hz);
            }
        }
    }
}

impl FxAlgo for Phaser {
    fn set_params(&mut self, p: &Params) {
        self.inc = [p.f("r1") / self.sr, p.f("r2") / self.sr];
        self.manual = [p.f("m1"), p.f("m2")];
        self.depth = p.f("dep") * 0.01;
        self.spin = p.f("spin") * 0.01 * 0.5;
        self.fb = p.f("fb") * 0.01 * 0.85;
        self.stages = 2 * (p.choice("stg") + 1).min(PHASER_MAX_STAGES / 2);
        self.sweep = p.choice("swp");
        self.mode = p.choice("mode");
        self.blend = p.f("bld") * 0.01;
        self.out.set(p.f("lo"), p.f("hi"), p.f("gain"));
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let dual = self.mode != 0;
        let parallel = self.mode == 2 || self.mode == 4;
        for i in 0..left.len() {
            for k in 0..2 {
                self.phase[k] += self.inc[k];
                if self.phase[k] >= 1.0 {
                    self.phase[k] -= 1.0;
                }
            }
            if self.n & 15 == 0 {
                self.sweep_coefs();
            }
            self.n = self.n.wrapping_add(1);
            let x = [left[i], right[i]];
            let mut y = [0.0f32; 2];
            for side in 0..2 {
                let first = self.chains[0][side].tick(x[side], self.a[0][side], self.stages, self.fb);
                y[side] = if !dual {
                    first
                } else if parallel {
                    0.5 * (first + self.chains[1][side].tick(x[side], self.a[1][side], self.stages, self.fb))
                } else {
                    self.chains[1][side].tick(first, self.a[1][side], self.stages, self.fb)
                };
                let wet = self.out.tick(y[side], side);
                y[side] = x[side] + (wet - x[side]) * self.blend;
            }
            left[i] = y[0];
            right[i] = y[1];
        }
    }

    fn reset(&mut self) {
        self.chains = [[AllPassChain::default(); 2]; 2];
        self.phase = [0.0; 2];
        self.n = 0;
        self.out.reset();
    }
}

// ---- pitch shifter -----------------------------------------------------------------------

/// One channel: PREDELAY into a splicing pitch shifter, its output back through FB
/// DELAY (OM p.138) and the damping.
struct PitchVoice {
    pre: DelayLine,
    shift: DelayLine,
    fb_line: DelayLine,
    window: f32,
    phase: f32,
    step: f32,
    pre_n: usize,
    fb_n: usize,
    fb: f32,
    level: f32,
    pan: (f32, f32),
    hf: OnePole,
    lf: OnePole,
}

impl PitchVoice {
    fn new(sr: f32) -> Self {
        Self {
            pre: DelayLine::new((0.51 * sr) as usize),
            shift: DelayLine::new((0.12 * sr) as usize),
            fb_line: DelayLine::new((0.51 * sr) as usize),
            window: 2000.0,
            phase: 0.0,
            step: 0.0,
            pre_n: 1,
            fb_n: 1,
            fb: 0.0,
            level: 0.1,
            pan: (0.7, 0.7),
            hf: OnePole::new(6000.0, sr),
            lf: OnePole::new(40.0, sr),
        }
    }

    #[inline]
    fn tick(&mut self, x: f32) -> f32 {
        self.pre.push(x);
        let delayed = self.pre.tap(self.pre_n);
        let back = self.fb_line.tap(self.fb_n);
        let back = self.lf.hp(self.hf.lp(back));
        self.shift.push(limit(delayed + self.fb * back));
        self.phase = (self.phase + self.step).rem_euclid(1.0);
        let other = (self.phase + 0.5).rem_euclid(1.0);
        let a = self.shift.tap_linear(1.0 + self.phase * self.window);
        let b = self.shift.tap_linear(1.0 + other * self.window);
        let (wa, wb) = ((FRAC_PI_2 * 2.0 * self.phase).sin(), (FRAC_PI_2 * 2.0 * other).sin());
        let y = a * wa * wa + b * wb * wb;
        self.fb_line.push(y);
        y
    }

    fn reset(&mut self) {
        self.pre.clear();
        self.shift.clear();
        self.fb_line.clear();
        self.hf.reset();
        self.lf.reset();
        self.phase = 0.0;
    }
}

struct Pitch {
    sr: f32,
    voices: [PitchVoice; 2],
    mix: f32,
    out: Output,
}

impl Pitch {
    fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        Self { sr, voices: [PitchVoice::new(sr), PitchVoice::new(sr)], mix: 0.5, out: Output::new(sr) }
    }
}

impl FxAlgo for Pitch {
    fn set_params(&mut self, p: &Params) {
        let window = (p.f("win") * 0.001 * self.sr).max(64.0);
        let sr = self.sr;
        let hf = p.f("hf");
        let lf = p.f("lf");
        let fields: [[&str; 7]; 2] = [["c1", "f1", "p1", "d1", "b1", "l1", "n1"], ["c2", "f2", "p2", "d2", "b2", "l2", "n2"]];
        for (v, ids) in self.voices.iter_mut().zip(fields.iter()) {
            let semis = p.f(ids[0]) + p.f(ids[1]) * 0.01;
            v.window = window;
            v.step = (1.0 - semis.clamp(-48.0, 48.0).div_euclid(12.0).exp2() * (semis.rem_euclid(12.0) / 12.0).exp2()) / window;
            v.pre_n = ((p.f(ids[2]) * 0.001 * sr) as usize).max(1);
            v.fb_n = ((p.f(ids[3]) * 0.001 * sr) as usize).max(1);
            v.fb = p.f(ids[4]) * 0.01;
            v.level = db_to_gain(p.f(ids[5]));
            v.pan = pan_gains(p.f(ids[6]) * 0.01);
            v.hf.set(hf, sr);
            v.lf.set(lf, sr);
        }
        self.mix = p.f("mix") * 0.01;
        self.out.set(p.f("lo"), p.f("hi"), p.f("out"));
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for i in 0..left.len() {
            let (l, r) = (left[i], right[i]);
            let a = self.voices[0].tick(l);
            let b = self.voices[1].tick(r);
            let (a, b) = (a * self.voices[0].level, b * self.voices[1].level);
            let wet_l = self.out.tick(a * self.voices[0].pan.0 + b * self.voices[1].pan.0, 0);
            let wet_r = self.out.tick(a * self.voices[0].pan.1 + b * self.voices[1].pan.1, 1);
            left[i] = l + (wet_l - l) * self.mix;
            right[i] = r + (wet_r - r) * self.mix;
        }
    }

    fn reset(&mut self) {
        self.voices.iter_mut().for_each(PitchVoice::reset);
        self.out.reset();
    }
}

// ---- 3-band compressor ------------------------------------------------------------------

/// The soft knee's reach (the same bend as the channels' compressors).
#[inline]
fn knee_over(over: f32, knee: f32) -> f32 {
    if over <= -0.5 * knee {
        0.0
    } else if over >= 0.5 * knee || knee <= 0.0 {
        over
    } else {
        let x = over + 0.5 * knee;
        x * x / (2.0 * knee)
    }
}

#[derive(Clone, Copy, Default)]
struct BandComp {
    env: f32,
    gr: f32,
    thr: f32,
    slope: f32,
    make: f32,
    att: f32,
    rel: f32,
}

/// Two Butterworth sections in a row: one side of a Linkwitz-Riley split.
#[inline]
fn lr4(b: &mut [Biquad; 2], x: f32, ch: usize) -> f32 {
    let y = b[0].tick(x, ch);
    b[1].tick(y, ch)
}

struct ThreeBand {
    sr: f32,
    /// Linkwitz-Riley splits (each two Butterworth sections) at the two
    /// frequencies, and the low band's all-pass matching the high split.
    lo: [Biquad; 2],
    mid_hp: [Biquad; 2],
    mid_lp: [Biquad; 2],
    hi: [Biquad; 2],
    ap: Biquad,
    bands: [BandComp; 3],
    knee: f32,
    decay: f32,
    out: [f32; 3],
}

impl ThreeBand {
    fn new(ctx: FxContext) -> Self {
        Self {
            sr: ctx.sample_rate,
            lo: [Biquad::default(), Biquad::default()],
            mid_hp: [Biquad::default(), Biquad::default()],
            mid_lp: [Biquad::default(), Biquad::default()],
            hi: [Biquad::default(), Biquad::default()],
            ap: Biquad::default(),
            bands: [BandComp::default(); 3],
            knee: 12.0,
            decay: 0.0,
            out: [0.0; 3],
        }
    }

    fn time_coef(&self, ms: f32) -> f32 {
        (-1.0 / (ms * 0.001 * self.sr)).exp()
    }
}

impl FxAlgo for ThreeBand {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr as f64;
        let q = std::f64::consts::FRAC_1_SQRT_2;
        let (f1, f2) = (p.f("x1") as f64, (p.f("x2") as f64).min(0.45 * sr));
        for b in &mut self.lo {
            b.set_coefficients(BiquadCoeffs::low_pass(sr, f1, q));
        }
        for b in &mut self.mid_hp {
            b.set_coefficients(BiquadCoeffs::high_pass(sr, f1, q));
        }
        for b in &mut self.mid_lp {
            b.set_coefficients(BiquadCoeffs::low_pass(sr, f2, q));
        }
        for b in &mut self.hi {
            b.set_coefficients(BiquadCoeffs::high_pass(sr, f2, q));
        }
        self.ap.set_coefficients(BiquadCoeffs::all_pass_2(sr, f2, q));
        self.knee = p.f("kn");
        self.decay = self.time_coef(5.0);
        let ids: [[&str; 5]; 3] =
            [["LT", "LR", "LG", "LL", "LA"], ["MT", "MR", "MG", "ML", "MA"], ["HT", "HR", "HG", "HL", "HA"]];
        for (i, f) in ids.iter().enumerate() {
            let r = p.f(f[1]);
            let att = self.time_coef(p.f(f[4]));
            let rel = self.time_coef(p.f(f[3]));
            let b = &mut self.bands[i];
            b.thr = p.f(f[0]) - crate::midas_pro::DBU_AT_FS;
            b.slope = 1.0 / r - 1.0;
            b.make = db_to_gain(p.f(f[2]));
            b.att = att;
            b.rel = rel;
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for i in 0..left.len() {
            let mut lo_hi = [[0.0f32; 3]; 2];
            for (ch, sample) in [left[i], right[i]].iter().enumerate() {
                // Two Linkwitz-Riley splits, the low band all-passed to match the
                // upper split: the three bands sum flat ("coherent band summing",
                // OM p.80), at any crossover settings.
                let low = lr4(&mut self.lo, *sample, ch);
                let low = self.ap.tick(low, ch);
                let above = lr4(&mut self.mid_hp, *sample, ch);
                let mid = lr4(&mut self.mid_lp, above, ch);
                let high = lr4(&mut self.hi, above, ch);
                lo_hi[ch] = [low, mid, high];
            }
            let mut out = [0.0f32; 2];
            for (bi, b) in self.bands.iter_mut().enumerate() {
                let level = lo_hi[0][bi].abs().max(lo_hi[1][bi].abs());
                b.env = level.max(b.env * self.decay);
                let over = knee_over(gain_to_db(b.env + 1e-9) - b.thr, self.knee);
                let target = over * b.slope;
                // Partially adaptive: the further over, the sooner it attacks.
                let coef = if target < b.gr { 1.0 - (1.0 - b.att) * (1.0 + 0.25 * (b.gr - target)) } else { b.rel };
                b.gr = coef * b.gr + (1.0 - coef) * target;
                let g = db_to_gain(b.gr) * b.make;
                out[0] += lo_hi[0][bi] * g;
                out[1] += lo_hi[1][bi] * g;
                self.out[bi] = b.gr;
            }
            left[i] = out[0];
            right[i] = out[1];
        }
    }

    fn reset(&mut self) {
        for b in self.lo.iter_mut().chain(&mut self.mid_hp).chain(&mut self.mid_lp).chain(&mut self.hi) {
            b.reset();
        }
        self.ap.reset();
        for b in &mut self.bands {
            b.env = 0.0;
            b.gr = 0.0;
        }
    }

    fn meter(&self) -> f32 {
        self.out.iter().fold(0.0f32, |m, x| m.min(*x))
    }
}

// ---- dynamic EQ ---------------------------------------------------------------------------

#[derive(Clone, Default)]
struct DeqBand {
    on: bool,
    listen: bool,
    kind: usize,
    q: f32,
    fixed: bool,
    ratio: f32,
    compress: bool,
    fast: bool,
    hz: f32,
    below: bool,
    thr: f32,
    limit: f32,
    sc: Biquad,
    eq: Biquad,
    env: f32,
    gain: f32,
    last: f32,
}

impl DeqBand {
    /// The way the band moves (true: up).
    fn up(&self) -> bool {
        self.below == self.compress
    }

    /// The gain in dB for a level.
    #[inline]
    fn target(&self, lvl: f32) -> f32 {
        let past = if self.below { self.thr - lvl } else { lvl - self.thr };
        if past <= 0.0 && !self.fixed {
            return 0.0;
        }
        let amount = if self.fixed {
            self.limit
        } else if self.compress {
            (1.0 - 1.0 / self.ratio) * past
        } else {
            (self.ratio - 1.0) * past
        }
        .min(self.limit);
        if self.up() {
            amount
        } else {
            -amount
        }
    }
}

struct DynEq {
    sr: f32,
    bands: Vec<DeqBand>,
    n: usize,
    env_att: f32,
    env_rel: f32,
    env_fast: f32,
    any_listen: bool,
}

/// How much narrower the bell is at no gain than at its limit (Sidecar's reading of
/// the proportional-Q filters, OM p.140: twice).
const DEQ_PROP_NARROW: f32 = 2.0;

impl DynEq {
    fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let coef = |ms: f32| (-1.0 / (ms * 0.001 * sr)).exp();
        Self {
            sr,
            bands: vec![DeqBand::default(); 4],
            n: 0,
            env_att: coef(3.0),
            env_rel: coef(300.0),
            env_fast: coef(60.0),
            any_listen: false,
        }
    }

    fn apply(band: &mut DeqBand, sr: f64) {
        let g = band.gain;
        let f = (band.hz as f64).min(0.45 * sr);
        let prop = 1.0 + (DEQ_PROP_NARROW - 1.0) * (1.0 - (g.abs() / band.limit.max(0.1)).min(1.0));
        let q = (band.q * prop) as f64;
        let c = match band.kind {
            1 => BiquadCoeffs::low_shelf(sr, f, std::f64::consts::FRAC_1_SQRT_2, g as f64),
            2 => BiquadCoeffs::high_shelf(sr, f, std::f64::consts::FRAC_1_SQRT_2, g as f64),
            3 => BiquadCoeffs::identity(),
            _ => BiquadCoeffs::peak(sr, f, q, g as f64),
        };
        band.eq.set_coefficients(c);
    }
}

impl FxAlgo for DynEq {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr as f64;
        let id = |band: usize, k: usize| p.spec.params[band * DEQ_FIELDS + k].id;
        let mut any = false;
        for (i, b) in self.bands.iter_mut().enumerate() {
            b.on = p.on(id(i, 0));
            b.listen = b.on && p.on(id(i, 1));
            any |= b.listen;
            b.kind = p.choice(id(i, 2));
            let oct = p.f(id(i, 3));
            let n = 2f32.powf(oct);
            b.q = n.sqrt() / (n - 1.0);
            b.fixed = p.on(id(i, 4));
            let ratio = p.f(id(i, 5));
            b.compress = ratio < 0.0;
            b.ratio = 25f32.powf(ratio.abs() * 0.01);
            b.fast = p.on(id(i, 6));
            b.hz = p.f(id(i, 7));
            b.below = p.on(id(i, 8));
            b.thr = p.f(id(i, 9));
            b.limit = p.f(id(i, 10));
            b.sc.set_coefficients(BiquadCoeffs::band_pass(sr, (b.hz as f64).min(0.45 * sr), b.q as f64));
        }
        self.any_listen = any;
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let sr = self.sr as f64;
        for i in 0..left.len() {
            let mut x = [left[i], right[i]];
            let dry = 0.5 * (x[0] + x[1]);
            let mut listen = 0.0;
            let update = self.n & 15 == 0;
            self.n = self.n.wrapping_add(1);
            for b in self.bands.iter_mut() {
                if !b.on {
                    continue;
                }
                let s = if b.kind == 3 { dry } else { b.sc.tick(dry, 0) };
                if b.listen {
                    listen += s;
                }
                let v = s.abs();
                let coef = if v > b.env {
                    self.env_att
                } else if b.fast {
                    self.env_fast
                } else {
                    self.env_rel
                };
                b.env = coef * b.env + (1.0 - coef) * v;
                let target = b.target(gain_to_db(b.env + 1e-9));
                b.gain += (target - b.gain) * 0.02;
                b.last = b.gain;
                if b.kind == 3 {
                    let g = db_to_gain(b.gain);
                    x = [x[0] * g, x[1] * g];
                } else {
                    if update {
                        Self::apply(b, sr);
                    }
                    x = [b.eq.tick(x[0], 0), b.eq.tick(x[1], 1)];
                }
            }
            if self.any_listen {
                x = [listen, listen];
            }
            left[i] = x[0];
            right[i] = x[1];
        }
    }

    fn reset(&mut self) {
        for b in &mut self.bands {
            b.sc.reset();
            b.eq.reset();
            b.env = 0.0;
            b.gain = 0.0;
            b.last = 0.0;
        }
    }

    fn band_gain(&self, band: usize) -> f32 {
        self.bands.get(band).map_or(0.0, |b| b.last)
    }

    fn meter(&self) -> f32 {
        self.bands.iter().fold(0.0f32, |m, b| m.min(b.last))
    }
}

/// A device of the PRO2's table.
pub fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    Some(match token {
        "MPDLY" => Box::new(Delay::new(ctx)),
        "MPDN780" => Box::new(Dn780::new(ctx)),
        "MPFLG" => Box::new(Flanger::new(ctx)),
        "MPPHS" => Box::new(Phaser::new(ctx)),
        "MPPIT" => Box::new(Pitch::new(ctx)),
        "MP3BC" => Box::new(ThreeBand::new(ctx)),
        "MPDEQ" => Box::new(DynEq::new(ctx)),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_device_passes_the_fx_checks() {
        for m in FX_MODELS[2..].iter() {
            crate::wing::fx::tests::check_device(m.token);
        }
    }
}
