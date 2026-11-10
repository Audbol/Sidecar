//! Dyn8: four bands of dynamic EQ and four bands of multiband compression in one
//! engine, the dLive's insert processor (Firmware Reference Guide, Processing Screen;
//! the Avantis guide's Dyn8 appendix and its panel pictures, the same design).
//! Clean-room: built from the guides' description, not from any firmware.
//!
//! Stated: 4 + 4 bands; per-band In; External Key Source with Self or External per
//! band, Split (the same key to both sides) or Wide (a true stereo key); Multiband
//! ballistics Peak and RMS (per-band Attack and Release) and Punch, Opto, Slow, Fast;
//! Hard or Soft knee; Link and Rel; crossover slope 6, 18 or 24 dB/oct; per-band
//! threshold, gain, ratio. Dynamic EQ: per-band threshold, gain (the most cut or boost),
//! width, frequency, Above or Below, Std 9 (smooth, frequency-conscious release) or
//! Fast 9. The numbers are the RackExtra Dynamic EQ's and Multiband's (the guides
//! give none of Dyn8's own: ratio 1:1 to 20:1, gain -6 to +18 dB, thresholds -50 to
//! +18 dBu, frequency 40 Hz to 18 kHz, width 2.25 to 1/9 octave); the panel picture's
//! starting points are taken (Self, Split, 2.4:1, 0.86 octave, thresholds 0 and -16
//! dB, Slope 24, Hard knee, Punch).
//!
//! Sidecar's reading: the dynamic EQ ahead of the multiband (the block diagram lists
//! them in that order); the dynamic EQ's gain range, ratio and knee; each ballistic's
//! times; Link as every band taking the first's controls and Rel as the first band's
//! move carrying the others' thresholds and gains; Self as the band's own filtered
//! signal; Split as the louder side's level driving both sides; a band's external key
//! band-limited like the band's own. A&H state 4 samples of latency; this one has none.
//!
//! The 64 instances and the one External Key Source per engine are the console's;
//! here a key arrives through [`FxAlgo::process_keyed`]. Two pairs of controls (Above
//! or Below with Std or Fast; Self or External with Split or Wide) each share one
//! choice parameter per band, to stay within the FX block's 64 parameters.

use super::*;

const FIRST: usize = 0;

/// A band's key: Self or External, Split or Wide.
const KEY: &[&str] = &["SELF SPLIT", "SELF WIDE", "EXT SPLIT", "EXT WIDE"];
/// A dynamic EQ band's Above or Below and its ballistics.
const EQ_MODE: &[&str] = &["ABOVE STD 9", "ABOVE FAST 9", "BELOW STD 9", "BELOW FAST 9"];

macro_rules! mb {
    ($n:literal, $b:literal) => {
        [
            p(concat!("mt_", $n), concat!($b, " THRESHOLD"), "dB", LIN, -50.0, 18.0, 137, 0.0),
            p(concat!("mg_", $n), concat!($b, " GAIN"), "dB", LIN, -6.0, 18.0, 49, 0.0),
            p(concat!("mr_", $n), concat!($b, " RATIO"), "", ParamKind::Log, 1.0, 20.0, 101, 2.4),
            tog(concat!("mi_", $n), concat!($b, " IN"), 1.0),
            p(concat!("ma_", $n), concat!($b, " ATTACK"), "ms", LOG, 0.3, 200.0, 101, 10.0),
            p(concat!("ml_", $n), concat!($b, " RELEASE"), "ms", LOG, 20.0, 2000.0, 101, 150.0),
            ch(concat!("mk_", $n), concat!($b, " KEY"), KEY, 0.0),
        ]
    };
}

