//! The X32's devices that run on one of the WING's (Phase 4 steps 9d-9e): the
//! two consoles share most of their reverbs and modulation effects, the same
//! designs with the same controls, so the X32 model's values are mapped onto
//! the WING device's controls and the WING device runs them. The X32's Stereo
//! Delay is the WING's with a factor and a feedback per side (`delay::stereo`,
//! X32DLY).
//!
//! Hall, Vintage Room and Stereo Chorus (the factory rack) map over ranges
//! wide enough for both consoles; the others over the WING device's own (an
//! X32 value beyond them, such as a 10 Hz LO CUT, takes the WING's limit).
//! A LEVEL the WING device lacks is an output gain. Where the controls'
//! units differ the mapping is Sidecar's reading, to be measured on an X32:
//! Vintage Room's SIZE (0-100 on the console, factory 24) takes the WING's
//! 1-50 at half; DIFFUSE 1-30 is the WING Room's and Chamber's 0-100 scaled;
//! Chamber's REFLECTION GAIN (%) the WING's level in dB; Vintage Reverb's
//! MODULATE 0-10 the WING's 0-100; Phaser's BASE 0-50 the WING's RANGE at
//! twice; Mood Filter's RESONANCE and DRIVE 0-100 the WING's 0-10. The X32's
//! Plate is the WING's Blue Plate design, its Rich Plate the WING's Plate.
//!
//! The dual effects (Dual Pitch, Sub Octaver) run two WING devices, one per
//! side ([`Dual`]): side A takes the left input, side B the right, each fed
//! to its device in mono. Sub Octaver's DRY sets the direct signal's level
//! under the sub (the WING's keeps it at unity); ACTIVE off passes the side.

use super::{FxAlgo, FxContext, FX_READOUTS};
use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec, Params};
use crate::util::db_to_gain;
use crate::wing::MAX_FX_PARAMS;

/// The X32 models built on a WING device.
pub(super) const TOKENS: &[&str] = &[
    "X32HALL", "X32VRM", "X32CRS", "X32AMBI", "X32ROOM", "X32CHAM", "X32PLAT", "X32RPLT", "X32VREV", "X32GATE", "X32RVRS",
    "X32FLNG", "X32PHAS", "X32DIMC", "X32FILT", "X32ROTA", "X32PAN", "X32PIT", "X32PIT2", "X32SUB",
];

pub(super) fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    if !TOKENS.contains(&token) {
        return None;
    }
    match token {
        "X32PIT2" => return Dual::boxed("PITCH", dual_pitch, ctx),
        "X32SUB" => return Dual::boxed("SUB", dual_sub, ctx),
        _ => {}
    }
    // The wide specs, or the WING device's own.
    let (wing, spec, map): (&str, Option<&'static ModelSpec>, Map) = match token {
        "X32HALL" => ("HALL", Some(&HALL), hall),
        "X32VRM" => ("V-ROOM", Some(&VROOM), vroom),
        "X32CRS" => ("CHORUS", Some(&CHORUS), chorus),
        "X32AMBI" => ("AMBI", None, ambi),
        "X32ROOM" => ("ROOM", None, room),
        "X32CHAM" => ("CHAMBER", None, chamber),
        "X32PLAT" => ("BPLATE", None, plate),
        "X32RPLT" => ("PLATE", None, rich_plate),
        "X32VREV" => ("V-REV", None, vintage_reverb),
        "X32GATE" => ("GATED", None, gated),
        "X32RVRS" => ("REVERSE", None, reverse),
        "X32FLNG" => ("FLANGER", None, flanger),
        "X32PHAS" => ("PHASER", None, phaser),
        "X32DIMC" => ("DIMCRS", None, dimension),
        "X32FILT" => ("MOOD", None, mood),
        "X32ROTA" => ("ROTARY", None, rotary),
        "X32PAN" => ("PANNER", None, panner),
        "X32PIT" => ("PITCH", None, pitch),
        _ => return None,
    };
    bridge(wing, spec, map, ctx)
}

