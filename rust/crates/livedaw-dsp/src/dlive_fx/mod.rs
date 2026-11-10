//! The dLive's RackExtra FX (its 20 models, SMR's four rooms four devices here, the
//! Multiband Compressor's two sizes two), its Dyn8 (`dynamics/dyn8.rs`) and, in [`ultra`],
//! the RackUltra FX card; the RackExtra models with the controls and every
//! range the dLive Firmware Reference Guide states (chapter DEEP Processing, its
//! panel pictures; see the "RackExtra FX" table in `docs/DLIVE_RESEARCH.md`, and
//! its Dyn8 chapter for the Dynamic EQ's and the Multiband's numbers).
//! Clean-room: built from the guide's description of the controls, not from the
//! console's firmware or Director software.
//!
//! The guide names the controls and gives few numbers, and no algorithm. Where it
//! is silent the choices are "Sidecar's reading", to be compared with a console;
//! each is called out where it is made.
//!
//! How each model is made:
//! - Reused: SMR's four rooms run on the WING reverb voice (`Space`, with SMR's
//!   decay bands, input filters and reflection and echo taps around it); the
//!   Dimension Chorus is the WING's Dimension CRS through its bridge; the delays'
//!   crossfading taps, the pitch models' splicing `Shifter` and the shared cut
//!   filters and limiters are the WING's.
//! - New here: the delays (Stereo Tap, 2-Tap, Bucket Brigade, Echo), Gated Verb, the
//!   modulation models (ADT, Chorus, Symphonic Chorus, Flanger, MOO Phaser,
//!   Rotator), Hypabass, the De-Esser, VS1, Pitch Doubler, Dynamic EQ, the
//!   Multibands and the Transient Controller: their controls don't fit a WING
//!   device.
//!
//! As on the other consoles, a time or modulation device returns the effect signal
//! only and the FX slot mixes; Hypabass adds to the dry signal, as the WING's sub
//! devices do, and the dynamics devices pass the signal they process. The 4-band
//! PEQ and Stereo Width every RackExtra engine has are the return strip's.

mod delay;
mod dynamics;
mod modulation;
mod pitch;
mod reverb;
#[cfg(test)]
mod tests;
pub mod ultra;

use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec, Params};
use crate::wing::fx::blocks::{OnePole, Svf};
use crate::wing::fx::modulation::shared::CutFilter;
use crate::wing::fx::x32_voices::bridge;
use crate::wing::fx::{FxAlgo, FxContext};

const LIN: ParamKind = ParamKind::Linear;
const LOG: ParamKind = ParamKind::Log;

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

/// A choice control.
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

/// An on/off control.
const fn tog(id: &'static str, label: &'static str, default: f32) -> ParamSpec {
    ParamSpec { id, label, unit: "", kind: ParamKind::Toggle, min: 0.0, max: 1.0, steps: 2, default }
}

const fn fx(token: &'static str, name: &'static str, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name, category: ModelCategory::FxPremium, params }
}

/// Dimension Chorus: Mode 1-4 and E, Output Wet Only or Dry+Wet.
static DIMENSION: [ParamSpec; 2] =
    [ch("mode", "MODE", &["1", "2", "3", "4", "E"], 0.0), ch("out", "OUTPUT", &["WET ONLY", "DRY+WET"], 0.0)];

/// Dyn8: an insert, so any slot on a Universal console (`slot_rule`).
const fn dyn8(params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token: "DLVDYN8", name: "Dyn8", category: ModelCategory::FxStandard, params }
}

/// The dLive's 16 RackExtra engines, then the RackUltra card's 8: where each lives in
/// a dLive's FX slots (`slot_rule`). Its 64 Dyn8 engines aren't FX slots: the engine
/// holds them in an array of their own, and an insert names Dyn8 `n` as slot
/// `DYN8_SLOTS.start + n`, past the last FX slot of any console (the engine's `MAX_FX`).
pub const RACKEXTRA_SLOTS: std::ops::Range<usize> = 0..16;
pub const ULTRA_SLOTS: std::ops::Range<usize> = 16..24;
pub const DYN8_SLOTS: std::ops::Range<usize> = 64..128;

