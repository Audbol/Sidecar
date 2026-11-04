//! The Yamaha ProMix 01 for Sidecar (Phase 5, `docs/UNIVERSAL_MIXER.md`): what
//! the console is beyond the engine's strips, and how it is addressed.
//!
//! * [`state`]: what the ProMix 01 has and the engine doesn't: its three
//!   dynamics processors (a pool patched into strips), the channel pairs, the
//!   fader groups, SEND3-4 as a stereo pair, the oscillator; written and read
//!   as text so a show keeps it.
//! * [`console`]: the console over the engine: the operations of its panel
//!   (pairing, grouped faders, patching a processor...) that move several of
//!   the engine's values at once.
//! * [`params`]: its 531 mix parameters (User's Guide Add-5 to Add-15), each
//!   an internal value 0..N-1 read from and written to the console: what its
//!   MIDI control changes carry.
//!
//! The console's strips are the engine's: channels 1-16 its channels, ST IN
//! aux 1, RTN1-2 aux 2-3, SEND1-4 buses 1-4, ST OUT main 1.
//!
//! Clean-room: built from Yamaha's public ProMix 01 User's Guide; no Yamaha
//! firmware or software is used.

pub mod console;
pub mod library;
pub mod midi;
pub mod params;
pub mod state;

pub use console::Console;
pub use state::State;