macro_rules! eq {
    ($n:literal, $b:literal, $f:expr, $w:expr) => {
        [
            tog(concat!("ei_", $n), concat!($b, " EQ IN"), 1.0),
            p(concat!("ef_", $n), concat!($b, " FREQ"), "Hz", LOG, 40.0, 18000.0, 201, $f),
            p(concat!("eg_", $n), concat!($b, " EQ GAIN"), "dB", LIN, -15.0, 15.0, 61, 0.0),
            p(concat!("ew_", $n), concat!($b, " WIDTH"), "oct", LOG, NARROWEST, $w, 101, 0.86),
            p(concat!("et_", $n), concat!($b, " EQ THRESHOLD"), "dB", LIN, -50.0, 18.0, 137, -16.0),
            ch(concat!("em_", $n), concat!($b, " EQ MODE"), EQ_MODE, 0.0),
            ch(concat!("ek_", $n), concat!($b, " EQ KEY"), KEY, 0.0),
        ]
    };
}

const COMMON: [ParamSpec; 7] = [
    p("xo_1", "CROSSOVER LF", "Hz", LOG, 40.0, 16000.0, 201, 150.0),
    p("xo_2", "CROSSOVER LM", "Hz", LOG, 40.0, 16000.0, 201, 800.0),
    p("xo_3", "CROSSOVER HM", "Hz", LOG, 40.0, 16000.0, 201, 4500.0),
    ch("slope", "SLOPE", MB_SLOPES, 2.0),
    ch("knee", "KNEE", MB_KNEE, 0.0),
    ch("ball", "BALLISTICS", MB_BALLISTICS, 2.0),
    ch("link", "LINK", MB_LINK, 0.0),
];
const M1: [ParamSpec; 7] = mb!("1", "LF");
const M2: [ParamSpec; 7] = mb!("2", "LM");
const M3: [ParamSpec; 7] = mb!("3", "HM");
const M4: [ParamSpec; 7] = mb!("4", "HF");
const E1: [ParamSpec; 7] = eq!("1", "LF", 100.0, WIDEST_END);
const E2: [ParamSpec; 7] = eq!("2", "LM", 500.0, WIDEST_MID);
const E3: [ParamSpec; 7] = eq!("3", "HM", 2000.0, WIDEST_MID);
const E4: [ParamSpec; 7] = eq!("4", "HF", 8000.0, WIDEST_END);

/// The whole panel: the multiband's page, then the dynamic EQ's.
pub(in super::super) static DYN8: [ParamSpec; 63] = join(&[&COMMON, &M1, &M2, &M3, &M4, &E1, &E2, &E3, &E4]);

const fn m_ids(n: usize) -> [&'static str; 7] {
    match n {
        0 => ["mt_1", "mg_1", "mr_1", "mi_1", "ma_1", "ml_1", "mk_1"],
        1 => ["mt_2", "mg_2", "mr_2", "mi_2", "ma_2", "ml_2", "mk_2"],
        2 => ["mt_3", "mg_3", "mr_3", "mi_3", "ma_3", "ml_3", "mk_3"],
        _ => ["mt_4", "mg_4", "mr_4", "mi_4", "ma_4", "ml_4", "mk_4"],
    }
}

const fn e_ids(n: usize) -> [&'static str; 7] {
    match n {
        0 => ["ei_1", "ef_1", "eg_1", "ew_1", "et_1", "em_1", "ek_1"],
        1 => ["ei_2", "ef_2", "eg_2", "ew_2", "et_2", "em_2", "ek_2"],
        2 => ["ei_3", "ef_3", "eg_3", "ew_3", "et_3", "em_3", "ek_3"],
        _ => ["ei_4", "ef_4", "eg_4", "ew_4", "et_4", "em_4", "ek_4"],
    }
}

/// A dynamic EQ band with a side each. `DeqBand`'s `det` filters the external key.
struct EqBand {
    core: DeqBand,
    on: bool,
    ext: bool,
    wide: bool,
    env: [f32; 2],
    gain: [f32; 2],
}

/// A multiband band's state on one side.
#[derive(Clone, Copy, Default)]
struct MbSide {
    env: f32,
    gr: f32,
}

