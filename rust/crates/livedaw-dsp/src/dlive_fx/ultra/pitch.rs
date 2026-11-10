//! Vocal Shifter, Vocal Tuner, Vocal Gridder and the three Harmonisers.
//!
//! Stated (guide, panel pictures). Vocal Shifter: Left and Right with Link, Semitone
//! +-12, Formant, Delay to 80 ms, Gain 0 to -40 dB, Precision (0.1 steps instead of 1).
//! Vocal Tuner: Global Key, Key and Scale (Major, Minor, Chromatic), Range (Default,
//! Baritone E2-A4, Alto/Tenor C3-F5, Soprano A3-C6), Reference Pitch, Bypass, Correction,
//! Retune, Transition, Tracking, five states per note. Vocal Gridder: Global Key, Key and
//! Scale, Bypass, Reference Pitch, Time Tolerance (how long it tunes to the current
//! note before the next), Pitch Tolerance (how far the voice may drift before it does),
//! Transition. Dual AutoKey: Manual, Global or Auto key from a side-chain source with a
//! confidence meter; 2 voices: Interval +-8 (+8 an octave up), Formant +-12 (0 corrected),
//! Gain 0 to -40 dB, Pan; Reverb, Chorus, Pitch and Time Variation, Auto Expand. Quad
//! Voice: the same with 4 voices and no AutoKey. MIDI Harmoniser: Monophonic with Glide or
//! Polyphonic with 4 voices; Width, Chorus, Pitch Variation, Use Velocity.
//!
//! Sidecar's reading, the algorithms all of it: a YIN pitch detector and a pitch-
//! synchronous grain voice (grains two periods wide, centred on input periods, respaced to
//! the new pitch, read at the formant's speed, 30 ms latency), so pitch and formant move
//! apart; every range and scale law not above; what the Tuner's five states mean (Off: not
//! a target; Bypass: a voice there stays as it is; On; Snap: at once; Soft: half
//! correction); the Harmonisers' effects; Interval as a musical interval number (+1
//! unison, +3 a third, +8 an octave, 0 unison too); Auto Expand as harmonies fading out
//! with the input in gaps; AutoKey as Krumhansl-Schmuckler matching of a chroma the
//! detector builds from the key input.

use super::super::{ch, p, pct, tog, LIN};
use crate::model::{ParamKind, ParamSpec, Params};
use crate::wing::fx::blocks::{coef, Smoothed};
use crate::wing::fx::pitch::yin::PitchDetector;
use crate::wing::fx::reverb::parts::{fader, spread};
use crate::wing::fx::reverb::space::{Config, Settings, Space};
use crate::wing::fx::utility::rail;
use crate::wing::fx::{FxAlgo, FxContext, FX_READOUTS};
use std::f32::consts::{FRAC_PI_2, TAU};

const INT: ParamKind = ParamKind::Int;

/// Latency of the grain voice (ms).
const LATENCY_MS: f32 = 30.0;
/// Most grains sounding at once.
const MAX_GRAINS: usize = 6;
/// Unvoiced input: the grain hop (ms).
const UNVOICED_HOP_MS: f32 = 6.0;
/// Ring length (s): latency, the longest Time Variation and Delay.
const RING_S: f32 = 0.5;

/// What a pitch detector found, plus the ring of input the grains read.
struct Analysis {
    det: PitchDetector,
    ring: Vec<f32>,
    mask: i64,
    t: i64,
    voiced: bool,
    hz: f32,
}

impl Analysis {
    fn new(sr: f32) -> Self {
        let len = ((RING_S * sr) as usize).next_power_of_two();
        Self { det: PitchDetector::new(sr), ring: vec![0.0; len], mask: len as i64 - 1, t: 0, voiced: false, hz: 0.0 }
    }

    /// Feeds a sample; true if the detector just reported.
    #[inline]
    fn push(&mut self, x: f32) -> bool {
        self.ring[(self.t & self.mask) as usize] = x;
        self.t += 1;
        match self.det.push(x) {
            Some(d) => {
                self.voiced = d.voiced && d.hz > 0.0;
                if self.voiced {
                    self.hz = d.hz;
                }
                true
            }
            None => false,
        }
    }

    #[inline]
    fn period_samples(&self, sr: f32) -> f32 {
        if self.voiced {
            sr / self.hz.max(1.0)
        } else {
            0.0
        }
    }

    #[inline]
    fn read(&self, at: f64) -> f32 {
        let i = at.floor() as i64;
        let f = (at - i as f64) as f32;
        let r = |k: i64| self.ring[((i + k) & self.mask) as usize];
        let (a, b, c, d) = (r(-1), r(0), r(1), r(2));
        let c0 = b;
        let c1 = 0.5 * (c - a);
        let c2 = a - 2.5 * b + 2.0 * c - 0.5 * d;
        let c3 = 0.5 * (d - a) + 1.5 * (b - c);
        ((c3 * f + c2) * f + c1) * f + c0
    }

    fn reset(&mut self) {
        self.det.reset();
        self.ring.fill(0.0);
        self.t = 0;
        self.voiced = false;
    }
}

#[derive(Clone, Copy, Default)]
struct Grain {
    src: f64,
    step: f32,
    phase: f32,
    inc: f32,
    gain: f32,
    on: bool,
}

/// One voice: grains two periods wide, respaced to the target pitch.
struct GrainVoice {
    grains: [Grain; MAX_GRAINS],
    to_mark: f32,
    last_a: f64,
    primed: bool,
}

impl GrainVoice {
    fn new() -> Self {
        Self { grains: [Grain::default(); MAX_GRAINS], to_mark: 0.0, last_a: 0.0, primed: false }
    }

    fn reset(&mut self) {
        *self = Self::new();
    }

    /// `delay` is the voice's own extra latency in samples.
    #[inline]
    fn tick(&mut self, an: &Analysis, sr: f32, lat: f32, ratio: f32, formant: f32, delay: f32) -> f32 {
        self.to_mark -= 1.0;
        if self.to_mark <= 0.0 {
            self.spawn(an, sr, lat, ratio, formant, delay);
        }
        let mut y = 0.0;
        for g in &mut self.grains {
            if !g.on {
                continue;
            }
            let w = 0.5 - 0.5 * (TAU * g.phase).cos();
            y += g.gain * w * an.read(g.src);
            g.src += f64::from(g.step);
            g.phase += g.inc;
            if g.phase >= 1.0 {
                g.on = false;
            }
        }
        y
    }

