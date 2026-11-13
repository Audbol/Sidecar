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

/// The Yamaha ProMix 01's port groups (Phase 5), in the WING's places as the
/// X32's are. Its interface inputs: 1-16 the channels' (LOCAL IN), 17-18 ST
/// IN L / R (AUX IN's place), 19-20 2TR IN (AES/EBU IN's); outputs: 1-2 ST OUT
/// (LOCAL OUT's), 3-4 SEND3-4 (AUX OUT's), 5-6 REC OUT (WLIVE REC's), 7-8
/// MONITOR OUT (ST CONNECT's), 9-10 the coaxial DIGITAL OUT (AES/EBU OUT's)
/// (User's Guide pp.5-7, p.106). Its sources: the oscillator, SEND1-4 and ST
/// OUT; it has no network, card or USB ports.
pub static PROMIX01_PORT_GROUPS: &[PortGroup] = &[
    PortGroup::new("INPUT", "IN", IN, Some(0), 16),
    PortGroup::new("ST IN", "ST", IN, Some(16), 2),
    PortGroup::new("2TR IN", "2TR", IN, Some(18), 2),
    PortGroup::new("OSCILLATOR", "OSC", IN, Some(OSC_SOURCE_BASE), NUM_OSCILLATORS as i32),
    PortGroup::new("AES50 A", "A", BOTH, None, 0),
    PortGroup::new("AES50 B", "B", BOTH, None, 0),
    PortGroup::new("AES50 C", "C", BOTH, None, 0),
    PortGroup::new("MONITOR OUT", "MON", OUT, Some(6), 2),
    PortGroup::new("USB AUDIO", "USB", BOTH, None, 0),
    PortGroup::new("WLIVE PLAY", "PLAY", IN, Some(PLAYBACK_SOURCE_BASE), NUM_PLAYBACK_CHANNELS as i32),
    PortGroup::new("DANTE", "DAN", BOTH, None, 0),
    PortGroup::new("USB PLAYER", "PLAYER", IN, Some(USB_PLAYER_SOURCE_BASE), USB_CHANNELS as i32),
    PortGroup::new("SEND", "SEND", IN, Some(BUS_SOURCES), 2 * 4),
    PortGroup::new("ST OUT", "ST", IN, Some(MAIN_SOURCES), 2),
    PortGroup::new("MATRIX", "MTX", IN, None, 0),
    PortGroup::new("USER SIGNAL", "USR", IN, None, 0),
    PortGroup::new("MONITOR", "MON", IN, None, 0),
    PortGroup::new("FX SEND", "FX", IN, None, 0),
    PortGroup::new("ST OUT", "ST", OUT, Some(0), 2),
    PortGroup::new("SEND OUT", "SEND", OUT, Some(2), 2),
    PortGroup::new("DIGITAL OUT", "DIG", OUT, Some(8), 2),
    PortGroup::new("REC OUT", "REC", OUT, Some(4), 2),
    PortGroup::new("RECORDER", "REC", OUT, None, USB_CHANNELS as i32),
    PortGroup::new("CH DIRECT", "CH", IN, None, 0),
    PortGroup::new("BUS DIRECT", "BUS", IN, None, 0),
];

