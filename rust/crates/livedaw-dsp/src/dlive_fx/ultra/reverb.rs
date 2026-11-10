//! Spaces Reverb Designer and Plate Reverb Designer.
//!
//! Stated (guide, panel pictures). Spaces: Pre Delay 0-170 ms, Decay 0.1-60 s, tabs
//! Space (Reflections model, Size, Stereo Spread, Direct Send, Early/Late/Sub levels,
//! Sub below 80 Hz), Echoes (6, each gain -40..+10 dB, 0-200 ms, on/off), Texture
//! (Impact, Density, Early/Mid/Late Diffusion 0-8, Modulation), Damping, Colour, Output
//! EQ. Plate: 11 types, Decay 0.1-10 s, Pre Delay 0-170 ms (the guide's chapter), Diffusion,
//! Size, Position, Shape, Width, Modulation, the same Echoes, Output EQ. Sidecar's
//! reading: the algorithm, which runs on the WING reverb voice as SMR's rooms do (the
//! tank `Space`, a tap bank for reflections and one for the echoes, like the SMR Expert
//! page's Echo 1 left and Echo 2 right); the Reflections model's names (numbered); the
//! ranges and meanings of Size, Density, Impact, Position, Shape and Damping; what each
//! Plate type does (an offset to its Decay's HF, size and build-up); the Output EQ as
//! shelves and cut filters; Colour as a tilt, the shelves' opposite ends moving together.

use super::super::{ch, p, pct, tog, LIN, LOG};
use crate::biquad::{Biquad, BiquadCoeffs};
use crate::model::{ParamSpec, Params};
use crate::wing::fx::reverb::parts::{fader, hash01, rate, run, sign, spread, Decay, Param, Taps, Voice};
use crate::wing::fx::reverb::space::{Config, Settings, Space};
use crate::wing::fx::utility::rail;
use crate::wing::fx::{FxAlgo, FxContext};

const REFL_MODELS: &[&str] = &["1", "2", "3", "4"];
const PLATES: &[&str] =
    &["CHROME", "STEEL", "BRIGHT", "DARK", "DARK VOX", "XL", "VOCAL FOCUS", "SMALL", "AMBIENCE", "CLASSIC", "IMPACT"];

macro_rules! echo {
    ($n:literal, $t:expr) => {
        [
            p(concat!("eg", $n), concat!("ECHO ", $n, " GAIN"), "dB", LIN, -40.0, 10.0, 51, 0.0),
            p(concat!("et", $n), concat!("ECHO ", $n, " TIME"), "ms", LIN, 0.0, 200.0, 201, $t),
            tog(concat!("eo", $n), concat!("ECHO ", $n, " ON"), 0.0),
        ]
    };
}

const ECHOES: [ParamSpec; 18] = {
    let a = echo!("1", 30.0);
    let b = echo!("2", 45.0);
    let c = echo!("3", 70.0);
    let d = echo!("4", 95.0);
    let e = echo!("5", 130.0);
    let f = echo!("6", 170.0);
    [a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2], d[0], d[1], d[2], e[0], e[1], e[2], f[0], f[1], f[2]]
};

const OUTPUT_EQ: [ParamSpec; 4] = [
    p("lc", "OUT LF CUT", "Hz", LOG, 20.0, 1000.0, 101, 20.0),
    p("hc", "OUT HF CUT", "Hz", LOG, 1000.0, 20000.0, 101, 20000.0),
    p("lg", "OUT LF", "dB", LIN, -12.0, 12.0, 49, 0.0),
    p("hg", "OUT HF", "dB", LIN, -12.0, 12.0, 49, 0.0),
];

