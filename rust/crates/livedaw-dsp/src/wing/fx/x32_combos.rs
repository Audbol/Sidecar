//! The X32's combination effects (Phase 4 step 9e): Delay / Chamber, Chorus /
//! Chamber, Flanger / Chamber, Delay / Chorus and Delay / Flanger run two of
//! the WING's devices side by side ([`Pair`]): both take the input, BALANCE
//! sets their levels (-100 the first alone, +100 the second, 0 both at full)
//! and MIX the wet sum against the dry input. Modulation Delay ([`ModDelay`])
//! runs a delay into a chorus, then a reverb (ambience, club = room, or hall)
//! after them (SER) or beside them (PAR), BALANCE between the two paths.
//!
//! The structure is Sidecar's reading of the X32's controls, to be measured
//! on an X32: the delays' PATTERN is a note fraction of TIME, its X variants
//! crossing the repeats from side to side (as does CROSS FEED from 50 up);
//! the chamber's DECAY and SIZE take the WING Chamber's ranges.

use super::{FxAlgo, FxContext};
use crate::model::{ModelSpec, Params};
use crate::wing::MAX_FX_PARAMS;

/// The combination models.
pub(super) const TOKENS: &[&str] = &["X32D/RV", "X32CR/R", "X32FL/R", "X32D/CR", "X32D/FL", "X32MODD"];

pub(super) fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    if !TOKENS.contains(&token) {
        return None;
    }
    let pair = |a: &str, b: &str, map: PairMap| Pair::boxed(a, b, map, ctx);
    match token {
        "X32D/RV" => pair("ST-DL", "CHAMBER", delay_chamber),
        "X32CR/R" => pair("CHORUS", "CHAMBER", chorus_chamber),
        "X32FL/R" => pair("FLANGER", "CHAMBER", flanger_chamber),
        "X32D/CR" => pair("ST-DL", "CHORUS", delay_chorus),
        "X32D/FL" => pair("ST-DL", "FLANGER", delay_flanger),
        "X32MODD" => ModDelay::boxed(ctx),
        _ => None,
    }
}

/// One WING device and its control values.
struct Stage {
    dev: Box<dyn FxAlgo>,
    spec: &'static ModelSpec,
    values: [f32; MAX_FX_PARAMS],
}

impl Stage {
    fn new(wing: &str, ctx: FxContext) -> Option<Self> {
        let spec = &crate::wing::FX_MODELS[super::model_index(wing)?];
        let mut values = [0.0; MAX_FX_PARAMS];
        spec.defaults_into(&mut values);
        Some(Self { dev: super::create(wing, ctx)?, spec, values })
    }

    fn set(&mut self, id: &str, v: f32) {
        if let Some(i) = self.spec.param_index(id) {
            self.values[i] = v;
        }
    }

    fn apply(&mut self) {
        self.dev.set_params(&Params::new(self.spec, &self.values));
    }
}

/// A stereo scratch buffer.
struct Buf([Vec<f32>; 2]);

impl Buf {
    fn new(n: usize) -> Self {
        Self([vec![0.0; n], vec![0.0; n]])
    }

    fn load(&mut self, l: &[f32], r: &[f32]) {
        let n = l.len();
        self.0[0][..n].copy_from_slice(l);
        self.0[1][..n].copy_from_slice(r);
    }

    fn run(&mut self, stage: &mut Stage, n: usize) {
        let [l, r] = &mut self.0;
        stage.dev.process(&mut l[..n], &mut r[..n]);
    }
}

/// Gains that ramp over a block to new targets (no zipper noise).
#[derive(Clone, Copy)]
struct Ramp {
    last: [f32; 3],
    target: [f32; 3],
}

impl Ramp {
    fn new() -> Self {
        Self { last: [1.0, 1.0, 1.0], target: [1.0, 1.0, 1.0] }
    }

    /// The gains at sample `i` of `n`.
    #[inline]
    fn at(&self, i: usize, n: usize) -> [f32; 3] {
        let t = (i + 1) as f32 / n as f32;
        std::array::from_fn(|k| self.last[k] + (self.target[k] - self.last[k]) * t)
    }

    fn done(&mut self) {
        self.last = self.target;
    }
}

