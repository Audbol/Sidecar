//! Console profiles: the console a configuration emulates and what that
//! console is (its counts, its strips' chains and process orders, its fader
//! and pan laws, its port groups, its processing models, its remote
//! protocols, its colours, its strips' labels). The
//! Universal core takes any configuration; a profile limits and dresses it
//! (`docs/UNIVERSAL_MIXER.md`, Phase 3). The WING is the first; the Universal
//! is no console's limits.

use crate::chain::{self, Node};
use crate::config::Counts;
use crate::params::{StripKind, DEFAULT_COLOUR, WING_BUSES, WING_COLOURS, WING_COLOUR_GRID};
use crate::ports::{PortGroup, WING_PORT_GROUPS};
use livedaw_dsp::fader::FaderLaw;
use livedaw_dsp::model::ModelTables;
use livedaw_dsp::util::PanLaw;
use livedaw_dsp::wing::{MODELS as WING_MODELS, PROCESS_ORDERS};

/// The console a configuration emulates (its profile: `docs/UNIVERSAL_MIXER.md`).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum Profile {
    /// No console's limits: the Universal Mixer's own.
    Universal,
    /// The Behringer WING.
    #[default]
    Wing,
    /// The Behringer X32 (Phase 4: its processing, sends and protocols follow).
    X32,
}

impl Profile {
    pub const ALL: [Profile; 3] = [Profile::Universal, Profile::Wing, Profile::X32];

    /// The id shows store (`<Console profile="...">`).
    pub fn id(self) -> &'static str {
        match self {
            Profile::Universal => "universal",
            Profile::Wing => "wing",
            Profile::X32 => "x32",
        }
    }

    pub fn from_id(id: &str) -> Option<Profile> {
        Self::ALL.into_iter().find(|p| p.id() == id)
    }

    /// What the console is (`ProfileSpec`).
    pub fn spec(self) -> &'static ProfileSpec {
        match self {
            Profile::Universal => &UNIVERSAL,
            Profile::Wing => &WING,
            Profile::X32 => &X32,
        }
    }

    /// The name the interface shows.
    pub fn name(self) -> &'static str {
        self.spec().name
    }

    /// Whether a console of `counts` is one of this profile's: the WING's
    /// aux, mains, matrices, DCAs and mute groups (what its surface
    /// addresses; channels, buses and FX slots can grow on a WING too); the
    /// X32's counts exactly.
    pub fn fits(self, counts: &Counts) -> bool {
        match self {
            Profile::Universal => true,
            Profile::X32 => *counts == self.spec().counts,
            Profile::Wing => {
                let w = self.spec().counts;
                counts.aux == w.aux
                    && counts.buses >= w.buses
                    && counts.mains == w.mains
                    && counts.matrices == w.matrices
                    && counts.dcas == w.dcas
                    && counts.mute_groups == w.mute_groups
            }
        }
    }

    /// This profile if a console of `counts` fits it, else the Universal.
    pub fn or_universal(self, counts: &Counts) -> Profile {
        if self.fits(counts) {
            self
        } else {
            Profile::Universal
        }
    }
}

/// A MIDI protocol: what a console's MIDI messages mean (the app's
/// `MidiProtocol::create` makes it, over the console-neutral `MidiPorts`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MidiProtocol {
    /// The WING's: MIDI REMOTE CONTROL (CH CONTROL, FX PARAMETER CONTROL,
    /// CUSTOM CONTROLS, SYSEX CONTROL, scenes, show control) and the DAW remote.
    Wing,
    /// The X32's fixed map: faders, mutes, pans and mute groups by CC on MIDI
    /// channels 1-3, scenes by program change.
    X32,
}

/// A network protocol a console serves (`livedaw-wing`'s servers for the WING's).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NetworkProtocol {
    /// The WING's discovery (UDP 2222): WING Edit and apps find the console.
    WingDiscovery,
    /// The WING's OSC (UDP 2223).
    WingOsc,
    /// The WING's native binary protocol (TCP 2222).
    WingNative,
    /// The X32's OSC (UDP 10023, `livedaw-x32`).
    X32Osc,
}

impl NetworkProtocol {
    /// The protocol's bit in the C ABI's set (`LdRemotes::network`): the WING
    /// remote's `enable` bits.
    pub fn bit(self) -> u32 {
        match self {
            Self::WingDiscovery => 1,
            Self::WingOsc => 2,
            Self::WingNative => 4,
            Self::X32Osc => 8,
        }
    }
}