const SPACE_MAIN: [ParamSpec; 15] = [
    p("pre", "PRE DELAY", "ms", LIN, 0.0, 170.0, 171, 20.0),
    p("decay", "DECAY", "s", LOG, 0.1, 60.0, 101, 2.0),
    ch("rmodel", "REFLECTIONS", REFL_MODELS, 0.0),
    p("size", "SIZE", "%", LIN, 0.0, 100.0, 101, 50.0),
    p("spread", "STEREO SPREAD", "%", LIN, 0.0, 100.0, 101, 80.0),
    p("direct", "DIRECT SEND", "dB", LIN, -60.0, 0.0, 61, -60.0),
    p("early", "EARLY", "dB", LIN, -60.0, 6.0, 67, 0.0),
    p("late", "LATE", "dB", LIN, -60.0, 6.0, 67, 0.0),
    p("sub", "SUB", "dB", LIN, -60.0, 6.0, 67, -60.0),
    p("impact", "IMPACT", "%", LIN, 0.0, 100.0, 101, 50.0),
    p("dens", "DENSITY", "%", LIN, 0.0, 100.0, 101, 60.0),
    p("ediff", "EARLY DIFFUSION", "", LIN, 0.0, 8.0, 81, 4.0),
    p("mdiff", "MID DIFFUSION", "", LIN, 0.0, 8.0, 81, 4.0),
    p("ldiff", "LATE DIFFUSION", "", LIN, 0.0, 8.0, 81, 5.0),
    p("mod", "MODULATION", "%", LIN, 0.0, 100.0, 101, 20.0),
];
const SPACE_TAIL: [ParamSpec; 3] = [
    p("damp", "DAMPING", "Hz", LOG, 500.0, 20000.0, 101, 6000.0),
    p("lfdamp", "LF DAMPING", "x", LOG, 0.25, 2.5, 101, 1.0),
    p("colour", "COLOUR", "dB", LIN, -12.0, 12.0, 49, 0.0),
];
const PLATE_MAIN: [ParamSpec; 9] = [
    ch("type", "TYPE", PLATES, 9.0),
    p("pre", "PRE DELAY", "ms", LIN, 0.0, 170.0, 171, 10.0),
    p("decay", "DECAY", "s", LOG, 0.1, 10.0, 101, 1.8),
    p("diff", "DIFFUSION", "%", LIN, 0.0, 100.0, 101, 60.0),
    p("size", "SIZE", "%", LIN, 0.0, 100.0, 101, 50.0),
    p("pos", "POSITION", "%", LIN, 0.0, 100.0, 101, 50.0),
    p("shape", "SHAPE", "%", LIN, 0.0, 100.0, 101, 35.0),
    p("width", "WIDTH", "%", LIN, 0.0, 100.0, 101, 80.0),
    p("mod", "MODULATION", "%", LIN, 0.0, 100.0, 101, 15.0),
];

