//! The X32's FX devices (Phase 4 step 9, `docs/UNIVERSAL_MIXER.md`): its 61
//! models ([`super::FX_MODELS`]), described with the parameters the public OSC
//! protocol document lists for each (pp. 85-115) and the factory values of a
//! freshly initialised X32 Compact.
//!
//! The algorithms land in batches (9d-9g); [`BUILT`] are the models with one
//! (most reverbs and modulation effects run on the WING's devices of the
//! same design, `wing::fx::x32_voices`, the combinations on two or three of
//! them, `wing::fx::x32_combos`; Stereo Delay is the WING's with a factor and
//! a feedback per side; the tap delays are `wing::fx::delay::x32taps`). Until a model's does, its device holds its settings
//! and keeps the signal as it was: an insert model (FX 5-8's) passes it
//! through, a send / return model (reverbs, delays, modulation, pitch)
//! returns silence, so a bus feeding it adds nothing.

use crate::model::{ModelCategory, Params};
use crate::wing::fx::{FxAlgo, FxContext};

/// The models whose algorithm has landed (created before this module's placeholders).
pub const BUILT: &[&str] = &[
    "X32HALL", "X32VRM", "X32DLY", "X32CRS", "X32AMBI", "X32ROOM", "X32CHAM", "X32PLAT", "X32RPLT", "X32VREV", "X32GATE",
    "X32RVRS", "X32FLNG", "X32PHAS", "X32DIMC", "X32FILT", "X32ROTA", "X32PAN", "X32PIT", "X32PIT2", "X32SUB", "X323TAP",
    "X324TAP", "X32D/RV", "X32CR/R", "X32FL/R", "X32D/CR", "X32D/FL", "X32MODD", "X32GEQ2", "X32GEQ", "X32TEQ2", "X32TEQ",
    "X32DES2", "X32DES", "X32LIM", "X32CMB", "X32CMB2", "X32ENH2", "X32ENH", "X32EXC2", "X32EXC", "X32AMP2", "X32AMP", "X32FAC",
    "X32FAC2", "X32FAC1M", "X32LEC", "X32LEC2", "X32ULC", "X32ULC2", "X32WAVD",
];

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
