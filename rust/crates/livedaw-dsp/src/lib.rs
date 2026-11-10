//! Realtime-safe DSP building blocks for Sidecar.
//!
//! Nothing in this crate allocates, locks or panics on the audio path.
//! Processing models are described in console-neutral terms in [`model`]; the
//! WING's processing (filter, EQ models, gate and dynamics models) lives in
//! [`wing`]; it is built from the biquads, delay lines and helpers here.

pub mod biquad;
pub mod delay;
pub mod denormals;
pub mod dlive;
pub mod dlive_fx;
pub mod dm2000;
pub mod fader;
pub mod generator;
pub mod midas_pro;
pub mod model;
pub mod promix;
pub mod sq;
pub mod surround;
pub mod universal;
pub mod util;
pub mod wing;
pub mod x32;
pub mod xenyx1204;
pub mod y01v;
pub mod y02r;

pub use biquad::{Biquad, BiquadCoeffs};
