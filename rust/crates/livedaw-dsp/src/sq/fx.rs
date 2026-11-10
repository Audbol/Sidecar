//! The SQ-5's RackFX (Reference Guide V1.6.0, section 10.7 pp.64-65, its panels'
//! pictures): the eleven RackExtra types (SMR's four models are four devices
//! here), each with the controls its panel shows that its algorithm can act on.
//!
//! The algorithms run on the WING's devices of the same kind
//! (`wing::fx::sq_voices`): Sidecar's reading, to be compared with a console.
//! Not modelled: SMR's HF SLOPE, the delay's WIDTH, LINK and tap tempo, the
//! choruses' and ADT's autopan, the flanger's LFO shapes. Defaults are the
//! panels' pictures' values where they show one, else Sidecar's.
//!
//! The RackFX add-ons (p.65: MultiBD Compressor 3 and 4, DynEQ4, De-Esser, Bucket
//! Brigade, Echo, Hypabass) take the dLive's RackExtra models of the same name
//! whole, their controls too ([`crate::dlive_fx`]): the guide names them and
//! gives no panels or ranges, and the two consoles' models share a maker. That
//! they match is Sidecar's reading. Sidecar has them all enabled: there is no key
//! to buy (Utility > General > Add-ons).

use super::p;
use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec};

const LOG: ParamKind = ParamKind::Log;
const LIN: ParamKind = ParamKind::Linear;

/// Spatial Modelling Reverb: Predelay, Decay Time, HF Decay, LF Cut and HF Cut
/// (the panel's EMT 250: 23 ms, 1.54 s, 1.65 kHz, 161 Hz, 7.53 kHz).
static SMR: [ParamSpec; 5] = [
    p("pre", "PREDELAY", "ms", LIN, 0.0, 200.0, 201, 23.0),
    p("decay", "DECAY", "s", LOG, 0.3, 25.0, 101, 1.54),
    p("hfd", "HF DECAY", "Hz", LOG, 1000.0, 20000.0, 101, 1650.0),
    p("lc", "LF CUT", "Hz", LOG, 20.0, 400.0, 101, 161.0),
    p("hc", "HF CUT", "Hz", LOG, 200.0, 20000.0, 101, 7530.0),
];

/// Stereo Tap Delay: the time, feedback 0-10, the input and feedback filters.
static TAP: [ParamSpec; 5] = [
    p("time", "TIME", "ms", LOG, 1.0, 3000.0, 301, 333.0),
    p("feed", "FEEDBACK", "", LIN, 0.0, 10.0, 101, 3.0),
    p("lc", "LO CUT", "Hz", LOG, 20.0, 400.0, 101, 100.0),
    p("hc", "HI CUT", "Hz", LOG, 200.0, 20000.0, 101, 10000.0),
    p("fb", "FB FILTER", "Hz", LOG, 200.0, 20000.0, 101, 8000.0),
];

/// ADT Doubler: double or quad, delay separation and thickness.
static ADT: [ParamSpec; 3] = [
    p("mode", "MODE", "", ParamKind::Choice(&["DOUBLE", "QUAD"]), 0.0, 1.0, 2, 0.0),
    p("sep", "SEPARATION", "%", LIN, 0.0, 100.0, 101, 50.0),
    p("thick", "THICKNESS", "%", LIN, 0.0, 100.0, 101, 50.0),
];

/// Chorus: rate, depth and the stereo field.
static CHORUS: [ParamSpec; 3] = [
    p("rate", "RATE", "Hz", LOG, 0.05, 5.0, 101, 0.5),
    p("depth", "DEPTH", "%", LIN, 0.0, 100.0, 101, 30.0),
    p("field", "STEREO FIELD", "", ParamKind::Choice(&["MONO", "LFO SPLIT", "MULTI VOICE"]), 0.0, 2.0, 3, 1.0),
];

/// Symphonic Chorus: frequency and depth.
static SYMPHONIC: [ParamSpec; 2] =
    [p("freq", "FREQUENCY", "Hz", LOG, 0.1, 5.0, 101, 0.6), p("depth", "DEPTH", "%", LIN, 0.0, 100.0, 101, 40.0)];

/// Flanger: speed, depth, resonate, stereo split and the emulated type.
static FLANGER: [ParamSpec; 5] = [
    p("speed", "SPEED", "Hz", LOG, 0.05, 5.0, 101, 0.3),
    p("depth", "DEPTH", "%", LIN, 0.0, 100.0, 101, 60.0),
    p("reso", "RESONATE", "%", LIN, 0.0, 90.0, 91, 40.0),
    p("split", "STEREO SPLIT", "deg", LIN, 0.0, 180.0, 181, 90.0),
    p("type", "TYPE", "", ParamKind::Choice(&["AMBIENT", "VINTAGE", "WILD"]), 0.0, 2.0, 3, 1.0),
];

