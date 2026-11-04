//! Port groups: a console's sources and destinations as its routing pages
//! group them ("LOCAL IN", "AES50 B", "BUS"...). The engine numbers its
//! sources and output ports one way for every console (`sources.rs`,
//! `routing.rs`); a profile names the ranges of that numbering its console has
//! (`ProfileSpec::port_groups`, Phase 3 step 3). The WING's are the first.

use crate::params::{WING_BUSES, WING_MAINS, WING_MATRICES};
use crate::routing::STRIP_SOURCE_BASE;
use crate::sources::{NUM_OSCILLATORS, NUM_USR_SOURCES, OSC_SOURCE_BASE, USR_SOURCE_BASE};
use livedaw_recorder::{NUM_PLAYBACK_CHANNELS, PLAYBACK_SOURCE_BASE, USB_CHANNELS, USB_PLAYER_SOURCE_BASE};

/// A group of a console's sources (inputs), destinations (output ports) or
/// both (a network or card's ports go both ways: AES50, DANTE...).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PortGroup {
    /// Its menu name ("LOCAL IN").
    pub name: &'static str,
    /// Its tiles' label prefix ("LCL 3").
    pub prefix: &'static str,
    /// Whether it holds sources, and output ports.
    pub input: bool,
    pub output: bool,
    /// The engine's index of its first port: a source (strip input), and an
    /// output port, both numbered alike; `None` when the engine has no such
    /// ports (the console's tiles show n/a, or the group is the app's: the
    /// recorder's taps, the direct outs).
    pub first: Option<i32>,
    /// Its ports as the console has them, and as many as it can grow to
    /// (the local ports follow the audio interface, in rows of 8).
    pub size: i32,
    pub max_size: i32,
}

impl PortGroup {
    const fn new(name: &'static str, prefix: &'static str, io: (bool, bool), first: Option<i32>, size: i32) -> Self {
        PortGroup { name, prefix, input: io.0, output: io.1, first, size, max_size: size }
    }

    /// Port `n` (0-based) of the group: its engine index, if the engine has it.
    pub fn port(&self, n: i32) -> Option<i32> {
        (0..self.max_size).contains(&n).then_some(self.first? + n)
    }

    /// Where an engine index sits in the group (0-based), if it does.
    pub fn number_of(&self, index: i32) -> Option<i32> {
        let n = index.checked_sub(self.first?)?;
        (0..self.max_size).contains(&n).then_some(n)
    }
}

/// The interface ports in a block of the WING's numbering (LOCAL, AES50 B, AES50 C).
pub const WING_BLOCK: i32 = 48;

const IN: (bool, bool) = (true, false);
const OUT: (bool, bool) = (false, true);
const BOTH: (bool, bool) = (true, true);

const BUS_SOURCES: i32 = STRIP_SOURCE_BASE;
const MAIN_SOURCES: i32 = BUS_SOURCES + 2 * WING_BUSES as i32;
const MTX_SOURCES: i32 = MAIN_SOURCES + 2 * WING_MAINS as i32;