/// The models, after an empty slot and EXT: the RackExtra models (20), Dyn8 and the
/// RackUltra models (11, `ultra`). On a dLive each engine holds only its own
/// (`slot_rule`); a Universal strip is offered them all, as FX slots 1-8 take any
/// premium model.
pub static FX_MODELS: [ModelSpec; 38] = [
    crate::wing::FX_MODELS[0],
    crate::wing::FX_MODELS[1],
    fx("DLVSMRC", "SMR Classic", &reverb::SMR),
    fx("DLVSMRH", "SMR Hall", &reverb::SMR),
    fx("DLVSMRR", "SMR Room", &reverb::SMR),
    fx("DLVSMRE", "SMR EMT", &reverb::SMR_EMT),
    fx("DLVTAP", "Stereo Tap Delay", &delay::TAP),
    fx("DLVBBD", "Bucket Brigade Delay", &delay::BBD),
    fx("DLV2TAP", "2-Tap Delay", &delay::TWO_TAP),
    fx("DLVECHO", "Echo", &delay::ECHO),
    fx("DLVGVRB", "Gated Verb", &reverb::GATED),
    fx("DLVADT", "ADT Doubler", &modulation::ADT),
    fx("DLVCHOR", "Chorus", &modulation::CHORUS),
    fx("DLVSYMP", "Symphonic Chorus", &modulation::SYMPHONIC),
    fx("DLVFLNG", "Flanger", &modulation::FLANGER),
    fx("DLVMOO", "MOO 12 Stage Phaser", &modulation::PHASER),
    fx("DLVHYPA", "Hypabass", &pitch::HYPABASS),
    fx("DLVDEES", "De-Esser", &dynamics::DE_ESSER),
    fx("DLVROT", "Rotator", &modulation::ROTATOR),
    fx("DLVVS1", "VS1 Vocal Shift", &pitch::VS1),
    fx("DLVPDBL", "Pitch Doubler", &pitch::DOUBLER),
    fx("DLVDYNEQ", "Dynamic EQ", &dynamics::DYN_EQ),
    fx("DLVMB3", "Multiband Compressor 3-band", &dynamics::MULTI3),
    fx("DLVMB4", "Multiband Compressor 4-band", &dynamics::MULTI4),
    fx("DLVTRAN", "Transient Controller", &dynamics::TRANSIENT),
    fx("DLVDIMC", "Dimension Chorus", &DIMENSION),
    dyn8(&dynamics::DYN8),
    fx("DLVSPACE", "Spaces Reverb Designer", &ultra::reverb::SPACES),
    fx("DLVPLATE", "Plate Reverb Designer", &ultra::reverb::PLATE),
    fx("DLVRDLY", "Rhythm Delay", &ultra::delay::RHYTHM),
    fx("DLVSAT", "Saturator", &ultra::tone::SATURATOR),
    fx("DLVAMP", "Amp+Cab Distortion", &ultra::tone::AMP_CAB),
    fx("DLVVSHFT", "Vocal Shifter", &ultra::pitch::SHIFTER),
    fx("DLVTUNER", "Vocal Tuner", &ultra::pitch::TUNER),
    fx("DLVGRID", "Vocal Gridder", &ultra::pitch::GRIDDER),
    fx("DLVH2", "Dual AutoKey Harmoniser", &ultra::pitch::HARM_DUAL),
    fx("DLVH4", "Quad Voice Harmoniser", &ultra::pitch::HARM_QUAD),
    fx("DLVHMIDI", "MIDI Harmoniser", &ultra::pitch::HARM_MIDI),
];

/// The RackUltra models' tokens (the last 11 of [`FX_MODELS`]).
pub fn is_ultra(token: &str) -> bool {
    ultra::TOKENS.contains(&token)
}

/// On a dLive (`models` is its table): whether its slot `slot` may hold `model`,
/// by which engines the model's kind runs on. `None` for any other console's
/// table, and for a dLive's RackExtra models, which follow the usual premium range.
pub fn slot_rule(models: &crate::model::ModelTables, model: usize, slot: usize) -> Option<bool> {
    if !std::ptr::eq(models.fx.as_ptr(), FX_MODELS.as_ptr()) {
        return None;
    }
    let token = models.fx.get(model)?.token;
    if is_ultra(token) {
        Some(ULTRA_SLOTS.contains(&slot))
    } else if token == "DLVDYN8" {
        Some(DYN8_SLOTS.contains(&slot))
    } else {
        None
    }
}

/// The dLive's RackExtra tokens.
pub fn is_dlive(token: &str) -> bool {
    token.starts_with("DLV") && FX_MODELS.iter().any(|m| m.token == token)
}