#[derive(Clone, Copy, Default)]
struct DynBand {
    b: MbBand,
    ext: bool,
    sides: [MbSide; 2],
}

/// Dyn8. See the module comment.
pub(in super::super) struct Dyn8 {
    sr: f32,
    eq: [EqBand; 4],
    a: f32,
    clock: usize,
    primed: bool,
    env_rel: f32,
    splits: [Split; 3],
    comp: [Split; 3],
    /// The external key through the same crossovers, used only when asked for.
    key_splits: [Split; 3],
    bands: [DynBand; 4],
    soft: bool,
    rms: bool,
    rms_coef: f32,
    any_ext: bool,
    meter: f32,
}

impl Dyn8 {
    pub(in super::super) fn new(ctx: FxContext) -> Self {
        let sr = ctx.sample_rate;
        let band = |is_end: bool, is_low: bool| EqBand {
            core: DeqBand::new(is_end, is_low),
            on: true,
            ext: false,
            wide: false,
            env: [0.0; 2],
            gain: [0.0; 2],
        };
        Self {
            sr,
            eq: [band(true, true), band(false, false), band(false, false), band(true, false)],
            a: rate(30.0, sr),
            clock: 0,
            primed: false,
            env_rel: coef(15.0, sr),
            splits: [Split::default(); 3],
            comp: [Split::default(); 3],
            key_splits: [Split::default(); 3],
            bands: [DynBand::default(); 4],
            soft: false,
            rms: false,
            rms_coef: coef(20.0, sr),
            any_ext: false,
            meter: 0.0,
        }
    }

    fn run(&mut self, left: &mut [f32], right: &mut [f32], key: Option<(&[f32], &[f32])>) {
        let mut lowest = 0.0f32;
        let key_on = key.is_some() && self.any_ext;
        for i in 0..left.len().min(right.len()) {
            if self.clock == 0 {
                self.clock = CTL;
                for b in &mut self.eq {
                    if b.core.ln_f.step(self.a) | b.core.ln_w.step(self.a) {
                        b.core.design(self.sr);
                    }
                }
            }
            self.clock -= 1;
            let (kl, kr) = key.map_or((0.0, 0.0), |(a, b)| (a[i], b[i]));

            // ---- dynamic EQ, in series ----
            let mut out = [left[i], right[i]];
            for b in &mut self.eq {
                if !b.on {
                    continue;
                }
                let fl = b.core.filter.tick(out[0], 0);
                let fr = b.core.filter.tick(out[1], 1);
                let (dl, dr) = if key_on && b.ext { (b.core.det.tick(kl, 0), b.core.det.tick(kr, 1)) } else { (fl, fr) };
                let m = dl.abs().max(dr.abs());
                let levels = if b.wide { [dl.abs(), dr.abs()] } else { [m, m] };
                for (c, a) in levels.into_iter().enumerate() {
                    b.env[c] = if a > b.env[c] { a } else { b.env[c] + (a - b.env[c]) * self.env_rel };
                    let want = if b.core.target_db == 0.0 { 0.0 } else { b.core.gain_for(level_db(b.env[c])) };
                    let (att, rel) = (b.core.att, b.core.rel);
                    b.gain[c] += (want - b.gain[c]) * if want.abs() > b.gain[c].abs() { att } else { rel };
                    lowest = lowest.min(b.gain[c]);
                }
                for (c, (o, f)) in out.iter_mut().zip([fl, fr]).enumerate() {
                    if b.core.shelf {
                        let (k, kb) = shelf_12(b.gain[c]);
                        let shape = b.core.shape.tick(*o, c);
                        *o += k * f + kb * shape;
                    } else {
                        *o += (db_gain(b.gain[c]) - 1.0) * f;
                    }
                }
            }

            // ---- multiband ----
            let mut band = [[0.0f32; 2]; 4];
            let mut kband = [[0.0f32; 2]; 4];
            for (c, x) in out.into_iter().enumerate() {
                let (b0, rest) = self.splits[0].tick(c, x);
                let (b1, rest) = self.splits[1].tick(c, rest);
                let (b2, b3) = self.splits[2].tick(c, rest);
                // All-pass compensation of the lower bands, so the four sum flat.
                let (a, b) = self.comp[0].tick(c, b0);
                let (a, b) = self.comp[1].tick(c, a + b);
                band[0][c] = a + b;
                let (a, b) = self.comp[2].tick(c, b1);
                band[1][c] = a + b;
                band[2][c] = b2;
                band[3][c] = b3;
                if key_on {
                    let k = if c == 0 { kl } else { kr };
                    let (k0, krest) = self.key_splits[0].tick(c, k);
                    let (k1, krest) = self.key_splits[1].tick(c, krest);
                    let (k2, k3) = self.key_splits[2].tick(c, krest);
                    kband[0][c] = k0;
                    kband[1][c] = k1;
                    kband[2][c] = k2;
                    kband[3][c] = k3;
                }
            }
            let (mut ol, mut or) = (0.0, 0.0);
            for (d, (s, k)) in self.bands.iter_mut().zip(band.into_iter().zip(kband)) {
                let DynBand { b, ext, sides } = d;
                if !b.on {
                    ol += s[0];
                    or += s[1];
                    continue;
                }
                let of = if key_on && *ext { k } else { s };
                let m = of[0].abs().max(of[1].abs());
                let levels = if b.wide { [of[0].abs(), of[1].abs()] } else { [m, m] };
                for (side, a) in sides.iter_mut().zip(levels) {
                    let lvl = if self.rms {
                        side.env += (a * a - side.env) * self.rms_coef;
                        side.env.sqrt()
                    } else {
                        side.env = if a > side.env { a } else { side.env + (a - side.env) * b.rel };
                        side.env
                    };
                    let target = knee_gr(level_db(lvl) - b.thr, b.slope, self.soft);
                    side.gr += (target - side.gr) * if target > side.gr { b.att } else { b.rel };
                    lowest = lowest.min(-side.gr);
                }
                ol += db_gain(b.gain - sides[0].gr) * s[0];
                or += db_gain(b.gain - sides[1].gr) * s[1];
            }
            left[i] = rail(ol);
            right[i] = rail(or);
        }
        self.meter = lowest.min(0.0);
    }
}

