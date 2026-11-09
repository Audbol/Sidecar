//! VS1 Vocal Shift, Pitch Doubler and Hypabass.
//!
//! The two pitch models are two voices, each on the WING's splicing `Shifter`, with
//! a short window: the guide states under 6 ms for both (5 ms here at unison, a
//! trade against low voices: Sidecar's reading of that limit). Stated: VS1
//! Semitones +-12, two voices and Link; Pitch Doubler Delay 0-25 ms, Pitch +-100
//! cents, LFO Rate 0.1-10 Hz, Depth 0-100. Sidecar's reading: Cents' range, Flat as
//! a fixed semitone flat (the guide lists it with the other interval switches),
//! Link as the second voice taking the first's interval, Width as how far the voices
//! pan apart, the Depth scale (100 = +-50 cents).
//!
//! Hypabass: the guide names Upper Freq Cut, LF Input Boost, Infra (18-36 Hz) and Sub
//! (above 36 Hz) levels and the two modes; the algorithm is Sidecar's reading. It
//! adds to the dry signal, as the WING's sub devices do, so its dry path stays at
//! unity. Infra Bass: octave dividers of the bass (the WING's Sub Octaver design),
//! each divider's output placed in its band. Lost Fundamental: harmonics of the bass
//! with the fundamental left out, so the ear implies it.

use super::{ch, p, pct, tog, LIN, LOG};
use crate::biquad::{Biquad, BiquadCoeffs};
use crate::model::{ParamKind, ParamSpec, Params};
use crate::wing::fx::blocks::{Envelope, Smoothed};
use crate::wing::fx::modulation::shared::{advance, ms};
use crate::wing::fx::pitch::shifter::{Shifter, ShifterSpec};
use crate::wing::fx::reverb::parts::fader;
use crate::wing::fx::utility::rail;
use crate::wing::fx::{FxAlgo, FxContext};
use std::f32::consts::{FRAC_PI_2, TAU};

const INT: ParamKind = ParamKind::Int;
/// Short window, for the guide's "under 6 ms". A splice's jump (window - search or
/// more) must outrun the head over the fade, up to an octave up.
const VS1_SPEC: ShifterSpec =
    ShifterSpec { max_offset_ms: 0.0, min_ms: 1.0, window_ms: 16.0, search_ms: 9.0, corr_ms: 6.0, fade_ms: 3.0 };

/// VS1 Vocal Shift.
pub(super) static VS1: [ParamSpec; 10] = [
    tog("link", "LINK", 0.0),
    p("semi_a", "SEMITONES A", "", INT, -12.0, 12.0, 25, 0.0),
    p("cent_a", "CENTS A", "", LIN, -50.0, 50.0, 101, 9.0),
    tog("oct_a", "OCT DOWN A", 0.0),
    tog("flat_a", "FLAT A", 0.0),
    p("semi_b", "SEMITONES B", "", INT, -12.0, 12.0, 25, 0.0),
    p("cent_b", "CENTS B", "", LIN, -50.0, 50.0, 101, -9.0),
    tog("oct_b", "OCT DOWN B", 0.0),
    tog("flat_b", "FLAT B", 0.0),
    p("width", "WIDTH", "%", LIN, 0.0, 100.0, 101, 60.0),
];

/// Pitch Doubler's controls, one set per voice.
pub(super) static DOUBLER: [ParamSpec; 14] = [
    p("dly_a", "DELAY A", "ms", LIN, 0.0, 25.0, 126, 8.0),
    p("pit_a", "PITCH A", "c", LIN, -100.0, 100.0, 201, 8.0),
    p("wid_a", "WIDTH A", "%", LIN, 0.0, 100.0, 101, 60.0),
    p("lev_a", "LEVEL A", "dB", LIN, -60.0, 0.0, 61, 0.0),
    tog("lfo_a", "LFO A", 0.0),
    p("rate_a", "LFO RATE A", "Hz", LOG, 0.1, 10.0, 101, 1.0),
    p("dep_a", "LFO DEPTH A", "", LIN, 0.0, 100.0, 101, 20.0),
    p("dly_b", "DELAY B", "ms", LIN, 0.0, 25.0, 126, 14.0),
    p("pit_b", "PITCH B", "c", LIN, -100.0, 100.0, 201, -8.0),
    p("wid_b", "WIDTH B", "%", LIN, 0.0, 100.0, 101, 60.0),
    p("lev_b", "LEVEL B", "dB", LIN, -60.0, 0.0, 61, 0.0),
    tog("lfo_b", "LFO B", 0.0),
    p("rate_b", "LFO RATE B", "Hz", LOG, 0.1, 10.0, 101, 1.3),
    p("dep_b", "LFO DEPTH B", "", LIN, 0.0, 100.0, 101, 20.0),
];