const fn join2<const N: usize>(parts: &[&[ParamSpec]]) -> [ParamSpec; N] {
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

/// Spaces Reverb Designer's panel.
pub(in super::super) static SPACES: [ParamSpec; 40] = join2(&[&SPACE_MAIN, &SPACE_TAIL, &ECHOES, &OUTPUT_EQ]);
/// Plate Reverb Designer's panel.
pub(in super::super) static PLATE: [ParamSpec; 31] = join2(&[&PLATE_MAIN, &ECHOES, &OUTPUT_EQ]);

const fn echo_ids(n: usize) -> [&'static str; 3] {
    match n {
        0 => ["eg1", "et1", "eo1"],
        1 => ["eg2", "et2", "eo2"],
        2 => ["eg3", "et3", "eo3"],
        3 => ["eg4", "et4", "eo4"],
        4 => ["eg5", "et5", "eo5"],
        _ => ["eg6", "et6", "eo6"],
    }
}

const REFL: usize = 14;
const ECHO: usize = 6;
/// Early reflections' span in ms against Size, widest to tightest (model 1..4).
const REFL_SPAN: [f32; 4] = [1.4, 0.8, 2.0, 1.1];
/// Plate types: Size scale, high-decay rate scale, build-up (ms), brightness shelf (dB).
const PLATE_TYPES: [(f32, f32, f32, f32); 11] = [
    (1.0, 0.8, 25.0, 3.0),  // Chrome
    (1.1, 1.0, 30.0, 1.0),  // Steel
    (1.0, 0.7, 25.0, 4.0),  // Bright
    (1.1, 2.0, 40.0, -4.0), // Dark
    (0.9, 1.7, 35.0, -3.0), // Dark Vox
    (1.6, 1.2, 60.0, 0.0),  // XL
    (0.9, 1.1, 20.0, 1.0),  // Vocal Focus
    (0.6, 1.2, 10.0, 0.0),  // Small
    (0.5, 1.6, 8.0, -1.0),  // Ambience
    (1.0, 1.1, 30.0, 0.0),  // Classic
    (1.0, 1.3, 12.0, 2.0),  // Impact
];
const SUB_HZ: f32 = 80.0;

/// A designer: tank, reflections, six echoes, output EQ. One struct, two panels.
pub(in super::super) struct Designer {
    is_plate: bool,
    sr: f32,
    space: Space<16>,
    size_ms: (f32, f32),
    reflections: Taps<REFL>,
    echoes: Taps<ECHO>,
    u: [[f32; REFL]; 2],
    sgn: [[f32; REFL]; 2],
    a: f32,
    clock: usize,
    primed: bool,
    early: Param,
    late: Param,
    sub_g: Param,
    direct: Param,
    tail_ap: Param,
    sub_lp: [Biquad; 2],
    shelf_lo: [Biquad; 2],
    shelf_hi: [Biquad; 2],
}

impl Designer {
    fn new(is_plate: bool, ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let room_ap = Some([0.15, 0.3]);
        let (rel, size, loop_ap, diff_ms, diff_ref, seed) = if is_plate {
            (spread::<16>(0.5, 1.5, 21), (10.0, 110.0), Some([0.2, 0.25]), [1.6, 2.3, 3.7, 5.3], 30.0, 0x61A7u32)
        } else {
            (spread::<16>(0.55, 1.45, 23), (10.0, 160.0), room_ap, [4.8, 3.6, 12.7, 9.3], 80.0, 0x1E43u32)
        };
        let max_size = size.1 * if is_plate { 1.6 } else { 1.0 };
        let cfg = Config { rel, max_size_ms: max_size, max_pre_ms: 170.0, loop_ap, diff_ms, diff_ref_ms: diff_ref, seed };
        let mut space = Space::new(sr, &cfg);
        space.cal = 1.5;
        let mut u = [[0.0; REFL]; 2];
        let mut sgn = [[1.0; REFL]; 2];
        for c in 0..2 {
            let s = seed ^ (0x51 * c as u32 + 7);
            for k in 0..REFL {
                u[c][k] = (0.13 * c as f32 + k as f32 * 0.618_034 + 0.2 * hash01(k as u32, s)).rem_euclid(1.0);
                sgn[c][k] = sign(k as u32, s ^ 0x3F);
            }
        }
        let f = || [Biquad::default(), Biquad::default()];
        Self {
            is_plate,
            sr,
            space,
            size_ms: size,
            reflections: Taps::new(sr, 420.0, REFL),
            echoes: Taps::new(sr, 400.0, ECHO),
            u,
            sgn,
            a: rate(40.0, sr),
            clock: 0,
            primed: false,
            early: Param::default(),
            late: Param::default(),
            sub_g: Param::default(),
            direct: Param::default(),
            tail_ap: Param::default(),
            sub_lp: f(),
            shelf_lo: f(),
            shelf_hi: f(),
        }
    }

    pub(in super::super) fn spaces(ctx: FxContext) -> Self {
        Self::new(false, ctx)
    }

    pub(in super::super) fn plate(ctx: FxContext) -> Self {
        Self::new(true, ctx)
    }

    fn set_echoes(&mut self, p: &Params) {
        let ms = 0.001 * self.sr;
        for k in 0..ECHO {
            let ids = echo_ids(k);
            let on = p.on(ids[2]);
            let g = if on { fader(p.f(ids[0])) } else { 0.0 };
            let t = (p.f(ids[1]) * ms).max(1.0);
            // Odd echoes to the left, even to the right, as SMR's Expert echoes.
            for c in 0..2 {
                self.echoes.set(c, k, t, if (k + c) % 2 == 0 { g } else { 0.0 });
            }
        }
    }

    fn set_reflections(&mut self, pre_ms: f32, span_ms: f32, gain_sum: f32) {
        let ms = 0.001 * self.sr;
        for c in 0..2 {
            let norm = (0.5 * REFL as f32).sqrt();
            for k in 0..REFL {
                let g = (1.0 - 0.75 * self.u[c][k]) * self.sgn[c][k] * gain_sum / norm;
                self.reflections.set(c, k, (pre_ms + 2.0 + span_ms * self.u[c][k]) * ms, g);
            }
        }
    }

    fn output_eq(&mut self, p: &Params, tilt_db: f32) {
        let sr = f64::from(self.sr);
        for b in &mut self.shelf_lo {
            b.set_coefficients(BiquadCoeffs::low_shelf(sr, 250.0, 0.7, f64::from(p.f("lg") - tilt_db)));
        }
        for b in &mut self.shelf_hi {
            b.set_coefficients(BiquadCoeffs::high_shelf(sr, 4000.0, 0.7, f64::from(p.f("hg") + tilt_db)));
        }
    }

    fn snap(&mut self) {
        self.space.snap();
        self.reflections.snap();
        self.echoes.snap();
        for q in [&mut self.early, &mut self.late, &mut self.sub_g, &mut self.direct, &mut self.tail_ap] {
            q.snap();
        }
        self.primed = true;
    }
}

impl FxAlgo for Designer {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        let hf = p.f(if self.is_plate { "decay" } else { "damp" });
        let (lo, hi) = self.size_ms;
        let size_pct = pct(p.f("size"));
        if self.is_plate {
            let (scale, hf_scale, build, bright) = PLATE_TYPES[p.choice("type").min(10)];
            let size_ms = (lo + (hi - lo) * size_pct) * scale;
            let mid = 1.0 / p.f("decay").max(0.05);
            let decay = Decay {
                bands: [mid * 1.15, mid, mid * (1.0 + 1.2 * hf_scale), mid * (1.5 + 5.0 * hf_scale)],
                xo: [250.0, 3000.0, 9000.0],
            };
            let s = Settings {
                pre_ms: p.f("pre"),
                size_ms,
                decay,
                diffusion: pct(p.f("diff")),
                shape: pct(p.f("shape")),
                build_ms: build * (0.6 + 0.8 * size_pct),
                mod_hz: 0.8,
                mod_ms: 0.02 + 1.2 * pct(p.f("mod")),
                lc: p.f("lc"),
                hc: p.f("hc"),
                width: 2.0 * pct(p.f("width")),
                ..Settings::default()
            };
            self.space.set(&s);
            // Position moves the reflection pattern, as a pickup's place on the plate.
            let pos = pct(p.f("pos"));
            self.set_reflections(s.pre_ms, size_ms * (0.4 + 1.2 * pos), 0.4);
            self.early.set(fader(0.0));
            self.late.set(fader(0.0));
            self.sub_g.set(0.0);
            self.direct.set(0.0);
            self.tail_ap.set(0.2 + 0.4 * s.diffusion);
            let tilt = bright;
            self.output_eq(p, tilt);
        } else {
            let size_ms = lo + (hi - lo) * size_pct;
            let mid = 1.0 / p.f("decay").max(0.05);
            let damp_hz = hf.min(sr * 0.4);
            let decay = Decay {
                bands: [mid / p.f("lfdamp").max(0.05), mid, mid * 1.6, mid * 6.0],
                xo: [300.0, damp_hz, (damp_hz * 2.0).min(sr * 0.45)],
            };
            let span = REFL_SPAN[p.choice("rmodel").min(3)];
            let s = Settings {
                pre_ms: p.f("pre"),
                size_ms,
                decay,
                diffusion: p.f("ediff") / 8.0,
                shape: pct(p.f("impact")),
                build_ms: size_ms * (0.05 + 0.35 * p.f("mdiff")),
                mod_hz: 0.6,
                mod_ms: 0.02 + 1.5 * pct(p.f("mod")),
                lc: p.f("lc"),
                hc: p.f("hc"),
                width: 2.0 * pct(p.f("spread")),
                ..Settings::default()
            };
            self.space.set(&s);
            self.set_reflections(s.pre_ms, size_ms * span, 0.5 + 0.5 * pct(p.f("dens")));
            self.early.set(fader(p.f("early")));
            self.late.set(fader(p.f("late")));
            self.sub_g.set(fader(p.f("sub")));
            self.direct.set(fader(p.f("direct")));
            self.tail_ap.set(0.1 + 0.06 * p.f("ldiff"));
            self.output_eq(p, p.f("colour") * 0.5);
        }
        self.set_echoes(p);
        for b in &mut self.sub_lp {
            b.set_coefficients(BiquadCoeffs::low_pass(f64::from(sr), f64::from(SUB_HZ), 0.707));
        }
        if !self.primed {
            self.snap();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        run(self, left, right);
    }

    fn reset(&mut self) {
        self.space.clear();
        self.reflections.clear();
        self.echoes.clear();
        for b in self.sub_lp.iter_mut().chain(self.shelf_lo.iter_mut()).chain(self.shelf_hi.iter_mut()) {
            b.reset();
        }
    }
}

impl Voice for Designer {
    fn clock(&mut self) -> &mut usize {
        &mut self.clock
    }

    fn control(&mut self) {
        self.space.control();
        self.tail_ap.step(self.a);
        self.space.fdn.set_ap_gain(self.tail_ap.cur);
        self.reflections.control(self.a, false);
        self.echoes.control(self.a, false);
        for q in [&mut self.early, &mut self.late, &mut self.sub_g, &mut self.direct] {
            q.step(self.a);
        }
    }

    #[inline]
    fn tick(&mut self, xl: f32, xr: f32) -> (f32, f32) {
        let (rl, rr) = self.reflections.tick(xl, xr);
        let (el, er) = self.echoes.tick(xl, xr);
        let ((bl, br), (tl, tr)) = self.space.tank_split(xl, xr);
        let (eg, lg) = (self.early.cur, self.late.cur);
        let (mut l, mut r) = (eg * (rl + 0.3 * bl) + lg * tl, eg * (rr + 0.3 * br) + lg * tr);
        let g = self.sub_g.cur;
        if g > 0.0 {
            l += g * self.sub_lp[0].tick(tl, 0);
            r += g * self.sub_lp[1].tick(tr, 0);
        }
        // The echoes and the Direct Send join after Width, so they stay on their side;
        // the Output EQ's shelves take everything.
        let (ol, or) = self.space.out.tick(l, r);
        let d = self.direct.cur;
        let l = self.shelf_hi[0].tick(self.shelf_lo[0].tick(ol + el + d * xl, 0), 0);
        let r = self.shelf_hi[1].tick(self.shelf_lo[1].tick(or + er + d * xr, 0), 0);
        (rail(l), rail(r))
    }
}