/// MOO 12 Stage Phaser: rate, depth, offset and resonance.
static PHASER: [ParamSpec; 4] = [
    p("rate", "RATE", "Hz", LOG, 0.05, 5.0, 101, 0.8),
    p("depth", "DEPTH", "%", LIN, 0.0, 100.0, 101, 50.0),
    p("offset", "OFFSET", "", LIN, 2.0, 98.0, 97, 20.0),
    p("reso", "RESONANCE", "%", LIN, 0.0, 80.0, 81, 20.0),
];

/// Gated Verb: predelay, attack, hold, release, diffuse, the decay filters and
/// its variant.
static GATED: [ParamSpec; 8] = [
    p("pre", "PREDELAY", "ms", LIN, 0.0, 200.0, 201, 8.0),
    p("att", "ATTACK", "ms", LIN, 0.0, 30.0, 31, 6.0),
    p("hold", "HOLD", "ms", LIN, 20.0, 500.0, 97, 150.0),
    p("rel", "RELEASE", "ms", LIN, 20.0, 500.0, 97, 150.0),
    p("diff", "DIFFUSE", "", LIN, 1.0, 30.0, 30, 30.0),
    p("lc", "LO CUT", "Hz", LOG, 20.0, 400.0, 101, 80.0),
    p("hc", "HI CUT", "Hz", LOG, 200.0, 20000.0, 101, 5000.0),
    p("type", "TYPE", "", ParamKind::Choice(&["GATED", "PANNED", "POWERBOX"]), 0.0, 2.0, 3, 0.0),
];

const fn fx(token: &'static str, name: &'static str, params: &'static [ParamSpec]) -> ModelSpec {
    ModelSpec { token, name, category: ModelCategory::FxPremium, params }
}

/// The first add-on (p.65) in [`FX_MODELS`].
pub const FIRST_ADDON: usize = 13;

/// An add-on's token, and the dLive model it runs on.
pub const ADDONS: [(&str, &str); 7] = [
    ("SQMB4", "DLVMB4"),
    ("SQMB3", "DLVMB3"),
    ("SQDEQ4", "DLVDYNEQ"),
    ("SQDEES", "DLVDEES"),
    ("SQBBD", "DLVBBD"),
    ("SQECHO", "DLVECHO"),
    ("SQHYPA", "DLVHYPA"),
];

/// The RackFX, after an empty slot and EXT: the RackExtra types, then the add-ons.
pub static FX_MODELS: [ModelSpec; 20] = [
    crate::wing::FX_MODELS[0],
    crate::wing::FX_MODELS[1],
    fx("SQSMRC", "SMR Classic", &SMR),
    fx("SQSMRH", "SMR Hall", &SMR),
    fx("SQSMRR", "SMR Room", &SMR),
    fx("SQSMRE", "SMR EMT", &SMR),
    fx("SQTAP", "Stereo Tap Delay", &TAP),
    fx("SQADT", "ADT Doubler", &ADT),
    fx("SQCHOR", "Chorus", &CHORUS),
    fx("SQSYMP", "Symphonic Chorus", &SYMPHONIC),
    fx("SQFLNG", "Flanger", &FLANGER),
    fx("SQPHAS", "MOO 12 Stage Phaser", &PHASER),
    fx("SQGATE2", "Gated Verb", &GATED),
    fx("SQMB4", "MultiBD Compressor 4", crate::dlive_fx::FX_MODELS[23].params),
    fx("SQMB3", "MultiBD Compressor 3", crate::dlive_fx::FX_MODELS[22].params),
    fx("SQDEQ4", "DynEQ4", crate::dlive_fx::FX_MODELS[21].params),
    fx("SQDEES", "De-Esser", crate::dlive_fx::FX_MODELS[17].params),
    fx("SQBBD", "Bucket Brigade", crate::dlive_fx::FX_MODELS[7].params),
    fx("SQECHO", "Echo", crate::dlive_fx::FX_MODELS[9].params),
    fx("SQHYPA", "Hypabass", crate::dlive_fx::FX_MODELS[16].params),
];

/// The SQ-5 effect types' tokens.
pub fn is_sq(token: &str) -> bool {
    token.starts_with("SQ") && FX_MODELS.iter().any(|m| m.token == token)
}