/// The Yamaha 02R's port groups (Phase 6), in the WING's places as the
/// X32's are. Its interface inputs: 1-16 MIC/LINE, 17-24 LINE (LOCAL IN), 25-28
/// 2TR IN D1 and D2 (AUX IN's place), the four I/O card slots' 8 inputs each
/// from 33 (AES50 A, B, C and ST CONNECT's places: slot 1 the tape returns
/// 1-8, slot 2 9-16); outputs: 1-2 STEREO OUT (LOCAL OUT's), 3-8 AUX SEND 1-6
/// (AUX OUT's), 9-12 2TR OUT D1 and D2 (AES/EBU OUT's), the slots' 8 outputs
/// each from 33 (MAN pp.11-15, p.39). Its sources: the oscillator, BUS 1-8,
/// AUX 1-8 and STEREO; it has no network or USB ports.
pub static Y02R_PORT_GROUPS: &[PortGroup] = &[
    PortGroup::new("INPUT", "IN", IN, Some(0), 24),
    PortGroup::new("2TR IN", "2TR", IN, Some(24), 4),
    PortGroup::new("AES/EBU IN", "AES", IN, None, 0),
    PortGroup::new("OSCILLATOR", "OSC", IN, Some(OSC_SOURCE_BASE), NUM_OSCILLATORS as i32),
    PortGroup::new("SLOT 1", "S1", BOTH, Some(32), 8),
    PortGroup::new("SLOT 2", "S2", BOTH, Some(40), 8),
    PortGroup::new("SLOT 3", "S3", BOTH, Some(48), 8),
    PortGroup::new("SLOT 4", "S4", BOTH, Some(56), 8),
    PortGroup::new("USB AUDIO", "USB", BOTH, None, 0),
    PortGroup::new("WLIVE PLAY", "PLAY", IN, Some(PLAYBACK_SOURCE_BASE), NUM_PLAYBACK_CHANNELS as i32),
    PortGroup::new("DANTE", "DAN", BOTH, None, 0),
    PortGroup::new("USB PLAYER", "PLAYER", IN, Some(USB_PLAYER_SOURCE_BASE), USB_CHANNELS as i32),
    PortGroup::new("BUS / AUX", "BUS", IN, Some(BUS_SOURCES), 2 * 16),
    PortGroup::new("STEREO", "ST", IN, Some(MAIN_SOURCES), 2),
    PortGroup::new("MATRIX", "MTX", IN, None, 0),
    PortGroup::new("USER SIGNAL", "USR", IN, None, 0),
    PortGroup::new("MONITOR", "MON", IN, None, 0),
    PortGroup::new("FX SEND", "FX", IN, None, 0),
    PortGroup::new("STEREO OUT", "ST", OUT, Some(0), 2),
    PortGroup::new("AUX SEND", "AUX", OUT, Some(2), 6),
    PortGroup::new("2TR OUT", "2TR", OUT, Some(8), 4),
    PortGroup::new("WLIVE REC", "REC", OUT, None, 0),
    PortGroup::new("RECORDER", "REC", OUT, None, USB_CHANNELS as i32),
    PortGroup::new("CH DIRECT", "CH", IN, None, 0),
    PortGroup::new("BUS DIRECT", "BUS", IN, None, 0),
];

/// The Yamaha 01V's port groups, in the WING's order: its inputs 1-16, the
/// Option I/O card's 8 channels in and out (engine inputs and outputs 17-24),
/// 2TR IN and DIGITAL STEREO IN; outputs 1-2 STEREO OUT, 3-6 OMNI OUT 1-4, 7-8
/// 2TR OUT, 9-10 DIGITAL STEREO OUT (Owner's Manual pp.20-23). Its sources: the
/// oscillator, BUS 1-4, AUX 1-4 and STEREO.
pub static Y01V_PORT_GROUPS: &[PortGroup] = &[
    PortGroup::new("INPUT", "IN", IN, Some(0), 16),
    PortGroup::new("2TR IN", "2TR", IN, Some(24), 2),
    PortGroup::new("DIGITAL IN", "DIG", IN, Some(26), 2),
    PortGroup::new("OSCILLATOR", "OSC", IN, Some(OSC_SOURCE_BASE), NUM_OSCILLATORS as i32),
    PortGroup::new("OPTION I/O", "OPT", BOTH, Some(16), 8),
    PortGroup::new("SLOT 2", "S2", BOTH, None, 0),
    PortGroup::new("SLOT 3", "S3", BOTH, None, 0),
    PortGroup::new("SLOT 4", "S4", BOTH, None, 0),
    PortGroup::new("USB AUDIO", "USB", BOTH, None, 0),
    PortGroup::new("WLIVE PLAY", "PLAY", IN, Some(PLAYBACK_SOURCE_BASE), NUM_PLAYBACK_CHANNELS as i32),
    PortGroup::new("DANTE", "DAN", BOTH, None, 0),
    PortGroup::new("USB PLAYER", "PLAYER", IN, Some(USB_PLAYER_SOURCE_BASE), USB_CHANNELS as i32),
    PortGroup::new("BUS / AUX", "BUS", IN, Some(BUS_SOURCES), 2 * 8),
    PortGroup::new("STEREO", "ST", IN, Some(MAIN_SOURCES), 2),
    PortGroup::new("MATRIX", "MTX", IN, None, 0),
    PortGroup::new("USER SIGNAL", "USR", IN, None, 0),
    PortGroup::new("MONITOR", "MON", IN, None, 0),
    PortGroup::new("FX SEND", "FX", IN, None, 0),
    PortGroup::new("STEREO OUT", "ST", OUT, Some(0), 2),
    PortGroup::new("OMNI OUT", "OMNI", OUT, Some(2), 4),
    PortGroup::new("2TR / DIGITAL", "2TR", OUT, Some(6), 4),
    PortGroup::new("WLIVE REC", "REC", OUT, None, 0),
    PortGroup::new("RECORDER", "REC", OUT, None, USB_CHANNELS as i32),
    PortGroup::new("CH DIRECT", "CH", IN, None, 0),
    PortGroup::new("BUS DIRECT", "BUS", IN, None, 0),
];