    fn spawn(&mut self, an: &Analysis, sr: f32, lat: f32, ratio: f32, formant: f32, delay: f32) {
        let period = an.period_samples(sr);
        let voiced = period > 0.0;
        let (p, hop) =
            if voiced { (period, period / ratio) } else { (sr * UNVOICED_HOP_MS * 0.001, sr * UNVOICED_HOP_MS * 0.001) };
        let target = (an.t as f64) - f64::from(lat + delay);
        // Whole periods from the last grain, so successive grains line up on the waveform.
        let a = if voiced && self.primed {
            let m = ((target - self.last_a) / f64::from(p)).round();
            self.last_a + m * f64::from(p)
        } else {
            target
        };
        self.last_a = a;
        self.primed = true;
        let f = formant.max(0.25);
        let room = (lat + delay - 4.0 - 0.5 * p).max(16.0);
        let len = (2.0 * p.max(hop)).min(2.0 * room / f).max(16.0);
        let g = Grain {
            src: a - 0.5 * f64::from(len) * f64::from(f),
            step: f,
            phase: 0.0,
            inc: 1.0 / len,
            gain: (2.0 * hop / len).min(1.0),
            on: true,
        };
        let slot = self.grains.iter().position(|g| !g.on).unwrap_or_else(|| {
            // All busy: take over the oldest (it has the largest phase).
            let mut best = 0;
            for (i, g) in self.grains.iter().enumerate() {
                if g.phase > self.grains[best].phase {
                    best = i;
                }
            }
            best
        });
        self.grains[slot] = g;
        self.to_mark += hop.max(8.0);
    }
}

// ---- keys and notes --------------------------------------------------------------------

const NOTE_NAMES: &[&str] = &["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const SCALES: &[&str] = &["MAJOR", "MINOR", "CHROMATIC"];

const MAJOR: [bool; 12] = [true, false, true, false, true, true, false, true, false, true, false, true];
const MINOR: [bool; 12] = [true, false, true, true, false, true, false, true, true, false, true, false];

/// The pitch classes of a key and scale.
fn scale_mask(key: usize, scale: usize) -> [bool; 12] {
    let base = match scale {
        0 => MAJOR,
        1 => MINOR,
        _ => return [true; 12],
    };
    let mut m = [false; 12];
    for (i, on) in base.into_iter().enumerate() {
        m[(i + key) % 12] = on;
    }
    m
}

/// `midi`'s nearest note in `mask`; `current`, if any, while the voice is still
/// within `keep` of it (semitones).
fn nearest(midi: f32, mask: &[bool; 12], current: Option<i32>, keep: f32) -> Option<i32> {
    if let Some(c) = current {
        if (midi - c as f32).abs() < keep && mask[c.rem_euclid(12) as usize] {
            return Some(c);
        }
    }
    let n = midi.round() as i32;
    (0..=12).flat_map(|d| [n + d, n - d]).find(|c| mask[c.rem_euclid(12) as usize])
}

/// Semitones above MIDI note 0 for `hz`, against A = `reference`.
#[inline]
fn midi_of(hz: f32, reference: f32) -> f32 {
    69.0 + 12.0 * (hz / reference.max(1.0)).log2()
}

#[inline]
fn ratio_of(semis: f32) -> f32 {
    2f32.powf(semis / 12.0)
}

#[inline]
fn pan_gains(pan: f32) -> (f32, f32) {
    let (s, c) = ((pan.clamp(-1.0, 1.0) + 1.0) * 0.5 * FRAC_PI_2).sin_cos();
    (c, s)
}

/// Which key to use: its own, the console's, or (`auto`) the detected one.
#[derive(Clone, Copy)]
struct KeyChoice {
    source: usize,
    key: usize,
    scale: usize,
    global: (usize, usize),
    auto_key: (usize, usize),
}

impl KeyChoice {
    fn new() -> Self {
        Self { source: 0, key: 0, scale: 0, global: (0, 0), auto_key: (0, 0) }
    }

    fn active(&self) -> (usize, usize) {
        match self.source {
            1 => self.global,
            2 => self.auto_key,
            _ => (self.key, self.scale),
        }
    }
}

// ---- Vocal Shifter ----------------------------------------------------------------------

macro_rules! shifter_side {
    ($n:literal) => {
        [
            p(concat!("semi_", $n), concat!("SEMITONE ", $n), "", LIN, -12.0, 12.0, 241, 0.0),
            p(concat!("form_", $n), concat!("FORMANT ", $n), "", LIN, -12.0, 12.0, 241, 0.0),
            p(concat!("dly_", $n), concat!("DELAY ", $n), "ms", LIN, 0.0, 80.0, 161, 0.0),
            p(concat!("gain_", $n), concat!("GAIN ", $n), "dB", LIN, -40.0, 0.0, 81, 0.0),
        ]
    };
}

const SIDE_L: [ParamSpec; 4] = shifter_side!("L");
const SIDE_R: [ParamSpec; 4] = shifter_side!("R");

/// Vocal Shifter's panel.
pub(in super::super) static SHIFTER: [ParamSpec; 10] = [
    tog("link", "LINK", 0.0),
    tog("prec", "PRECISION", 0.0),
    SIDE_L[0],
    SIDE_L[1],
    SIDE_L[2],
    SIDE_L[3],
    SIDE_R[0],
    SIDE_R[1],
    SIDE_R[2],
    SIDE_R[3],
];

struct ShifterSide {
    an: Analysis,
    voice: GrainVoice,
    ratio: f32,
    formant: f32,
    delay: f32,
    gain: Smoothed,
}

/// Vocal Shifter: each side on its own, on its own detector.
pub(in super::super) struct VocalShifter {
    sr: f32,
    lat: f32,
    sides: [ShifterSide; 2],
}

impl VocalShifter {
    pub(in super::super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let side = || ShifterSide {
            an: Analysis::new(sr),
            voice: GrainVoice::new(),
            ratio: 1.0,
            formant: 1.0,
            delay: 0.0,
            gain: Smoothed::new(1.0, 20.0, sr),
        };
        Self { sr, lat: LATENCY_MS * 0.001 * sr, sides: [side(), side()] }
    }
}

impl FxAlgo for VocalShifter {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        let link = p.on("link");
        let precise = p.on("prec");
        let round = |v: f32| if precise { v } else { v.round() };
        for (i, s) in self.sides.iter_mut().enumerate() {
            let ids =
                if i == 0 || link { ["semi_L", "form_L", "dly_L", "gain_L"] } else { ["semi_R", "form_R", "dly_R", "gain_R"] };
            s.ratio = ratio_of(round(p.f(ids[0])));
            s.formant = ratio_of(round(p.f(ids[1])));
            s.delay = round(p.f(ids[2])) * 0.001 * sr;
            s.gain.target = fader(p.f(ids[3]));
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let (sr, lat) = (self.sr, self.lat);
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            for (x, s) in [&mut *l, &mut *r].into_iter().zip(self.sides.iter_mut()) {
                s.an.push(*x);
                let y = s.voice.tick(&s.an, sr, lat, s.ratio, s.formant, s.delay);
                *x = y * s.gain.tick();
            }
        }
    }

    fn reset(&mut self) {
        for s in &mut self.sides {
            s.an.reset();
            s.voice.reset();
            s.gain.snap(s.gain.target);
        }
    }

    fn latency(&self) -> u32 {
        self.lat as u32
    }
}