/// BALANCE (-100..100) as the two paths' levels: each at full until the
/// other side, falling to 0 at the end.
fn balance(b: f32) -> (f32, f32) {
    let b = (b * 0.01).clamp(-1.0, 1.0);
    ((1.0 - b).min(1.0), (1.0 + b).min(1.0))
}

/// Sets both devices from the X32 model; returns BALANCE and MIX.
type PairMap = fn(&Params, &mut Stage, &mut Stage) -> (f32, f32);

/// Two devices side by side.
struct Pair {
    a: Stage,
    b: Stage,
    map: PairMap,
    bufs: [Buf; 2],
    /// Gains of A, B and the dry input.
    ramp: Ramp,
    primed: bool,
}

impl Pair {
    fn boxed(a: &str, b: &str, map: PairMap, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
        let n = ctx.max_block;
        Some(Box::new(Self {
            a: Stage::new(a, ctx)?,
            b: Stage::new(b, ctx)?,
            map,
            bufs: [Buf::new(n), Buf::new(n)],
            ramp: Ramp::new(),
            primed: false,
        }))
    }
}

/// The wet gains and the dry one for BALANCE and MIX.
fn gains(bal: f32, mix: f32) -> [f32; 3] {
    let (ga, gb) = balance(bal);
    let mix = (mix * 0.01).clamp(0.0, 1.0);
    [ga * mix, gb * mix, 1.0 - mix]
}