/// The DM2000's inputs and outputs, in the engine's numbering (V2 Owner's Manual
/// p.17: AD 1-24 first; each slot's 16 inputs and outputs as one group).
pub mod dm2000_ports {
    pub const OMNI: i32 = 2;
    pub const SLOT: i32 = 24;
    pub const DIGITAL_2TR: i32 = 120;
    pub const ANALOG_2TR: i32 = 126;
}

/// The Yamaha DM2000's port groups, in the WING's order: AD 1-24, 2TR analog in and
/// digital in, the oscillator, six slots of 16 in and out, BUS 1-8 and AUX 1-8 (AUX
/// 9-12 at USER SIGNAL's place), STEREO as sources; STEREO OUT, OMNI OUT 1-8 and the
/// 2TR digital outs (Owner's Manual pp.47-50, the block diagram).
pub static DM2000_PORT_GROUPS: &[PortGroup] = &[
    PortGroup::new("AD IN", "AD", IN, Some(0), 24),
    PortGroup::new("2TR ANALOG", "2TA", IN, Some(dm2000_ports::ANALOG_2TR), 4),
    PortGroup::new("2TR DIGITAL", "2TD", IN, Some(dm2000_ports::DIGITAL_2TR), 6),
    PortGroup::new("OSCILLATOR", "OSC", IN, Some(OSC_SOURCE_BASE), NUM_OSCILLATORS as i32),
    PortGroup::new("SLOT 1", "S1", BOTH, Some(dm2000_ports::SLOT), 16),
    PortGroup::new("SLOT 2", "S2", BOTH, Some(dm2000_ports::SLOT + 16), 16),
    PortGroup::new("SLOT 3", "S3", BOTH, Some(dm2000_ports::SLOT + 32), 16),
    PortGroup::new("SLOT 4", "S4", BOTH, Some(dm2000_ports::SLOT + 48), 16),
    PortGroup::new("SLOT 5", "S5", BOTH, Some(dm2000_ports::SLOT + 64), 16),
    PortGroup::new("WLIVE PLAY", "PLAY", IN, Some(PLAYBACK_SOURCE_BASE), NUM_PLAYBACK_CHANNELS as i32),
    PortGroup::new("SLOT 6", "S6", BOTH, Some(dm2000_ports::SLOT + 80), 16),
    PortGroup::new("USB PLAYER", "PLAYER", IN, Some(USB_PLAYER_SOURCE_BASE), USB_CHANNELS as i32),
    PortGroup::new("BUS / AUX", "BUS", IN, Some(BUS_SOURCES), 2 * 16),
    PortGroup::new("STEREO", "ST", IN, Some(MAIN_SOURCES), 2),
    PortGroup::new("MATRIX", "MTX", IN, None, 0),
    PortGroup::new("AUX 9-12", "AUX", IN, Some(EXTRA_BUS_SOURCES), 2 * 4),
    PortGroup::new("MONITOR", "MON", IN, None, 0),
    PortGroup::new("FX SEND", "FX", IN, None, 0),
    PortGroup::new("STEREO OUT", "ST", OUT, Some(0), 2),
    PortGroup::new("OMNI OUT", "OMNI", OUT, Some(dm2000_ports::OMNI), 8),
    PortGroup::new("2TR OUT", "2TD", OUT, Some(dm2000_ports::DIGITAL_2TR), 6),
    PortGroup::new("WLIVE REC", "REC", OUT, None, 0),
    PortGroup::new("RECORDER", "REC", OUT, None, USB_CHANNELS as i32),
    PortGroup::new("CH DIRECT", "CH", IN, None, 0),
    PortGroup::new("BUS DIRECT", "BUS", IN, None, 0),
];

