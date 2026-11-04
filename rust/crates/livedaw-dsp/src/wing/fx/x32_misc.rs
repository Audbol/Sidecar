//! The X32's insert effects with no WING counterpart (Phase 4 step 9g): Tube
//! Stage (DRV, DRV2), Stereo Imager (IMG), Edison EX1 (EDI) and Sound Maxer
//! (SON). Each is a small design of Sidecar's own from the controls' names
//! and ranges, to be measured on an X32:
//!
//! * **Tube Stage**: LO CUT, then DRIVE (0-100: up to +30 dB into the stage)
//!   through a soft saturator whose EVEN EAR adds asymmetry (even harmonics)
//!   and ODD EAR symmetric clipping (odd harmonics), level-compensated; then
//!   the LO / HI shelves (+-12 dB at LO / HI FREQ), HI CUT and GAIN. DRV2 has
//!   a stage per side (A left, B right).
//! * **Stereo Imager**: the mid and side signals; BALANCE between them
//!   (-100 mid only, +100 side only), MONO PAN places the mid, STEREO PAN the
//!   side, the SHV shelf (GAIN 0-12 dB at SHV FREQ, SHV Q its slope) lifts
//!   the side's lows for width; OUT GAIN.
//! * **Edison EX1**: STEREO INPUT / OUTPUT as L/R or M/S; ST SPREAD (+-50)
//!   scales the side signal (-50 mono, +50 twice as wide), LMF SPREAD the side
//!   below 500 Hz on top; BALANCE the left / right level; CENTER DISTANCE
//!   (+-50) the mid's level (+50 6 dB down, -50 6 dB up); OUT GAIN. ACTIVE off
//!   passes the signal.
//! * **Sound Maxer** (per side): LO CONTOUR (0-10) a low shelf at 50 Hz and
//!   PROCESS (0-10) a high shelf at 5 kHz, each up to +12 dB, then OUT GAIN.
//!   ACTIVE off passes the side.

use super::blocks::fast_tanh;
use super::{FxAlgo, FxContext};
use crate::biquad::{Biquad, BiquadCoeffs};
use crate::model::Params;
use crate::util::db_to_gain;
use std::f64::consts::FRAC_1_SQRT_2;

/// The models implemented here.
pub(super) const TOKENS: &[&str] = &["X32DRV", "X32DRV2", "X32IMG", "X32EDI", "X32SON"];

pub(super) fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    if !TOKENS.contains(&token) {
        return None;
    }
    let sr = ctx.sample_rate as f64;
    Some(match token {
        "X32DRV" => Box::new(TubeStage::new(false, sr)),
        "X32DRV2" => Box::new(TubeStage::new(true, sr)),
        "X32IMG" => Box::new(Imager::new(sr)),
        "X32EDI" => Box::new(Edison::new(sr)),
        "X32SON" => Box::new(SoundMaxer::new(sr)),
        _ => return None,
    })
}

/// Control `base` of the stereo model, or of side `sfx` ("base_a").
fn f(p: &Params, base: &str, sfx: Option<&str>) -> f32 {
    match sfx {
        Some(s) => p.f(&super::x32_voices::format_id(base, s)),
        None => p.f(base),
    }
}

/// Gains that ramp to new targets over a block.
#[derive(Clone, Copy)]
struct Ramp<const N: usize> {
    last: [f32; N],
    target: [f32; N],
}

impl<const N: usize> Ramp<N> {
    fn new(v: [f32; N]) -> Self {
        Self { last: v, target: v }
    }

    #[inline]
    fn at(&self, i: usize, n: usize) -> [f32; N] {
        let t = (i + 1) as f32 / n.max(1) as f32;
        std::array::from_fn(|k| self.last[k] + (self.target[k] - self.last[k]) * t)
    }

    fn done(&mut self) {
        self.last = self.target;
    }
}

// ---- Tube Stage ---------------------------------------------------------------------

#[derive(Default)]
struct Stage {
    /// LO CUT, LO shelf, HI shelf, HI CUT (two channels each).
    filters: [Biquad; 4],
    dc: Biquad,
}

struct TubeStage {
    sr: f64,
    dual: bool,
    stages: [Stage; 2],
    /// Per side: drive in, compensation, even, odd, gain out.
    ramp: [Ramp<5>; 2],
}

impl TubeStage {
    fn new(dual: bool, sr: f64) -> Self {
        let mut stages: [Stage; 2] = Default::default();
        for s in &mut stages {
            s.dc.set_coefficients(BiquadCoeffs::high_pass_1(sr, 8.0));
        }
        Self { sr, dual, stages, ramp: [Ramp::new([1.0, 1.0, 0.0, 0.0, 1.0]); 2] }
    }
}