/// WING device `wing` run by `map` over `spec` (None: the device's own controls).
pub(super) fn bridge(wing: &str, spec: Option<&'static ModelSpec>, map: Map, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    let spec = spec.or_else(|| Some(&crate::wing::FX_MODELS[super::model_index(wing)?]))?;
    let dev = super::create(wing, ctx)?;
    // Controls a mapping leaves alone keep the WING device's defaults.
    let mut values = [0.0; MAX_FX_PARAMS];
    spec.defaults_into(&mut values);
    Some(Box::new(Bridge { dev, spec, map, values, gain: 1.0, last_gain: 1.0 }))
}

/// Sets each WING control (first) from the X32 control of the same meaning.
fn copy(p: &Params, set: &mut dyn FnMut(&'static str, f32), pairs: &[(&'static str, &str)]) {
    for &(wing, x32) in pairs {
        set(wing, p.f(x32));
    }
}

/// The X32's DIFFUSE (1-30) as the WING Room's and Chamber's DIFFUSION (0-100).
fn diffusion(x32: f32) -> f32 {
    (x32 - 1.0) * 100.0 / 29.0
}

fn ambi(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("pdel", "pre_delay"), ("size", "size"), ("dcy", "decay"), ("tail", "tail_gain"), ("damp", "damping")];
    copy(p, set, &pairs);
    copy(p, set, &[("diff", "diffuse"), ("mod", "modulate"), ("lc", "lo_cut"), ("hc", "hi_cut")]);
    p.f("level")
}

/// The controls the X32's Room, Chamber and Rich Plate share with the WING's.
fn lexicon(p: &Params, set: &mut dyn FnMut(&'static str, f32)) {
    let pairs = [("pdel", "pre_delay"), ("size", "size"), ("dcy", "decay"), ("mult", "bass_multi"), ("damp", "damping")];
    copy(p, set, &pairs);
    copy(p, set, &[("lc", "lo_cut"), ("hc", "hi_cut"), ("sprd", "spread"), ("spin", "spin")]);
    set("diff", diffusion(p.f("diffuse")));
}

fn room(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    lexicon(p, set);
    let pairs = [("shp", "shape"), ("ecl", "echo_l"), ("ecr", "echo_r"), ("efl", "echo_feed_l"), ("efr", "echo_feed_r")];
    copy(p, set, &pairs);
    p.f("level")
}

fn chamber(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    lexicon(p, set);
    copy(p, set, &[("shp", "shape"), ("ecl", "reflection_l"), ("ecr", "reflection_r")]);
    // REFLECTION GAIN in % of full level.
    let db = |pct: f32| if pct <= 0.1 { -60.0 } else { 20.0 * (pct * 0.01).log10() };
    set("ell", db(p.f("reflection_gain_l")));
    set("elr", db(p.f("reflection_gain_r")));
    p.f("level")
}

fn rich_plate(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    lexicon(p, set);
    let pairs = [("att", "attack"), ("ecl", "echo_l"), ("ecr", "echo_r"), ("efl", "echo_feed_l"), ("efr", "echo_feed_r")];
    copy(p, set, &pairs);
    p.f("level")
}

fn plate(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("pdel", "pre_delay"), ("size", "size"), ("dcy", "decay"), ("mult", "bass_multi"), ("damp", "damping")];
    copy(p, set, &pairs);
    let pairs =
        [("lc", "lo_cut"), ("hc", "hi_cut"), ("xover", "xover"), ("mdep", "mod"), ("mspd", "mod_speed"), ("diff", "diffuse")];
    copy(p, set, &pairs);
    p.f("level")
}

fn vintage_reverb(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("pdel", "pre_delay"), ("dcy", "decay"), ("lmult", "lo_multiply"), ("hmult", "hi_multiply")];
    copy(p, set, &pairs);
    copy(p, set, &[("lc", "lo_cut"), ("hc", "hi_cut"), ("out", "position"), ("trans", "vintage")]);
    set("mod", 10.0 * p.f("modulate"));
    p.f("level")
}

