//! The Yamaha 01V for Sidecar (`docs/UNIVERSAL_MIXER.md`): how the console is
//! addressed over MIDI. The mix itself is the engine's (the console has no
//! state of its own yet, unlike the ProMix 01's and the 02R's).
//!
//! * [`params`]: its edit buffer as bytes (Sidecar's addresses, within the
//!   manual's 1014 bytes): what its parameter changes address and its scene
//!   dumps carry.
//! * [`midi`]: its MIDI (Owner's Manual chapter 17 and appendix C): program
//!   changes, control changes through the assign table, SysEx parameter
//!   changes, function calls, key remote, bulk dumps.
//!
//! Clean-room: built from Yamaha's public 01V Owner's Manual; no Yamaha
//! firmware or software is used.

pub mod midi;
pub mod params;