impl FxAlgo for TubeStage {
    fn set_params(&mut self, p: &Params) {
        for side in 0..if self.dual { 2 } else { 1 } {
            let sfx = self.dual.then_some(["a", "b"][side]);
            let v = |id: &str| f(p, id, sfx) as f64;
            let st = &mut self.stages[side];
            let sr = self.sr;
            st.filters[0].set_coefficients(BiquadCoeffs::high_pass(sr, v("lo_cut"), FRAC_1_SQRT_2));
            st.filters[1].set_coefficients(BiquadCoeffs::low_shelf(sr, v("lo_freq"), FRAC_1_SQRT_2, v("lo_gain")));
            st.filters[2].set_coefficients(BiquadCoeffs::high_shelf(sr, v("hi_freq"), FRAC_1_SQRT_2, v("hi_gain")));
            st.filters[3].set_coefficients(BiquadCoeffs::low_pass(sr, v("hi_cut").min(0.45 * sr), FRAC_1_SQRT_2));
            let drive = db_to_gain(0.3 * f(p, "drive", sfx));
            // The level the saturator leaves a moderate signal at, about unity.
            let comp = 1.0 / drive.sqrt();
            let first = self.ramp[side].last == [1.0, 1.0, 0.0, 0.0, 1.0];
            self.ramp[side].target =
                [drive, comp, f(p, "even_ear", sfx) / 50.0, f(p, "odd_ear", sfx) / 50.0, db_to_gain(f(p, "gain", sfx))];
            if first {
                self.ramp[side].done();
            }
        }
        if !self.dual {
            self.ramp[1] = self.ramp[0];
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len();
        for (ch, x) in [left, right].into_iter().enumerate() {
            // The stereo model: one stage's settings, each channel its filter state.
            let (side, fch) = if self.dual { (ch, 0) } else { (0, ch) };
            let ramp = self.ramp[ch.min(1)];
            let st = &mut self.stages[side];
            for (i, v) in x.iter_mut().enumerate() {
                let [drive, comp, even, odd, gain] = ramp.at(i, n);
                let u = drive * st.filters[0].tick(*v, fch);
                let t = fast_tanh(u);
                let s = u + 0.5 * even * t * t;
                let y = (1.0 - odd) * fast_tanh(0.5 * s) * 2.0 + odd * fast_tanh(s);
                let y = st.dc.tick(comp * y, fch);
                let y = st.filters[1].tick(y, fch);
                let y = st.filters[2].tick(y, fch);
                let y = st.filters[3].tick(y, fch);
                *v = gain * y;
            }
        }
        for r in &mut self.ramp {
            r.done();
        }
    }

    fn reset(&mut self) {
        for st in &mut self.stages {
            st.filters.iter_mut().for_each(Biquad::reset);
            st.dc.reset();
        }
    }
}

// ---- Stereo Imager ------------------------------------------------------------------

struct Imager {
    sr: f64,
    shelf: Biquad,
    /// mid -> L, mid -> R, side -> L, side -> R, out.
    ramp: Ramp<5>,
    primed: bool,
}

impl Imager {
    fn new(sr: f64) -> Self {
        Self { sr, shelf: Biquad::default(), ramp: Ramp::new([1.0, 1.0, 1.0, 1.0, 1.0]), primed: false }
    }
}

/// A balance (-1..1) as the two levels: each at full until the other side.
fn balance(b: f32) -> (f32, f32) {
    let b = b.clamp(-1.0, 1.0);
    ((1.0 - b).min(1.0), (1.0 + b).min(1.0))
}

impl FxAlgo for Imager {
    fn set_params(&mut self, p: &Params) {
        let (gm, gs) = balance(p.f("balance") * 0.01);
        let (ml, mr) = balance(p.f("mono_pan") * 0.01);
        let (sl, sr) = balance(p.f("stereo_pan") * 0.01);
        self.ramp.target = [gm * ml, gm * mr, gs * sl, gs * sr, db_to_gain(p.f("out_gain"))];
        let q = (p.f("shv_q") as f64 / 10.0 * 2.0).clamp(0.3, 2.0);
        self.shelf.set_coefficients(BiquadCoeffs::low_shelf(self.sr, p.f("shv_freq") as f64, q, p.f("shv_gain") as f64));
        if !self.primed {
            self.primed = true;
            self.ramp.done();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len();
        for (i, (l, r)) in left.iter_mut().zip(right.iter_mut()).enumerate() {
            let [ml, mr, sl, sr, out] = self.ramp.at(i, n);
            let m = 0.5 * (*l + *r);
            let s = self.shelf.tick(0.5 * (*l - *r), 0);
            *l = out * (ml * m + sl * s);
            *r = out * (mr * m - sr * s);
        }
        self.ramp.done();
    }

    fn reset(&mut self) {
        self.shelf.reset();
    }
}

// ---- Edison EX1 ---------------------------------------------------------------------

struct Edison {
    lmf: Biquad,
    /// LMF SPREAD: the low side's extra gain.
    lmf_amount: f32,
    ms_in: bool,
    ms_out: bool,
    /// on, side, mid, left, right, out.
    ramp: Ramp<6>,
    primed: bool,
}

impl Edison {
    fn new(sr: f64) -> Self {
        let mut lmf = Biquad::default();
        lmf.set_coefficients(BiquadCoeffs::low_pass(sr, 500.0, FRAC_1_SQRT_2));
        Self { lmf, lmf_amount: 0.0, ms_in: false, ms_out: false, ramp: Ramp::new([1.0, 1.0, 1.0, 1.0, 1.0, 1.0]), primed: false }
    }
}

impl FxAlgo for Edison {
    fn set_params(&mut self, p: &Params) {
        let on = if p.on("active") { 1.0 } else { 0.0 };
        let side = 1.0 + p.f("st_spread") / 50.0;
        let lmf = p.f("lmf_spread") / 50.0;
        let mid = db_to_gain(-6.0 * p.f("center_distance") / 50.0);
        let (bl, br) = balance(p.f("balance") / 50.0);
        self.lmf_amount = lmf;
        self.ramp.target = [on, side, mid, bl, br, db_to_gain(p.f("out_gain"))];
        self.ms_in = p.choice("stereo_input") == 1;
        self.ms_out = p.choice("stereo_output") == 1;
        if !self.primed {
            self.primed = true;
            self.ramp.done();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len();
        for (i, (l, r)) in left.iter_mut().zip(right.iter_mut()).enumerate() {
            let [on, side, mid, bl, br, out] = self.ramp.at(i, n);
            let (dl, dr) = (*l, *r);
            let (m, s) = if self.ms_in { (dl, dr) } else { (0.5 * (dl + dr), 0.5 * (dl - dr)) };
            let s = side * s + self.lmf_amount * self.lmf.tick(s, 0);
            let m = mid * m;
            let (yl, yr) = if self.ms_out { (m, s) } else { (m + s, m - s) };
            let (yl, yr) = (out * bl * yl, out * br * yr);
            *l = dl + (yl - dl) * on;
            *r = dr + (yr - dr) * on;
        }
        self.ramp.done();
    }

    fn reset(&mut self) {
        self.lmf.reset();
    }
}

// ---- Sound Maxer --------------------------------------------------------------------

struct SoundMaxer {
    sr: f64,
    /// Per side: LO CONTOUR and PROCESS shelves.
    shelves: [[Biquad; 2]; 2],
    /// Per side: on, out.
    ramp: [Ramp<2>; 2],
    primed: bool,
}

impl SoundMaxer {
    fn new(sr: f64) -> Self {
        Self { sr, shelves: Default::default(), ramp: [Ramp::new([1.0, 1.0]); 2], primed: false }
    }
}

impl FxAlgo for SoundMaxer {
    fn set_params(&mut self, p: &Params) {
        for side in 0..2 {
            let sfx = Some(["a", "b"][side]);
            let lo = 1.2 * f(p, "lo_contour", sfx) as f64;
            let hi = 1.2 * f(p, "process", sfx) as f64;
            self.shelves[side][0].set_coefficients(BiquadCoeffs::low_shelf(self.sr, 50.0, FRAC_1_SQRT_2, lo));
            self.shelves[side][1].set_coefficients(BiquadCoeffs::high_shelf(self.sr, 5000.0, FRAC_1_SQRT_2, hi));
            let on = if f(p, "active", sfx) >= 0.5 { 1.0 } else { 0.0 };
            self.ramp[side].target = [on, db_to_gain(f(p, "out_gain", sfx))];
        }
        if !self.primed {
            self.primed = true;
            self.ramp.iter_mut().for_each(Ramp::done);
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len();
        for (side, x) in [left, right].into_iter().enumerate() {
            let ramp = self.ramp[side];
            let [lo, hi] = &mut self.shelves[side];
            for (i, v) in x.iter_mut().enumerate() {
                let [on, out] = ramp.at(i, n);
                let y = out * hi.tick(lo.tick(*v, 0), 0);
                *v += (y - *v) * on;
            }
            self.ramp[side].done();
        }
    }

    fn reset(&mut self) {
        self.shelves.iter_mut().flatten().for_each(Biquad::reset);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wing::MAX_FX_PARAMS;

    #[test]
    fn devices() {
        for token in TOKENS {
            crate::wing::fx::tests::check_device(token);
        }
    }

    /// DC level (left, right) through `token` with `edit`, inputs `l` / `r`.
    fn dc(token: &str, edit: &[(&str, f32)], l: f32, r: f32) -> (f32, f32) {
        let x32 = crate::x32::FX_MODELS;
        let spec = &x32[crate::model::model_index(x32, token).unwrap()];
        let mut values = [0.0; MAX_FX_PARAMS];
        spec.defaults_into(&mut values);
        for (id, v) in edit {
            values[spec.param_index(id).unwrap()] = *v;
        }
        let mut dev = create(token, FxContext { sample_rate: 48_000.0, max_block: 256 }).unwrap();
        dev.set_params(&Params::new(spec, &values));
        let (mut bl, mut br) = (vec![l; 256], vec![r; 256]);
        for _ in 0..50 {
            bl.fill(l);
            br.fill(r);
            dev.process(&mut bl, &mut br);
        }
        (bl[255], br[255])
    }

    #[test]
    fn the_stereo_tools_move_mid_and_side() {
        let flat = [("shv_gain", 0.0)];
        // The imager: BALANCE -100 keeps the mid only (a hard-left input lands centred).
        let (l, r) = dc("X32IMG", &[&flat[..], &[("balance", -100.0)]].concat(), 1.0, 0.0);
        assert!((l - 0.5).abs() < 1e-3 && (r - 0.5).abs() < 1e-3, "{l} {r}");
        // MONO PAN hard right puts the mid right.
        let (l, r) = dc("X32IMG", &[&flat[..], &[("balance", -100.0), ("mono_pan", 100.0)]].concat(), 0.5, 0.5);
        assert!(l.abs() < 1e-3 && (r - 0.5).abs() < 1e-3, "{l} {r}");
        // Edison: ST SPREAD -50 is mono; ACTIVE off passes.
        let (l, r) = dc("X32EDI", &[("st_spread", -50.0)], 1.0, 0.0);
        assert!((l - 0.5).abs() < 1e-3 && (r - 0.5).abs() < 1e-3, "{l} {r}");
        let (l, r) = dc("X32EDI", &[("st_spread", -50.0), ("active", 0.0)], 1.0, 0.0);
        assert!((l - 1.0).abs() < 1e-6 && r.abs() < 1e-6);
        // Sound Maxer: B off passes the right side; A's OUT GAIN -6 dB halves the left at DC
        // (its shelves don't reach DC... the low one does: none set).
        let (l, r) =
            dc("X32SON", &[("active_b", 0.0), ("lo_contour_a", 0.0), ("process_a", 0.0), ("out_gain_a", -6.0206)], 0.5, 0.5);
        assert!((r - 0.5).abs() < 1e-6 && (l - 0.25).abs() < 1e-3, "{l} {r}");
    }

    #[test]
    fn the_tube_stage_saturates_more_with_drive() {
        // A loud sine's crest factor falls as DRIVE rises (it clips).
        let crest = |drive: f32| {
            let x32 = crate::x32::FX_MODELS;
            let spec = &x32[crate::model::model_index(x32, "X32DRV").unwrap()];
            let mut values = [0.0; MAX_FX_PARAMS];
            spec.defaults_into(&mut values);
            values[spec.param_index("drive").unwrap()] = drive;
            let mut dev = create("X32DRV", FxContext { sample_rate: 48_000.0, max_block: 256 }).unwrap();
            dev.set_params(&Params::new(spec, &values));
            let (mut peak, mut sum, mut count) = (0.0f32, 0.0f32, 0);
            for b in 0..100 {
                let mut l: Vec<f32> =
                    (0..256).map(|i| 0.5 * (std::f32::consts::TAU * 1000.0 * (b * 256 + i) as f32 / 48_000.0).sin()).collect();
                let mut r = l.clone();
                dev.process(&mut l, &mut r);
                if b > 50 {
                    for x in l {
                        peak = peak.max(x.abs());
                        sum += x * x;
                        count += 1;
                    }
                }
            }
            peak / (sum / count as f32).sqrt()
        };
        let (soft, hard) = (crest(0.0), crest(100.0));
        assert!(hard < soft - 0.1, "crest {soft} at 0, {hard} at 100");
    }
}