/// The PRO2's inputs and outputs, in the engine's numbering (Owner's Manual pp.11,
/// 305). DL251's 48 mic/line inputs first; the surface's 8 inputs after them (p.305
/// lists them as outputs, a misprint: the other tables have "Surface I/O"); the
/// surface's two AES3 inputs. Outputs: the surface's master XLRs, DL251's 16, the
/// surface's 8 line outputs with its 4 monitor outputs.
pub mod pro2_ports {
    pub const DL251_IN: i32 = 0;
    pub const SURFACE_IN: i32 = 48;
    pub const AES_IN: i32 = 56;
    pub const MASTER_OUT: i32 = 0;
    pub const DL251_OUT: i32 = 2;
    pub const SURFACE_OUT: i32 = 18;
    pub const MONITOR_OUT: i32 = 26;
}

/// The XENYX Q1202USB's inputs and outputs, in the engine's numbering (the panel's jacks
/// and the USB interface; Sidecar's own order). Inputs: MIC/LINE 1-4, LINE 5/6 to 11/12
/// (L, R each; eight jacks), the 2-TR input (L, R), USB from the computer (L, R).
/// Outputs: MAIN L, R, FX SEND, CTRL ROOM L, R, the 2-TR output (L, R), USB to the
/// computer (L, R), PHONES L, R.
pub mod xenyx1202_ports {
    pub const MIC_LINE: i32 = 0;
    pub const LINE: i32 = 4;
    pub const TWO_TR_IN: i32 = 12;
    pub const USB_IN: i32 = 14;
    pub const MAIN_OUT: i32 = 0;
    pub const FX_OUT: i32 = 2;
    pub const CTRL_OUT: i32 = 3;
    pub const TWO_TR_OUT: i32 = 5;
    pub const USB_OUT: i32 = 7;
    pub const PHONES_OUT: i32 = 9;
}

/// The XENYX Q1202USB's port groups.
pub static XENYX1202_PORT_GROUPS: &[PortGroup] = &[
    PortGroup::new("MIC/LINE IN", "MIC", IN, Some(xenyx1202_ports::MIC_LINE), 4),
    PortGroup::new("LINE IN", "LIN", IN, Some(xenyx1202_ports::LINE), 8),
    PortGroup::new("2-TR IN", "2TR", IN, Some(xenyx1202_ports::TWO_TR_IN), 2),
    PortGroup::new("USB IN", "USB", IN, Some(xenyx1202_ports::USB_IN), 2),
    PortGroup::new("OSCILLATOR", "OSC", IN, Some(OSC_SOURCE_BASE), NUM_OSCILLATORS as i32),
    PortGroup::new("FX SEND", "FX", IN, Some(BUS_SOURCES), 2),
    PortGroup::new("MAIN MIX", "MIX", IN, Some(MAIN_SOURCES), 2),
    PortGroup::new("MAIN OUT", "MAIN", OUT, Some(xenyx1202_ports::MAIN_OUT), 2),
    PortGroup::new("FX SEND OUT", "FX", OUT, Some(xenyx1202_ports::FX_OUT), 1),
    PortGroup::new("CTRL ROOM OUT", "CTRL", OUT, Some(xenyx1202_ports::CTRL_OUT), 2),
    PortGroup::new("2-TR OUT", "2TR", OUT, Some(xenyx1202_ports::TWO_TR_OUT), 2),
    PortGroup::new("USB OUT", "USB", OUT, Some(xenyx1202_ports::USB_OUT), 2),
    PortGroup::new("PHONES", "PH", OUT, Some(xenyx1202_ports::PHONES_OUT), 2),
];

/// The plug-in host's port groups: the interface's inputs and outputs, one pair per channel
/// (the engine's own numbering, ports 1 to the channel count).
macro_rules! plugin_host_groups {
    ($n:expr) => {
        &[PortGroup::new("INTERFACE IN", "IN", IN, Some(0), $n), PortGroup::new("INTERFACE OUT", "OUT", OUT, Some(0), $n)]
    };
}