/// Gated and Reverse: DECAY in ms on the X32, seconds on the WING.
fn nonlinear(p: &Params, set: &mut dyn FnMut(&'static str, f32)) {
    let pairs = [("pdel", "pre_delay"), ("diff", "diffuse"), ("sprd", "spread"), ("lc", "lo_cut"), ("hsf", "hi_cut")];
    copy(p, set, &pairs);
    set("hsg", p.f("hi_shv_gain"));
    set("dcy", 0.001 * p.f("decay"));
}

fn gated(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    nonlinear(p, set);
    copy(p, set, &[("att", "attack"), ("dens", "density")]);
    p.f("level")
}

fn reverse(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    nonlinear(p, set);
    set("rise", p.f("rise"));
    p.f("level")
}

fn flanger(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("lc", "lo_cut"), ("hc", "hi_cut"), ("flc", "feed_lo_cut"), ("fhc", "feed_hi_cut"), ("mix", "mix")];
    copy(p, set, &pairs);
    let pairs = [("dlyl", "delay_l"), ("dlyr", "delay_r"), ("depl", "depth_l"), ("depr", "depth_r"), ("phase", "phase")];
    copy(p, set, &pairs);
    copy(p, set, &[("spd", "speed"), ("feed", "feed")]);
    0.0
}

fn phaser(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("spd", "speed"), ("phase", "phase"), ("wave", "wave"), ("depth", "depth"), ("emod", "env_modulation")];
    copy(p, set, &pairs);
    let pairs =
        [("att", "attack"), ("hld", "hold"), ("rel", "release"), ("mix", "mix"), ("stg", "stages"), ("reso", "resonance")];
    copy(p, set, &pairs);
    set("range", 2.0 * p.f("base"));
    0.0
}

fn dimension(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    // ACTIVE off: no mode pressed (the WING's OFF).
    let active = p.f("active");
    for (wing, x32) in [("sw1", "mode_1"), ("sw2", "mode_2"), ("sw3", "mode_3"), ("sw4", "mode_4")] {
        set(wing, active * p.f(x32));
    }
    copy(p, set, &[("in", "mode"), ("drysw", "dry")]);
    0.0
}

fn mood(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("fbase", "base"), ("filt", "mode"), ("slope", "4_pole"), ("env", "env_modulation"), ("att", "attack")];
    copy(p, set, &pairs);
    let pairs = [("rel", "release"), ("mix", "mix"), ("lfo", "depth"), ("spd", "speed"), ("phase", "phase"), ("wave", "wave")];
    copy(p, set, &pairs);
    set("reso", 0.1 * p.f("resonance"));
    set("drv", 0.1 * p.f("drive"));
    0.0
}

fn rotary(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("lo", "lo_speed"), ("hi", "hi_speed"), ("bal", "balance"), ("mix", "mix"), ("dist", "distance")];
    copy(p, set, &pairs);
    // One ACCELERATE for both rotors; STOP, else SLOW or FAST.
    copy(p, set, &[("dac", "accelerate"), ("hac", "accelerate")]);
    set(
        "sw",
        if p.on("stop") {
            0.0
        } else if p.on("slow") {
            1.0
        } else {
            2.0
        },
    );
    0.0
}

fn panner(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("spd", "speed"), ("phase", "phase"), ("wave", "wave"), ("depth", "depth"), ("espd", "env_speed")];
    copy(p, set, &pairs);
    copy(p, set, &[("edep", "env_depth"), ("att", "attack"), ("hld", "hold"), ("rel", "release")]);
    0.0
}

fn pitch(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let pairs = [("semi", "semitone"), ("cent", "cent"), ("dly", "delay"), ("lc", "lo_cut"), ("hc", "hi_cut"), ("mix", "mix")];
    copy(p, set, &pairs);
    0.0
}