/// Pans a voice: `pan` -1..1 to (left, right) gains (equal power).
#[inline]
fn pan_gains(pan: f32) -> (f32, f32) {
    let (s, c) = ((pan.clamp(-1.0, 1.0) + 1.0) * 0.5 * FRAC_PI_2).sin_cos();
    (c, s)
}

/// The pitch ratio of an interval in cents.
#[inline]
fn ratio_of(cents: f32) -> f32 {
    2f32.powf(cents / 1200.0)
}

struct Voice {
    shifter: Shifter,
    ratio: Smoothed,
    gl: Smoothed,
    gr: Smoothed,
}

impl Voice {
    fn new(spec: ShifterSpec, sr: f32) -> Self {
        Self {
            shifter: Shifter::new(spec, sr, false),
            ratio: Smoothed::new(1.0, 20.0, sr),
            gl: Smoothed::new(0.7, 30.0, sr),
            gr: Smoothed::new(0.7, 30.0, sr),
        }
    }

    fn snap(&mut self) {
        for s in [&mut self.ratio, &mut self.gl, &mut self.gr] {
            s.snap(s.target);
        }
    }
}

/// VS1: two voices sharing the input, each its interval and pan.
pub(super) struct VocalShift {
    voices: [Voice; 2],
    primed: bool,
}

impl VocalShift {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        Self { voices: [Voice::new(VS1_SPEC, sr), Voice::new(VS1_SPEC, sr)], primed: false }
    }
}

impl FxAlgo for VocalShift {
    fn set_params(&mut self, p: &Params) {
        let link = p.on("link");
        let width = pct(p.f("width"));
        for (i, v) in self.voices.iter_mut().enumerate() {
            let id = |base: &'static str| if i == 0 || link { base } else { b_id(base) };
            let cents = 100.0 * p.f(id("semi_a"))
                + p.f(id("cent_a"))
                + if p.on(id("oct_a")) { -1200.0 } else { 0.0 }
                + if p.on(id("flat_a")) { -100.0 } else { 0.0 };
            v.ratio.target = ratio_of(cents);
            let (gl, gr) = pan_gains(if i == 0 { -width } else { width });
            v.gl.target = gl;
            v.gr.target = gr;
        }
        if !self.primed {
            self.primed = true;
            for v in &mut self.voices {
                v.snap();
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let m = 0.5 * (*l + *r);
            let (mut ol, mut or) = (0.0, 0.0);
            for v in &mut self.voices {
                let y = v.shifter.tick_mono(m, v.ratio.tick(), 0.0);
                ol += v.gl.tick() * y;
                or += v.gr.tick() * y;
            }
            *l = ol;
            *r = or;
        }
    }

    fn reset(&mut self) {
        for v in &mut self.voices {
            v.shifter.clear();
            v.snap();
        }
    }
}

/// The second voice's id for a first voice's ("semi_a" to "semi_b").
fn b_id(id: &'static str) -> &'static str {
    match id {
        "semi_a" => "semi_b",
        "cent_a" => "cent_b",
        "oct_a" => "oct_b",
        "flat_a" => "flat_b",
        other => other,
    }
}

// ---- Pitch Doubler --------------------------------------------------------------------

const DOUBLER_SPEC: ShifterSpec =
    ShifterSpec { max_offset_ms: 25.0, min_ms: 1.0, window_ms: 16.0, search_ms: 9.0, corr_ms: 6.0, fade_ms: 3.0 };
/// LFO Depth 100 is +-50 cents.
const LFO_CENTS: f32 = 50.0;

struct DoublerVoice {
    v: Voice,
    delay: Smoothed,
    cents: f32,
    level: Smoothed,
    lfo_on: bool,
    phase: f32,
    inc: f32,
    depth: f32,
}

/// Pitch Doubler: two voices, each with a delay, a detune and an optional pitch LFO.
pub(super) struct PitchDoubler {
    sr: f32,
    voices: [DoublerVoice; 2],
    primed: bool,
}

impl PitchDoubler {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let voice = || DoublerVoice {
            v: Voice::new(DOUBLER_SPEC, sr),
            delay: Smoothed::new(0.0, 40.0, sr),
            cents: 0.0,
            level: Smoothed::new(1.0, 30.0, sr),
            lfo_on: false,
            phase: 0.0,
            inc: 0.0,
            depth: 0.0,
        };
        Self { sr, voices: [voice(), voice()], primed: false }
    }
}

impl FxAlgo for PitchDoubler {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        let (ids_a, ids_b) = (
            ["dly_a", "pit_a", "wid_a", "lev_a", "lfo_a", "rate_a", "dep_a"],
            ["dly_b", "pit_b", "wid_b", "lev_b", "lfo_b", "rate_b", "dep_b"],
        );
        for (i, ids) in [ids_a, ids_b].into_iter().enumerate() {
            let v = &mut self.voices[i];
            v.delay.target = ms(p.f(ids[0]), sr);
            v.cents = p.f(ids[1]);
            let (gl, gr) = pan_gains(pct(p.f(ids[2])) * if i == 0 { -1.0 } else { 1.0 });
            v.v.gl.target = gl;
            v.v.gr.target = gr;
            v.level.target = fader(p.f(ids[3]));
            v.lfo_on = p.on(ids[4]);
            v.inc = p.f(ids[5]) / sr;
            v.depth = pct(p.f(ids[6])) * LFO_CENTS;
        }
        if !self.primed {
            self.primed = true;
            for v in &mut self.voices {
                v.v.snap();
                v.delay.snap(v.delay.target);
                v.level.snap(v.level.target);
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let m = 0.5 * (*l + *r);
            let (mut ol, mut or) = (0.0, 0.0);
            for v in &mut self.voices {
                advance(&mut v.phase, v.inc);
                let cents = v.cents + if v.lfo_on { v.depth * (TAU * v.phase).sin() } else { 0.0 };
                v.v.ratio.target = ratio_of(cents);
                let y = v.v.shifter.tick_mono(m, v.v.ratio.tick(), v.delay.tick());
                let g = v.level.tick();
                ol += g * v.v.gl.tick() * y;
                or += g * v.v.gr.tick() * y;
            }
            *l = ol;
            *r = or;
        }
    }