// ---- Vocal Tuner ------------------------------------------------------------------------

const RANGES: &[&str] = &["DEFAULT", "BARITONE", "ALTO/TENOR", "SOPRANO"];
const RANGE_MIDI: [(f32, f32); 4] = [(24.0, 96.0), (40.0, 69.0), (48.0, 77.0), (57.0, 84.0)];
const STATES: &[&str] = &["OFF", "BYPASS", "ON", "SNAP", "SOFT"];

macro_rules! note_state {
    ($id:literal, $name:literal) => {
        ch($id, concat!("NOTE ", $name), STATES, 2.0)
    };
}

/// Vocal Tuner's panel.
pub(in super::super) static TUNER: [ParamSpec; 22] = [
    tog("glob", "GLOBAL KEY", 0.0),
    ch("key", "KEY", NOTE_NAMES, 0.0),
    ch("scale", "SCALE", SCALES, 2.0),
    ch("range", "RANGE", RANGES, 0.0),
    p("ref", "REFERENCE PITCH", "Hz", LIN, 415.0, 466.0, 103, 440.0),
    tog("byp", "BYPASS", 0.0),
    p("corr", "CORRECTION", "%", LIN, 0.0, 100.0, 101, 100.0),
    p("retune", "RETUNE", "ms", crate::model::ParamKind::Log, 1.0, 400.0, 101, 40.0),
    p("trans", "TRANSITION", "ms", LIN, 0.0, 500.0, 251, 60.0),
    p("track", "TRACKING", "%", LIN, 0.0, 100.0, 101, 50.0),
    note_state!("n0", "C"),
    note_state!("n1", "C#"),
    note_state!("n2", "D"),
    note_state!("n3", "D#"),
    note_state!("n4", "E"),
    note_state!("n5", "F"),
    note_state!("n6", "F#"),
    note_state!("n7", "G"),
    note_state!("n8", "G#"),
    note_state!("n9", "A"),
    note_state!("n10", "A#"),
    note_state!("n11", "B"),
];

const NOTE_STATE_IDS: [&str; 12] = ["n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7", "n8", "n9", "n10", "n11"];

/// Vocal Tuner: pulls the voice towards the nearest allowed note.
pub(in super::super) struct VocalTuner {
    sr: f32,
    lat: f32,
    an: Analysis,
    voice: GrainVoice,
    keys: KeyChoice,
    mask: [bool; 12],
    states: [usize; 12],
    range: (f32, f32),
    reference: f32,
    bypass: bool,
    correction: f32,
    retune: f32,
    snap: f32,
    trans: f32,
    hold: usize,
    held: usize,
    note: Option<i32>,
    target: f32,
    eased: f32,
    shift: f32,
    fast: bool,
    midi_snap: Option<i32>,
    heard: Option<i32>,
    last_midi: f32,
}

impl VocalTuner {
    pub(in super::super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        Self {
            sr,
            lat: LATENCY_MS * 0.001 * sr,
            an: Analysis::new(sr),
            voice: GrainVoice::new(),
            keys: KeyChoice::new(),
            mask: [true; 12],
            states: [2; 12],
            range: RANGE_MIDI[0],
            reference: 440.0,
            bypass: false,
            correction: 1.0,
            retune: 1.0,
            snap: coef(1.0, sr),
            trans: 1.0,
            hold: 0,
            held: 0,
            note: None,
            target: 0.0,
            eased: 0.0,
            shift: 0.0,
            fast: false,
            midi_snap: None,
            heard: None,
            last_midi: 0.0,
        }
    }

    fn analysed(&mut self) {
        if self.bypass || !self.an.voiced {
            if self.bypass || self.held == 0 {
                self.target = 0.0;
                self.note = None;
                self.fast = false;
            } else {
                self.held = self.held.saturating_sub((0.005 * self.sr) as usize);
            }
            return;
        }
        self.held = self.hold;
        let midi = midi_of(self.an.hz, self.reference);
        self.last_midi = midi;
        self.heard = Some(midi.round() as i32);
        if midi < self.range.0 || midi > self.range.1 {
            self.target = 0.0;
            self.note = None;
            return;
        }
        let here = self.states[midi.round() as i32 as usize % 12];
        if here == 1 {
            // Bypass: a voice at this note stays as it is.
            self.target = 0.0;
            self.note = None;
            return;
        }
        let mut allowed = self.mask;
        for (a, s) in allowed.iter_mut().zip(self.states) {
            *a = *a && s != 0;
        }
        let candidate = self.midi_snap.or_else(|| nearest(midi, &allowed, self.note, 0.7));
        let Some(n) = candidate else {
            self.target = 0.0;
            return;
        };
        self.note = Some(n);
        let state = self.states[n.rem_euclid(12) as usize];
        let amount = self.correction * if state == 4 { 0.5 } else { 1.0 };
        self.target = (n as f32 - midi) * amount;
        self.fast = state == 3;
    }
}