pub static PLUGINHOST16_PORT_GROUPS: &[PortGroup] = plugin_host_groups!(16);
pub static PLUGINHOST32_PORT_GROUPS: &[PortGroup] = plugin_host_groups!(32);
pub static PLUGINHOST64_PORT_GROUPS: &[PortGroup] = plugin_host_groups!(64);

/// The Midas PRO2's port groups, in the WING's order: its stage box, surface inputs
/// and AES3 in; the talk oscillator's sources; the 16 aux sends, the masters and
/// matrices as sources; the master, DL251 and surface outputs.
pub static PRO2_PORT_GROUPS: &[PortGroup] = &[
    PortGroup::new("DL251 IN", "DL", IN, Some(pro2_ports::DL251_IN), 48),
    PortGroup::new("SURFACE IN", "SRF", IN, Some(pro2_ports::SURFACE_IN), 8),
    PortGroup::new("AES3 IN", "AES", IN, Some(pro2_ports::AES_IN), 2),
    PortGroup::new("OSCILLATOR", "OSC", IN, Some(OSC_SOURCE_BASE), NUM_OSCILLATORS as i32),
    PortGroup::new("SLOT 1", "S1", BOTH, None, 0),
    PortGroup::new("SLOT 2", "S2", BOTH, None, 0),
    PortGroup::new("SLOT 3", "S3", BOTH, None, 0),
    PortGroup::new("SLOT 4", "S4", BOTH, None, 0),
    PortGroup::new("USB AUDIO", "USB", BOTH, None, 0),
    PortGroup::new("WLIVE PLAY", "PLAY", IN, Some(PLAYBACK_SOURCE_BASE), NUM_PLAYBACK_CHANNELS as i32),
    PortGroup::new("DANTE", "DAN", BOTH, None, 0),
    PortGroup::new("USB PLAYER", "PLAYER", IN, Some(USB_PLAYER_SOURCE_BASE), USB_CHANNELS as i32),
    PortGroup::new("AUX SEND", "AUX", IN, Some(BUS_SOURCES), 2 * 16),
    PortGroup::new("MASTER", "MST", IN, Some(MAIN_SOURCES), 2 * 2),
    PortGroup::new("MATRIX", "MTX", IN, Some(MTX_SOURCES), 2 * 8),
    PortGroup::new("USER SIGNAL", "USR", IN, None, 0),
    PortGroup::new("MONITOR", "MON", IN, None, 0),
    PortGroup::new("FX SEND", "FX", IN, None, 0),
    PortGroup::new("MASTER OUT", "MST", OUT, Some(pro2_ports::MASTER_OUT), 2),
    PortGroup::new("DL251 OUT", "DL", OUT, Some(pro2_ports::DL251_OUT), 16),
    PortGroup::new("SURFACE OUT", "SRF", OUT, Some(pro2_ports::SURFACE_OUT), 8),
    PortGroup::new("MONITOR OUT", "MON", OUT, Some(pro2_ports::MONITOR_OUT), 4),
    PortGroup::new("WLIVE REC", "REC", OUT, None, 0),
    PortGroup::new("RECORDER", "REC", OUT, None, USB_CHANNELS as i32),
    PortGroup::new("CH DIRECT", "CH", IN, None, 0),
    PortGroup::new("BUS DIRECT", "BUS", IN, None, 0),
];

