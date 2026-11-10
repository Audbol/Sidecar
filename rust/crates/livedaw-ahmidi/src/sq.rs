//! The SQ's MIDI protocol (`livedaw-sq`) as a connection's protocol: one
//! [`livedaw_sq::Midi`] per client, so NRPN state and what each has heard stay its own.

use crate::server::{Protocol, Setup};
use crate::Event;
use livedaw_engine::Engine;
use livedaw_sq::{Midi, Taper};

/// One client of the SQ.
#[derive(Default)]
pub struct Session {
    midi: Midi,
}

impl Protocol for Session {
    fn receive(&mut self, engine: &Engine, msg: &[u8]) {
        self.midi.receive(engine, msg);
    }

    fn poll(&mut self, engine: &Engine) {
        self.midi.poll(engine);
    }

    fn take_out(&mut self) -> Option<Vec<u8>> {
        self.midi.take_out()
    }

    fn take_event(&mut self) -> Option<Event> {
        match self.midi.take_event()? {
            livedaw_sq::Event::RecallScene(n) => Some(Event::RecallScene(n)),
        }
    }

    fn scene_recalled(&mut self, n: u16) {
        self.midi.scene_recalled(n);
    }

    fn set_setup(&mut self, setup: Setup) {
        self.midi.channel = setup.channel & 0x0F;
        self.midi.taper = if setup.audio_taper { Taper::Audio } else { Taper::Linear };
    }
}