/// Mode 1-4 press one of the Dimension's switches; E (Sidecar's reading) all four,
/// its deepest and fastest setting.
fn dimension_map(p: &Params, set: &mut dyn FnMut(&'static str, f32)) -> f32 {
    let mode = p.choice("mode");
    for (i, id) in ["sw1", "sw2", "sw3", "sw4"].into_iter().enumerate() {
        set(id, f32::from(u8::from(mode == i || mode == 4)));
    }
    set("in", 1.0);
    set("drysw", f32::from(u8::from(p.choice("out") == 1)));
    0.0
}

/// Creates a device (message thread: allocates). `None` for any other token.
pub fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    use dynamics::{DeEsser, Dyn8, DynEq, Multiband, TransientController};
    use modulation::{Adt, ChorusVoice, Flanger, MooPhaser, Rotator};
    use pitch::{Hypabass, PitchDoubler, VocalShift};
    use reverb::{GatedVerb, Smr, SmrKind};
    if !is_dlive(token) {
        return None;
    }
    Some(match token {
        "DLVSMRC" => Box::new(Smr::new(SmrKind::Classic, ctx)),
        "DLVSMRH" => Box::new(Smr::new(SmrKind::Hall, ctx)),
        "DLVSMRR" => Box::new(Smr::new(SmrKind::Room, ctx)),
        "DLVSMRE" => Box::new(Smr::new(SmrKind::Emt, ctx)),
        "DLVTAP" => Box::new(delay::DualDelay::new(2700.0, ctx)),
        "DLVBBD" => Box::new(delay::BucketBrigade::new(ctx)),
        "DLV2TAP" => Box::new(delay::DualDelay::new(1360.0, ctx)),
        "DLVECHO" => Box::new(delay::TapeEcho::new(ctx)),
        "DLVGVRB" => Box::new(GatedVerb::new(ctx)),
        "DLVADT" => Box::new(Adt::new(ctx)),
        "DLVCHOR" => Box::new(ChorusVoice::new(false, ctx)),
        "DLVSYMP" => Box::new(ChorusVoice::new(true, ctx)),
        "DLVFLNG" => Box::new(Flanger::new(ctx)),
        "DLVMOO" => Box::new(MooPhaser::new(ctx)),
        "DLVHYPA" => Box::new(Hypabass::new(ctx)),
        "DLVDEES" => Box::new(DeEsser::new(ctx)),
        "DLVROT" => Box::new(Rotator::new(ctx)),
        "DLVVS1" => Box::new(VocalShift::new(ctx)),
        "DLVPDBL" => Box::new(PitchDoubler::new(ctx)),
        "DLVDYNEQ" => Box::new(DynEq::new(ctx)),
        "DLVMB3" => Box::new(Multiband::new(3, ctx)),
        "DLVMB4" => Box::new(Multiband::new(4, ctx)),
        "DLVTRAN" => Box::new(TransientController::new(ctx)),
        "DLVDIMC" => return bridge("DIMCRS", None, dimension_map, ctx),
        "DLVDYN8" => Box::new(Dyn8::new(ctx)),
        token if is_ultra(token) => return ultra::create(token, ctx),
        _ => return None,
    })
}

// ---- shared filters ---------------------------------------------------------------------

/// The input filters the guide gives as 24 dB/oct: two cascaded `CutFilter`s (a
/// Linkwitz-Riley shape, -6 dB at the corner: Sidecar's reading of the slope's shape).
struct Cut24 {
    a: CutFilter,
    b: CutFilter,
}

impl Cut24 {
    fn new(sr: f32) -> Self {
        Self { a: CutFilter::new(sr), b: CutFilter::new(sr) }
    }

    fn set(&mut self, lc: f32, hc: f32) {
        self.a.set(lc, hc);
        self.b.set(lc, hc);
    }

    /// As `CutFilter::tick`: channel 0 first, once per sample.
    #[inline]
    fn tick(&mut self, ch: usize, x: f32) -> f32 {
        self.b.tick(ch, self.a.tick(ch, x))
    }

    fn reset(&mut self) {
        self.a.reset();
        self.b.reset();
    }
}

/// A stereo low- or high-pass of 6, 12, 18 or 24 dB/oct (`poles` 1 to 4): the
/// feedback filters' SLOPE. The odd pole is a one-pole, the rest 2-pole sections
/// with Butterworth Qs.
struct Slope {
    hp: bool,
    poles: usize,
    one: [OnePole; 2],
    svf: [[Svf; 2]; 2],
}

/// Section Qs by number of poles.
const fn slope_q(poles: usize) -> [f32; 2] {
    match poles {
        3 => [1.0, 1.0],
        4 => [0.541, 1.307],
        _ => [0.707, 0.707],
    }
}

impl Slope {
    fn new(hp: bool) -> Self {
        Self { hp, poles: 2, one: [OnePole::default(); 2], svf: [[Svf::default(); 2]; 2] }
    }

    fn set(&mut self, hz: f32, poles: usize, sr: f32) {
        self.poles = poles.clamp(1, 4);
        for o in &mut self.one {
            o.set(hz, sr);
        }
        let q = slope_q(self.poles);
        for (stage, q) in self.svf.iter_mut().zip(q) {
            for s in stage {
                s.set(hz, q, sr);
            }
        }
    }

    #[inline]
    fn tick(&mut self, ch: usize, x: f32) -> f32 {
        let ch = ch & 1;
        let mut y = x;
        if self.poles & 1 == 1 {
            y = if self.hp { self.one[ch].hp(y) } else { self.one[ch].lp(y) };
        }
        for stage in self.svf.iter_mut().take(self.poles / 2) {
            let o = stage[ch].tick(y);
            y = if self.hp { o.hp } else { o.lp };
        }
        y
    }

    fn reset(&mut self) {
        for o in &mut self.one {
            o.reset();
        }
        for s in self.svf.iter_mut().flatten() {
            s.reset();
        }
    }
}

/// A knob's position 0..1 from a percentage.
#[inline]
fn pct(v: f32) -> f32 {
    0.01 * v
}

/// Index of the choice as a pole count: 6, 12, 18 or 24 dB/oct (`SLOPES`).
#[inline]
fn poles_of(index: usize) -> usize {
    index + 1
}

/// The slope choices of the feedback filters.
const SLOPES: &[&str] = &["6", "12", "18", "24"];