/// The Allen & Heath SQ-5's port groups, in the WING's order: its 16 mic inputs
/// and its stereo line inputs ST1-ST3 (engine inputs 16-21), its local outputs
/// (1-12, A and B) and the AES output.
pub static SQ5_PORT_GROUPS: &[PortGroup] = &[
    PortGroup::new("LOCAL IN", "LCL", IN, Some(0), 16),
    PortGroup::new("ST IN", "ST", IN, Some(16), 6),
    PortGroup::new("AES/EBU IN", "AES", IN, None, 0),
    PortGroup::new("SIGNAL GEN", "GEN", IN, Some(OSC_SOURCE_BASE), NUM_OSCILLATORS as i32),
    PortGroup::new("SLINK A", "SL", BOTH, None, 0),
    PortGroup::new("SLINK", "SL", BOTH, Some(WING_BLOCK), WING_BLOCK),
    PortGroup::new("I/O PORT", "IO", BOTH, Some(2 * WING_BLOCK), WING_BLOCK),
    PortGroup::new("ST CONNECT", "SC", BOTH, None, 0),
    PortGroup::new("USB AUDIO", "USB", BOTH, None, 0),
    PortGroup::new("SQ-DRIVE", "PLAY", IN, Some(PLAYBACK_SOURCE_BASE), NUM_PLAYBACK_CHANNELS as i32),
    PortGroup::new("DANTE", "DAN", BOTH, None, 0),
    PortGroup::new("USB PLAYER", "PLAYER", IN, Some(USB_PLAYER_SOURCE_BASE), USB_CHANNELS as i32),
    PortGroup::new("MIX", "MIX", IN, Some(BUS_SOURCES), 2 * 16),
    PortGroup::new("LR", "LR", IN, Some(MAIN_SOURCES), 2),
    PortGroup::new("MATRIX", "MTX", IN, Some(MTX_SOURCES), 2 * 3),
    PortGroup::new("USER SIGNAL", "USR", IN, None, 0),
    PortGroup::new("MONITOR", "MON", IN, None, 0),
    PortGroup::new("FX SEND", "FX", IN, None, 0),
    PortGroup::new("LOCAL OUT", "LCL", OUT, Some(0), 14),
    PortGroup::new("AES OUT", "AES", OUT, Some(14), 2),
    PortGroup::new("AUX OUT", "AUX", OUT, None, 0),
    PortGroup::new("WLIVE REC", "REC", OUT, None, 0),
    PortGroup::new("RECORDER", "REC", OUT, None, USB_CHANNELS as i32),
    PortGroup::new("CH DIRECT", "CH", IN, None, 0),
    PortGroup::new("BUS DIRECT", "BUS", IN, None, 0),
];

/// Where a dLive's sockets sit in the engine's numbering (inputs and outputs
/// alike): the MixRack's from 0, the DX expanders', the Surface's, the I/O Ports'.
pub mod dlive_ports {
    pub const DX_12: i32 = 64;
    pub const DX_34: i32 = 96;
    pub const SURFACE: i32 = 128;
    pub const PORT_1: i32 = 144;
    pub const PORT_2: i32 = 272;
    pub const PORT_3: i32 = 400;
}

/// Buses 17 on (`routing::EXTRA_BUS_SOURCES_AT`).
const EXTRA_BUS_SOURCES: i32 = STRIP_SOURCE_BASE + crate::routing::EXTRA_BUS_SOURCES_AT as i32;

