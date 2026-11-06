//! Behringer X32 interoperability for Sidecar (Phase 4 step 5,
//! `docs/UNIVERSAL_MIXER.md`): an OSC server on UDP 10023 that answers like an
//! X32, so X32 apps can control a console configured as one.
//!
//! * [`value`]: the X32's parameter types, its value steps and laws, and the
//!   text forms of `/node` and `/`.
//! * [`map`]: its parameter tree mapped onto the engine's strips; values
//!   without an equivalent are kept by the server.
//! * [`meters`]: the meter replies.
//! * [`oscillator`]: its oscillator, on the engine's console oscillator.
//! * [`scn`]: scene files (`.scn`), written and read.
//! * [`sysex`]: OSC commands as text in MIDI SysEx.
//! * [`server`]: the server itself.
//!
//! The OSC codec and the engine handle are `livedaw-wing`'s.
//!
//! Clean-room: built from P.-G. Maillot's public "Unofficial X32/M32 OSC
//! Remote Protocol" and replies of a real console to read-only requests; no
//! Behringer firmware or X32-Edit code is used.

pub mod map;
pub mod meters;
pub mod oscillator;
pub mod scn;
pub mod server;
pub mod sysex;
pub mod value;

pub use map::{Console, Found, LinkFollower, Target, Tree};
pub use server::{Config, Identity, Status, X32Server, OSC_PORT};
pub use value::{Fmt, Law, Ty, Val};