impl FxAlgo for VocalTuner {
    fn set_params(&mut self, p: &Params) {
        self.keys.source = usize::from(p.on("glob"));
        self.keys.key = p.choice("key");
        self.keys.scale = p.choice("scale");
        let (k, s) = self.keys.active();
        self.mask = scale_mask(k, s);
        for (st, id) in self.states.iter_mut().zip(NOTE_STATE_IDS) {
            *st = p.choice(id).min(4);
        }
        self.range = RANGE_MIDI[p.choice("range").min(3)];
        self.reference = p.f("ref");
        self.bypass = p.on("byp");
        self.correction = pct(p.f("corr"));
        self.retune = coef(p.f("retune"), self.sr);
        self.trans = coef(p.f("trans").max(0.5), self.sr);
        // Tracking: how long the note is held through a gap in the detection, 10 to 130 ms.
        self.hold = ((10.0 + 1.2 * p.f("track")) * 0.001 * self.sr) as usize;
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let (sr, lat) = (self.sr, self.lat);
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            if self.an.push(0.5 * (*l + *r)) {
                self.analysed();
            }
            // The note's change eases over Transition; the correction over Retune.
            self.eased += (self.target - self.eased) * self.trans;
            let c = if self.fast { self.snap } else { self.retune };
            self.shift += (self.eased - self.shift) * c;
            let y = self.voice.tick(&self.an, sr, lat, ratio_of(self.shift), 1.0, 0.0);
            *l = y;
            *r = y;
        }
    }

    fn reset(&mut self) {
        self.an.reset();
        self.voice.reset();
        self.target = 0.0;
        self.eased = 0.0;
        self.shift = 0.0;
        self.note = None;
        self.heard = None;
    }

    fn latency(&self) -> u32 {
        self.lat as u32
    }

    fn midi_note(&mut self, note: u8, velocity: u8) {
        if velocity > 0 {
            self.midi_snap = Some(i32::from(note));
        } else if self.midi_snap == Some(i32::from(note)) {
            self.midi_snap = None;
        }
    }

    fn global_key(&mut self, key: u8, scale: u8) {
        self.keys.global = (usize::from(key).min(11), usize::from(scale).min(2));
        let (k, s) = self.keys.active();
        self.mask = scale_mask(k, s);
    }

    /// Readouts: 0 the correction in cents, 1 the note heard plus one (1 = C).
    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        out[0] = 100.0 * self.shift;
        out[1] = self.heard.map_or(0.0, |n| (n.rem_euclid(12) + 1) as f32);
    }
}

// ---- Vocal Gridder ----------------------------------------------------------------------

/// Vocal Gridder's panel.
pub(in super::super) static GRIDDER: [ParamSpec; 8] = [
    tog("glob", "GLOBAL KEY", 0.0),
    ch("key", "KEY", NOTE_NAMES, 0.0),
    ch("scale", "SCALE", SCALES, 0.0),
    tog("byp", "BYPASS", 0.0),
    p("ref", "REFERENCE PITCH", "Hz", LIN, 415.0, 466.0, 103, 440.0),
    p("ttol", "TIME TOLERANCE", "ms", LIN, 0.0, 500.0, 251, 100.0),
    p("ptol", "PITCH TOLERANCE", "c", LIN, 0.0, 200.0, 201, 50.0),
    p("trans", "TRANSITION", "ms", LIN, 0.0, 200.0, 201, 5.0),
];

/// Vocal Gridder: a hard snap to the current note, moving to the next only when the
/// voice has drifted past Pitch Tolerance and Time Tolerance has run out.
pub(in super::super) struct VocalGridder {
    sr: f32,
    lat: f32,
    an: Analysis,
    voice: GrainVoice,
    keys: KeyChoice,
    mask: [bool; 12],
    reference: f32,
    bypass: bool,
    ttol: u32,
    ptol: f32,
    trans: f32,
    note: Option<i32>,
    since: u32,
    /// Detector frames since the last voiced one (a short gap keeps the note).
    gap: u32,
    target: f32,
    shift: f32,
}

/// Frames (about 5 ms each) of unvoiced input a held note survives.
const GAP_FRAMES: u32 = 12;

impl VocalGridder {
    pub(in super::super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        Self {
            sr,
            lat: LATENCY_MS * 0.001 * sr,
            an: Analysis::new(sr),
            voice: GrainVoice::new(),
            keys: KeyChoice::new(),
            mask: [true; 12],
            reference: 440.0,
            bypass: false,
            ttol: 0,
            ptol: 0.5,
            trans: 1.0,
            note: None,
            since: 0,
            gap: 0,
            target: 0.0,
            shift: 0.0,
        }
    }

    fn analysed(&mut self) {
        if self.bypass || !self.an.voiced {
            self.gap += 1;
            if self.bypass || self.gap > GAP_FRAMES {
                self.target = 0.0;
                self.note = None;
            }
            return;
        }
        self.gap = 0;
        let midi = midi_of(self.an.hz, self.reference);
        let next = match self.note {
            Some(n) if (midi - n as f32).abs() <= self.ptol || self.since < self.ttol => n,
            cur => {
                let n = nearest(midi, &self.mask, cur, 0.0).unwrap_or(midi.round() as i32);
                if Some(n) != cur {
                    self.since = 0;
                }
                n
            }
        };
        self.note = Some(next);
        self.target = next as f32 - midi;
    }
}

