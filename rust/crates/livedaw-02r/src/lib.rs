//! The Yamaha 02R for Sidecar (Phase 6, `docs/UNIVERSAL_MIXER.md`): what the
//! console is beyond the engine's strips, and how it is addressed.
//!
//! * [`state`]: what the 02R has and the engine doesn't: the channel pairs,
//!   the fader groups A-D and mute groups E-H, the pan gangs, the direct outs,
//!   the dynamics processors' keys, recall safe and fade times, the
//!   oscillator; written and read as text so a show keeps it.
//! * [`console`]: the console over the engine: the operations of its panel
//!   (a pair, a group's faders, a mute group's ON keys, a gang's pans) that
//!   move several of the engine's values at once, and its values as the
//!   console has them (fader steps 0-127, pans 0-32, attenuation in dB, EQ
//!   and dynamics steps).
//! * [`library`]: its EQ, dynamics and effects libraries, 40 presets each and
//!   user programs.
//!
//! The console's strips are the engine's: MIC/LINE 1-16 channels 1-16, LINE
//! 17/18-23/24 channels 17-20, TAPE 1-16 channels 21-36, EFF1-2 aux 1-2, BUS
//! 1-8 buses 1-8, AUX 1-8 buses 9-16, STEREO main 1.
//!
//! Clean-room: built from Yamaha's public 02R User's Guides (Version 1 and
//! 2); no Yamaha firmware or software is used.

pub mod console;
pub mod library;
pub mod state;

pub use console::{Console, Origin, Y2Strip};
pub use library::Library;
pub use state::State;
