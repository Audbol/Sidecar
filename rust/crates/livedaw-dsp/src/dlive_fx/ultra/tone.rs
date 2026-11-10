//! Saturator and Amp+Cab Distortion.
//!
//! Saturator. Stated: Modes A-E (triode, pentode, germanium, bipolar channel strip,
//! tape), Bias (a DC offset into the stage), PAD (-30 dB in, +30 dB out), Tilt +-20 dB,
//! LF Cut 20 Hz-1 kHz (12 dB/oct), LF Punch, HF Cut 1-24 kHz (6 or 12 dB/oct), Level
//! -40 to +10 dB. Sidecar's reading: each mode's transfer curve; a fixed 6 dB of drive
//! into the stage, which the guide doesn't mention (the input level is the drive); LF
//! Punch as a low shelf boost ahead of the stage; Tilt as a 1 kHz pivot.
//!
//! Amp+Cab Distortion. Stated: 5 amps (Deluxe, VX30, MPlex, BDS1, BassVT), 9 cabinets or
//! None, Gain, Output, Thicken, EQ. The amps run on the WING's amp voicings: Deluxe its
//! Deluxe, VX30 its Jazz C, MPlex its UK Rock, BDS1 its rack amp (a SansAmp-style DI),
//! BassVT its UK Rock, darker: which voicing each takes is Sidecar's reading of the
//! names. The panel's EQ as Bass, Mid, Treble, Thicken as a low-mid shelf, the
//! cabinet responses (a high-pass, a low-pass, a resonance): Sidecar's reading. Four
//! cabinets' names aren't in the sources we can read and are numbered.

use super::super::{ch, p, pct, tog, LIN};
use crate::biquad::{Biquad, BiquadCoeffs};
use crate::model::{ParamSpec, Params};
use crate::wing::fx::amp::oversample::Oversampler2;
use crate::wing::fx::blocks::{fast_tanh, DcBlock};
use crate::wing::fx::utility::rail;
use crate::wing::fx::x32_voices::bridge;
use crate::wing::fx::{FxAlgo, FxContext};

// ---- Saturator ----------------------------------------------------------------------

const MODES: &[&str] = &["A", "B", "C", "D", "E"];
const HF_SLOPES: &[&str] = &["6", "12"];

/// Saturator's panel.
pub(in super::super) static SATURATOR: [ParamSpec; 10] = [
    ch("mode", "MODE", MODES, 0.0),
    p("bias", "BIAS", "%", LIN, -100.0, 100.0, 201, 0.0),
    tog("pad", "PAD", 0.0),
    p("tilt", "TILT", "dB", LIN, -20.0, 20.0, 81, 0.0),
    p("lc", "LF CUT", "Hz", crate::model::ParamKind::Log, 20.0, 1000.0, 101, 20.0),
    p("punch", "LF PUNCH", "%", LIN, 0.0, 100.0, 101, 0.0),
    p("hc", "HF CUT", "Hz", crate::model::ParamKind::Log, 1000.0, 24000.0, 101, 24000.0),
    ch("hs", "HF SLOPE", HF_SLOPES, 1.0),
    p("level", "LEVEL", "dB", LIN, -40.0, 10.0, 51, 0.0),
    p("mix", "MIX", "%", LIN, 0.0, 100.0, 101, 100.0),
];

#[derive(Default)]
struct SatFilters {
    low: Biquad,
    high: Biquad,
    lc: Biquad,
    punch: Biquad,
    hc: Biquad,
}

/// Saturator: filters around an oversampled stage.
pub(in super::super) struct Saturator {
    sr: f32,
    mode: usize,
    drive: f32,
    bias: f32,
    out: f32,
    mix: f32,
    pad: f32,
    pad_out: f32,
    os: [Oversampler2; 2],
    dc: [DcBlock; 2],
    filters: [SatFilters; 2],
    tilt_db: f32,
    hc_poles: usize,
}

impl Saturator {
    pub(in super::super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        Self {
            sr,
            mode: 0,
            drive: 2.0,
            bias: 0.0,
            out: 1.0,
            mix: 1.0,
            pad: 1.0,
            pad_out: 1.0,
            os: [Oversampler2::default(); 2],
            dc: [DcBlock::new(sr * 2.0), DcBlock::new(sr * 2.0)],
            filters: [SatFilters::default(), SatFilters::default()],
            tilt_db: 0.0,
            hc_poles: 2,
        }
    }
}