impl FxAlgo for VocalGridder {
    fn set_params(&mut self, p: &Params) {
        self.keys.source = usize::from(p.on("glob"));
        self.keys.key = p.choice("key");
        self.keys.scale = p.choice("scale");
        let (k, s) = self.keys.active();
        self.mask = scale_mask(k, s);
        self.reference = p.f("ref");
        self.bypass = p.on("byp");
        self.ttol = (p.f("ttol") * 0.001 * self.sr) as u32;
        self.ptol = p.f("ptol") * 0.01;
        self.trans = coef(p.f("trans").max(0.5), self.sr);
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let (sr, lat) = (self.sr, self.lat);
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            self.since = self.since.saturating_add(1);
            if self.an.push(0.5 * (*l + *r)) {
                self.analysed();
            }
            self.shift += (self.target - self.shift) * self.trans;
            let y = self.voice.tick(&self.an, sr, lat, ratio_of(self.shift), 1.0, 0.0);
            *l = y;
            *r = y;
        }
    }

    fn reset(&mut self) {
        self.an.reset();
        self.voice.reset();
        self.target = 0.0;
        self.shift = 0.0;
        self.note = None;
        self.since = u32::MAX / 2;
    }

    fn latency(&self) -> u32 {
        self.lat as u32
    }

    fn global_key(&mut self, key: u8, scale: u8) {
        self.keys.global = (usize::from(key).min(11), usize::from(scale).min(2));
        let (k, s) = self.keys.active();
        self.mask = scale_mask(k, s);
    }
}

// ---- Harmonisers -------------------------------------------------------------------------

/// The chromatic profiles (Krumhansl-Kessler ratings) AutoKey correlates the chroma with.
const KK_MAJOR: [f32; 12] = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
const KK_MINOR: [f32; 12] = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];

/// AutoKey: a decaying chroma of the voiced notes, the key its best match.
struct KeyTracker {
    det: PitchDetector,
    chroma: [f32; 12],
    /// Key, scale and confidence 0..1.
    found: (usize, usize, f32),
    since: u32,
    every: u32,
    decay: f32,
}

impl KeyTracker {
    fn new(sr: f32) -> Self {
        Self {
            det: PitchDetector::new(sr),
            chroma: [0.0; 12],
            found: (0, 0, 0.0),
            since: 0,
            every: (0.25 * sr) as u32,
            decay: 0.998,
        }
    }

    fn reset(&mut self) {
        self.det.reset();
        self.chroma = [0.0; 12];
        self.found = (0, 0, 0.0);
    }

    fn note(&mut self, hz: f32) {
        let pc = midi_of(hz, 440.0).round() as i32;
        for c in &mut self.chroma {
            *c *= self.decay;
        }
        self.chroma[pc.rem_euclid(12) as usize] += 1.0;
    }

    fn analyse(&mut self) {
        let mean: f32 = self.chroma.iter().sum::<f32>() / 12.0;
        if mean <= 0.0 {
            self.found.2 = 0.0;
            return;
        }
        let (mut best, mut second, mut at) = (-2.0f32, -2.0f32, (0usize, 0usize));
        for scale in 0..2 {
            let profile = if scale == 0 { &KK_MAJOR } else { &KK_MINOR };
            let pm: f32 = profile.iter().sum::<f32>() / 12.0;
            for key in 0..12 {
                let (mut n, mut da, mut db) = (0.0f32, 0.0f32, 0.0f32);
                for (i, &weight) in profile.iter().enumerate() {
                    let a = self.chroma[(i + key) % 12] - mean;
                    let b = weight - pm;
                    n += a * b;
                    da += a * a;
                    db += b * b;
                }
                let r = n / (da * db).sqrt().max(1e-9);
                if r > best {
                    second = best;
                    best = r;
                    at = (key, scale);
                } else if r > second {
                    second = r;
                }
            }
        }
        // Confidence: how clearly the best fit beats the runner-up.
        self.found = (at.0, at.1, ((best - second) * 4.0).clamp(0.0, 1.0));
    }
}

const KEY_SOURCE_AUTO: &[&str] = &["MANUAL", "GLOBAL", "AUTO"];
const KEY_SOURCE: &[&str] = &["MANUAL", "GLOBAL"];

macro_rules! voice {
    ($n:literal, $i:expr) => {
        [
            p(concat!("int_", $n), concat!("INTERVAL ", $n), "", INT, -8.0, 8.0, 17, $i),
            p(concat!("form_", $n), concat!("FORMANT ", $n), "", LIN, -12.0, 12.0, 241, 0.0),
            p(concat!("gain_", $n), concat!("GAIN ", $n), "dB", LIN, -40.0, 0.0, 81, -6.0),
            p(concat!("pan_", $n), concat!("PAN ", $n), "", LIN, -100.0, 100.0, 201, 0.0),
        ]
    };
}

const fn voice_ids(n: usize) -> [&'static str; 4] {
    match n {
        0 => ["int_1", "form_1", "gain_1", "pan_1"],
        1 => ["int_2", "form_2", "gain_2", "pan_2"],
        2 => ["int_3", "form_3", "gain_3", "pan_3"],
        _ => ["int_4", "form_4", "gain_4", "pan_4"],
    }
}

const H_FX: [ParamSpec; 5] = [
    p("rev", "REVERB", "%", LIN, 0.0, 100.0, 101, 15.0),
    p("chor", "CHORUS", "%", LIN, 0.0, 100.0, 101, 10.0),
    p("pvar", "PITCH VARIATION", "%", LIN, 0.0, 100.0, 101, 0.0),
    p("tvar", "TIME VARIATION", "%", LIN, 0.0, 100.0, 101, 0.0),
    tog("exp", "AUTO EXPAND", 0.0),
];

const V1: [ParamSpec; 4] = voice!("1", 3.0);
const V2: [ParamSpec; 4] = voice!("2", 5.0);
const V3: [ParamSpec; 4] = voice!("3", -3.0);
const V4: [ParamSpec; 4] = voice!("4", 8.0);
const HEAD: [ParamSpec; 3] =
    [ch("ksrc", "KEY SOURCE", KEY_SOURCE_AUTO, 0.0), ch("key", "KEY", NOTE_NAMES, 0.0), ch("scale", "SCALE", SCALES, 0.0)];
const HEAD2: [ParamSpec; 3] =
    [ch("ksrc", "KEY SOURCE", KEY_SOURCE, 0.0), ch("key", "KEY", NOTE_NAMES, 0.0), ch("scale", "SCALE", SCALES, 0.0)];

const fn join_h<const N: usize>(parts: &[&[ParamSpec]]) -> [ParamSpec; N] {
    let mut out = [parts[0][0]; N];
    let (mut n, mut i) = (0, 0);
    while i < parts.len() {
        let mut j = 0;
        while j < parts[i].len() {
            out[n] = parts[i][j];
            n += 1;
            j += 1;
        }
        i += 1;
    }
    assert!(n == N);
    out
}

