//! The dLive's RackUltra FX card: 8 engines with a channel strip of their own
//! ([`strip`]) and 11 models. Clean-room: built from the Firmware Reference Guide's
//! description (chapter DEEP Processing, RackExtra FX and RackUltra FX, see the
//! "RackUltra FX" table in `docs/DLIVE_RESEARCH.md`), not from any firmware.
//!
//! The guide names the controls and gives some numbers, and no algorithm: every
//! algorithm here is Sidecar's reading, and each module says what is stated and what
//! is not. Tuners and harmonisers run on a pitch-synchronous grain voice ([`pitch`])
//! rather than the splicing delay of the RackExtra's pitch models, because Formant
//! has to move independently of pitch.

pub(super) mod delay;
pub(super) mod pitch;
pub(super) mod reverb;
pub mod strip;
#[cfg(test)]
mod tests;
pub(super) mod tone;

use crate::wing::fx::{FxAlgo, FxContext};

/// The RackUltra models' tokens.
pub const TOKENS: [&str; 11] =
    ["DLVSPACE", "DLVPLATE", "DLVRDLY", "DLVSAT", "DLVAMP", "DLVVSHFT", "DLVTUNER", "DLVGRID", "DLVH2", "DLVH4", "DLVHMIDI"];

/// Creates a device (message thread: allocates). `None` for any other token.
pub(super) fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    Some(match token {
        "DLVSPACE" => Box::new(reverb::Designer::spaces(ctx)),
        "DLVPLATE" => Box::new(reverb::Designer::plate(ctx)),
        "DLVRDLY" => Box::new(delay::RhythmDelay::new(ctx)),
        "DLVSAT" => Box::new(tone::Saturator::new(ctx)),
        "DLVAMP" => tone::AmpCab::boxed(ctx)?,
        "DLVVSHFT" => Box::new(pitch::VocalShifter::new(ctx)),
        "DLVTUNER" => Box::new(pitch::VocalTuner::new(ctx)),
        "DLVGRID" => Box::new(pitch::VocalGridder::new(ctx)),
        "DLVH2" => Box::new(pitch::Harmoniser::new(2, true, ctx)),
        "DLVH4" => Box::new(pitch::Harmoniser::new(4, false, ctx)),
        "DLVHMIDI" => Box::new(pitch::MidiHarmoniser::new(ctx)),
        _ => return None,
    })
}