/// The remote protocols a console speaks (Phase 3 step 6): the app runs these
/// and no others.
#[derive(Clone, Copy, Debug)]
pub struct Remotes {
    /// Over its MIDI ports (none: the console has no MIDI remote).
    pub midi: Option<MidiProtocol>,
    pub network: &'static [NetworkProtocol],
}

impl Remotes {
    /// The WING's: its MIDI protocol, discovery, OSC and the native protocol.
    pub const WING: Remotes = Remotes {
        midi: Some(MidiProtocol::Wing),
        network: &[NetworkProtocol::WingDiscovery, NetworkProtocol::WingOsc, NetworkProtocol::WingNative],
    };

    /// The network protocols as a set of [`NetworkProtocol::bit`]s.
    pub fn network_bits(&self) -> u32 {
        self.network.iter().fold(0, |bits, p| bits | p.bit())
    }
}

/// The colours a console's strips, sources and DCAs take: a strip's colour is
/// an index (1-based) into `colours`.
#[derive(Clone, Copy, Debug)]
pub struct Palette {
    /// Name and RGB of each colour.
    pub colours: &'static [(&'static str, u32)],
    /// The colour indexes in the order its colour picker shows them.
    pub grid: &'static [i32],
    /// The colour of a new channel, source and DCA.
    pub default: i32,
    /// A new strip's colour by its kind and slot (0-based); `None`: the WING's
    /// factory colours (`StripParams::new_in_slot`).
    pub factory: Option<fn(StripKind, usize) -> i32>,
}

impl Palette {
    /// The WING's 18 (`params::WING_COLOURS`) and ICON/COLOR's grid.
    pub const WING: Palette = Palette { colours: &WING_COLOURS, grid: &WING_COLOUR_GRID, default: DEFAULT_COLOUR, factory: None };

    /// The X32's 16 (`config/color` 0-15 are indexes 1-16): OFF and seven
    /// colours, then the same eight inverted (coloured text on black; drawn as
    /// the colour for now). The RGB values are Sidecar's own (the references
    /// give none). New strips take the colours an X32 starts with (seen on
    /// the owner's console: channels YE, aux in GN, FX returns, buses and
    /// matrices MG, mains WH).
    pub const X32: Palette = Palette {
        colours: &X32_COLOURS,
        grid: &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
        default: 8, // WH: a new DCA's
        factory: Some(|kind, slot| match kind {
            StripKind::Channel => 4,                                  // YE
            StripKind::Aux if (6..8).contains(&slot) => 4,            // YE: the USB playback
            StripKind::Aux if slot < 8 => 3,                          // GN: aux in
            StripKind::Bus if slot < 12 => 7,                         // CY
            StripKind::Aux | StripKind::Bus | StripKind::Matrix => 6, // MG: FX returns, FX sends, matrices
            StripKind::Main => 8,                                     // WH
        }),
    };

    /// Colour `index` (1-based), if the palette has it.
    pub fn colour(&self, index: i32) -> Option<(&'static str, u32)> {
        usize::try_from(index - 1).ok().and_then(|i| self.colours.get(i)).copied()
    }
}

/// Strips of one kind, from slot `first` (0-based) for `count`, that run their
/// own chain and go by their own label, numbered from 1 again: the X32's FX
/// returns, aux slots 9-16 ("FX 1").
#[derive(Clone, Copy, Debug)]
pub struct SlotRange {
    pub kind: StripKind,
    pub first: usize,
    pub count: usize,
    pub chain: &'static [Node],
    pub label: &'static str,
}

impl SlotRange {
    pub fn holds(&self, kind: StripKind, index: usize) -> bool {
        kind == self.kind && index >= self.first && index < self.first + self.count
    }
}