/// Dual AutoKey Harmoniser's panel.
pub(in super::super) static HARM_DUAL: [ParamSpec; 16] = join_h(&[&HEAD, &V1, &V2, &H_FX]);
/// Quad Voice Harmoniser's panel.
pub(in super::super) static HARM_QUAD: [ParamSpec; 24] = join_h(&[&HEAD2, &V1, &V2, &V3, &V4, &H_FX]);

/// What the harmonisers add to the voices: a modulated delay (Chorus) and a small
/// room (Reverb, the WING voice).
struct HarmFx {
    chorus: [Vec<f32>; 2],
    w: usize,
    phase: f32,
    step: f32,
    amount: f32,
    max: f32,
    space: Space<8>,
    rev: f32,
    primed: bool,
}

impl HarmFx {
    fn new(sr: f32) -> Self {
        let max = 0.012 * sr;
        let len = (max as usize + 8).next_power_of_two();
        let cfg = Config {
            rel: spread::<8>(0.6, 1.4, 31),
            max_size_ms: 40.0,
            max_pre_ms: 10.0,
            loop_ap: Some([0.1, 0.25]),
            diff_ms: [2.1, 3.7, 5.3, 7.9],
            diff_ref_ms: 20.0,
            seed: 0x7A11,
        };
        let mut space = Space::new(sr, &cfg);
        space.cal = 1.5;
        Self {
            chorus: [vec![0.0; len], vec![0.0; len]],
            w: 0,
            phase: 0.0,
            step: 0.9 / sr,
            amount: 0.0,
            max,
            space,
            rev: 0.0,
            primed: false,
        }
    }

    fn reset(&mut self) {
        for c in &mut self.chorus {
            c.fill(0.0);
        }
        self.space.clear();
        self.phase = 0.0;
    }

    fn set(&mut self, chorus: f32, rev: f32) {
        self.amount = chorus;
        self.rev = rev;
        let s = Settings {
            size_ms: 25.0,
            decay: crate::wing::fx::reverb::parts::Decay::lexicon(0.8, 1.0, 400.0, 6000.0),
            ..Settings::default()
        };
        self.space.set(&s);
        if !self.primed {
            self.primed = true;
            self.space.snap();
        }
    }

    #[inline]
    fn tick(&mut self, l: f32, r: f32, ctl: bool) -> (f32, f32) {
        if ctl && self.rev > 0.0 {
            self.space.control();
        }
        let mask = self.chorus[0].len() - 1;
        self.chorus[0][self.w & mask] = l;
        self.chorus[1][self.w & mask] = r;
        let (s, c) = (self.phase * TAU).sin_cos();
        let read = |buf: &[f32], d: f32, w: usize| {
            let i = d.floor();
            let f = d - i;
            let i = i as usize;
            let a = buf[(w + mask + 1 - i) & mask];
            let b = buf[(w + mask + 1 - i - 1) & mask];
            a + (b - a) * f
        };
        let (mut ol, mut or) = (l, r);
        if self.amount > 0.0 {
            let d = 0.5 * self.max * (1.0 + 0.6 * s);
            let e = 0.5 * self.max * (1.0 + 0.6 * c);
            ol += self.amount * read(&self.chorus[0], d, self.w);
            or += self.amount * read(&self.chorus[1], e, self.w);
            self.phase += self.step;
            if self.phase >= 1.0 {
                self.phase -= 1.0;
            }
        }
        self.w = self.w.wrapping_add(1);
        if self.rev <= 0.0 {
            return (ol, or);
        }
        let ((el, er), (tl, tr)) = self.space.tank_split(l, r);
        (ol + self.rev * (tl + 0.3 * el), or + self.rev * (tr + 0.3 * er))
    }
}

struct HVoice {
    voice: GrainVoice,
    ratio: f32,
    formant: f32,
    gain: f32,
    gl: f32,
    gr: f32,
    lfo: f32,
    lfo_rate: f32,
}

impl HVoice {
    fn new(seed: f32) -> Self {
        Self { voice: GrainVoice::new(), ratio: 1.0, formant: 1.0, gain: 0.5, gl: 0.7, gr: 0.7, lfo: seed, lfo_rate: 0.0 }
    }
}

/// Samples between the room's control updates (the tank's `CTL`).
const CTL_SAMPLES: u32 = crate::wing::fx::reverb::parts::CTL as u32;

/// The diatonic Harmonisers: each voice a scale interval from the sung note.
pub(in super::super) struct Harmoniser {
    sr: f32,
    lat: f32,
    n: usize,
    auto: bool,
    an: Analysis,
    voices: [HVoice; 4],
    intervals: [i32; 4],
    keys: KeyChoice,
    tracker: KeyTracker,
    fx: HarmFx,
    pvar: f32,
    tvar: f32,
    expand: bool,
    env: f32,
    env_rel: f32,
    sung: Option<i32>,
    ctl: u32,
}

impl Harmoniser {
    pub(in super::super) fn new(n: usize, auto: bool, ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        Self {
            sr,
            lat: LATENCY_MS * 0.001 * sr,
            n: n.min(4),
            auto,
            an: Analysis::new(sr),
            voices: [HVoice::new(0.13), HVoice::new(0.37), HVoice::new(0.61), HVoice::new(0.89)],
            intervals: [0; 4],
            keys: KeyChoice::new(),
            tracker: KeyTracker::new(sr),
            fx: HarmFx::new(sr),
            pvar: 0.0,
            tvar: 0.0,
            expand: false,
            env: 0.0,
            env_rel: coef(60.0, sr),
            sung: None,
            ctl: 0,
        }
    }

    /// Semitones from the sung note to the note `interval` away in the scale.
    fn semis(mask: &[bool; 12], sung: i32, interval: i32) -> f32 {
        if interval == 0 {
            return 0.0;
        }
        let steps = interval.abs() - 1;
        let dir = interval.signum();
        let start = nearest(sung as f32, mask, None, 0.0).unwrap_or(sung);
        let (mut note, mut left) = (start, steps);
        while left > 0 {
            note += dir;
            if mask[note.rem_euclid(12) as usize] {
                left -= 1;
            }
        }
        (note - start) as f32
    }