/// The WING's port groups, in the order of its routing menus (and of the
/// app's `routing::Group`, `src/ui/RoutingGroups.h`, which a test compares).
pub static WING_PORT_GROUPS: &[PortGroup] = &[
    PortGroup { max_size: WING_BLOCK, ..PortGroup::new("LOCAL IN", "LCL", IN, Some(0), 24) },
    PortGroup::new("AUX IN", "AUX", IN, None, 8),
    PortGroup::new("AES/EBU IN", "AES", IN, None, 2),
    PortGroup::new("OSCILLATOR", "OSC", IN, Some(OSC_SOURCE_BASE), NUM_OSCILLATORS as i32),
    PortGroup::new("AES50 A", "A", BOTH, None, WING_BLOCK),
    PortGroup::new("AES50 B", "B", BOTH, Some(WING_BLOCK), WING_BLOCK),
    PortGroup::new("AES50 C", "C", BOTH, Some(2 * WING_BLOCK), WING_BLOCK),
    PortGroup::new("ST CONNECT", "SC", BOTH, None, 32),
    PortGroup::new("USB AUDIO", "USB", BOTH, None, 48),
    PortGroup::new("WLIVE PLAY", "PLAY", IN, Some(PLAYBACK_SOURCE_BASE), NUM_PLAYBACK_CHANNELS as i32),
    PortGroup::new("DANTE", "DAN", BOTH, None, 64),
    PortGroup::new("USB PLAYER", "PLAYER", IN, Some(USB_PLAYER_SOURCE_BASE), USB_CHANNELS as i32),
    PortGroup::new("BUS", "BUS", IN, Some(BUS_SOURCES), 2 * WING_BUSES as i32),
    PortGroup::new("MAIN", "MAIN", IN, Some(MAIN_SOURCES), 2 * WING_MAINS as i32),
    PortGroup::new("MATRIX", "MTX", IN, Some(MTX_SOURCES), 2 * WING_MATRICES as i32),
    PortGroup::new("USER SIGNAL", "USR", IN, Some(USR_SOURCE_BASE), NUM_USR_SOURCES as i32),
    PortGroup::new("MONITOR", "MON", IN, None, 4),
    PortGroup::new("FX SEND", "FX", IN, None, 32),
    PortGroup { max_size: WING_BLOCK, ..PortGroup::new("LOCAL OUT", "LCL", OUT, Some(0), 8) },
    PortGroup::new("AUX OUT", "AUX", OUT, None, 8),
    PortGroup::new("AES/EBU OUT", "AES", OUT, None, 2),
    PortGroup::new("WLIVE REC", "REC", OUT, None, 64),
    // The app's taps for the USB recorder, not output ports.
    PortGroup::new("RECORDER", "REC", OUT, None, USB_CHANNELS as i32),
    // The app's direct outs, sources of the OUTPUTS: their tiles are the channels
    // and aux, and the buses.
    PortGroup::new("CH DIRECT", "CH", IN, None, 0),
    PortGroup::new("BUS DIRECT", "BUS", IN, None, 0),
];

/// The X32's port groups (Phase 4 step 10), in the WING's places (the app's
/// menus are laid out by place: `routing::Group`), named and numbered as the
/// X32 numbers its ports (`profile::x32_ports`, `livedaw-x32`'s `map::ports`):
/// inputs local 1-32, AES50 A and B 1-48, the card's 1-32, aux in 1-6; outputs
/// local 1-16, aux out 1-6, AES50 A and B, the card, P16 1-16. The places the
/// X32 has nothing for (AES/EBU in, ST CONNECT's inputs, USB AUDIO, DANTE) are n/a.
pub static X32_PORT_GROUPS: &[PortGroup] = &[
    PortGroup::new("LOCAL IN", "LCL", IN, Some(0), 32),
    PortGroup::new("AUX IN", "AUX", IN, Some(160), 6),
    PortGroup::new("AES/EBU IN", "AES", IN, None, 2),
    PortGroup::new("OSCILLATOR", "OSC", IN, Some(OSC_SOURCE_BASE), NUM_OSCILLATORS as i32),
    PortGroup::new("AES50 A", "A", BOTH, Some(32), WING_BLOCK),
    PortGroup::new("AES50 B", "B", BOTH, Some(80), WING_BLOCK),
    PortGroup::new("CARD", "CARD", BOTH, Some(128), 32),
    PortGroup::new("P16", "P16", OUT, Some(160), 16),
    PortGroup::new("USB AUDIO", "USB", BOTH, None, 32),
    PortGroup::new("WLIVE PLAY", "PLAY", IN, Some(PLAYBACK_SOURCE_BASE), NUM_PLAYBACK_CHANNELS as i32),
    PortGroup::new("DANTE", "DAN", BOTH, None, 64),
    PortGroup::new("USB PLAYER", "PLAYER", IN, Some(USB_PLAYER_SOURCE_BASE), USB_CHANNELS as i32),
    PortGroup::new("BUS", "BUS", IN, Some(BUS_SOURCES), 2 * 16),
    PortGroup::new("MAIN", "MAIN", IN, Some(MAIN_SOURCES), 2 * 2),
    PortGroup::new("MATRIX", "MTX", IN, Some(MTX_SOURCES), 2 * 6),
    PortGroup::new("USER SIGNAL", "USR", IN, Some(USR_SOURCE_BASE), NUM_USR_SOURCES as i32),
    PortGroup::new("MONITOR", "MON", IN, None, 4),
    PortGroup::new("FX SEND", "FX", IN, None, 32),
    PortGroup::new("LOCAL OUT", "LCL", OUT, Some(0), 16),
    PortGroup::new("AUX OUT", "AUX", OUT, Some(16), 6),
    PortGroup::new("AES/EBU OUT", "AES", OUT, None, 2),
    PortGroup::new("WLIVE REC", "REC", OUT, None, 64),
    PortGroup::new("RECORDER", "REC", OUT, None, USB_CHANNELS as i32),
    PortGroup::new("CH DIRECT", "CH", IN, None, 0),
    PortGroup::new("BUS DIRECT", "BUS", IN, None, 0),
];