/// What a console is: what a new console of the profile has and how its
/// strips run.
#[derive(Debug)]
pub struct ProfileSpec {
    pub profile: Profile,
    /// The name the interface shows.
    pub name: &'static str,
    /// A new console's counts.
    pub counts: Counts,
    /// The chain of a strip of each kind: input channels, aux channels,
    /// then buses, mains and matrices (`chain.rs`).
    templates: [&'static [Node]; 3],
    /// The input channels' process orders (`Node::Slot`'s blocks, letters as
    /// `chain::order_block` reads them); `StripParams::process_order` picks one.
    pub process_orders: &'static [&'static str],
    /// The fixed pre-fader order of aux channels, and of buses, mains and matrices.
    aux_order: &'static [u8],
    output_order: &'static [u8],
    /// Where a level sits on the console's faders (the interface and the
    /// remotes ask it; scene fades run along it).
    pub fader: FaderLaw,
    /// How its strips pan (mono sources) and balance (stereo ones).
    pub pan: PanLaw,
    /// Its sources and destinations as its routing pages group them: ranges
    /// of the engine's numbering (`ports.rs`).
    pub port_groups: &'static [PortGroup],
    /// The models its blocks may hold (`livedaw_dsp::model`).
    pub models: &'static ModelTables,
    /// The remote protocols it speaks.
    pub remotes: Remotes,
    /// Its colours.
    pub palette: Palette,
    /// What its strips are called ("CH 1", "MTX 2"): input channels, aux
    /// channels, buses, mains, matrices.
    labels: [&'static str; 5],
    /// Strips whose chain and label aren't their kind's.
    pub slots: &'static [SlotRange],
    /// The buses (slots) that feed Main 1 at 0 dB on a new console: a WING's
    /// subgroups 9-16; none on an X32.
    pub buses_to_main: std::ops::Range<usize>,
    /// The mains (0-based) that are mono: the X32's M/C.
    pub mono_mains: &'static [usize],
    /// Input channels show their source's name, colour and icon at first (the
    /// WING's LINK CUSTOMIZATION TO SOURCE); an X32's have their own.
    pub source_link: bool,
    /// Bus sends start on (at -oo, an X32's); a WING's start off.
    pub sends_on: bool,
    /// The tap a new input's bus sends take (`SendParams::tap`; -1: their
    /// mode's): an X32's EQ->, the tap after the EQ in its default order.
    pub send_tap: i32,
    /// Where an input channel's PRE (TAP) sends take their signal at first
    /// (`StripParams::tap`, a `TAP_POINTS` index): the WING's "5", an X32's PFL.
    pub tap_point: i32,
    /// The rest of a new strip's factory settings (kind, slot), after the
    /// shared ones: an X32's low cut at 20 Hz, its buses' compressors on RMS.
    pub factory: Option<fn(&crate::params::StripParams, StripKind, usize)>,
    /// A new strip's name (kind, slot): an X32's "MixBus 1", "USB L"...
    pub factory_name: Option<fn(StripKind, usize) -> Option<String>>,
    /// A new console's output patch, if not main 1 on outputs 1-2 (an X32's
    /// buses and mains on its outputs: `x32_factory_outputs`).
    pub factory_outputs: Option<fn(&crate::routing::OutputPatch)>,
    /// A new console's DCAs: their names ("DCA 1") and fader (dB), if not the
    /// WING's (no name, faders down).
    pub dca_names: Option<&'static str>,
    pub dca_fader_db: f32,
    /// A new console's FX rack, slot by slot (tokens of `models.fx`, on their
    /// defaults): an X32's VRM, HALL, DLY, CRS and GEQs. Empty: every slot empty.
    pub fx_factory: &'static [&'static str],
    /// The buses (0-based slots) feeding the first FX slots as send effects
    /// (`FxParams::source_left`): an X32's buses 13-16 feed FX 1-4. Empty:
    /// every slot an insert.
    pub fx_send_buses: &'static [usize],
}

/// The pan of a strip a new console links into a pair (odd left, even
/// right): an X32's USB playback (aux in 7-8) and FX returns.
pub fn x32_pair_pan(kind: StripKind, slot: usize) -> f32 {
    let linked = kind == StripKind::Aux && (6..16).contains(&slot);
    match (linked, slot % 2) {
        (false, _) => 0.0,
        (true, 0) => -100.0,
        (true, _) => 100.0,
    }
}

/// An X32's factory names: aux in 7-8 the USB playback, buses 1-12 "MixBus",
/// 13-16 "Fx 1"-"Fx 4" (they feed FX 1-4).
fn x32_name(kind: StripKind, slot: usize) -> Option<String> {
    match (kind, slot) {
        (StripKind::Aux, 6) => Some("USB L".into()),
        (StripKind::Aux, 7) => Some("USB R".into()),
        (StripKind::Bus, 0..=11) => Some(format!("MixBus {}", slot + 1)),
        (StripKind::Bus, 12..=15) => Some(format!("Fx {}", slot - 11)),
        _ => None,
    }
}

