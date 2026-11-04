//! The X32's FX devices (Phase 4 step 9, `docs/UNIVERSAL_MIXER.md`): its 61
//! models ([`super::FX_MODELS`]), described with the parameters the public OSC
//! protocol document lists for each (pp. 85-115) and the factory values of a
//! freshly initialised X32 Compact.
//!
//! The algorithms land in batches (9d-9g); [`BUILT`] are the models with one
//! (Hall, Vintage Room and Stereo Chorus run on the WING's devices,
//! `wing::fx::x32_voices`; Stereo Delay is the WING's with a factor and a
//! feedback per side). Until a model's does, its device holds its settings
//! and keeps the signal as it was: an insert model (FX 5-8's) passes it
//! through, a send / return model (reverbs, delays, modulation, pitch)
//! returns silence, so a bus feeding it adds nothing.

use crate::model::{ModelCategory, Params};
use crate::wing::fx::{FxAlgo, FxContext};

/// The models whose algorithm has landed (created before this module's placeholders).
pub const BUILT: &[&str] = &["X32HALL", "X32VRM", "X32DLY", "X32CRS"];

/// Creates the placeholder device of X32 model `token` (`X32HALL`...); `None`
/// for other tokens.
pub fn create(token: &str, _ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    let spec = super::FX_MODELS.iter().find(|m| m.token == token && m.token.starts_with("X32"))?;
    Some(Box::new(Placeholder { silent: spec.category == ModelCategory::FxPremium }))
}

/// A model whose algorithm hasn't landed yet.
struct Placeholder {
    silent: bool,
}

impl FxAlgo for Placeholder {
    fn set_params(&mut self, _p: &Params) {}

    fn process(&mut self, left: &mut [f32], right: &mut [f32]) {
        if self.silent {
            left.fill(0.0);
            right.fill(0.0);
        }
    }

    fn reset(&mut self) {}
}