/// The group (of `groups`) an engine index belongs to, and its number there.
pub fn group_of(groups: &'static [PortGroup], output: bool, index: i32) -> Option<(&'static PortGroup, i32)> {
    groups.iter().filter(|g| if output { g.output } else { g.input }).find_map(|g| Some((g, g.number_of(index)?)))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::routing::{strip_source, strip_source_input};
    use crate::sources::source_index;
    use crate::StripKind;

    #[test]
    fn the_wings_groups_cover_the_engines_numbering() {
        let g = WING_PORT_GROUPS;
        assert_eq!(g.len(), 25);
        // Every source a group names is one the engine has.
        for group in g.iter().filter(|g| g.input && g.first.is_some()) {
            for n in 0..group.size {
                let i = group.port(n).unwrap();
                assert!(source_index(i).is_some() || strip_source(i).is_some(), "{} {}", group.prefix, n + 1);
            }
        }
        assert_eq!(group_of(g, false, 50).map(|(g, n)| (g.prefix, n)), Some(("B", 2)));
        assert_eq!(group_of(g, false, 30).map(|(g, n)| (g.prefix, n)), Some(("LCL", 30)), "local ports up to 48");
        let mtx2r = strip_source_input(StripKind::Matrix, 1, 1); // MTX 2 R: the group's 4th
        assert_eq!(group_of(g, false, mtx2r).map(|(g, n)| (g.prefix, n)), Some(("MTX", 3)));
        assert_eq!(group_of(g, true, 100).map(|(g, n)| (g.name, n)), Some(("AES50 C", 4)));
        assert_eq!(group_of(g, false, 0x7fff_0000), None);
        assert_eq!(g[1].port(0), None, "AUX IN: n/a");
    }

    #[test]
    fn the_x32s_groups_are_its_ports_in_the_wings_places() {
        let (g, w) = (X32_PORT_GROUPS, WING_PORT_GROUPS);
        assert_eq!(g.len(), w.len());
        for (x, wing) in g.iter().zip(w) {
            assert!(x.input == wing.input || x.first.is_none() || wing.first.is_none(), "{}", x.name);
        }
        for group in g.iter().filter(|g| g.input && g.first.is_some()) {
            for n in 0..group.size {
                let i = group.port(n).unwrap();
                assert!(source_index(i).is_some() || strip_source(i).is_some(), "{} {}", group.prefix, n + 1);
            }
        }
        // As livedaw-x32 numbers them: AES50 A 1 is input 33, the card's 1 is 129, aux in 1 161.
        assert_eq!(group_of(g, false, 32).map(|(g, n)| (g.prefix, n)), Some(("A", 0)));
        assert_eq!(group_of(g, false, 128).map(|(g, n)| (g.prefix, n)), Some(("CARD", 0)));
        assert_eq!(group_of(g, false, 160).map(|(g, n)| (g.prefix, n)), Some(("AUX", 0)));
        assert_eq!(group_of(g, true, 17).map(|(g, n)| (g.name, n)), Some(("AUX OUT", 1)));
        assert_eq!(group_of(g, true, 160).map(|(g, n)| (g.name, n)), Some(("P16", 0)));
        let mtx6r = strip_source_input(StripKind::Matrix, 5, 1);
        assert_eq!(group_of(g, false, mtx6r).map(|(g, n)| (g.prefix, n)), Some(("MTX", 11)));
    }
}