/// The Allen & Heath dLive's port groups, in the WING's places: a DM64's 64
/// inputs and 32 outputs (R-nn), the DX expander ports (DX 1/2 and 3/4, 32 each
/// way), the S-Class Surface's 8 XLR and 2 stereo AES inputs and 8 XLR and 3
/// stereo AES outputs (S-nn), its USB stereo playback (the USB player), the
/// three 128-channel I/O Ports (Pn-nn). The USER SIGNAL place holds the second
/// half of the mixes, which take two groups (a tile per side).
pub static DLIVE_PORT_GROUPS: &[PortGroup] = &[
    PortGroup::new("MIXRACK", "R", IN, Some(0), 64),
    PortGroup::new("SURFACE", "S", IN, Some(dlive_ports::SURFACE), 12),
    PortGroup::new("AES/EBU IN", "AES", IN, None, 0),
    PortGroup::new("SIGNAL GEN", "GEN", IN, Some(OSC_SOURCE_BASE), NUM_OSCILLATORS as i32),
    PortGroup::new("DX 1/2", "DX1", BOTH, Some(dlive_ports::DX_12), 32),
    PortGroup::new("DX 3/4", "DX3", BOTH, Some(dlive_ports::DX_34), 32),
    PortGroup::new("I/O PORT 1", "P1", BOTH, Some(dlive_ports::PORT_1), 128),
    PortGroup::new("I/O PORT 2", "P2", BOTH, Some(dlive_ports::PORT_2), 128),
    PortGroup::new("USB AUDIO", "USB", BOTH, None, 0),
    PortGroup::new("MULTITRACK", "PLAY", IN, Some(PLAYBACK_SOURCE_BASE), NUM_PLAYBACK_CHANNELS as i32),
    PortGroup::new("I/O PORT 3", "P3", BOTH, Some(dlive_ports::PORT_3), 128),
    PortGroup::new("SURFACE USB", "USB", IN, Some(USB_PLAYER_SOURCE_BASE), USB_CHANNELS as i32),
    PortGroup::new("MIX 1-16", "MIX", IN, Some(BUS_SOURCES), 2 * 16),
    PortGroup::new("LR", "LR", IN, Some(MAIN_SOURCES), 2),
    PortGroup::new("MATRIX", "MTX", IN, Some(MTX_SOURCES), 2 * 8),
    PortGroup::new("MIX 17-32", "MIX", IN, Some(EXTRA_BUS_SOURCES), 2 * 16),
    PortGroup::new("MONITOR", "MON", IN, None, 0),
    PortGroup::new("FX SEND", "FX", IN, None, 0),
    PortGroup::new("MIXRACK OUT", "R", OUT, Some(0), 32),
    PortGroup::new("SURFACE OUT", "S", OUT, Some(dlive_ports::SURFACE), 14),
    PortGroup::new("AES OUT", "AES", OUT, None, 0),
    PortGroup::new("WLIVE REC", "REC", OUT, None, 0),
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

    #[test]
    fn the_promix01s_groups_are_its_ports_in_the_wings_places() {
        let (g, w) = (PROMIX01_PORT_GROUPS, WING_PORT_GROUPS);
        assert_eq!(g.len(), w.len());
        for (p, wing) in g.iter().zip(w) {
            assert!(p.input == wing.input || p.first.is_none() || wing.first.is_none(), "{}", p.name);
        }
        for group in g.iter().filter(|g| g.input && g.first.is_some()) {
            for n in 0..group.size {
                let i = group.port(n).unwrap();
                assert!(source_index(i).is_some() || strip_source(i).is_some(), "{} {}", group.prefix, n + 1);
            }
        }
        assert_eq!(group_of(g, false, 15).map(|(g, n)| (g.name, n)), Some(("INPUT", 15)));
        assert_eq!(group_of(g, false, 17).map(|(g, n)| (g.name, n)), Some(("ST IN", 1)));
        assert_eq!(group_of(g, false, 16 + 4), None, "20 inputs");
        assert_eq!(group_of(g, true, 3).map(|(g, n)| (g.name, n)), Some(("SEND OUT", 1)));
        assert_eq!(group_of(g, true, 9).map(|(g, n)| (g.name, n)), Some(("DIGITAL OUT", 1)));
        let send4 = strip_source_input(StripKind::Bus, 3, 0);
        assert_eq!(group_of(g, false, send4).map(|(g, n)| (g.prefix, n)), Some(("SEND", 6)));
    }

    #[test]
    fn the_02rs_groups_are_its_ports_in_the_wings_places() {
        let (g, w) = (Y02R_PORT_GROUPS, WING_PORT_GROUPS);
        assert_eq!(g.len(), w.len());
        for (p, wing) in g.iter().zip(w) {
            assert!(p.input == wing.input || p.first.is_none() || wing.first.is_none(), "{}", p.name);
            assert!(p.output == wing.output || p.first.is_none() || wing.first.is_none(), "{}", p.name);
        }
        for group in g.iter().filter(|g| g.input && g.first.is_some()) {
            for n in 0..group.size {
                let i = group.port(n).unwrap();
                assert!(source_index(i).is_some() || strip_source(i).is_some(), "{} {}", group.prefix, n + 1);
            }
        }
        assert_eq!(group_of(g, false, 23).map(|(g, n)| (g.name, n)), Some(("INPUT", 23)));
        assert_eq!(group_of(g, false, 32).map(|(g, n)| (g.name, n)), Some(("SLOT 1", 0)), "TAPE 1");
        assert_eq!(group_of(g, false, 47).map(|(g, n)| (g.name, n)), Some(("SLOT 2", 7)), "TAPE 16");
        assert_eq!(group_of(g, true, 7).map(|(g, n)| (g.name, n)), Some(("AUX SEND", 5)));
        assert_eq!(group_of(g, true, 63).map(|(g, n)| (g.name, n)), Some(("SLOT 4", 7)));
        let aux8 = strip_source_input(StripKind::Bus, 15, 0);
        assert_eq!(group_of(g, false, aux8).map(|(g, n)| (g.prefix, n)), Some(("BUS", 30)));
    }
}