    fn run(&mut self, left: &mut [f32], right: &mut [f32], key: Option<(&[f32], &[f32])>) {
        let (sr, lat) = (self.sr, self.lat);
        for (i, (l, r)) in left.iter_mut().zip(right.iter_mut()).enumerate() {
            let mono = 0.5 * (*l + *r);
            // AutoKey keys from the key input if patched, otherwise from the input.
            if self.keys.source == 2 && self.auto {
                let k = key.map_or(mono, |(a, b)| 0.5 * (a[i] + b[i]));
                if let Some(d) = self.tracker.det.push(k) {
                    if d.voiced && d.hz > 0.0 {
                        self.tracker.note(d.hz);
                    }
                }
                self.tracker.since += 1;
                if self.tracker.since >= self.tracker.every {
                    self.tracker.since = 0;
                    self.tracker.analyse();
                    self.keys.auto_key = (self.tracker.found.0, self.tracker.found.1);
                }
            }
            if self.an.push(mono) {
                if self.an.voiced {
                    self.sung = Some(midi_of(self.an.hz, 440.0).round() as i32);
                } else {
                    self.sung = None;
                }
                let (k, s) = self.keys.active();
                let mask = scale_mask(k, s);
                for (v, iv) in self.voices.iter_mut().zip(self.intervals).take(self.n) {
                    let sung = self.sung.unwrap_or(69);
                    v.ratio = ratio_of(Self::semis(&mask, sung, iv));
                }
            }
            self.ctl += 1;
            let ctl = self.ctl >= CTL_SAMPLES;
            if ctl {
                self.ctl = 0;
            }
            // Auto Expand: the voices fade with the input's level in the gaps.
            let a = mono.abs();
            self.env += (a - self.env) * if a > self.env { 0.2 } else { self.env_rel };
            let expand = if self.expand { ((20.0 * self.env.max(1e-9).log10() + 55.0) / 15.0).clamp(0.0, 1.0) } else { 1.0 };

            let (mut hl, mut hr) = (0.0f32, 0.0f32);
            for v in self.voices.iter_mut().take(self.n) {
                v.lfo += v.lfo_rate;
                if v.lfo >= 1.0 {
                    v.lfo -= 1.0;
                }
                let drift = (v.lfo * TAU).sin();
                let ratio = v.ratio * ratio_of(drift * self.pvar * 0.4);
                let delay = sr * 0.004 * self.tvar * (1.0 + (v.lfo * TAU * 0.7).cos());
                let y = v.voice.tick(&self.an, sr, lat, ratio, v.formant, delay) * v.gain;
                hl += v.gl * y;
                hr += v.gr * y;
            }
            let (ol, or) = self.fx.tick(hl * expand, hr * expand, ctl);
            *l = rail(ol);
            *r = rail(or);
        }
    }
}

impl FxAlgo for Harmoniser {
    fn set_params(&mut self, p: &Params) {
        self.keys.source = p.choice("ksrc");
        self.keys.key = p.choice("key");
        self.keys.scale = p.choice("scale");
        self.pvar = pct(p.f("pvar"));
        self.tvar = pct(p.f("tvar"));
        self.expand = p.on("exp");
        self.fx.set(pct(p.f("chor")), pct(p.f("rev")));
        for (i, v) in self.voices.iter_mut().enumerate().take(self.n) {
            let ids = voice_ids(i);
            self.intervals[i] = p.f(ids[0]).round() as i32;
            v.formant = ratio_of(p.f(ids[1]));
            v.gain = fader(p.f(ids[2]));
            let (gl, gr) = pan_gains(p.f(ids[3]) * 0.01);
            v.gl = gl;
            v.gr = gr;
            v.lfo_rate = (0.25 + 0.15 * i as f32) / self.sr;
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        self.run(left, right, None);
    }

    fn process_keyed(&mut self, left: &mut [f32], right: &mut [f32], key: Option<(&[f32], &[f32])>) {
        self.run(left, right, key);
    }

    fn reset(&mut self) {
        self.an.reset();
        self.tracker.reset();
        self.fx.reset();
        for v in &mut self.voices {
            v.voice.reset();
        }
        self.env = 0.0;
        self.sung = None;
    }

    fn latency(&self) -> u32 {
        self.lat as u32
    }

    fn global_key(&mut self, key: u8, scale: u8) {
        self.keys.global = (usize::from(key).min(11), usize::from(scale).min(2));
    }

    /// Readouts (AutoKey): 0 the confidence 0..1, 1 the key plus one, 2 the scale (0
    /// Major, 1 Minor).
    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        out[0] = self.tracker.found.2;
        out[1] = self.tracker.found.0 as f32 + 1.0;
        out[2] = self.tracker.found.1 as f32;
    }
}

// ---- MIDI Harmoniser --------------------------------------------------------------------

const MIDI_MODES: &[&str] = &["MONOPHONIC", "POLYPHONIC"];

/// MIDI Harmoniser's panel.
pub(in super::super) static HARM_MIDI: [ParamSpec; 7] = [
    ch("mode", "MODE", MIDI_MODES, 0.0),
    p("glide", "GLIDE", "ms", LIN, 0.0, 500.0, 251, 40.0),
    p("width", "WIDTH", "%", LIN, 0.0, 100.0, 101, 60.0),
    p("chor", "CHORUS", "%", LIN, 0.0, 100.0, 101, 10.0),
    p("pvar", "PITCH VARIATION", "%", LIN, 0.0, 100.0, 101, 0.0),
    tog("vel", "USE VELOCITY", 0.0),
    p("gain", "GAIN", "dB", LIN, -40.0, 0.0, 81, -6.0),
];

/// Most notes held at once.
const MIDI_VOICES: usize = 4;

#[derive(Clone, Copy)]
struct Held {
    note: i32,
    gain: f32,
}