    fn reset(&mut self) {
        for v in &mut self.voices {
            v.v.shifter.clear();
            v.v.snap();
            v.delay.snap(v.delay.target);
            v.level.snap(v.level.target);
            v.phase = 0.0;
        }
    }
}

// ---- Hypabass -------------------------------------------------------------------------

/// Hypabass.
pub(super) static HYPABASS: [ParamSpec; 5] = [
    p("cut", "UPPER FREQ CUT", "Hz", LOG, 50.0, 300.0, 101, 120.0),
    p("boost", "LF INPUT BOOST", "dB", LIN, 0.0, 12.0, 25, 3.0),
    p("infra", "INFRA", "%", LIN, 0.0, 100.0, 101, 50.0),
    p("sub", "SUB", "%", LIN, 0.0, 100.0, 101, 50.0),
    ch("mode", "MODE", &["INFRA BASS", "LOST FUNDAMENTAL"], 0.0),
];

/// Below this is Infra, above Sub (the guide's split).
const INFRA_TOP_HZ: f32 = 36.0;
const Q_BUTTER: f64 = std::f64::consts::FRAC_1_SQRT_2;

/// A comparator with hysteresis into a flip-flop: an octave down of its input.
#[derive(Clone, Copy, Default)]
struct Divider {
    high: bool,
    flip: bool,
}

impl Divider {
    #[inline]
    fn tick(&mut self, x: f32, hysteresis: f32) -> f32 {
        if !self.high && x > hysteresis {
            self.high = true;
            self.flip = !self.flip;
        } else if self.high && x < -hysteresis {
            self.high = false;
        }
        if self.flip {
            1.0
        } else {
            -1.0
        }
    }
}

/// One generator: an input band, an envelope, an octave divider and an output band.
#[derive(Clone, Default)]
struct Band {
    /// The source band, high-pass and low-pass.
    src: [Biquad; 2],
    env: Envelope,
    div: Divider,
    /// The band its output goes to, high-pass and low-pass.
    out: [Biquad; 2],
}

impl Band {
    fn reset(&mut self) {
        for b in self.src.iter_mut().chain(self.out.iter_mut()) {
            b.reset();
        }
        self.env.reset();
        self.div = Divider::default();
    }
}

fn hp(sr: f64, hz: f64) -> BiquadCoeffs {
    BiquadCoeffs::high_pass(sr, hz, Q_BUTTER)
}

fn lp(sr: f64, hz: f64) -> BiquadCoeffs {
    BiquadCoeffs::low_pass(sr, hz, Q_BUTTER)
}

pub(super) struct Hypabass {
    sr: f64,
    lost: bool,
    /// Keeps the upper frequencies out of the generator.
    cut: Biquad,
    boost: Smoothed,
    infra_level: Smoothed,
    sub_level: Smoothed,
    infra: Band,
    sub: Band,
    primed: bool,
    last_cut: f32,
}

impl Hypabass {
    pub(super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let s = |v: f32| Smoothed::new(v, 30.0, sr);
        let band = || {
            let mut b = Band::default();
            b.env.set(1.0, 0.0, 40.0, sr);
            b
        };
        Self {
            sr: f64::from(sr),
            lost: false,
            cut: Biquad::default(),
            boost: s(1.0),
            infra_level: s(0.5),
            sub_level: s(0.5),
            infra: band(),
            sub: band(),
            primed: false,
            last_cut: -1.0,
        }
    }