/// The X32's output signals (`/outputs/main/NN/src`, OSC document p.39) as
/// the engine's sources, POST: 0 OFF, 1-2 Main L / R, 3 M/C, 4-19 MixBus 1-16,
/// 20-25 Matrix 1-6 (direct outs and the monitor: `livedaw-x32`'s map).
pub fn x32_output_signal(signal: i32) -> i32 {
    use crate::routing::strip_source_input;
    match signal {
        1 | 2 => strip_source_input(StripKind::Main, 0, signal as usize - 1),
        3 => strip_source_input(StripKind::Main, 1, 0),
        4..=19 => strip_source_input(StripKind::Bus, signal as usize - 4, 0),
        20..=25 => strip_source_input(StripKind::Matrix, signal as usize - 20, 0),
        _ => -1,
    }
}

/// The interface outputs as the X32 numbers its own: local 1-16, aux out
/// 1-6, AES50 A 1-48, AES50 B 1-48, the card's 1-32, P16 1-16 (from 0).
pub mod x32_ports {
    pub const LOCAL_OUT: i32 = 0;
    pub const AUX_OUT: i32 = 16;
    pub const AES50_A: i32 = 32;
    pub const AES50_B: i32 = 80;
    pub const CARD: i32 = 128;
    pub const P16: i32 = 160;
}

/// The output signals 1-16 of a new X32 (`/outputs/main/01..16/src`): MixBus
/// 1-14, then Main L and R on 15-16, as a full-size X32 has them.
pub const X32_FACTORY_OUTPUTS: [i32; 16] = [4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 1, 2];

/// A new X32's output patch (`ProfileSpec::factory_outputs`), as its factory
/// routing blocks give it: local outputs 1-16 and AES50 A and B 1-16 and 17-32
/// the output signals 1-16 (`OUT1-8`, `OUT9-16`); the card's 1-32 the local
/// inputs 1-32 (`AN1-8` ...). P16 and the aux outputs start OFF.
fn x32_factory_outputs(patch: &crate::routing::OutputPatch) {
    use x32_ports::*;
    for (k, &signal) in X32_FACTORY_OUTPUTS.iter().enumerate() {
        let source = x32_output_signal(signal);
        for port in [LOCAL_OUT, AES50_A, AES50_A + 16, AES50_B, AES50_B + 16] {
            patch.set(port + k as i32, source);
        }
    }
    for k in 0..32 {
        patch.set(CARD + k, k);
    }
}

/// An X32's factory strip (`ProfileSpec::factory`), as a freshly initialised
/// X32 Compact has it (its colours: `Palette::X32`): channels' low cut at
/// 20 Hz; the FX returns at 0 dB, they and the USB playback (aux in 7-8)
/// linked pairs panned apart; buses 13-16 (the FX sends) at 0 dB; buses',
/// matrices' and mains' compressors detecting RMS; delays in milliseconds.
fn x32_factory(p: &crate::params::StripParams, kind: StripKind, slot: usize) {
    // The X32's delays are times, 0.3 .. 500 ms (factory 0.3 ms, off).
    use std::sync::atomic::Ordering::Relaxed;
    p.input.delay_mode.store(2, Relaxed);
    p.input.delay.store(0.3);
    if kind == StripKind::Channel {
        p.filter().low_cut_freq.store(20.0);
    }
    if kind == StripKind::Aux {
        p.pan.store(x32_pair_pan(kind, slot));
        // Aux in 1-6 the aux inputs (interface inputs 161-166), 7-8 the USB
        // player's L and R.
        if slot < 8 {
            p.stereo.store(false, Relaxed);
            p.input_right.store(-1, Relaxed);
            p.input_left.store(
                if slot < 6 { 160 + slot as i32 } else { livedaw_recorder::USB_PLAYER_SOURCE_BASE + slot as i32 - 6 },
                Relaxed,
            );
        }
        if slot >= 8 {
            // FX returns 1-8: FX 1 L, FX 1 R ... FX 4 R.
            p.fader_db.store(0.0);
            p.input_left.store(crate::sources::FX_RETURN_SOURCE_BASE + (slot - 8) as i32, Relaxed);
        }
    }
    if kind == StripKind::Bus && slot >= 12 {
        p.fader_db.store(0.0);
    }
    if kind.is_output() {
        if let Some(det) = p.dynamics().param("det") {
            det.store(1.0); // RMS
        }
    }
}