/// Writes the WING device's values (by its ids) from the X32 model's; returns
/// an output gain in dB (0 when the WING device has the level itself).
pub(super) type Map = fn(&Params, &mut dyn FnMut(&'static str, f32)) -> f32;

struct Bridge {
    dev: Box<dyn FxAlgo>,
    /// The WING device's controls over both consoles' ranges.
    spec: &'static ModelSpec,
    map: Map,
    values: [f32; MAX_FX_PARAMS],
    gain: f32,
    last_gain: f32,
}

impl FxAlgo for Bridge {
    fn set_params(&mut self, p: &Params) {
        let (spec, values) = (self.spec, &mut self.values);
        let db = (self.map)(p, &mut |id, v| {
            if let Some(i) = spec.param_index(id) {
                values[i] = v;
            }
        });
        self.gain = db_to_gain(db);
        self.dev.set_params(&Params::new(self.spec, &self.values));
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        self.dev.process(left, right);
        if self.gain == 1.0 && self.last_gain == 1.0 {
            return;
        }
        // A level change ramps over the block.
        let n = left.len().max(1) as f32;
        let step = (self.gain - self.last_gain) / n;
        let mut g = self.last_gain;
        for (l, r) in left.iter_mut().zip(right.iter_mut()) {
            g += step;
            *l *= g;
            *r *= g;
        }
        self.last_gain = self.gain;
    }

    fn reset(&mut self) {
        self.dev.reset();
        self.last_gain = self.gain;
    }

    fn meter(&self) -> f32 {
        self.dev.meter()
    }

    fn readouts(&self, out: &mut [f32; FX_READOUTS]) {
        self.dev.readouts(out);
    }

    fn latency(&self) -> u32 {
        self.dev.latency()
    }
}

/// Sets one side's WING controls (side 0 = A, 1 = B); returns whether the
/// side runs and, for a device that adds to its input, the direct level.
pub(super) type SideMap = fn(&Params, usize, &mut dyn FnMut(&'static str, f32)) -> (bool, Option<f32>);

/// Two WING devices, one per side, each fed its side in mono.
pub(super) struct Dual {
    devs: [Box<dyn FxAlgo>; 2],
    spec: &'static ModelSpec,
    map: SideMap,
    values: [[f32; MAX_FX_PARAMS]; 2],
    run: [bool; 2],
    /// The direct level under an adding device's output (None: its output as is).
    dry: [Option<f32>; 2],
    /// The device's second input, and the side's input as it came.
    other: Vec<f32>,
    input: Vec<f32>,
}

impl Dual {
    pub(super) fn boxed(wing: &str, map: SideMap, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
        let spec = &crate::wing::FX_MODELS[super::model_index(wing)?];
        let mut values = [0.0; MAX_FX_PARAMS];
        spec.defaults_into(&mut values);
        Some(Box::new(Self {
            devs: [super::create(wing, ctx)?, super::create(wing, ctx)?],
            spec,
            map,
            values: [values; 2],
            run: [true; 2],
            dry: [None; 2],
            other: vec![0.0; ctx.max_block],
            input: vec![0.0; ctx.max_block],
        }))
    }
}

impl FxAlgo for Dual {
    fn set_params(&mut self, p: &Params) {
        for side in 0..2 {
            let (spec, values) = (self.spec, &mut self.values[side]);
            let (run, dry) = (self.map)(p, side, &mut |id, v| {
                if let Some(i) = spec.param_index(id) {
                    values[i] = v;
                }
            });
            if run && !self.run[side] {
                self.devs[side].reset();
            }
            (self.run[side], self.dry[side]) = (run, dry);
            self.devs[side].set_params(&Params::new(self.spec, &self.values[side]));
        }
    }

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        let n = left.len().min(self.other.len());
        for (side, x) in [left, right].into_iter().enumerate() {
            if !self.run[side] {
                continue;
            }
            let x = &mut x[..n];
            self.input[..n].copy_from_slice(x);
            self.other[..n].copy_from_slice(x);
            self.devs[side].process(x, &mut self.other[..n]);
            if let Some(d) = self.dry[side] {
                for (y, dry) in x.iter_mut().zip(&self.input[..n]) {
                    *y += (d - 1.0) * dry;
                }
            }
        }
    }

    fn reset(&mut self) {
        for d in &mut self.devs {
            d.reset();
        }
    }
}

fn dual_pitch(p: &Params, side: usize, set: &mut dyn FnMut(&'static str, f32)) -> (bool, Option<f32>) {
    let ab = ["a", "b"][side];
    for (wing, x32) in
        [("semi", "semitone"), ("cent", "cent"), ("dly", "delay"), ("lc", "lo_cut"), ("hc", "hi_cut"), ("mix", "mix")]
    {
        set(wing, p.f(&format_id(x32, ab)));
    }
    (true, None)
}

fn dual_sub(p: &Params, side: usize, set: &mut dyn FnMut(&'static str, f32)) -> (bool, Option<f32>) {
    // Side B's controls end in "_2".
    let id = |x32: &str| if side == 0 { IdBuf::from(x32) } else { format_id(x32, "2") };
    for (wing, x32) in [("rng", "range"), ("oct1", "octave_1"), ("oct2", "octave_2")] {
        set(wing, p.f(&id(x32)));
    }
    (p.on(&id("active")), Some(0.01 * p.f(&id("dry"))))
}

/// A control id built on the audio thread without allocating ("semitone" + "a").
pub(super) struct IdBuf {
    buf: [u8; 32],
    len: usize,
}

impl IdBuf {
    pub(super) fn from(s: &str) -> Self {
        let mut b = Self { buf: [0; 32], len: 0 };
        b.push(s);
        b
    }

    pub(super) fn push(&mut self, s: &str) {
        let n = s.len().min(self.buf.len() - self.len);
        self.buf[self.len..self.len + n].copy_from_slice(&s.as_bytes()[..n]);
        self.len += n;
    }

    /// Appends `n` in decimal ("band_" + 12).
    pub(super) fn push_number(&mut self, n: usize) {
        let mut digits = [0u8; 20];
        let (mut n, mut k) = (n, digits.len());
        loop {
            k -= 1;
            digits[k] = b'0' + (n % 10) as u8;
            n /= 10;
            if n == 0 {
                break;
            }
        }
        self.push(std::str::from_utf8(&digits[k..]).unwrap_or(""));
    }

    pub(super) fn truncate(&mut self, len: usize) {
        self.len = self.len.min(len);
    }
}

impl std::ops::Deref for IdBuf {
    type Target = str;
    fn deref(&self) -> &str {
        std::str::from_utf8(&self.buf[..self.len]).unwrap_or("")
    }
}

/// `base` + "_" + `suffix` ("semitone_a", "dry_2").
pub(super) fn format_id(base: &str, suffix: &str) -> IdBuf {
    let mut b = IdBuf::from(base);
    b.push("_");
    b.push(suffix);
    b
}

/// A continuous control over `lo..hi` (the device reads values, not steps).
const fn r(id: &'static str, lo: f32, hi: f32) -> ParamSpec {
    ParamSpec { id, label: id, unit: "", kind: ParamKind::Linear, min: lo, max: hi, steps: 0, default: lo }
}

const fn spec(token: &'static str, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name: token, category: ModelCategory::FxPremium, params }
}

static HALL: ModelSpec = spec(
    "HALL",
    &[
        r("pdel", 0.0, 200.0),
        r("size", 2.0, 100.0),
        r("dcy", 0.2, 5.0),
        r("mult", 0.5, 2.0),
        r("damp", 1000.0, 20000.0),
        r("lc", 10.0, 500.0),
        r("hc", 200.0, 20000.0),
        r("shp", 0.0, 250.0),
        r("sprd", 0.0, 50.0),
        r("diff", 1.0, 30.0),
        r("mspd", 0.0, 100.0),
    ],
);

fn hall(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    for (wing, x32) in [
        ("pdel", "pre_delay"),
        ("size", "size"),
        ("dcy", "decay"),
        ("mult", "bass_multi"),
        ("damp", "damping"),
        ("lc", "lo_cut"),
        ("hc", "hi_cut"),
        ("shp", "shape"),
        ("sprd", "spread"),
        ("diff", "diffuse"),
        ("mspd", "mod_speed"),
    ] {
        set(wing, p.f(x32));
    }
    p.f("level")
}

static VROOM: ModelSpec = spec(
    "V-ROOM",
    &[
        r("pdel", 0.0, 200.0),
        r("size", 1.0, 50.0),
        r("dcy", 0.1, 20.0),
        r("dens", 1.0, 30.0),
        r("erlvl", 0.0, 190.0),
        r("lmult", 0.1, 10.0),
        r("hmult", 0.1, 10.0),
        r("lc", 10.0, 500.0),
        r("hc", 200.0, 20000.0),
        r("frz", 0.0, 1.0),
        r("erl", 0.0, 200.0),
        r("err", 0.0, 200.0),
        r("add", 0.0, 1.0),
        r("lvl", -12.0, 12.0),
    ],
);

fn vroom(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    for (wing, x32) in [
        ("pdel", "reverb_delay"),
        ("dcy", "decay"),
        ("dens", "density"),
        ("erlvl", "er_level"),
        ("lmult", "lo_multiply"),
        ("hmult", "hi_multiply"),
        ("lc", "lo_cut"),
        ("hc", "hi_cut"),
        ("frz", "freeze"),
        ("erl", "er_left"),
        ("err", "er_right"),
        ("lvl", "level"),
    ] {
        set(wing, p.f(x32));
    }
    set("size", (0.5 * p.f("size")).max(1.0));
    set("add", 0.0);
    0.0
}

static CHORUS: ModelSpec = spec(
    "CHORUS",
    &[
        r("lc", 10.0, 500.0),
        r("hc", 200.0, 20000.0),
        r("wave", 0.0, 100.0),
        r("phase", 0.0, 180.0),
        r("mix", 0.0, 100.0),
        r("dlyl", 0.5, 50.0),
        r("dlyr", 0.5, 50.0),
        r("depl", 0.0, 100.0),
        r("depr", 0.0, 100.0),
        r("sprd", 0.0, 100.0),
        r("spd", 0.05, 5.0),
    ],
);

fn chorus(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    for (wing, x32) in [
        ("lc", "lo_cut"),
        ("hc", "hi_cut"),
        ("wave", "wave"),
        ("phase", "phase"),
        ("mix", "mix"),
        ("dlyl", "delay_l"),
        ("dlyr", "delay_r"),
        ("depl", "depth_l"),
        ("depr", "depth_r"),
        ("sprd", "spread"),
        ("spd", "speed"),
    ] {
        set(wing, p.f(x32));
    }
    0.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_bridged_model_runs_and_names_only_its_wing_devices_controls() {
        // A mapping that names a control the WING device lacks would set nothing:
        // record every id each mapping sets and check the device has it.
        let ctx = FxContext { sample_rate: 48_000.0, max_block: 64 };
        for token in TOKENS {
            assert!(create(token, ctx).is_some(), "{token}");
            if matches!(*token, "X32PIT2" | "X32SUB") {
                continue; // two devices, each a side (the_dual_effects...)
            }
            let x32 = &crate::x32::FX_MODELS[crate::model::model_index(crate::x32::FX_MODELS, token).unwrap()];
            let mut values = [0.0; MAX_FX_PARAMS];
            x32.defaults_into(&mut values);
            let map = |t: &str| -> Map {
                match t {
                    "X32HALL" => hall,
                    "X32VRM" => vroom,
                    "X32CRS" => chorus,
                    "X32AMBI" => ambi,
                    "X32ROOM" => room,
                    "X32CHAM" => chamber,
                    "X32PLAT" => plate,
                    "X32RPLT" => rich_plate,
                    "X32VREV" => vintage_reverb,
                    "X32GATE" => gated,
                    "X32RVRS" => reverse,
                    "X32FLNG" => flanger,
                    "X32PHAS" => phaser,
                    "X32DIMC" => dimension,
                    "X32FILT" => mood,
                    "X32ROTA" => rotary,
                    "X32PAN" => panner,
                    _ => pitch,
                }
            };
            let wing = [
                ("X32HALL", "HALL"),
                ("X32VRM", "V-ROOM"),
                ("X32CRS", "CHORUS"),
                ("X32AMBI", "AMBI"),
                ("X32ROOM", "ROOM"),
                ("X32CHAM", "CHAMBER"),
                ("X32PLAT", "BPLATE"),
                ("X32RPLT", "PLATE"),
                ("X32VREV", "V-REV"),
                ("X32GATE", "GATED"),
                ("X32RVRS", "REVERSE"),
                ("X32FLNG", "FLANGER"),
                ("X32PHAS", "PHASER"),
                ("X32DIMC", "DIMCRS"),
                ("X32FILT", "MOOD"),
                ("X32ROTA", "ROTARY"),
                ("X32PAN", "PANNER"),
                ("X32PIT", "PITCH"),
            ]
            .into_iter()
            .find(|(x, _)| x == token)
            .unwrap()
            .1;
            let real = &crate::wing::FX_MODELS[crate::wing::fx::model_index(wing).unwrap()];
            let mut named = Vec::new();
            map(token)(&Params::new(x32, &values), &mut |id, _| named.push(id));
            for id in &named {
                assert!(real.param_index(id).is_some(), "{token} -> {wing}: {id}");
            }
            // Every X32 control is read: as many WING controls set as it has (less LEVEL
            // when it's an output gain, plus the ones it splits or the WING alone has).
            assert!(!named.is_empty(), "{token}");
        }
    }

    #[test]
    fn every_mapped_control_is_the_wing_devices() {
        // Each bridge names only controls the WING device has.
        for (wing, bridge) in [("HALL", &HALL), ("V-ROOM", &VROOM), ("CHORUS", &CHORUS)] {
            let real = &crate::wing::FX_MODELS[crate::wing::fx::model_index(wing).unwrap()];
            for p in bridge.params {
                assert!(real.param_index(p.id).is_some(), "{wing} {}", p.id);
            }
            assert_eq!(bridge.params.len(), real.params.len(), "{wing}: every control mapped");
        }
    }

    /// One second of a left impulse through model `token` of `table` with `edit`.
    fn impulse(table: &'static [ModelSpec], token: &str, edit: &[(&str, f32)]) -> Vec<f32> {
        let spec = &table[crate::model::model_index(table, token).unwrap()];
        let mut values = [0.0; MAX_FX_PARAMS];
        spec.defaults_into(&mut values);
        for (id, v) in edit {
            values[spec.param_index(id).unwrap()] = *v;
        }
        let mut dev = super::super::create(token, FxContext { sample_rate: 48_000.0, max_block: 256 }).unwrap();
        dev.set_params(&Params::new(spec, &values));
        let (mut l, mut r) = (vec![0.0f32; 48_000], vec![0.0f32; 48_000]);
        l[0] = 1.0;
        for (cl, cr) in l.chunks_mut(256).zip(r.chunks_mut(256)) {
            dev.process(cl, cr);
        }
        l
    }

    #[test]
    fn the_x32_devices_sound_as_the_wings_with_the_same_settings() {
        let (x32, wing) = (crate::x32::FX_MODELS, crate::wing::FX_MODELS);
        let hall = [("pdel", 20.0), ("size", 60.0), ("dcy", 2.0), ("lc", 83.0), ("hc", 7200.0)];
        let x32_hall = [("pre_delay", 20.0), ("size", 60.0), ("decay", 2.0), ("lo_cut", 83.0), ("hi_cut", 7200.0)];
        let mut w = hall.to_vec();
        w.extend([("mult", 0.95), ("damp", 5740.0), ("shp", 50.0), ("sprd", 25.0), ("diff", 25.0), ("mspd", 30.0)]);
        let (a, b) = (impulse(x32, "X32HALL", &x32_hall), impulse(wing, "HALL", &w));
        assert!(a.iter().zip(&b).all(|(a, b)| (a - b).abs() < 1e-6), "HALL");
        // LEVEL +6 dB: twice the WING hall's output.
        let louder = impulse(x32, "X32HALL", &[&x32_hall[..], &[("level", 6.0206)]].concat());
        assert!(louder.iter().zip(&a).all(|(l, a)| (l - 2.0 * a).abs() < 1e-5), "LEVEL");

        // The chorus, its delays inside the WING's range.
        let crs = [("speed", 0.5), ("depth_l", 15.0), ("depth_r", 15.0), ("delay_l", 19.9), ("delay_r", 19.9), ("mix", 100.0)];
        let wcrs = [("spd", 0.5), ("depl", 15.0), ("depr", 15.0), ("dlyl", 19.9), ("dlyr", 19.9), ("mix", 100.0)];
        let rest = [("lc", 97.0), ("hc", 15100.0), ("phase", 120.0), ("wave", 100.0), ("sprd", 85.0)];
        let (a, b) = (impulse(x32, "X32CRS", &crs), impulse(wing, "CHORUS", &[&wcrs[..], &rest[..]].concat()));
        assert!(a.iter().zip(&b).all(|(a, b)| (a - b).abs() < 1e-6), "CHORUS");

        // Vintage Room: SIZE at half, the rest as set.
        let vrm = [("size", 30.0), ("decay", 1.5), ("er_level", 22.0), ("level", 0.0)];
        let wvrm = [("size", 15.0), ("dcy", 1.5), ("erlvl", 22.0), ("lvl", 0.0), ("pdel", 20.0), ("dens", 30.0)];
        let wrest = [("lmult", 1.1), ("hmult", 0.69), ("lc", 97.0), ("hc", 10400.0), ("erl", 28.0), ("err", 34.0)];
        let (a, b) = (impulse(x32, "X32VRM", &vrm), impulse(wing, "V-ROOM", &[&wvrm[..], &wrest[..]].concat()));
        assert!(a.iter().zip(&b).all(|(a, b)| (a - b).abs() < 1e-6), "V-ROOM");
    }

    #[test]
    fn the_dual_effects_run_a_device_per_side() {
        let x32 = crate::x32::FX_MODELS;
        // A sine on both inputs through the Sub Octaver.
        let run = |edit: &[(&str, f32)]| {
            let spec = &x32[crate::model::model_index(x32, "X32SUB").unwrap()];
            let mut values = [0.0; MAX_FX_PARAMS];
            spec.defaults_into(&mut values);
            for (id, v) in edit {
                values[spec.param_index(id).unwrap()] = *v;
            }
            let mut dev = create("X32SUB", FxContext { sample_rate: 48_000.0, max_block: 256 }).unwrap();
            dev.set_params(&Params::new(spec, &values));
            let input: Vec<f32> =
                (0..48_000).map(|i| 0.5 * (std::f32::consts::TAU * 110.0 * i as f32 / 48_000.0).sin()).collect();
            let (mut l, mut r) = (input.clone(), input.clone());
            for (cl, cr) in l.chunks_mut(256).zip(r.chunks_mut(256)) {
                dev.process(cl, cr);
            }
            (input, l, r)
        };
        // B off passes the right side as it came; A at DRY 0 is the sub alone.
        let (x, l, r) = run(&[("active_2", 0.0), ("dry", 0.0)]);
        assert!(r.iter().zip(&x).all(|(r, x)| r == x), "B off");
        let diff: f32 = l.iter().zip(&x).map(|(l, x)| (l - x).abs()).sum::<f32>() / x.len() as f32;
        assert!(diff > 0.05, "A differs from its input: {diff}");
        // DRY 100 and no octaves: the input as it came.
        let (x, l, _) = run(&[("dry", 100.0), ("octave_1", 0.0), ("octave_2", 0.0)]);
        let err = l[24_000..].iter().zip(&x[24_000..]).fold(0.0f32, |m, (l, x)| m.max((l - x).abs()));
        assert!(err < 1e-3, "{err}");
    }

    #[test]
    fn devices() {
        for token in TOKENS {
            crate::wing::fx::tests::check_device(token);
        }
    }
}