/// MIDI Harmoniser: the voice is re-pitched to the notes played: the last one
/// (Monophonic, with Glide) or up to four (Polyphonic).
pub(in super::super) struct MidiHarmoniser {
    sr: f32,
    lat: f32,
    an: Analysis,
    voices: [GrainVoice; MIDI_VOICES],
    pans: [(f32, f32); MIDI_VOICES],
    level: [f32; MIDI_VOICES],
    active: [Option<Held>; MIDI_VOICES],
    /// Glide's current pitch per voice, semitones from the sung note.
    semis: [f32; MIDI_VOICES],
    /// Target gain and current per voice.
    fade: [f32; MIDI_VOICES],
    glide: f32,
    poly: bool,
    use_vel: bool,
    width: f32,
    pvar: f32,
    gain: f32,
    drift: f32,
    fx: HarmFx,
    held: [i32; 16],
    nheld: usize,
    ctl: u32,
}

impl MidiHarmoniser {
    pub(in super::super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        Self {
            sr,
            lat: LATENCY_MS * 0.001 * sr,
            an: Analysis::new(sr),
            voices: [GrainVoice::new(), GrainVoice::new(), GrainVoice::new(), GrainVoice::new()],
            pans: [(0.7, 0.7); MIDI_VOICES],
            level: [0.0; MIDI_VOICES],
            active: [None; MIDI_VOICES],
            semis: [0.0; MIDI_VOICES],
            fade: [0.0; MIDI_VOICES],
            glide: 1.0,
            poly: false,
            use_vel: false,
            width: 0.6,
            pvar: 0.0,
            gain: 0.5,
            drift: 0.0,
            fx: HarmFx::new(sr),
            held: [0; 16],
            nheld: 0,
            ctl: 0,
        }
    }

    fn voice_for(&self, note: i32) -> Option<usize> {
        self.active.iter().position(|v| v.is_some_and(|h| h.note == note))
    }

    fn note_on(&mut self, note: i32, velocity: u8) {
        if self.nheld < self.held.len() && !self.held[..self.nheld].contains(&note) {
            self.held[self.nheld] = note;
            self.nheld += 1;
        }
        let gain = if self.use_vel { f32::from(velocity) / 127.0 } else { 1.0 };
        if self.poly {
            let slot = self.voice_for(note).or_else(|| self.active.iter().position(|v| v.is_none())).unwrap_or(MIDI_VOICES - 1);
            self.active[slot] = Some(Held { note, gain });
            self.fade[slot] = 1.0;
        } else {
            self.active[0] = Some(Held { note, gain });
            self.fade[0] = 1.0;
        }
    }

    fn note_off(&mut self, note: i32) {
        if let Some(i) = self.held[..self.nheld].iter().position(|&n| n == note) {
            self.held.copy_within(i + 1..self.nheld, i);
            self.nheld -= 1;
        }
        if self.poly {
            if let Some(i) = self.voice_for(note) {
                self.active[i] = None;
                self.fade[i] = 0.0;
            }
        } else if self.nheld > 0 {
            // Last note priority: fall back to the note still held longest.
            let n = self.held[self.nheld - 1];
            let g = self.active[0].map_or(1.0, |h| h.gain);
            self.active[0] = Some(Held { note: n, gain: g });
        } else {
            self.active[0] = None;
            self.fade[0] = 0.0;
        }
    }

    /// The voice-to-pitch step, per detector report.
    fn pitch_up(&mut self) {
        let midi = midi_of(self.an.hz.max(1.0), 440.0);
        if self.an.voiced {
            for (i, a) in self.active.iter().enumerate() {
                if let Some(h) = a {
                    self.semis[i] = h.note as f32 - midi;
                }
            }
        }
    }
}

impl FxAlgo for MidiHarmoniser {
    fn set_params(&mut self, p: &Params) {
        self.poly = p.choice("mode") == 1;
        self.glide = coef(p.f("glide").max(0.5), self.sr);
        self.use_vel = p.on("vel");
        self.width = pct(p.f("width"));
        self.pvar = pct(p.f("pvar"));
        self.gain = fader(p.f("gain"));
        self.fx.set(pct(p.f("chor")), 0.0);
        for (i, pan) in self.pans.iter_mut().enumerate() {
            let x = if i % 2 == 0 { -self.width } else { self.width } * (0.5 + 0.5 * ((i / 2) as f32));
            *pan = pan_gains(x.clamp(-1.0, 1.0));
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let (sr, lat) = (self.sr, self.lat);
        let fade = coef(10.0, sr);
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            if self.an.push(0.5 * (*l + *r)) {
                self.pitch_up();
            }
            self.drift += 0.5 / sr;
            if self.drift >= 1.0 {
                self.drift -= 1.0;
            }
            self.ctl += 1;
            let ctl = self.ctl >= CTL_SAMPLES;
            if ctl {
                self.ctl = 0;
            }
            let (mut hl, mut hr) = (0.0f32, 0.0f32);
            for (i, voice) in self.voices.iter_mut().enumerate() {
                let want = self.fade[i];
                self.level[i] += (want - self.level[i]) * fade;
                if self.level[i] < 1e-5 && want <= 0.0 {
                    continue;
                }
                let drift = (self.drift * TAU + i as f32).sin() * self.pvar * 0.4;
                let target = self.semis[i] + drift;
                // Mono has Glide; Poly goes straight there.
                self.semis[i] += if self.poly { 0.0 } else { (target - self.semis[i]) * self.glide };
                let g = self.active[i].map_or(1.0, |h| h.gain);
                let y = voice.tick(&self.an, sr, lat, ratio_of(self.semis[i]), 1.0, 0.0) * self.level[i] * g;
                hl += self.pans[i].0 * y;
                hr += self.pans[i].1 * y;
            }
            let (ol, or) = self.fx.tick(hl * self.gain, hr * self.gain, ctl);
            *l = rail(ol);
            *r = rail(or);
        }
    }

    fn reset(&mut self) {
        self.an.reset();
        for v in &mut self.voices {
            v.reset();
        }
        self.fx.reset();
        self.active = [None; MIDI_VOICES];
        self.level = [0.0; MIDI_VOICES];
        self.fade = [0.0; MIDI_VOICES];
        self.nheld = 0;
    }

    fn latency(&self) -> u32 {
        self.lat as u32
    }

    fn midi_note(&mut self, note: u8, velocity: u8) {
        if velocity > 0 {
            self.note_on(i32::from(note), velocity);
        } else {
            self.note_off(i32::from(note));
        }
    }
}