impl ProfileSpec {
    /// The chain of a strip of `kind`.
    pub fn template(&self, kind: StripKind) -> &'static [Node] {
        match kind {
            StripKind::Channel => self.templates[0],
            StripKind::Aux => self.templates[1],
            StripKind::Bus | StripKind::Main | StripKind::Matrix => self.templates[2],
        }
    }

    /// The label of a strip of `kind`, before its number ("CH" for "CH 1").
    pub fn label(&self, kind: StripKind) -> &'static str {
        self.labels[StripKind::ALL.iter().position(|&k| k == kind).unwrap_or(0)]
    }

    /// The slot range strip `index` (0-based) of `kind` is in, if any.
    pub fn slot(&self, kind: StripKind, index: usize) -> Option<&'static SlotRange> {
        self.slots.iter().find(|r| r.holds(kind, index))
    }

    /// The chain of strip `index` (0-based) of `kind`: its slot range's, or its kind's.
    pub fn template_at(&self, kind: StripKind, index: usize) -> &'static [Node] {
        self.slot(kind, index).map_or_else(|| self.template(kind), |r| r.chain)
    }

    /// What strip `index` (0-based) of `kind` is called: the label and number
    /// ("FX", 1 for the X32's aux slot 9).
    pub fn label_at(&self, kind: StripKind, index: usize) -> (&'static str, usize) {
        match self.slot(kind, index) {
            Some(r) => (r.label, index - r.first + 1),
            None => (self.label(kind), index + 1),
        }
    }

    /// The pre-fader blocks' order (`Node::Slot`) of a strip of `kind`;
    /// input channels: process order `order` (clamped).
    pub fn pre_fader_order(&self, kind: StripKind, order: i32) -> &'static [u8] {
        match kind {
            StripKind::Channel => {
                let i = order.clamp(0, self.process_orders.len() as i32 - 1) as usize;
                self.process_orders[i].as_bytes()
            }
            StripKind::Aux => self.aux_order,
            _ => self.output_order,
        }
    }
}

/// The X32's colours (`Palette::X32`).
const X32_COLOURS: [(&str, u32); 16] = [
    ("OFF", 0x303030),
    ("RD", 0xe02424),
    ("GN", 0x28c040),
    ("YE", 0xe8d020),
    ("BL", 0x2850e8),
    ("MG", 0xd028d0),
    ("CY", 0x20c8d8),
    ("WH", 0xe8e8e8),
    ("OFFi", 0x303030),
    ("RDi", 0xe02424),
    ("GNi", 0x28c040),
    ("YEi", 0xe8d020),
    ("BLi", 0x2850e8),
    ("MGi", 0xd028d0),
    ("CYi", 0x20c8d8),
    ("WHi", 0xe8e8e8),
];

/// The WING's strip labels, as on its scribble strips (`StripKind::label`).
const WING_LABELS: [&str; 5] = ["CH", "AUX", "BUS", "MAIN", "MTX"];

/// The Behringer WING: 40 channels, 8 aux, 16 buses, 4 mains, 8 matrices, 16
/// DCAs, 8 mute groups, 16 FX slots; its chains (`chain.rs`, Figs. 34 and 68
/// of its manual) and 24 process orders.
static WING: ProfileSpec = ProfileSpec {
    profile: Profile::Wing,
    name: "WING",
    counts: Counts::WING,
    templates: [chain::CHANNEL, chain::AUX, chain::OUTPUT],
    process_orders: PROCESS_ORDERS,
    aux_order: b"DEI",
    output_order: b"DI",
    fader: FaderLaw::WING,
    pan: PanLaw::WING,
    port_groups: WING_PORT_GROUPS,
    models: &WING_MODELS,
    remotes: Remotes::WING,
    palette: Palette::WING,
    labels: WING_LABELS,
    slots: &[],
    buses_to_main: 8..WING_BUSES,
    mono_mains: &[],
    source_link: true,
    sends_on: false,
    send_tap: -1,
    tap_point: crate::params::DEFAULT_TAP,
    factory: None,
    factory_name: None,
    factory_outputs: None,
    dca_names: None,
    dca_fader_db: livedaw_dsp::fader::FADER_MIN,
    fx_factory: &[],
    fx_send_buses: &[],
};

/// No console's limits. Its strips have the WING's blocks (a profile's own set
/// comes with Phase 4's consoles), it speaks the WING's remote protocols, and a
/// new one starts with the WING's counts.
static UNIVERSAL: ProfileSpec = ProfileSpec {
    profile: Profile::Universal,
    name: "Universal",
    counts: Counts::WING,
    templates: [chain::CHANNEL, chain::AUX, chain::OUTPUT],
    process_orders: PROCESS_ORDERS,
    aux_order: b"DEI",
    output_order: b"DI",
    fader: FaderLaw::WING,
    pan: PanLaw::WING,
    port_groups: WING_PORT_GROUPS,
    models: &WING_MODELS,
    remotes: Remotes::WING,
    palette: Palette::WING,
    labels: WING_LABELS,
    slots: &[],
    buses_to_main: 8..WING_BUSES,
    mono_mains: &[],
    source_link: true,
    sends_on: false,
    send_tap: -1,
    tap_point: crate::params::DEFAULT_TAP,
    factory: None,
    factory_name: None,
    factory_outputs: None,
    dca_names: None,
    dca_fader_db: livedaw_dsp::fader::FADER_MIN,
    fx_factory: &[],
    fx_send_buses: &[],
};