/// The stage, driven by `x` (at 2x).
#[inline]
fn stage(mode: usize, x: f32) -> f32 {
    match mode {
        // Triode: a square term on a soft clip (mostly 2nd harmonic).
        0 => {
            let y = fast_tanh(0.9 * x);
            y - 0.3 * y * y
        }
        // Pentode: symmetric odd-harmonic soft clip.
        1 => fast_tanh(1.15 * x) / 1.15,
        // Germanium: uneven knees and a little crossover softness.
        2 => {
            let a = x.abs();
            let y = 1.0 - (-1.4 * a).exp();
            let thin = (a * 8.0).min(1.0);
            let s = if x < 0.0 { -0.9 } else { 1.0 };
            s * y * (0.82 + 0.18 * thin)
        }
        // Bipolar channel strip: linear up to a firm knee, then hard.
        3 => {
            if x.abs() < 0.7 {
                x
            } else {
                let over = (x.abs() - 0.7) / 0.5;
                x.signum() * (0.7 + 0.3 * fast_tanh(over))
            }
        }
        // Tape: soft and symmetric, compressing evenly.
        _ => {
            let y = fast_tanh(0.95 * x);
            y + 0.12 * (y * y * y - y)
        }
    }
}

impl FxAlgo for Saturator {
    fn set_params(&mut self, p: &Params) {
        let sr = f64::from(self.sr);
        self.mode = p.choice("mode").min(4);
        self.drive = 2.0;
        self.bias = p.f("bias") * 0.005;
        let pad = p.on("pad");
        self.pad = if pad { 10f32.powf(-30.0 / 20.0) } else { 1.0 };
        self.pad_out = if pad { 10f32.powf(30.0 / 20.0) } else { 1.0 };
        self.out = 10f32.powf(p.f("level") * 0.05);
        self.mix = pct(p.f("mix"));
        self.tilt_db = p.f("tilt");
        self.hc_poles = if p.choice("hs") == 0 { 1 } else { 2 };
        let punch = pct(p.f("punch"));
        for f in &mut self.filters {
            f.low.set_coefficients(BiquadCoeffs::low_shelf_1(sr, 1000.0, f64::from(-0.5 * self.tilt_db)));
            f.high.set_coefficients(BiquadCoeffs::high_shelf_1(sr, 1000.0, f64::from(0.5 * self.tilt_db)));
            f.lc.set_coefficients(BiquadCoeffs::high_pass(sr, f64::from(p.f("lc")), 0.707));
            f.punch.set_coefficients(BiquadCoeffs::low_shelf(sr, 90.0, 0.7, f64::from(9.0 * punch)));
            let hc = f64::from(p.f("hc")).min(sr * 0.48);
            if self.hc_poles == 1 {
                f.hc.set_coefficients(BiquadCoeffs::low_pass_1(sr, hc));
            } else {
                f.hc.set_coefficients(BiquadCoeffs::low_pass(sr, hc, 0.707));
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            for (c, x) in [&mut *l, &mut *r].into_iter().enumerate() {
                let dry = *x;
                let f = &mut self.filters[c];
                let mut y = f.lc.tick(dry, 0);
                y = f.punch.tick(y, 0);
                let up = self.os[c].up(y * self.pad);
                let mut driven = [0.0f32; 2];
                for (d, s) in driven.iter_mut().zip(up) {
                    let v = stage(self.mode, s * self.drive + self.bias);
                    *d = self.dc[c].tick(v);
                }
                let mut out = self.os[c].down(driven) / self.drive * self.pad_out;
                let f = &mut self.filters[c];
                out = f.high.tick(f.low.tick(out, 0), 0);
                out = f.hc.tick(out, 0);
                *x = rail(dry + (out * self.out - dry) * self.mix);
            }
        }
    }

    fn reset(&mut self) {
        for o in &mut self.os {
            o.reset();
        }
        for d in &mut self.dc {
            d.reset();
        }
        for f in &mut self.filters {
            f.low.reset();
            f.high.reset();
            f.lc.reset();
            f.punch.reset();
            f.hc.reset();
        }
    }
}

// ---- Amp + Cab ----------------------------------------------------------------------

const AMPS: &[&str] = &["DELUXE", "VX30", "MPLEX", "BDS1", "BASSVT"];
const CABS: &[&str] = &["NONE", "FDR 1X12", "FV 2X10", "VX 2X12", "BASS 8X10B", "BASS 1X15", "CAB 6", "CAB 7", "CAB 8", "CAB 9"];

/// Amp+Cab Distortion's panel.
pub(in super::super) static AMP_CAB: [ParamSpec; 8] = [
    ch("amp", "AMP", AMPS, 0.0),
    ch("cab", "CABINET", CABS, 1.0),
    p("gain", "GAIN", "", LIN, 0.0, 10.0, 101, 4.0),
    p("bass", "BASS", "", LIN, 0.0, 10.0, 101, 5.0),
    p("mid", "MID", "", LIN, 0.0, 10.0, 101, 5.0),
    p("treble", "TREBLE", "", LIN, 0.0, 10.0, 101, 5.0),
    p("thick", "THICKEN", "%", LIN, 0.0, 100.0, 101, 0.0),
    p("out", "OUTPUT", "dB", LIN, -24.0, 12.0, 73, 0.0),
];

/// Cabinet responses (high-pass Hz, low-pass Hz, resonance Hz, resonance dB), in
/// `CABS`'s order after None. Sidecar's reading of each cabinet's size and the names.
const CAB_RESPONSE: [(f64, f64, f64, f64); 9] = [
    (90.0, 4800.0, 2400.0, 4.0),
    (80.0, 5400.0, 3000.0, 5.0),
    (85.0, 5600.0, 2200.0, 3.0),
    (45.0, 3800.0, 1500.0, 3.0),
    (50.0, 3400.0, 900.0, 3.0),
    (70.0, 4200.0, 1800.0, 4.0),
    (100.0, 5200.0, 3400.0, 5.0),
    (75.0, 6000.0, 2800.0, 2.0),
    (60.0, 4600.0, 1200.0, 4.0),
];

fn amp_common(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    set("bass", p.f("bass"));
    set("treb", p.f("treble"));
    set("cab", 0.0);
    p.f("out")
}

fn deluxe(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    set("vol", 1.0 + 0.9 * p.f("gain"));
    set("out", 5.5);
    amp_common(p, set)
}

fn vx30(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    set("vol", p.f("gain"));
    set("mid", p.f("mid"));
    set("out", 5.0);
    amp_common(p, set)
}

fn mplex(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    set("gain", p.f("gain"));
    set("mid", p.f("mid"));
    set("pres", 5.0);
    set("mstr", 5.0);
    set("out", 5.0);
    amp_common(p, set)
}

fn bds1(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    set("pre", p.f("gain"));
    set("buzz", p.f("bass"));
    set("punch", p.f("mid"));
    set("crunch", p.f("treble"));
    set("drive", p.f("gain"));
    set("out", 5.0);
    set("leq", p.f("bass"));
    set("heq", p.f("treble"));
    set("cab", 0.0);
    p.f("out")
}

fn bass_vt(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    set("gain", 0.6 * p.f("gain"));
    set("bass", (p.f("bass") + 2.0).min(10.0));
    set("mid", p.f("mid"));
    set("treb", (p.f("treble") - 1.5).max(0.0));
    set("pres", 3.0);
    set("mstr", 5.0);
    set("out", 5.0);
    set("cab", 0.0);
    p.f("out")
}

/// The amp's choice picks one of the five WING voicings; the cabinet and Thicken are
/// filters after it.
pub(in super::super) struct AmpCab {
    amps: [Box<dyn FxAlgo>; 5],
    active: usize,
    sr: f64,
    thick: [Biquad; 2],
    hp: [Biquad; 2],
    res: [Biquad; 2],
    lp: [Biquad; 2],
    cab_on: bool,
}

impl AmpCab {
    pub(in super::super) fn boxed(ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
        let amps = [
            bridge("DELUXE", None, deluxe, ctx)?,
            bridge("JAZZC", None, vx30, ctx)?,
            bridge("UKROCK", None, mplex, ctx)?,
            bridge("RACKAMP", None, bds1, ctx)?,
            bridge("UKROCK", None, bass_vt, ctx)?,
        ];
        let f = || [Biquad::default(), Biquad::default()];
        Some(Box::new(Self {
            amps,
            active: 0,
            sr: f64::from(ctx.sample_rate),
            thick: f(),
            hp: f(),
            res: f(),
            lp: f(),
            cab_on: false,
        }))
    }
}

impl FxAlgo for AmpCab {
    fn set_params(&mut self, p: &Params) {
        let amp = p.choice("amp").min(4);
        if amp != self.active {
            self.amps[self.active].reset();
            self.active = amp;
        }
        self.amps[amp].set_params(p);
        let sr = self.sr;
        let thick = 9.0 * pct(p.f("thick"));
        for b in &mut self.thick {
            b.set_coefficients(BiquadCoeffs::low_shelf(sr, 220.0, 0.7, f64::from(thick)));
        }
        let cab = p.choice("cab");
        self.cab_on = cab > 0;
        if let Some(&(hp, lp, res, db)) = CAB_RESPONSE.get(cab.wrapping_sub(1)) {
            let top = sr * 0.45;
            for b in &mut self.hp {
                b.set_coefficients(BiquadCoeffs::high_pass(sr, hp, 0.9));
            }
            for b in &mut self.res {
                b.set_coefficients(BiquadCoeffs::peak(sr, res, 1.2, db));
            }
            for b in &mut self.lp {
                b.set_coefficients(BiquadCoeffs::low_pass(sr, lp.min(top), 0.707));
            }
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        self.amps[self.active].process(left, right);
        for (c, buf) in [&mut *left, &mut *right].into_iter().enumerate() {
            for s in buf.iter_mut() {
                let mut y = self.thick[c].tick(*s, 0);
                if self.cab_on {
                    y = self.lp[c].tick(self.res[c].tick(self.hp[c].tick(y, 0), 0), 0);
                }
                *s = y;
            }
        }
    }

    fn reset(&mut self) {
        for a in &mut self.amps {
            a.reset();
        }
        for b in self.thick.iter_mut().chain(self.hp.iter_mut()).chain(self.res.iter_mut()).chain(self.lp.iter_mut()) {
            b.reset();
        }
    }
}
