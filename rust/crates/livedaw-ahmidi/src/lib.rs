//! MIDI over TCP for the Allen & Heath dLive and SQ-5, so third-party controllers
//! and apps can drive Sidecar as they would the consoles.
//!
//! From the makers' public documents: the dLive MIDI Over TCP/IP Protocol V2.0
//! (TCP 51325 to the MixRack, 51328 to the Surface; its TLS ports are not built) and
//! the SQ MIDI Protocol, Issue 5 (TCP 51325). Clean-room: nothing of the consoles'
//! firmware or software.
//!
//! [`stream`] turns a TCP byte stream into MIDI messages, a partial message or
//! SysEx at a time, with running status. [`dlive`] is the dLive's protocol: every
//! message of the document parsed and built, what the engine has a counterpart for
//! mapped to it ([`dlive::Midi`]). [`sq`] runs the SQ's (`livedaw-sq`) over the same
//! stream. [`server`] is the listener: a thread per connection, each with its own
//! parser and its own record of what its client has heard, so changes from
//! anywhere reach every client but the one that made them.
//!
//! Nothing here runs on the audio thread: the server reads and writes the engine's
//! shared atomics like the UI does.

pub mod dlive;
pub mod server;
pub mod sq;
pub mod stream;

pub use server::{Config, Protocol, Server, Setup, Status, MAX_CLIENTS, MIXRACK_PORT, SURFACE_PORT};

/// What a client asks of the app, on the message thread (the show lives there).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Event {
    /// Recall scene `n`, from 1.
    RecallScene(u16),
}