impl FxAlgo for Dyn8 {
    fn set_params(&mut self, p: &Params) {
        let sr = self.sr;
        // ---- dynamic EQ ----
        for (i, b) in self.eq.iter_mut().enumerate() {
            let ids = e_ids(i);
            b.on = p.on(ids[0]);
            let (f, w) = (p.f(ids[1]), p.f(ids[3]));
            let widest = if b.core.is_end { WIDEST_END } else { WIDEST_MID };
            let shelf = b.core.is_end && w >= widest * 0.995;
            let moved = shelf != b.core.shelf;
            b.core.ln_f.set(f.ln());
            b.core.ln_w.set(w.ln());
            b.core.shelf = shelf;
            b.core.thr = p.f(ids[4]) - DBU_OFFSET;
            b.core.target_db = p.f(ids[2]);
            let mode = p.choice(ids[5]).min(3);
            b.core.above = mode < 2;
            let (att, rel) = if mode % 2 == 1 { (1.0, 40.0) } else { (5.0, (120.0 * (1000.0 / f).powf(0.3)).clamp(60.0, 500.0)) };
            b.core.att = coef(att, sr);
            b.core.rel = coef(rel, sr);
            let key = p.choice(ids[6]).min(3);
            b.ext = key >= 2;
            b.wide = key % 2 == 1;
            if !b.core.designed || !self.primed || moved {
                b.core.ln_f.snap();
                b.core.ln_w.snap();
                b.core.design(sr);
                b.core.designed = true;
            }
        }

        // ---- multiband ----
        let poles = [1, 3, 4][p.choice("slope").min(2)];
        let mut f = [p.f("xo_1"), p.f("xo_2"), p.f("xo_3")];
        f[1] = f[1].max(f[0] * 1.2);
        f[2] = f[2].max(f[1] * 1.2);
        for (s, hz) in self.splits.iter_mut().zip(f) {
            s.set(hz.min(sr * 0.45), poles, sr);
        }
        for (s, hz) in self.key_splits.iter_mut().zip(f) {
            s.set(hz.min(sr * 0.45), poles, sr);
        }
        self.comp[0].set(f[1].min(sr * 0.45), poles, sr);
        self.comp[1].set(f[2].min(sr * 0.45), poles, sr);
        self.comp[2].set(f[2].min(sr * 0.45), poles, sr);
        self.soft = p.choice("knee") == 1;
        let mode = p.choice("ball").min(5);
        self.rms = mode == 1 || (mode >= 2 && auto_ballistics(mode).2);
        let link = p.choice("link");
        let first = m_ids(FIRST);
        let (t0, g0, r0) = (p.f(first[0]), p.f(first[1]), p.f(first[2]));
        for (i, d) in self.bands.iter_mut().enumerate() {
            let ids = m_ids(i);
            let (thr, gain, ratio) = match link {
                1 => (t0, g0, r0),
                // Rel: band 1's move carries each band's own. Its value is its own
                // starting point here, so band 1's setting is the distance moved.
                2 if i > FIRST => (p.f(ids[0]) + t0, p.f(ids[1]) + g0, p.f(ids[2])),
                _ => (p.f(ids[0]), p.f(ids[1]), p.f(ids[2])),
            };
            let b = &mut d.b;
            b.thr = thr.clamp(-50.0, 18.0) - DBU_OFFSET;
            b.gain = gain.clamp(-6.0, 18.0);
            b.slope = 1.0 - 1.0 / ratio.max(1.0);
            b.on = p.on(ids[3]);
            let (att, rel) = if mode < 2 {
                let times = if link == 1 { first } else { ids };
                (p.f(times[4]), p.f(times[5]))
            } else {
                (auto_ballistics(mode).0, auto_ballistics(mode).1)
            };
            b.att = coef(att, sr);
            b.rel = coef(rel, sr);
            let key = p.choice(ids[6]).min(3);
            d.ext = key >= 2;
            b.wide = key % 2 == 1;
        }
        self.any_ext = self.eq.iter().any(|b| b.ext) || self.bands.iter().any(|d| d.ext);
        self.primed = true;
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        self.run(left, right, None);
    }