/// The Behringer X32: its counts (`Counts::X32`), fader law, colours and
/// labels and its models (`livedaw_dsp::x32`: gate, compressor, 4- and
/// 6-band EQ); its aux in and FX returns (aux slots 1-8 and 9-16) run EQ only
/// (`chain::X32_AUX`); its channels and buses run its chains (`chain::X32_CHANNEL`,
/// `X32_OUTPUT`: one insert, the compressor and insert PRE or POST by the
/// process order, everything before the fader; the low cut is the filter
/// block's). Its pan law is the WING's, its routing pages list the WING's port
/// groups, and it speaks its OSC (`livedaw-x32`; its MIDI comes with step 6).
static X32: ProfileSpec = ProfileSpec {
    profile: Profile::X32,
    name: "X32",
    counts: Counts::X32,
    templates: [chain::X32_CHANNEL, chain::X32_AUX, chain::X32_OUTPUT],
    process_orders: chain::X32_ORDERS,
    aux_order: b"",
    output_order: b"EDI",
    fader: FaderLaw::X32,
    pan: PanLaw::WING,
    port_groups: WING_PORT_GROUPS,
    models: &livedaw_dsp::x32::MODELS,
    remotes: Remotes { midi: Some(MidiProtocol::X32), network: &[NetworkProtocol::X32Osc] },
    palette: Palette::X32,
    labels: WING_LABELS,
    slots: &[
        SlotRange { kind: StripKind::Aux, first: 0, count: 8, chain: chain::X32_AUX, label: "AUX" },
        SlotRange { kind: StripKind::Aux, first: 8, count: 8, chain: chain::X32_AUX, label: "FX" },
    ],
    // Its buses don't feed LR at first; main 2 is the mono M/C.
    buses_to_main: 0..0,
    mono_mains: &[1],
    source_link: false,
    sends_on: true,
    send_tap: 3, // EQ-> (GEDI: after the second slot)
    tap_point: crate::strip::tap::PFL as i32,
    factory: Some(x32_factory),
    factory_name: Some(x32_name),
    factory_outputs: Some(x32_factory_outputs),
    dca_names: Some("DCA"),
    dca_fader_db: 0.0,
    fx_factory: &["X32VRM", "X32HALL", "X32DLY", "X32CRS", "X32GEQ2", "X32GEQ2", "X32GEQ2", "X32GEQ"],
    fx_send_buses: &[12, 13, 14, 15],
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn profiles() {
        for p in Profile::ALL {
            assert_eq!(Profile::from_id(p.id()), Some(p));
            assert_eq!(p.spec().profile, p);
            assert!(p.fits(&p.spec().counts));
        }
        assert_eq!(Profile::from_id("m32"), None);
        assert!(Profile::Wing.fits(&Counts { channels: 64, buses: 24, fx: 32, ..Counts::WING }));
        for c in [Counts { mains: 2, ..Counts::WING }, Counts { buses: 8, ..Counts::WING }, Counts { dcas: 24, ..Counts::WING }] {
            assert!(!Profile::Wing.fits(&c));
            assert_eq!(Profile::Wing.or_universal(&c), Profile::Universal);
        }
    }

    #[test]
    fn the_x32() {
        let x = Profile::X32.spec();
        assert_eq!(Profile::from_id("x32"), Some(Profile::X32));
        assert_eq!((x.name, x.counts), ("X32", Counts::X32));
        assert!(Profile::X32.fits(&Counts::X32));
        assert!(!Profile::X32.fits(&Counts { channels: 40, ..Counts::X32 }));
        assert_eq!(Profile::X32.or_universal(&Counts::WING), Profile::Universal);
        assert!(!Profile::Wing.fits(&Counts::X32), "an X32 isn't a WING");
        assert_eq!((x.fader.steps, x.fader.max_db()), (1024, 10.0));
        assert_eq!(x.palette.colours.len(), 16);
        assert_eq!(x.palette.colour(5), Some(("BL", 0x2850e8)), "config/color 4");
        let factory = x.palette.factory.unwrap();
        assert_eq!(x.palette.colour(factory(StripKind::Channel, 0)), Some(("YE", 0xe8d020)));
        assert_eq!((factory(StripKind::Aux, 5), factory(StripKind::Aux, 8)), (3, 6), "aux in GN, FX returns MG");
        assert_eq!((factory(StripKind::Bus, 0), factory(StripKind::Bus, 12), factory(StripKind::Main, 1)), (7, 6, 8));
        assert_eq!(factory(StripKind::Aux, 6), 4, "the USB playback YE");
        assert_eq!((x.remotes.midi, x.remotes.network_bits()), (Some(MidiProtocol::X32), 8), "its MIDI and OSC");
    }

    #[test]
    fn the_x32s_aux_in_and_fx_returns() {
        let x = Profile::X32.spec();
        assert_eq!(x.template_at(StripKind::Aux, 0), chain::X32_AUX);
        assert_eq!(x.template_at(StripKind::Aux, 15), chain::X32_AUX);
        assert_eq!(x.template_at(StripKind::Channel, 0), chain::X32_CHANNEL);
        assert_eq!(x.template_at(StripKind::Matrix, 0), chain::X32_OUTPUT);
        // GEDI by default (the compressor and the insert POST the EQ).
        assert_eq!(x.pre_fader_order(StripKind::Channel, 0), b"GEDI");
        assert_eq!(x.pre_fader_order(StripKind::Channel, 3), b"GIDE", "both PRE");
        assert_eq!(x.pre_fader_order(StripKind::Bus, 0), b"EDI");
        for chain in [chain::X32_CHANNEL, chain::X32_OUTPUT] {
            let count = |n: Node| chain.iter().filter(|&&x| x == n).count();
            assert_eq!((count(Node::PostInsert), count(Node::Eq), count(Node::Dynamics)), (0, 0, 0), "by slot only");
            for n in [Node::Input, Node::Pfl, Node::Fader, Node::Post] {
                assert_eq!(count(n), 1, "{n:?}");
            }
        }
        // Every slot names a block of the X32's.
        for order in chain::X32_ORDERS {
            assert!(order.bytes().all(|b| chain::order_block(b).is_some()), "{order}");
        }
        assert_eq!(x.label_at(StripKind::Aux, 7), ("AUX", 8));
        assert_eq!(x.label_at(StripKind::Aux, 8), ("FX", 1));
        assert_eq!(x.label_at(StripKind::Bus, 3), ("BUS", 4));
        // No dynamics or inserts on them; every tap point still once.
        let count = |n: Node| chain::X32_AUX.iter().filter(|&&x| x == n).count();
        for n in [Node::Dynamics, Node::PreInsert, Node::PostInsert, Node::Delay, Node::Filter] {
            assert_eq!(count(n), 0, "{n:?}");
        }
        for n in [Node::Input, Node::Eq, Node::Pfl, Node::Fader, Node::Post] {
            assert_eq!(count(n), 1, "{n:?}");
        }
        // The WING has no slot ranges.
        assert_eq!(Profile::Wing.spec().template_at(StripKind::Aux, 9), chain::AUX);
        assert_eq!(Profile::Wing.spec().label_at(StripKind::Aux, 9), ("AUX", 10));
    }

    #[test]
    fn the_wings_labels() {
        for p in Profile::ALL {
            for kind in StripKind::ALL {
                assert_eq!(p.spec().label(kind), kind.label(), "{p:?} {kind:?}");
            }
        }
    }

    #[test]
    fn the_wings_colours() {
        for p in [Profile::Universal, Profile::Wing] {
            let c = p.spec().palette;
            assert_eq!(c.colours.len(), 18, "{p:?}");
            assert_eq!(c.colour(1), Some(("Dark Blue", 0x380088)));
            assert_eq!(c.colour(19), None);
            assert_eq!(c.colour(0), None);
            let mut grid = c.grid.to_vec();
            grid.sort_unstable();
            assert_eq!(grid, (1..=18).collect::<Vec<_>>(), "every colour once in the picker");
        }
    }

    #[test]
    fn the_wings_remotes() {
        for p in [Profile::Universal, Profile::Wing] {
            let r = p.spec().remotes;
            assert_eq!(r.midi, Some(MidiProtocol::Wing), "{p:?}");
            // The WING remote's enable bits: discovery 1, OSC 2, native 4.
            assert_eq!(r.network_bits(), 7, "{p:?}");
        }
    }

    #[test]
    fn the_wings_chains_and_process_orders() {
        let w = Profile::Wing.spec();
        assert_eq!(w.process_orders.len(), 24);
        assert_eq!(w.pre_fader_order(StripKind::Channel, 99), PROCESS_ORDERS[23].as_bytes());
        assert_eq!(w.pre_fader_order(StripKind::Aux, 0), b"DEI");
        assert_eq!(w.pre_fader_order(StripKind::Matrix, 0), b"DI");
        for kind in [StripKind::Channel, StripKind::Aux, StripKind::Bus, StripKind::Main, StripKind::Matrix] {
            assert_eq!(w.template(kind), chain::template(kind));
        }
    }

    #[test]
    fn the_wings_laws() {
        for p in Profile::ALL {
            let s = p.spec();
            // The X32's faders run the same law on 1024 steps.
            assert_eq!(s.fader.points, FaderLaw::WING.points);
            assert_eq!(s.fader.steps, if p == Profile::X32 { 1024 } else { 0 });
            assert_eq!(s.pan.gains(true, -1.0), livedaw_dsp::util::wing_mono_pan(-1.0));
            assert_eq!(s.pan.gains(false, 0.5), livedaw_dsp::util::stereo_balance(0.5));
            assert_eq!(s.port_groups, WING_PORT_GROUPS);
        }
    }

    #[test]
    fn the_wings_models() {
        use livedaw_dsp::wing::{AUX_DYN_MODELS, DYN_MODELS, EQ_MODELS_BUS, EQ_MODELS_CH, FILTER_TOOLS, FX_MODELS};
        for p in [Profile::Universal, Profile::Wing] {
            let m = p.spec().models;
            assert_eq!(m.table(0), DYN_MODELS);
            assert_eq!(m.table(1), EQ_MODELS_CH);
            assert_eq!(m.table(2), EQ_MODELS_BUS);
            assert_eq!(m.table(3), FILTER_TOOLS);
            assert_eq!(m.table(4), FX_MODELS);
            assert_eq!(m.table(9), DYN_MODELS, "out of range: dynamics");
            assert_eq!(m.aux_dynamics, AUX_DYN_MODELS);
            assert!(AUX_DYN_MODELS.iter().all(|t| m.aux_may_hold(t)));
            assert!(!m.aux_may_hold("NO SUCH MODEL"));
        }
    }

    #[test]
    fn the_x32s_models_run_on_its_strips() {
        use crate::params::StripParams;
        let x = Profile::X32.spec().models;
        assert_eq!(x.table(0), livedaw_dsp::x32::DYN_MODELS);
        assert_eq!(x.table(1), livedaw_dsp::x32::EQ_CHANNEL);
        assert_eq!(x.table(2), livedaw_dsp::x32::EQ_BUS);
        assert!(x.aux_dynamics.is_empty(), "its aux in and FX returns have no dynamics");
        // A new X32 strip: the gate on GATE, the dynamics on the compressor, the
        // 4-band EQ (buses: 6 bands).
        let ch = StripParams::new_with(StripKind::Channel, x);
        assert_eq!(ch.gate().model_spec().unwrap().token, "X32GATE");
        assert_eq!(ch.dynamics().model_spec().unwrap().token, "X32COMP");
        assert_eq!(ch.eq().model_spec().unwrap().token, "X32EQ4");
        assert_eq!(ch.dynamics().param("ratio").unwrap().load(), 5.0, "3.0:1 (item 5)");
        let bus = StripParams::new_with(StripKind::Bus, x);
        assert_eq!(bus.eq().model_spec().unwrap().token, "X32EQ6");
        // Its factory strip beyond the shared settings (as the owner's X32 shows).
        let spec = Profile::X32.spec();
        let ch1 = StripParams::new_in_slot_of(StripKind::Channel, 0, spec);
        assert_eq!(ch1.filter().low_cut_freq.load(), 20.0);
        assert_eq!((ch1.input.delay_mode.load(std::sync::atomic::Ordering::Relaxed), ch1.input.delay.load()), (2, 0.3), "0.3 ms");
        let bus1 = StripParams::new_in_slot_of(StripKind::Bus, 0, spec);
        assert_eq!(bus1.dynamics().param("det").unwrap().load(), 1.0, "RMS");
        assert_eq!(bus1.tap.load(std::sync::atomic::Ordering::Relaxed), crate::strip::tap::PFL as i32);
        assert!(bus.eq().param("6q").is_some() && ch.eq().param("6q").is_none());
    }
}