impl FxAlgo for Pair {
    fn set_params(&mut self, p: &Params) {
        let (bal, mix) = (self.map)(p, &mut self.a, &mut self.b);
        self.a.apply();
        self.b.apply();
        self.ramp.target = gains(bal, mix);
        if !self.primed {
            self.primed = true;
            self.ramp.done();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len().min(self.bufs[0].0[0].len());
        let [ba, bb] = &mut self.bufs;
        ba.load(&left[..n], &right[..n]);
        bb.load(&left[..n], &right[..n]);
        ba.run(&mut self.a, n);
        bb.run(&mut self.b, n);
        for i in 0..n {
            let [ga, gb, dry] = self.ramp.at(i, n);
            left[i] = dry * left[i] + ga * ba.0[0][i] + gb * bb.0[0][i];
            right[i] = dry * right[i] + ga * ba.0[1][i] + gb * bb.0[1][i];
        }
        self.ramp.done();
    }

    fn reset(&mut self) {
        self.a.dev.reset();
        self.b.dev.reset();
        self.ramp.done();
    }
}

// ---- the mappings ----------------------------------------------------------------------

/// The WING ST-DL's FACTOR index of each PATTERN fraction (1/4, 1/3, 3/8, 1/2,
/// 2/3, 3/4, 1), and whether PATTERN crosses (its X half).
fn pattern(p: &Params) -> (f32, bool) {
    const FACT: [f32; 7] = [0.0, 1.0, 6.0, 2.0, 3.0, 4.0, 5.0];
    let k = p.choice("pattern");
    (FACT[k % 7], k >= 7)
}

/// The delay of D/RV, D/CR and D/FL: TIME x PATTERN, the repeats through
/// FEED HI CUT, crossing for the X patterns or CROSS FEED from 50.
fn delay(p: &Params, d: &mut Stage) {
    let (fact, cross) = pattern(p);
    d.set("time", p.f("time"));
    d.set("fact", fact);
    d.set("pat", 4.0); // L:R 1:1
    d.set("offs", 0.0);
    d.set("mode", if cross || p.f("cross_feed") >= 50.0 { 1.0 } else { 0.0 });
    d.set("feed", p.f("feedback"));
    d.set("fhc", p.f("feed_hi_cut"));
    d.set("lc", 20.0);
    d.set("hc", 20_000.0);
}

/// The chamber of D/RV, CR/R and FL/R.
fn chamber(p: &Params, c: &mut Stage) {
    for (wing, x32) in [("pdel", "pre_delay"), ("dcy", "decay"), ("size", "size"), ("damp", "damping"), ("lc", "lo_cut")] {
        c.set(wing, p.f(x32));
    }
}

/// The chorus of CR/R and D/CR (both sides alike; the WING device's own mix 100).
fn chorus(p: &Params, c: &mut Stage) {
    let (dly, dep) = (p.f("delay"), p.f("depth"));
    for (id, v) in [("dlyl", dly), ("dlyr", dly), ("depl", dep), ("depr", dep), ("spd", p.f("speed"))] {
        c.set(id, v);
    }
    c.set("phase", p.f("phase"));
    c.set("wave", p.f("wave"));
    c.set("mix", 100.0);
}

/// The flanger of FL/R and D/FL.
fn flanger(p: &Params, f: &mut Stage) {
    let (dly, dep) = (p.f("delay"), p.f("depth"));
    for (id, v) in [("dlyl", dly), ("dlyr", dly), ("depl", dep), ("depr", dep), ("spd", p.f("speed"))] {
        f.set(id, v);
    }
    f.set("phase", p.f("phase"));
    f.set("feed", p.f("feed"));
    f.set("mix", 100.0);
}

fn delay_chamber(p: &Params, a: &mut Stage, b: &mut Stage) -> (f32, f32) {
    delay(p, a);
    chamber(p, b);
    (p.f("balance"), p.f("mix"))
}

fn chorus_chamber(p: &Params, a: &mut Stage, b: &mut Stage) -> (f32, f32) {
    chorus(p, a);
    chamber(p, b);
    (p.f("balance"), p.f("mix"))
}

fn flanger_chamber(p: &Params, a: &mut Stage, b: &mut Stage) -> (f32, f32) {
    flanger(p, a);
    chamber(p, b);
    (p.f("balance"), p.f("mix"))
}

fn delay_chorus(p: &Params, a: &mut Stage, b: &mut Stage) -> (f32, f32) {
    delay(p, a);
    chorus(p, b);
    (p.f("balance"), p.f("mix"))
}

fn delay_flanger(p: &Params, a: &mut Stage, b: &mut Stage) -> (f32, f32) {
    delay(p, a);
    flanger(p, b);
    (p.f("balance"), p.f("mix"))
}

// ---- Modulation Delay ------------------------------------------------------------------

/// Modulation Delay: delay into chorus, and a reverb in series or beside.
struct ModDelay {
    delay: Stage,
    chorus: Stage,
    /// AMB, CLUB and HALL.
    reverbs: [Stage; 3],
    kind: usize,
    serial: bool,
    mods: Buf,
    verb: Buf,
    /// Gains of the delay path, the reverb path and the dry input.
    ramp: Ramp,
    primed: bool,
}

/// The X32's DELAY choices (1, 1/2, 2/3, 3/2) as the WING ST-DL's FACTOR index.
const MODD_FACT: [f32; 4] = [5.0, 2.0, 3.0, 9.0];

impl ModDelay {
    fn boxed(ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
        let n = ctx.max_block;
        Some(Box::new(Self {
            delay: Stage::new("ST-DL", ctx)?,
            chorus: Stage::new("CHORUS", ctx)?,
            reverbs: [Stage::new("AMBI", ctx)?, Stage::new("ROOM", ctx)?, Stage::new("HALL", ctx)?],
            kind: 0,
            serial: false,
            mods: Buf::new(n),
            verb: Buf::new(n),
            ramp: Ramp::new(),
            primed: false,
        }))
    }
}

impl FxAlgo for ModDelay {
    fn set_params(&mut self, p: &Params) {
        let d = &mut self.delay;
        d.set("time", p.f("time"));
        d.set("fact", MODD_FACT[p.choice("delay").min(3)]);
        d.set("pat", 4.0);
        d.set("offs", 0.0);
        d.set("feed", p.f("feed"));
        d.set("lc", p.f("lo_cut"));
        d.set("hc", p.f("hi_cut"));
        d.apply();
        // DEPTH RATE sweeps the chorus, RATE its speed; slightly apart sides.
        let c = &mut self.chorus;
        let depth = p.f("depth_rate");
        for (id, v) in [("depl", depth), ("depr", depth), ("spd", p.f("rate")), ("dlyl", 15.0), ("dlyr", 17.0), ("mix", 100.0)] {
            c.set(id, v);
        }
        c.apply();
        // DECAY 1-10 in seconds, each reverb within its range.
        self.kind = p.choice("type").min(2);
        for r in &mut self.reverbs {
            r.set("dcy", p.f("decay"));
            r.set("damp", p.f("damping"));
            r.apply();
        }
        let new_serial = p.choice("setup") == 1;
        if new_serial != self.serial {
            self.reverbs.iter_mut().for_each(|r| r.dev.reset());
        }
        self.serial = new_serial;
        self.ramp.target = gains(p.f("balance"), p.f("mix"));
        if !self.primed {
            self.primed = true;
            self.ramp.done();
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len().min(self.mods.0[0].len());
        self.mods.load(&left[..n], &right[..n]);
        self.mods.run(&mut self.delay, n);
        self.mods.run(&mut self.chorus, n);
        // SER: the reverb takes the modulated delay; PAR: the input.
        if self.serial {
            let (l, r) = (&self.mods.0[0][..n], &self.mods.0[1][..n]);
            self.verb.load(l, r);
        } else {
            self.verb.load(&left[..n], &right[..n]);
        }
        self.verb.run(&mut self.reverbs[self.kind], n);
        for i in 0..n {
            let [gd, gr, dry] = self.ramp.at(i, n);
            left[i] = dry * left[i] + gd * self.mods.0[0][i] + gr * self.verb.0[0][i];
            right[i] = dry * right[i] + gd * self.mods.0[1][i] + gr * self.verb.0[1][i];
        }
        self.ramp.done();
    }

    fn reset(&mut self) {
        self.delay.dev.reset();
        self.chorus.dev.reset();
        self.reverbs.iter_mut().for_each(|r| r.dev.reset());
        self.ramp.done();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn devices() {
        for token in TOKENS {
            crate::wing::fx::tests::check_device(token);
        }
    }

    #[test]
    fn every_control_set_is_the_wing_devices() {
        // A Stage::set on an id the device lacks would do nothing: check each.
        for (token, a, b) in [
            ("X32D/RV", "ST-DL", "CHAMBER"),
            ("X32CR/R", "CHORUS", "CHAMBER"),
            ("X32FL/R", "FLANGER", "CHAMBER"),
            ("X32D/CR", "ST-DL", "CHORUS"),
            ("X32D/FL", "ST-DL", "FLANGER"),
        ] {
            let x32 = &crate::x32::FX_MODELS[crate::model::model_index(crate::x32::FX_MODELS, token).unwrap()];
            let mut values = [0.0; MAX_FX_PARAMS];
            x32.defaults_into(&mut values);
            let ctx = FxContext { sample_rate: 48_000.0, max_block: 64 };
            let (mut sa, mut sb) = (Stage::new(a, ctx).unwrap(), Stage::new(b, ctx).unwrap());
            let before = (sa.values, sb.values);
            let map: PairMap = match token {
                "X32D/RV" => delay_chamber,
                "X32CR/R" => chorus_chamber,
                "X32FL/R" => flanger_chamber,
                "X32D/CR" => delay_chorus,
                _ => delay_flanger,
            };
            map(&Params::new(x32, &values), &mut sa, &mut sb);
            // Something changed on each side (the mapping reached both devices).
            assert!(sa.values != before.0 || sb.values != before.1, "{token}");
        }
        // Every id the mappings use exists on its device.
        let ids: &[(&str, &[&str])] = &[
            ("ST-DL", &["time", "fact", "pat", "offs", "mode", "feed", "fhc", "lc", "hc"]),
            ("CHAMBER", &["pdel", "dcy", "size", "damp", "lc"]),
            ("CHORUS", &["dlyl", "dlyr", "depl", "depr", "spd", "phase", "wave", "mix"]),
            ("FLANGER", &["dlyl", "dlyr", "depl", "depr", "spd", "phase", "feed", "mix"]),
            ("AMBI", &["dcy", "damp"]),
            ("ROOM", &["dcy", "damp"]),
            ("HALL", &["dcy", "damp"]),
        ];
        for (wing, ids) in ids {
            let spec = &crate::wing::FX_MODELS[crate::wing::fx::model_index(wing).unwrap()];
            for id in *ids {
                assert!(spec.param_index(id).is_some(), "{wing} {id}");
            }
        }
    }

    #[test]
    fn balance_takes_one_path_or_both() {
        assert_eq!(balance(-100.0), (1.0, 0.0));
        assert_eq!(balance(0.0), (1.0, 1.0));
        assert_eq!(balance(100.0), (0.0, 1.0));
        assert_eq!(gains(0.0, 0.0), [0.0, 0.0, 1.0], "MIX 0: dry only");
    }
}