    fn process_keyed(&mut self, left: &mut [f32], right: &mut [f32], key: Option<(&[f32], &[f32])>) {
        self.run(left, right, key);
    }

    fn reset(&mut self) {
        for b in &mut self.eq {
            b.core.reset();
            b.env = [0.0; 2];
            b.gain = [0.0; 2];
        }
        for s in self.splits.iter_mut().chain(self.comp.iter_mut()).chain(self.key_splits.iter_mut()) {
            s.reset();
        }
        for d in &mut self.bands {
            d.sides = [MbSide::default(); 2];
        }
        self.meter = 0.0;
    }

    fn meter(&self) -> f32 {
        self.meter
    }

    fn band_gain(&self, band: usize) -> f32 {
        self.eq.get(band).map_or(0.0, |b| if b.gain[0].abs() > b.gain[1].abs() { b.gain[0] } else { b.gain[1] })
    }

    /// Readouts 0-3: the multiband bands' gain reduction (dB, <= 0, the deeper
    /// side); 4-7: the dynamic EQ bands' gain (dB, signed, the larger side).
    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        for (o, d) in out[..4].iter_mut().zip(&self.bands) {
            *o = -d.sides[0].gr.max(d.sides[1].gr);
        }
        for (b, o) in (0..4).zip(&mut out[4..8]) {
            *o = self.band_gain(b);
        }
    }
}