    /// The bands: Infra Bass divides an octave above its output band, Lost
    /// Fundamental's harmonics sit an octave or more above their source.
    fn design(&mut self, cut: f32) {
        let sr = self.sr;
        let (cut, top) = (f64::from(cut), f64::from(INFRA_TOP_HZ));
        self.cut.set_coefficients(lp(sr, cut));
        if self.lost {
            self.infra.src = [band_of(hp(sr, 18.0)), band_of(lp(sr, top))];
            self.infra.out = [band_of(hp(sr, top)), band_of(lp(sr, 4.0 * top))];
            self.sub.src = [band_of(hp(sr, top)), band_of(lp(sr, cut))];
            self.sub.out = [band_of(hp(sr, cut)), band_of(lp(sr, 4.0 * cut))];
        } else {
            self.infra.src = [band_of(hp(sr, top)), band_of(lp(sr, 2.0 * top))];
            self.infra.out = [band_of(hp(sr, 18.0)), band_of(lp(sr, top))];
            self.sub.src = [band_of(hp(sr, 2.0 * top)), band_of(lp(sr, cut.max(2.5 * top)))];
            self.sub.out = [band_of(hp(sr, top)), band_of(lp(sr, cut))];
        }
    }

    /// The generated signal of a band for one sample of the bass.
    #[inline]
    fn generate(b: &mut Band, x: f32, lost: bool) -> f32 {
        let s = b.src[0].tick(x, 0);
        let s = b.src[1].tick(s, 0);
        let env = b.env.tick(s.abs());
        let y = if lost {
            // Chebyshev terms: a sine of the source's amplitude gives its 2nd and 3rd
            // harmonics and no fundamental.
            let w = (s / (env + 1e-6)).clamp(-1.0, 1.0);
            env * (0.7 * (2.0 * w * w - 1.0) + 0.4 * (4.0 * w * w * w - 3.0 * w))
        } else {
            env * b.div.tick(s, 0.1 * env)
        };
        let y = b.out[0].tick(y, 0);
        b.out[1].tick(y, 0)
    }
}

/// A filter set to `c`.
fn band_of(c: BiquadCoeffs) -> Biquad {
    let mut b = Biquad::default();
    b.set_coefficients(c);
    b
}

impl FxAlgo for Hypabass {
    fn set_params(&mut self, p: &Params) {
        let lost = p.choice("mode") == 1;
        let cut = p.f("cut");
        if lost != self.lost || cut != self.last_cut {
            self.lost = lost;
            self.last_cut = cut;
            self.design(cut);
        }
        self.boost.target = 10f32.powf(p.f("boost") * 0.05);
        self.infra_level.target = pct(p.f("infra"));
        self.sub_level.target = pct(p.f("sub"));
        if !self.primed {
            self.primed = true;
            self.boost.snap(self.boost.target);
            self.infra_level.snap(self.infra_level.target);
            self.sub_level.snap(self.sub_level.target);
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            let m = 0.5 * (*l + *r);
            let x = self.cut.tick(m, 0) * self.boost.tick();
            let lost = self.lost;
            let gi = Self::generate(&mut self.infra, x, lost) * self.infra_level.tick();
            let gs = Self::generate(&mut self.sub, x, lost) * self.sub_level.tick();
            let g = gi + gs;
            *l = rail(*l + g);
            *r = rail(*r + g);
        }
    }

    fn reset(&mut self) {
        self.cut.reset();
        self.infra.reset();
        self.sub.reset();
    }
}
