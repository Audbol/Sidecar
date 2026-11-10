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
use crate::ports::{
    dlive_ports, dm2000_ports, pro2_ports, xenyx1204_ports, PortGroup, DLIVE_PORT_GROUPS, DM2000_PORT_GROUPS, PRO2_PORT_GROUPS,
    PROMIX01_PORT_GROUPS, SQ5_PORT_GROUPS, WING_PORT_GROUPS, X32_PORT_GROUPS, XENYX1204_PORT_GROUPS, Y01V_PORT_GROUPS,
    Y02R_PORT_GROUPS,
};
use livedaw_dsp::fader::FaderLaw;
use livedaw_dsp::model::ModelTables;
use livedaw_dsp::util::PanLaw;
use livedaw_dsp::wing::{MODELS as WING_MODELS, PROCESS_ORDERS};

/// A new strip's factory settings, after the shared ones (`ProfileSpec::factory`).
type StripFactory = fn(&ProfileSpec, &crate::params::StripParams, StripKind, usize);
/// A new strip's name (`ProfileSpec::factory_name`).
type StripName = fn(&ProfileSpec, StripKind, usize) -> Option<String>;

/// The console a configuration emulates (its profile: `docs/UNIVERSAL_MIXER.md`).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum Profile {
    /// No console's limits: the Universal Mixer's own.
    Universal,
    /// The Behringer WING.
    #[default]
    Wing,
    /// The Behringer X32 (Phase 4).
    X32,
    /// The Yamaha ProMix 01 (Phase 5).
    ProMix01,
    /// The Yamaha 02R, Version 2 (Phase 6: its processing, effects and MIDI follow).
    Yamaha02R,
    /// The Allen & Heath SQ-5 (its effects, MIDI and surface follow).
    AhSq5,
    /// The Yamaha 01V (the original): its processing and effects; its surface
    /// and MIDI follow.
    Yamaha01V,
    /// The Allen & Heath dLive, its 64 mix busses as its Mixer Config lays them
    /// out (its FoH template's on a new console; its own effects, MIDI and surface
    /// follow).
    AhDLive(DLiveLayout),
    /// The Yamaha DM2000 (Version 2; a DM2000VCM adds only the Add-On Effects,
    /// which aren't emulated): its processing and effects. Its surface and MIDI
    /// follow.
    Dm2000,
    /// The Midas PRO2: its processing and effects. Its surface follows; it has no
    /// published control protocol.
    MidasPro2,
    /// The Behringer XENYX 1204USB, Sidecar's first analog mixer: its blocks, routing
    /// and surface; no remote protocol.
    Xenyx1204,
}

impl Profile {
    pub const ALL: [Profile; 11] = [
        Profile::Universal,
        Profile::Wing,
        Profile::X32,
        Profile::ProMix01,
        Profile::Yamaha02R,
        Profile::AhSq5,
        Profile::Yamaha01V,
        Profile::AhDLive(DLiveLayout::FOH),
        Profile::Dm2000,
        Profile::MidasPro2,
        Profile::Xenyx1204,
    ];

    /// The id shows store (`<Console profile="...">`).
    pub fn id(self) -> &'static str {
        match self {
            Profile::Universal => "universal",
            Profile::Wing => "wing",
            Profile::X32 => "x32",
            Profile::ProMix01 => "promix01",
            Profile::Yamaha02R => "02r",
            Profile::AhSq5 => "sq5",
            Profile::Yamaha01V => "01v",
            Profile::AhDLive(_) => "dlive",
            Profile::Dm2000 => "dm2000",
            Profile::MidasPro2 => "pro2",
            Profile::Xenyx1204 => "xenyx1204",
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
            Profile::ProMix01 => &PROMIX01,
            Profile::Yamaha02R => &Y02R,
            Profile::AhSq5 => &SQ5,
            Profile::Yamaha01V => &Y01V,
            Profile::AhDLive(layout) => dlive_spec(layout),
            Profile::Dm2000 => &DM2000,
            Profile::MidasPro2 => &PRO2,
            Profile::Xenyx1204 => &XENYX1204,
        }
    }

    /// The name the interface shows.
    pub fn name(self) -> &'static str {
        self.spec().name
    }

    /// Whether a console of `counts` is one of this profile's: the WING's
    /// aux, mains, matrices, DCAs and mute groups (what its surface
    /// addresses; channels, buses and FX slots can grow on a WING too); the
    /// X32's, the ProMix 01's and the 02R's counts exactly.
    pub fn fits(self, counts: &Counts) -> bool {
        match self {
            Profile::Universal => true,
            Profile::X32
            | Profile::ProMix01
            | Profile::Yamaha02R
            | Profile::AhSq5
            | Profile::Yamaha01V
            | Profile::AhDLive(_)
            | Profile::Dm2000
            | Profile::MidasPro2
            | Profile::Xenyx1204 => *counts == self.spec().counts,
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
    /// The 01V's: parameter changes, an assignable CC table, program changes,
    /// bulk dumps (`livedaw-01v`).
    Yamaha01V,
    /// The ProMix 01's: its 531 parameters by CC (Channel or Register mode),
    /// scenes by program change, Yamaha bulk dumps (`livedaw-promix`).
    ProMix01,
    /// The 02R's: SysEx parameter changes, recall / store and fader remote,
    /// control changes through its map, scenes by program change, Yamaha bulk
    /// dumps (`livedaw-02r`).
    Yamaha02R,
    /// The SQ-5's: NRPN levels, mutes, pans and assignments, scenes by bank and
    /// program change (`livedaw-sq`).
    AhSq,
    /// The DM2000's: SysEx parameter changes and requests, its control change
    /// tables, scenes by program change, Key Remote, meters, MMC (`livedaw-dm2000`).
    Dm2000,
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
    /// Allen & Heath MIDI over TCP (the dLive's and SQ's, `livedaw-ahmidi`).
    AhMidiTcp,
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
            Self::AhMidiTcp => 16,
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
    pub factory: Option<fn(&ProfileSpec, StripKind, usize) -> i32>,
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
        factory: Some(|_spec, kind, slot| match kind {
            StripKind::Channel => 4,                                  // YE
            StripKind::Aux if (6..8).contains(&slot) => 4,            // YE: the USB playback
            StripKind::Aux if slot < 8 => 3,                          // GN: aux in
            StripKind::Bus if slot < 12 => 7,                         // CY
            StripKind::Aux | StripKind::Bus | StripKind::Matrix => 6, // MG: FX returns, FX sends, matrices
            StripKind::Main => 8,                                     // WH
        }),
    };

    /// The ProMix 01's: the console has no strip colours (its scribble strips
    /// are the LCD's), so Sidecar's own eight, a new strip coloured by what it
    /// is: channels white, ST IN yellow, the effect returns magenta, the sends
    /// cyan, ST OUT red.
    pub const PROMIX01: Palette = Palette {
        colours: &PROMIX01_COLOURS,
        grid: &[1, 2, 3, 4, 5, 6, 7, 8],
        default: 8,
        factory: Some(|_spec, kind, slot| match kind {
            StripKind::Channel => 8,
            StripKind::Aux if slot == 0 => 4,
            StripKind::Aux => 6,
            StripKind::Bus => 7,
            StripKind::Main | StripKind::Matrix => 2,
        }),
    };

    /// The 02R's: no strip colours either, so the ProMix 01's eight, a new strip
    /// coloured by what it is: MIC/LINE white, LINE 17-24 yellow, the tape
    /// returns green, the effect returns magenta, BUS 1-8 cyan, AUX 1-8 blue,
    /// STEREO red.
    pub const Y02R: Palette = Palette {
        colours: &PROMIX01_COLOURS,
        grid: &[1, 2, 3, 4, 5, 6, 7, 8],
        default: 8,
        factory: Some(|_spec, kind, slot| match kind {
            StripKind::Channel if slot >= y02r::TAPE => 3,
            StripKind::Channel if slot >= y02r::LINE => 4,
            StripKind::Channel => 8,
            StripKind::Aux => 6,
            StripKind::Bus if slot >= y02r::AUX => 5,
            StripKind::Bus => 7,
            StripKind::Main | StripKind::Matrix => 2,
        }),
    };

    /// The DM2000's: it has no strip colours (its displays are fluorescent), so the
    /// ProMix 01's eight, a new strip coloured by what it is (Sidecar's): CH 1-24
    /// white, the slots' 25-72 green, the effect returns 73-88 magenta, 2TR 89-96
    /// yellow, BUS cyan, AUX blue, MATRIX yellow, STEREO red.
    pub const DM2000: Palette = Palette {
        colours: &PROMIX01_COLOURS,
        grid: &[1, 2, 3, 4, 5, 6, 7, 8],
        default: 8,
        factory: Some(|_spec, kind, slot| match kind {
            StripKind::Channel if slot < 24 => 8,
            StripKind::Channel if slot < 72 => 3,
            StripKind::Channel if slot < 88 => 6,
            StripKind::Channel => 4,
            StripKind::Aux => 6,
            StripKind::Bus if slot >= dm2000::AUX => 5,
            StripKind::Bus => 7,
            StripKind::Matrix => 4,
            StripKind::Main => 2,
        }),
    };

    /// The XENYX 1204USB's: no strip colours on the panel (the surface has console tape),
    /// so the ProMix 01's eight, every strip white (Sidecar's).
    pub const XENYX1204: Palette =
        Palette { colours: &PROMIX01_COLOURS, grid: &[1, 2, 3, 4, 5, 6, 7, 8], default: 8, factory: Some(|_s, _k, _n| 8) };

    /// The PRO2's: the input name fields start green (Owner's Manual p.243), and the
    /// ProMix 01's eight for the rest, a new strip coloured by what it is (Sidecar's).
    pub const PRO2: Palette = Palette {
        colours: &PROMIX01_COLOURS,
        grid: &[1, 2, 3, 4, 5, 6, 7, 8],
        default: 3,
        factory: Some(|_spec, kind, _slot| match kind {
            StripKind::Channel | StripKind::Aux => 3,
            StripKind::Bus => 5,
            StripKind::Matrix => 4,
            StripKind::Main => 2,
        }),
    };

    /// The 01V's: the ProMix 01's eight again, a new strip coloured by what it
    /// is: CH 1-12 white, 13/14 and 15/16 yellow, CH 17-24 green, the effect
    /// returns and sends magenta, BUS 1-4 cyan, AUX 1-4 blue, STEREO red.
    pub const Y01V: Palette = Palette {
        colours: &PROMIX01_COLOURS,
        grid: &[1, 2, 3, 4, 5, 6, 7, 8],
        default: 8,
        factory: Some(|_spec, kind, slot| match kind {
            StripKind::Channel if slot >= y01v::OPTION => 3,
            StripKind::Channel if slot >= y01v::STEREO => 4,
            StripKind::Channel => 8,
            StripKind::Aux => 6,
            StripKind::Bus if slot >= y01v::EFF => 6,
            StripKind::Bus if slot >= y01v::AUX => 5,
            StripKind::Bus => 7,
            StripKind::Main | StripKind::Matrix => 2,
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
    /// The fixed pre-fader order of aux channels; the orders of buses, mains
    /// and matrices (one for most consoles; a ProMix 01's send or ST OUT
    /// picks its dynamics PRE or POST fader by its process order).
    aux_order: &'static [u8],
    output_orders: &'static [&'static str],
    /// Where a level sits on the console's faders (the interface and the
    /// remotes ask it; scene fades run along it).
    pub fader: FaderLaw,
    /// The mains' faders, if not `fader` (the 02R's STEREO: 0 to -120 dB).
    pub main_fader: Option<FaderLaw>,
    /// The buses' faders, if not `fader` (the 01V's masters: 0 to -72 dB).
    pub bus_fader: Option<FaderLaw>,
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
    pub factory: Option<StripFactory>,
    /// A new strip's name (kind, slot): an X32's "MixBus 1", "USB L"...
    pub factory_name: Option<StripName>,
    /// A new console's output patch, if not main 1 on outputs 1-2 (an X32's
    /// buses and mains on its outputs: `x32_factory_outputs`).
    pub factory_outputs: Option<fn(&ProfileSpec, &crate::routing::OutputPatch)>,
    /// A new console's DCAs: their names ("DCA 1") and fader (dB), if not the
    /// WING's (no name, faders down).
    pub dca_names: Option<&'static str>,
    pub dca_fader_db: f32,
    /// A new console's FX rack, slot by slot (tokens of `models.fx`, on their
    /// defaults): an X32's VRM, HALL, DLY, CRS and GEQs. Empty: every slot empty.
    pub fx_factory: &'static [&'static str],
    /// An EQ block a new console's buses, mains and matrices start with, before
    /// their EQ (the token of a model of the bus EQ table; the SQ's GEQ). Strips
    /// without an EQ node don't get one.
    pub factory_eq_block: Option<&'static str>,
    /// That block comes after the mixes' EQ, not before it.
    pub factory_eq_after: bool,
    /// A preamp block a new console's input channels start with, off, right after
    /// their INPUT (the token of a preamp model of `models.dynamics`; the dLive's).
    /// None: no such block, and every dynamics model goes anywhere its table offers it.
    pub factory_preamp_block: Option<&'static str>,
    /// The buses (0-based slots) feeding the first FX slots as send effects
    /// (`FxParams::source_left`): an X32's buses 13-16 feed FX 1-4. Empty:
    /// every slot an insert.
    pub fx_send_buses: &'static [usize],
    /// A dLive's Mixer Config (None: any other console).
    pub dlive_layout: Option<DLiveLayout>,
    /// INPUT's trim is an attenuator further down the chain
    /// (`chain::Node::Attenuator`), INPUT doing the polarity alone (a DM2000's).
    pub attenuator: bool,
    /// The range of a strip's delay when the console sets it in whole samples
    /// and the WING's 16-sample floor doesn't apply (a DM2000's 0-43400); None:
    /// the WING's, in `DELAY_MODES` units.
    pub delay_samples: Option<(u32, u32)>,
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
fn x32_name(_spec: &ProfileSpec, kind: StripKind, slot: usize) -> Option<String> {
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
fn x32_factory_outputs(_spec: &ProfileSpec, patch: &crate::routing::OutputPatch) {
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
fn x32_factory(_spec: &ProfileSpec, p: &crate::params::StripParams, kind: StripKind, slot: usize) {
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

/// A place a dynamics model can go (`ProfileSpec::may_hold`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DynSlot {
    Gate,
    /// The compressor slot, or an extra dynamics block.
    Dynamics,
    Preamp,
}

impl ProfileSpec {
    /// Whether dynamics model `token` (of `models.dynamics`) may go in `slot`. A
    /// console with a preamp block keeps its preamp models there, and only there;
    /// every other console (the Universal first) offers its table's models anywhere.
    pub fn may_hold(&self, slot: DynSlot, token: &str) -> bool {
        if self.profile == Profile::Dm2000 {
            return slot != DynSlot::Preamp && livedaw_dsp::dm2000::is_gate(token) == (slot == DynSlot::Gate);
        }
        if self.profile == Profile::MidasPro2 {
            return slot != DynSlot::Preamp && livedaw_dsp::midas_pro::is_gate(token) == (slot == DynSlot::Gate);
        }
        if self.profile == Profile::Xenyx1204 {
            // The soft clip sits after the preamp (the GATE slot), the compressor after it.
            return slot != DynSlot::Preamp && livedaw_dsp::xenyx1204::is_clip(token) == (slot == DynSlot::Gate);
        }
        self.factory_preamp_block.is_none() || livedaw_dsp::dlive::is_preamp(token) == (slot == DynSlot::Preamp)
    }

    /// [`Self::may_hold`] on a strip of `kind`: a PRO2's compressors are a pair, the
    /// inputs' four modes and the outputs' five (Owner's Manual pp.250, 273).
    pub fn may_hold_on(&self, kind: StripKind, slot: DynSlot, token: &str) -> bool {
        if self.profile == Profile::MidasPro2
            && slot == DynSlot::Dynamics
            && livedaw_dsp::midas_pro::is_output_compressor(token) != kind.is_output()
        {
            return false;
        }
        self.may_hold(slot, token)
    }

    /// The matrices (0-based slots) feeding the first FX slots as send effects: a
    /// PRO2's matrices 1-4 feed FX 1-4 (p.211). Other consoles' sends are buses.
    pub fn fx_send_matrices(&self) -> &'static [usize] {
        if self.profile == Profile::MidasPro2 {
            &[0, 1, 2, 3]
        } else {
            &[]
        }
    }

    /// The chain of a strip of `kind`.
    pub fn template(&self, kind: StripKind) -> &'static [Node] {
        match kind {
            StripKind::Channel => self.templates[0],
            StripKind::Aux => self.templates[1],
            StripKind::Bus | StripKind::Main | StripKind::Matrix => self.templates[2],
        }
    }

    /// The fader law of a strip of `kind`: the mains' or buses' own, or the console's.
    pub fn fader_for(&self, kind: StripKind) -> FaderLaw {
        match (kind, self.main_fader, self.bus_fader) {
            (StripKind::Main, Some(law), _) | (StripKind::Bus | StripKind::Matrix, _, Some(law)) => law,
            _ => self.fader,
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

    /// The blocks' order (`Node::Slot`) of a strip of `kind`; input channels,
    /// and a ProMix 01's outputs: process order `order` (clamped).
    pub fn pre_fader_order(&self, kind: StripKind, order: i32) -> &'static [u8] {
        match kind {
            StripKind::Channel => {
                let i = order.clamp(0, self.process_orders.len() as i32 - 1) as usize;
                self.process_orders[i].as_bytes()
            }
            StripKind::Aux => self.aux_order,
            _ => {
                let i = order.clamp(0, self.output_orders.len() as i32 - 1) as usize;
                self.output_orders[i].as_bytes()
            }
        }
    }
}

/// The ProMix 01's factory strip (`ProfileSpec::factory`), as its scene 00
/// starts (User's Guide p.32, p.45, p.62): ST IN and the returns stereo, the
/// returns fed by the internal effects (FX 1-2: `fx_send_buses`) at 0 dB; the
/// four sends mono, SEND1-2 (no master) at 0 dB.
fn promix01_factory(_spec: &ProfileSpec, p: &crate::params::StripParams, kind: StripKind, slot: usize) {
    use std::sync::atomic::Ordering::Relaxed;
    match kind {
        StripKind::Aux if slot >= 1 => {
            let fx = crate::sources::FX_RETURN_SOURCE_BASE + 2 * (slot as i32 - 1);
            p.fader_db.store(0.0);
            p.input_left.store(fx, Relaxed);
            p.input_right.store(fx + 1, Relaxed);
        }
        StripKind::Aux => {
            // ST IN: the interface's inputs 17-18.
            p.input_left.store(16, Relaxed);
            p.input_right.store(17, Relaxed);
        }
        StripKind::Bus => {
            p.stereo.store(false, Relaxed);
            if slot < 2 {
                // SEND1-2 feed the effects and have no master (p.32).
                p.fader_db.store(0.0);
            }
        }
        _ => {}
    }
}

/// The ProMix 01's strip names: ST IN, RTN1-2, SEND1-4 and ST OUT as its
/// panel prints them.
fn promix01_name(_spec: &ProfileSpec, kind: StripKind, slot: usize) -> Option<String> {
    match (kind, slot) {
        (StripKind::Aux, 0) => Some("ST IN".into()),
        (StripKind::Aux, _) => Some(format!("RTN{slot}")),
        (StripKind::Bus, _) => Some(format!("SEND{}", slot + 1)),
        (StripKind::Main, 0) => Some("ST OUT".into()),
        _ => None,
    }
}

/// A new ProMix 01's output patch: ST OUT on outputs 1-2 (STEREO OUT), 5-6
/// (REC OUT) and 9-10 (DIGITAL OUT), SEND3-4 on 3-4 (`PROMIX01_PORT_GROUPS`).
fn promix01_factory_outputs(_spec: &ProfileSpec, patch: &crate::routing::OutputPatch) {
    use crate::routing::strip_source_input;
    for side in 0..2 {
        let st = strip_source_input(StripKind::Main, 0, side);
        for port in [0, 4, 8] {
            patch.set(port + side as i32, st);
        }
        patch.set(2 + side as i32, strip_source_input(StripKind::Bus, 2 + side, 0));
    }
}

/// The ProMix 01's strip labels: CH 1-16, the sends, ST OUT; its aux strips go
/// by their slot ranges' (ST IN, RTN).
const PROMIX01_LABELS: [&str; 5] = ["CH", "ST IN", "SEND", "ST OUT", "MTX"];

/// Sidecar's colours for the ProMix 01 (`Palette::PROMIX01`).
const PROMIX01_COLOURS: [(&str, u32); 8] = [
    ("GRAY", 0x606060),
    ("RED", 0xe02424),
    ("GREEN", 0x28c040),
    ("YELLOW", 0xe8d020),
    ("BLUE", 0x2850e8),
    ("MAGENTA", 0xd028d0),
    ("CYAN", 0x20c8d8),
    ("WHITE", 0xe8e8e8),
];

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
    output_orders: &["DI"],
    fader: FaderLaw::WING,
    main_fader: None,
    bus_fader: None,
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
    factory_eq_block: None,
    factory_eq_after: false,
    factory_preamp_block: None,
    dlive_layout: None,
    attenuator: false,
    delay_samples: None,
    fx_send_buses: &[],
};

/// No console's limits. Its strips may hold any emulated console's blocks
/// (`livedaw_dsp::universal`: the WING's, then the X32's), it speaks the WING's
/// remote protocols, and a new one starts with the WING's counts.
static UNIVERSAL: ProfileSpec = ProfileSpec {
    profile: Profile::Universal,
    name: "Universal",
    counts: Counts::WING,
    templates: [chain::CHANNEL, chain::AUX, chain::OUTPUT],
    process_orders: PROCESS_ORDERS,
    aux_order: b"DEI",
    output_orders: &["DI"],
    fader: FaderLaw::WING,
    main_fader: None,
    bus_fader: None,
    pan: PanLaw::WING,
    port_groups: WING_PORT_GROUPS,
    models: &livedaw_dsp::universal::MODELS,
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
    factory_eq_block: None,
    factory_eq_after: false,
    factory_preamp_block: None,
    dlive_layout: None,
    attenuator: false,
    delay_samples: None,
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
    output_orders: &["EDI"],
    fader: FaderLaw::X32,
    main_fader: None,
    bus_fader: None,
    pan: PanLaw::WING,
    port_groups: X32_PORT_GROUPS,
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
    factory_eq_block: None,
    factory_eq_after: false,
    factory_preamp_block: None,
    dlive_layout: None,
    attenuator: false,
    delay_samples: None,
    fx_send_buses: &[12, 13, 14, 15],
};

/// The Yamaha ProMix 01 (Phase 5): its counts (`Counts::PROMIX01`), its
/// 128-step fader law, its labels and names (ST IN, RTN1-2, SEND1-4, ST OUT),
/// its port groups, its factory strip and output patch; its models
/// (`livedaw_dsp::promix`: the 3-band EQ, the compressor, gate and ducking)
/// and chains: channels phase, EQ, dynamics, fader; ST IN and the returns EQ
/// and fader (as the X32's aux in); the sends and ST OUT (EQ, ST OUT only)
/// the dynamics PRE or POST their fader (`chain::PROMIX01_OUTPUT_ORDERS`).
/// It speaks its MIDI (`livedaw-promix`), its only remote protocol.
static PROMIX01: ProfileSpec = ProfileSpec {
    profile: Profile::ProMix01,
    name: "ProMix 01",
    counts: Counts::PROMIX01,
    templates: [chain::PROMIX01_CHANNEL, chain::X32_AUX, chain::PROMIX01_SEND],
    process_orders: chain::PROMIX01_ORDERS,
    aux_order: b"",
    output_orders: chain::PROMIX01_OUTPUT_ORDERS,
    fader: FaderLaw::PROMIX01,
    main_fader: None,
    bus_fader: None,
    pan: PanLaw::WING,
    port_groups: PROMIX01_PORT_GROUPS,
    models: &livedaw_dsp::promix::MODELS,
    remotes: Remotes { midi: Some(MidiProtocol::ProMix01), network: &[] },
    palette: Palette::PROMIX01,
    labels: PROMIX01_LABELS,
    slots: &[
        SlotRange { kind: StripKind::Aux, first: 0, count: 1, chain: chain::X32_AUX, label: "ST IN" },
        SlotRange { kind: StripKind::Aux, first: 1, count: 2, chain: chain::X32_AUX, label: "RTN" },
        SlotRange { kind: StripKind::Main, first: 0, count: 1, chain: chain::PROMIX01_MAIN, label: "ST OUT" },
    ],
    // The sends don't feed ST OUT, the only main.
    buses_to_main: 0..0,
    mono_mains: &[],
    source_link: false,
    sends_on: true,
    send_tap: -1,
    tap_point: crate::strip::tap::PFL as i32,
    factory: Some(promix01_factory),
    factory_name: Some(promix01_name),
    factory_outputs: Some(promix01_factory_outputs),
    dca_names: None,
    dca_fader_db: 0.0,
    // REVERB HALL 1 and DELAY L-C-R (Sidecar's choice: the guide doesn't say
    // what scene 00's effects are).
    fx_factory: &["PMREV", "PMDLCR"],
    factory_eq_block: None,
    factory_eq_after: false,
    factory_preamp_block: None,
    dlive_layout: None,
    attenuator: false,
    delay_samples: None,
    fx_send_buses: &[0, 1],
};

/// The 02R's strips (Phase 6): channel slots 16-19 are LINE 17/18-23/24,
/// 20-35 TAPE 1-16; bus slots 8-15 AUX 1-8.
pub mod y02r {
    /// The first LINE channel (0-based), and the first tape return.
    pub const LINE: usize = 16;
    pub const TAPE: usize = 20;
    /// AUX 1-8 are bus slots 8-15 (after BUS 1-8).
    pub const AUX: usize = 8;
}

/// The 02R's factory strip (`ProfileSpec::factory`), as its scene 0 starts
/// (MAN-GS pp.22-56, V2 p.129): LINE 17/18-23/24 stereo on inputs 17-24, the
/// tape returns on the card slots 1-2 (TAPE 1-8 slot 1, 9-16 slot 2), the
/// effect returns stereo, fed by the internal effects (FX 1-2:
/// `fx_send_buses`, AUX 7-8); BUS 1-8 and AUX 1-8 mono with their masters at
/// 0 dB (V2 p.129).
fn y02r_factory(_spec: &ProfileSpec, p: &crate::params::StripParams, kind: StripKind, slot: usize) {
    use std::sync::atomic::Ordering::Relaxed;
    match kind {
        StripKind::Channel if (y02r::LINE..y02r::TAPE).contains(&slot) => {
            let first = (y02r::LINE + 2 * (slot - y02r::LINE)) as i32;
            p.stereo.store(true, Relaxed);
            p.input_left.store(first, Relaxed);
            p.input_right.store(first + 1, Relaxed);
        }
        StripKind::Channel if slot >= y02r::TAPE => {
            // Card slot 1's 8 inputs are engine inputs 32-39, slot 2's 40-47.
            p.input_left.store(32 + (slot - y02r::TAPE) as i32, Relaxed);
        }
        StripKind::Aux => {
            let fx = crate::sources::FX_RETURN_SOURCE_BASE + 2 * slot as i32;
            p.stereo.store(true, Relaxed);
            p.input_left.store(fx, Relaxed);
            p.input_right.store(fx + 1, Relaxed);
        }
        StripKind::Bus => {
            p.stereo.store(false, Relaxed);
            p.fader_db.store(0.0);
        }
        _ => {}
    }
}

/// The 02R's strip names as its panel and display print them.
fn y02r_name(_spec: &ProfileSpec, kind: StripKind, slot: usize) -> Option<String> {
    match (kind, slot) {
        (StripKind::Channel, s) if (y02r::LINE..y02r::TAPE).contains(&s) => {
            let n = 17 + 2 * (s - y02r::LINE);
            Some(format!("LINE {n}/{}", n + 1))
        }
        (StripKind::Channel, s) if s >= y02r::TAPE => Some(format!("TAPE {}", s - y02r::TAPE + 1)),
        (StripKind::Aux, s) => Some(format!("EFF{}", s + 1)),
        (StripKind::Bus, s) if s < y02r::AUX => Some(format!("BUS {}", s + 1)),
        (StripKind::Bus, s) => Some(format!("AUX {}", s - y02r::AUX + 1)),
        (StripKind::Main, 0) => Some("STEREO".into()),
        _ => None,
    }
}

/// A new 02R's output patch (MAN p.11, p.39): STEREO on STEREO OUT (1-2) and
/// 2TR OUT D1 and D2 (9-12), AUX 1-6 on the AUX SEND outputs (3-8), BUS 1-8
/// on each card slot's 8 outputs (its direct outs off).
fn y02r_factory_outputs(_spec: &ProfileSpec, patch: &crate::routing::OutputPatch) {
    use crate::routing::strip_source_input;
    for side in 0..2 {
        let st = strip_source_input(StripKind::Main, 0, side);
        for port in [0, 8, 10] {
            patch.set(port + side as i32, st);
        }
    }
    for aux in 0..6 {
        patch.set(2 + aux as i32, strip_source_input(StripKind::Bus, y02r::AUX + aux, 0));
    }
    for slot in 0..4 {
        for bus in 0..8 {
            patch.set(32 + 8 * slot + bus as i32, strip_source_input(StripKind::Bus, bus, 0));
        }
    }
}

/// The 02R's strip labels: CH 1-16 (and its slot ranges LINE, TAPE), EFF,
/// BUS (and AUX), STEREO.
const Y02R_LABELS: [&str; 5] = ["CH", "EFF", "BUS", "STEREO", "MTX"];

/// The Yamaha 02R (Phase 6), Version 2: its counts (`Counts::Y02R`), its
/// 128-step fader laws (the channels' +10 to -90 dB, STEREO's 0 to -120 dB),
/// its labels and names (MIC/LINE 1-16, LINE 17/18-23/24, TAPE 1-16, EFF1-2,
/// BUS 1-8, AUX 1-8, STEREO), its port groups, its factory strip and output
/// patch; its models (`livedaw_dsp::y02r`: the 4-band EQ, the six dynamics
/// types) and chains: the inputs and tape returns attenuator and phase, EQ,
/// dynamics, delay, fader; the effect returns no dynamics; BUS 1-8 dynamics and
/// master, AUX 1-8 the master alone; STEREO EQ, dynamics and fader
/// (`chain::Y02R_*`). It speaks its MIDI (`livedaw-02r`), its only remote.
static Y02R: ProfileSpec = ProfileSpec {
    profile: Profile::Yamaha02R,
    name: "02R",
    counts: Counts::Y02R,
    templates: [chain::Y02R_CHANNEL, chain::Y02R_RETURN, chain::Y02R_BUS],
    process_orders: chain::Y02R_ORDERS,
    aux_order: b"",
    output_orders: &["DI"],
    fader: FaderLaw::Y02R,
    main_fader: Some(FaderLaw::Y02R_STEREO),
    bus_fader: None,
    pan: PanLaw::WING,
    port_groups: Y02R_PORT_GROUPS,
    models: &livedaw_dsp::y02r::MODELS,
    remotes: Remotes { midi: Some(MidiProtocol::Yamaha02R), network: &[] },
    palette: Palette::Y02R,
    labels: Y02R_LABELS,
    slots: &[
        SlotRange { kind: StripKind::Channel, first: y02r::LINE, count: 4, chain: chain::Y02R_CHANNEL, label: "LINE" },
        SlotRange { kind: StripKind::Channel, first: y02r::TAPE, count: 16, chain: chain::Y02R_CHANNEL, label: "TAPE" },
        SlotRange { kind: StripKind::Bus, first: y02r::AUX, count: 8, chain: chain::Y02R_AUX, label: "AUX" },
        SlotRange { kind: StripKind::Main, first: 0, count: 1, chain: chain::Y02R_STEREO, label: "STEREO" },
    ],
    // BUS 1-8 and AUX 1-8 don't feed STEREO (MAN p.195).
    buses_to_main: 0..0,
    mono_mains: &[],
    source_link: false,
    // The aux sends start on (MAN-GS p.43).
    sends_on: true,
    send_tap: -1,
    tap_point: crate::strip::tap::PFL as i32,
    factory: Some(y02r_factory),
    factory_name: Some(y02r_name),
    factory_outputs: Some(y02r_factory_outputs),
    dca_names: None,
    dca_fader_db: 0.0,
    // REVERB HALL1 and DELAY L-C-R 1 (Sidecar's choice: the guides don't say
    // what scene 0's effects are).
    fx_factory: &["Y2REV", "Y2DLCR"],
    factory_eq_block: None,
    factory_eq_after: false,
    factory_preamp_block: None,
    dlive_layout: None,
    // AUX 7 feeds Effect 1, AUX 8 Effect 2 (MAN p.90).
    attenuator: false,
    delay_samples: None,
    fx_send_buses: &[y02r::AUX + 6, y02r::AUX + 7],
};

/// Where the 01V's channels and buses of each sort start.
pub mod y01v {
    /// The stereo channels 13/14 and 15/16, then CH 17-24 (0-based).
    pub const STEREO: usize = 12;
    pub const OPTION: usize = 14;
    /// AUX 1-4 are bus slots 4-7 (after BUS 1-4), EFFECT 1-2 slots 8-9.
    pub const AUX: usize = 4;
    pub const EFF: usize = 8;
}

/// A new 01V's strips, as its scene 00 starts them where the manual says: 13/14
/// and 15/16 stereo, CH 17-24 on the Option I/O card with their 2-band EQ, the
/// effect returns stereo on the internal effects (FX 1-2: `fx_send_buses`), the
/// buses mono with their masters at 0 dB.
fn y01v_factory(_spec: &ProfileSpec, p: &crate::params::StripParams, kind: StripKind, slot: usize) {
    use std::sync::atomic::Ordering::Relaxed;
    match kind {
        StripKind::Channel if (y01v::STEREO..y01v::OPTION).contains(&slot) => {
            let first = (y01v::STEREO + 2 * (slot - y01v::STEREO)) as i32;
            p.stereo.store(true, Relaxed);
            p.input_left.store(first, Relaxed);
            p.input_right.store(first + 1, Relaxed);
        }
        StripKind::Channel if slot >= y01v::OPTION => {
            // Their EQ is the 2-band (Owner's Manual p.264).
            p.input_left.store(16 + (slot - y01v::OPTION) as i32, Relaxed);
            p.eq().set_model(livedaw_dsp::y01v::TWO_BAND_EQ_INDEX);
        }
        StripKind::Aux => {
            let fx = crate::sources::FX_RETURN_SOURCE_BASE + 2 * slot as i32;
            p.stereo.store(true, Relaxed);
            p.input_left.store(fx, Relaxed);
            p.input_right.store(fx + 1, Relaxed);
        }
        StripKind::Bus => {
            p.stereo.store(false, Relaxed);
            p.fader_db.store(0.0);
        }
        _ => {}
    }
}

/// The 01V's strip names as its panel prints them.
fn y01v_name(_spec: &ProfileSpec, kind: StripKind, slot: usize) -> Option<String> {
    match (kind, slot) {
        (StripKind::Channel, s) if (y01v::STEREO..y01v::OPTION).contains(&s) => {
            let n = 13 + 2 * (s - y01v::STEREO);
            Some(format!("{n}/{}", n + 1))
        }
        (StripKind::Channel, s) if s >= y01v::OPTION => Some(format!("CH {}", s - y01v::OPTION + 17)),
        (StripKind::Aux, s) => Some(format!("RETURN {}", s + 1)),
        (StripKind::Bus, s) if s < y01v::AUX => Some(format!("BUS {}", s + 1)),
        (StripKind::Bus, s) if s < y01v::EFF => Some(format!("AUX {}", s - y01v::AUX + 1)),
        (StripKind::Bus, s) => Some(format!("EFFECT {}", s - y01v::EFF + 1)),
        (StripKind::Main, 0) => Some("STEREO".into()),
        _ => None,
    }
}

/// A new 01V's output patch: STEREO on STEREO OUT, 2TR OUT and DIGITAL STEREO
/// OUT, AUX 1-4 on OMNI OUT 1-4 (Owner's Manual p.114), BUS 1-4 and AUX 1-4 on
/// the Option I/O card's 8 outputs.
fn y01v_factory_outputs(_spec: &ProfileSpec, patch: &crate::routing::OutputPatch) {
    use crate::routing::strip_source_input;
    for side in 0..2 {
        let st = strip_source_input(StripKind::Main, 0, side);
        for port in [0, 6, 8] {
            patch.set(port + side as i32, st);
        }
    }
    for aux in 0..4 {
        patch.set(2 + aux as i32, strip_source_input(StripKind::Bus, y01v::AUX + aux, 0));
    }
    for bus in 0..8 {
        patch.set(16 + bus as i32, strip_source_input(StripKind::Bus, bus, 0));
    }
}

/// The 01V's strip labels: CH 1-12 (and its slot ranges ST, OPT), RTN, BUS (and
/// AUX, EFF), STEREO.
const Y01V_LABELS: [&str; 5] = ["CH", "RTN", "BUS", "STEREO", "MTX"];

/// The Yamaha 01V (the original, Owner's Manual): 24 inputs on 22 channels, its
/// 128-step fader laws (the inputs' +6 to -72 dB, the masters' 0 to -72 dB,
/// STEREO's 0 to -96 dB), its labels and names, its port groups, its factory
/// strip and output patch; its models (`livedaw_dsp::y01v`) and chains (pp.24-25,
/// 60); the effects are fed by EFFECT 1-2. Its MIDI is `livedaw-01v`.
static Y01V: ProfileSpec = ProfileSpec {
    profile: Profile::Yamaha01V,
    name: "01V",
    counts: Counts::Y01V,
    templates: [chain::Y01V_CHANNEL, chain::Y01V_RETURN, chain::Y01V_BUS],
    process_orders: chain::Y02R_ORDERS,
    aux_order: b"",
    output_orders: &["DI"],
    fader: FaderLaw::Y01V,
    main_fader: Some(FaderLaw::Y01V_STEREO),
    bus_fader: Some(FaderLaw::Y01V_MASTER),
    pan: PanLaw::WING,
    port_groups: Y01V_PORT_GROUPS,
    models: &livedaw_dsp::y01v::MODELS,
    remotes: Remotes { midi: Some(MidiProtocol::Yamaha01V), network: &[] },
    palette: Palette::Y01V,
    labels: Y01V_LABELS,
    slots: &[
        SlotRange { kind: StripKind::Channel, first: y01v::STEREO, count: 2, chain: chain::Y01V_CHANNEL, label: "ST" },
        SlotRange { kind: StripKind::Channel, first: y01v::OPTION, count: 8, chain: chain::Y01V_OPTION, label: "OPT" },
        SlotRange { kind: StripKind::Bus, first: y01v::AUX, count: 4, chain: chain::Y01V_AUX, label: "AUX" },
        SlotRange { kind: StripKind::Bus, first: y01v::EFF, count: 2, chain: chain::Y01V_BUS, label: "EFF" },
        SlotRange { kind: StripKind::Main, first: 0, count: 1, chain: chain::Y01V_STEREO, label: "STEREO" },
    ],
    // Each bus has its own TO ST switch, off at first (Sidecar's reading: the
    // manual doesn't say).
    buses_to_main: 0..0,
    mono_mains: &[],
    source_link: false,
    sends_on: true,
    send_tap: -1,
    tap_point: crate::strip::tap::PFL as i32,
    factory: Some(y01v_factory),
    factory_name: Some(y01v_name),
    factory_outputs: Some(y01v_factory_outputs),
    dca_names: None,
    dca_fader_db: 0.0,
    // HALL and CHORUS, as a console's display shows them on scene 00 (a photo,
    // November 2026, with the scene's EDIT flag on; the manual doesn't say).
    fx_factory: &["O1HALL", "O1CHO"],
    factory_eq_block: None,
    factory_eq_after: false,
    factory_preamp_block: None,
    dlive_layout: None,
    // EFFECT 1 feeds Effect 1, EFFECT 2 Effect 2 (p.24).
    attenuator: false,
    delay_samples: None,
    fx_send_buses: &[y01v::EFF, y01v::EFF + 1],
};

/// A dLive's Main mix (Mixer Config; Firmware Reference Guide, MixRack Setup). How many
/// of the 64 busses a mode takes isn't stated: Sidecar counts one for each signal, Msum's
/// included (it has its own output and master).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
pub enum MainMode {
    /// No Main mix: a monitor console's.
    None,
    /// Standard stereo.
    #[default]
    Lr,
    /// LR, and an output of their mono sum, pre-fade and pre-processing.
    LrMsum,
    /// LR and an independent mono M: a channel assigns to any of the two.
    LrM,
    /// Pan runs left, centre, right; stereo channels feed LR alone.
    Lcr,
    /// Pan balances L and R; each channel's Divergence blends C against LR.
    LcrPlus,
    /// 5.1: LR, C, LFE, Ls/Rs.
    Surround,
}

impl MainMode {
    pub const ALL: [MainMode; 7] =
        [MainMode::None, MainMode::Lr, MainMode::LrMsum, MainMode::LrM, MainMode::Lcr, MainMode::LcrPlus, MainMode::Surround];

    pub fn index(self) -> i32 {
        Self::ALL.iter().position(|m| *m == self).unwrap_or(1) as i32
    }

    pub fn from_index(i: i32) -> Option<Self> {
        Self::ALL.get(usize::try_from(i).ok()?).copied()
    }

    /// Of the 64 busses.
    pub fn busses(self) -> u32 {
        match self {
            MainMode::None => 0,
            MainMode::Lr => 2,
            MainMode::Surround => 6,
            _ => 3,
        }
    }

    /// The engine's main strips (a console has at least one: None's stays silent).
    pub fn mains(self) -> usize {
        match self {
            MainMode::None | MainMode::Lr => 1,
            MainMode::Surround => 4,
            _ => 2,
        }
    }

    /// Main strip `slot` carries a mono signal.
    pub fn is_mono(self, slot: usize) -> bool {
        match self {
            MainMode::Surround => slot == 1 || slot == 2,
            _ => slot > 0,
        }
    }

    /// Main strip `slot`'s name.
    pub fn strip_name(self, slot: usize) -> Option<&'static str> {
        Some(match (self, slot) {
            (_, 0) => "LR",
            (MainMode::LrMsum, 1) => "Msum",
            (MainMode::LrM, 1) => "M",
            (MainMode::Lcr | MainMode::LcrPlus | MainMode::Surround, 1) => "C",
            (MainMode::Surround, 2) => "LFE",
            (MainMode::Surround, 3) => "Ls/Rs",
            _ => return None,
        })
    }

    /// The channel's assignment and level to main `slot` are its send to LR's (the
    /// signals of one Main mix); else the send to `slot` itself.
    pub fn shares_lr_send(self, slot: usize) -> bool {
        match self {
            MainMode::LrMsum | MainMode::Lcr | MainMode::LcrPlus | MainMode::Surround => slot > 0,
            _ => false,
        }
    }
}

/// How a dLive's 64 configurable mix busses are laid out (its Mixer Config:
/// Firmware Reference Guide, MixRack Setup). A stereo mix takes two busses, mono
/// ones come and go in pairs, the Main takes what its mode does, and each stereo PAFL
/// bus two. They run, in the engine's bus
/// slots, Groups then Aux then FX sends, each mono then stereo; the matrices mono
/// then stereo. (The RackUltra card's 8 stereo sends are outside the 64, and not
/// here yet.) `stereo_inputs` is its IP Stereo Config: bit n set, inputs 2n+1 and 2n+2
/// are one stereo channel.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct DLiveLayout {
    pub grp: u8,
    pub st_grp: u8,
    pub aux: u8,
    pub st_aux: u8,
    pub fx: u8,
    pub st_fx: u8,
    pub mtx: u8,
    pub st_mtx: u8,
    pub stereo_inputs: u64,
    /// Stereo PAFL busses (1 for a normal system; only the first is modelled, the others
    /// only take their busses).
    pub pafl: u8,
    pub main: MainMode,
}

impl DLiveLayout {
    /// Its FoH Template Show: a new console's.
    pub const FOH: DLiveLayout = DLiveLayout {
        grp: 4,
        st_grp: 4,
        aux: 8,
        st_aux: 8,
        fx: 8,
        st_fx: 0,
        mtx: 4,
        st_mtx: 4,
        stereo_inputs: 0xf000_0000_0000_0000,
        pafl: 1,
        main: MainMode::Lr,
    };
    /// The busses there are, and the RackExtra FX sends.
    pub const BUSSES: u32 = 64;
    pub const MAX_FX_SENDS: u32 = 16;
    /// The most PAFL busses: one for each of up to 4 Surfaces (Sidecar's reading of the
    /// guide's Multi-Surface and PAFL sections; it gives no maximum).
    pub const MAX_PAFL: u8 = 4;

    /// From `values()`'s order; None if one is out of range.
    pub fn from_values(v: [i32; 8], stereo_inputs: u64) -> Option<Self> {
        let n = |i: usize| u8::try_from(v[i]).ok();
        Some(Self {
            grp: n(0)?,
            st_grp: n(1)?,
            aux: n(2)?,
            st_aux: n(3)?,
            fx: n(4)?,
            st_fx: n(5)?,
            mtx: n(6)?,
            st_mtx: n(7)?,
            stereo_inputs,
            pafl: 1,
            main: MainMode::Lr,
        })
    }

    /// The input channels in slot order: the first input (0-based) of each, and whether
    /// it is a stereo channel.
    pub fn channel_inputs(self) -> Vec<(usize, bool)> {
        let mut channels = Vec::with_capacity(128);
        for pair in 0..64 {
            let first = 2 * pair;
            if self.stereo_inputs >> pair & 1 != 0 {
                channels.push((first, true));
            } else {
                channels.push((first, false));
                channels.push((first + 1, false));
            }
        }
        channels
    }

    /// Groups, stereo Groups, Aux, stereo Aux, FX, stereo FX, Matrices, stereo Matrices.
    pub fn values(self) -> [i32; 8] {
        [self.grp, self.st_grp, self.aux, self.st_aux, self.fx, self.st_fx, self.mtx, self.st_mtx].map(i32::from)
    }

    /// How many of the 64 busses it takes, the Main's and the PAFL's included.
    pub fn busses_used(self) -> u32 {
        let v = self.values().map(|n| n as u32);
        v[0] + 2 * v[1] + v[2] + 2 * v[3] + v[4] + 2 * v[5] + v[6] + 2 * v[7] + self.main.busses() + 2 * u32::from(self.pafl)
    }

    /// What the Mixer Config screen shows as Mixes Remaining (negative: too many).
    pub fn busses_remaining(self) -> i32 {
        Self::BUSSES as i32 - self.busses_used() as i32
    }

    /// It fits: the busses, the FX sends, mono mixes in pairs, the PAFL busses.
    pub fn is_valid(self) -> bool {
        self.busses_used() <= Self::BUSSES
            && self.pafl <= Self::MAX_PAFL
            && u32::from(self.fx) + u32::from(self.st_fx) <= Self::MAX_FX_SENDS
            && [self.grp, self.aux, self.fx, self.mtx].iter().all(|n| n % 2 == 0)
    }

    /// Where each range starts in the bus slots.
    pub fn st_grp_at(self) -> usize {
        usize::from(self.grp)
    }
    pub fn aux_at(self) -> usize {
        self.st_grp_at() + usize::from(self.st_grp)
    }
    pub fn st_aux_at(self) -> usize {
        self.aux_at() + usize::from(self.aux)
    }
    pub fn fx_at(self) -> usize {
        self.st_aux_at() + usize::from(self.st_aux)
    }
    pub fn st_fx_at(self) -> usize {
        self.fx_at() + usize::from(self.fx)
    }

    pub fn counts(self) -> Counts {
        Counts {
            channels: 128 - self.stereo_inputs.count_ones() as usize,
            buses: self.st_fx_at() + usize::from(self.st_fx),
            matrices: usize::from(self.mtx) + usize::from(self.st_mtx),
            mains: self.main.mains(),
            ..Counts::DLIVE
        }
    }

    /// What a show stores: the eight numbers, the stereo inputs, PAFL busses, Main mode.
    pub fn to_text(self) -> String {
        format!("{},{},{},{}", self.values().map(|n| n.to_string()).join(","), self.stereo_inputs, self.pafl, self.main.index())
    }

    /// Shows from before the PAFL busses counted have only the eight numbers and the
    /// stereo inputs: they get one PAFL bus, or none if it wouldn't fit any more.
    pub fn from_text(text: &str) -> Option<Self> {
        let mut parts = text.split(',').map(str::trim);
        let numbers: Vec<i32> = parts.by_ref().take(8).map(|n| n.parse().ok()).collect::<Option<_>>()?;
        let stereo_inputs = parts.next()?.parse().ok()?;
        let layout = Self::from_values(numbers.try_into().ok()?, stereo_inputs)?;
        let Some(pafl) = parts.next() else {
            return Some(if layout.busses_used() > Self::BUSSES { Self { pafl: 0, ..layout } } else { layout });
        };
        let pafl = pafl.parse().ok()?;
        let main = MainMode::from_index(parts.next()?.parse().ok()?)?;
        Some(Self { pafl, main, ..layout })
    }

    fn bus_is_stereo(self, slot: usize) -> bool {
        (self.st_grp_at()..self.aux_at()).contains(&slot)
            || (self.st_aux_at()..self.fx_at()).contains(&slot)
            || slot >= self.st_fx_at()
    }

    /// A bus slot's name and number ("StAux", 2).
    fn bus_name(self, slot: usize) -> (&'static str, usize) {
        if slot < self.st_grp_at() {
            ("Grp", slot + 1)
        } else if slot < self.aux_at() {
            ("StGrp", slot - self.st_grp_at() + 1)
        } else if slot < self.st_aux_at() {
            ("Aux", slot - self.aux_at() + 1)
        } else if slot < self.fx_at() {
            ("StAux", slot - self.st_aux_at() + 1)
        } else if slot < self.st_fx_at() {
            ("FX", slot - self.fx_at() + 1)
        } else {
            ("StFX", slot - self.st_fx_at() + 1)
        }
    }

    /// The mixes on the MixRack's outputs after the LR, as far as 32 go: Aux, stereo
    /// Aux, Matrices, stereo Matrices, leaving a socket that already has something.
    /// (The FoH template's, where it's Sidecar's reading; the rest follows it.)
    pub fn patch_mix_outputs(self, patch: &crate::routing::OutputPatch) {
        use crate::routing::strip_source_input;
        let mut port = 2i32;
        let mut place = |kind: StripKind, index: usize, stereo: bool| {
            for side in 0..1 + usize::from(stereo) {
                if port < 32 && patch.get(port) < 0 {
                    patch.set(port, strip_source_input(kind, index, side));
                }
                port += 1;
            }
        };
        // The Main's other signals come right after the LR (Sidecar's reading).
        for slot in 1..self.main.mains() {
            place(StripKind::Main, slot, !self.main.is_mono(slot));
        }
        for i in 0..usize::from(self.aux) {
            place(StripKind::Bus, self.aux_at() + i, false);
        }
        for i in 0..usize::from(self.st_aux) {
            place(StripKind::Bus, self.st_aux_at() + i, true);
        }
        for i in 0..usize::from(self.mtx) {
            place(StripKind::Matrix, i, false);
        }
        for i in 0..usize::from(self.st_mtx) {
            place(StripKind::Matrix, usize::from(self.mtx) + i, true);
        }
    }

    fn slot_ranges(self) -> Vec<SlotRange> {
        let bus = |first: usize, count: u8, chain: &'static [Node], label: &'static str| SlotRange {
            kind: StripKind::Bus,
            first,
            count: usize::from(count),
            chain,
            label,
        };
        // The stereo channels, a range for each run of them.
        let mut ranges: Vec<SlotRange> = Vec::new();
        for (slot, _) in self.channel_inputs().iter().enumerate().filter(|(_, (_, stereo))| *stereo) {
            match ranges.last_mut() {
                Some(run) if run.first + run.count == slot => run.count += 1,
                _ => ranges.push(SlotRange {
                    kind: StripKind::Channel,
                    first: slot,
                    count: 1,
                    chain: chain::DLIVE_CHANNEL,
                    label: "ST",
                }),
            }
        }
        ranges.extend([
            bus(self.st_grp_at(), self.st_grp, chain::DLIVE_MIX, "STGRP"),
            bus(self.aux_at(), self.aux, chain::DLIVE_MIX, "AUX"),
            bus(self.st_aux_at(), self.st_aux, chain::DLIVE_MIX, "STAUX"),
            bus(self.fx_at(), self.fx, chain::DLIVE_FX_SEND, "FX"),
            bus(self.st_fx_at(), self.st_fx, chain::DLIVE_FX_SEND, "STFX"),
            SlotRange {
                kind: StripKind::Matrix,
                first: usize::from(self.mtx),
                count: usize::from(self.st_mtx),
                chain: chain::DLIVE_MIX,
                label: "STMTX",
            },
        ]);
        ranges.retain(|r| r.count > 0);
        ranges
    }

    fn build_spec(self) -> ProfileSpec {
        let fx_sends = usize::from(self.fx) + usize::from(self.st_fx);
        let sends: Vec<usize> = (0..fx_sends).map(|i| self.fx_at() + i).collect();
        ProfileSpec {
            profile: Profile::AhDLive(self),
            counts: self.counts(),
            slots: Box::leak(self.slot_ranges().into_boxed_slice()),
            buses_to_main: 0..self.aux_at(),
            fx_send_buses: Box::leak(sends.into_boxed_slice()),
            dlive_layout: Some(self),
            ..DLIVE
        }
    }
}

/// What a dLive of `layout` is: the FoH template's is the static one, any other is
/// made once and kept (a console goes through only a few layouts).
fn dlive_spec(layout: DLiveLayout) -> &'static ProfileSpec {
    use std::collections::HashMap;
    use std::sync::{Mutex, OnceLock};
    if layout == DLiveLayout::FOH {
        return &DLIVE;
    }
    static SPECS: OnceLock<Mutex<HashMap<DLiveLayout, &'static ProfileSpec>>> = OnceLock::new();
    let mut specs = SPECS.get_or_init(Default::default).lock().unwrap_or_else(|e| e.into_inner());
    specs.entry(layout).or_insert_with(|| Box::leak(Box::new(layout.build_spec())))
}

pub mod dlive {
    /// The stereo channels 121/122 to 127/128 (0-based, after the 120 mono).
    pub const STEREO: usize = 120;
    /// Bus slots: Grp 1-4 (mono) from 0, then these.
    pub const ST_GRP: usize = 4;
    pub const AUX: usize = 8;
    pub const ST_AUX: usize = 16;
    pub const FX: usize = 24;
    /// Matrices: Mtx 1-4 (mono) from 0, then the stereo ones.
    pub const ST_MTX: usize = 4;
}

/// A new dLive's strips, as its FoH Template Show starts them (Template Shows
/// chapter): every input muted; Ip 1-64 on the MixRack's sockets, 65-96 on DX
/// 1/2, 97-114 on DX 3/4, 115-120 on Surface 1-6; 121/122 stereo on Surface 7/8,
/// 123/124 and 125/126 on Surface AES 9/10 and 11/12, 127/128 on the Surface's
/// USB playback; the FX returns stereo on their FX; Groups, Aux and Matrices 1-4
/// mono, 5-8 (their stereo ones) stereo. Their masters and LR at 0 dB are
/// Sidecar's reading.
fn dlive_factory(spec: &ProfileSpec, p: &crate::params::StripParams, kind: StripKind, slot: usize) {
    use std::sync::atomic::Ordering::Relaxed;
    let layout = spec.dlive_layout.unwrap_or(DLiveLayout::FOH);
    match kind {
        StripKind::Channel => {
            p.mute.store(true, Relaxed);
            // By its input number, as the Template Show patches them.
            let source = |input: usize| match input {
                0..=113 => input as i32,
                114..=125 => dlive_ports::SURFACE + (input - 114) as i32,
                _ => livedaw_recorder::USB_PLAYER_SOURCE_BASE + (input - 126) as i32,
            };
            if let Some(&(first, stereo)) = layout.channel_inputs().get(slot) {
                p.input_left.store(source(first), Relaxed);
                if stereo {
                    p.stereo.store(true, Relaxed);
                    p.input_right.store(source(first + 1), Relaxed);
                }
            }
        }
        StripKind::Aux => {
            let fx = crate::sources::FX_RETURN_SOURCE_BASE + 2 * slot as i32;
            p.mute.store(true, Relaxed);
            p.stereo.store(true, Relaxed);
            p.input_left.store(fx, Relaxed);
            p.input_right.store(fx + 1, Relaxed);
        }
        StripKind::Bus => {
            p.stereo.store(layout.bus_is_stereo(slot), Relaxed);
            p.fader_db.store(0.0);
        }
        StripKind::Matrix => {
            p.stereo.store(slot >= usize::from(layout.mtx), Relaxed);
            p.fader_db.store(0.0);
        }
        StripKind::Main => {
            p.fader_db.store(0.0);
            if layout.main.is_mono(slot) {
                p.stereo.store(false, Relaxed);
                p.mono.store(true, Relaxed);
            }
        }
    }
}

/// A new dLive strip's name, in the SQ's style (Sidecar's reading: the guides
/// don't list them), numbered as its MIDI protocol numbers them.
fn dlive_name(spec: &ProfileSpec, kind: StripKind, slot: usize) -> Option<String> {
    let layout = spec.dlive_layout.unwrap_or(DLiveLayout::FOH);
    match (kind, slot) {
        (StripKind::Channel, s) => {
            let &(first, stereo) = layout.channel_inputs().get(s)?;
            Some(if stereo { format!("{}/{}", first + 1, first + 2) } else { format!("Ip{}", first + 1) })
        }
        (StripKind::Aux, s) => Some(format!("Fx{}Rtn", s + 1)),
        (StripKind::Bus, s) => {
            let (name, number) = layout.bus_name(s);
            Some(format!("{name}{number}"))
        }
        (StripKind::Main, s) => layout.main.strip_name(s).map(str::to_string),
        (StripKind::Matrix, s) if s < usize::from(layout.mtx) => Some(format!("Mtx{}", s + 1)),
        (StripKind::Matrix, s) => Some(format!("StMtx{}", s - usize::from(layout.mtx) + 1)),
    }
}

/// A new dLive's outputs (Sidecar's reading: the guides don't list the factory
/// patch): the MixRack's 1-2 LR, 3-10 Aux 1-8, 11-26 StAux 1-8, 27-30 Mtx 1-4,
/// 31-32 StMtx 1; the Surface's 1-2 LR.
fn dlive_factory_outputs(spec: &ProfileSpec, patch: &crate::routing::OutputPatch) {
    use crate::routing::strip_source_input;
    for side in 0..2 {
        let lr = strip_source_input(StripKind::Main, 0, side);
        patch.set(side as i32, lr);
        patch.set(dlive_ports::SURFACE + side as i32, lr);
    }
    spec.dlive_layout.unwrap_or(DLiveLayout::FOH).patch_mix_outputs(patch);
}

impl Palette {
    /// The dLive's "7 colours and off" (the same eight as the SQ's, by its MIDI
    /// table's names), a new strip coloured by what it is: Sidecar's reading.
    pub const DLIVE: Palette = Palette {
        colours: &SQ_COLOURS,
        grid: &[1, 2, 3, 4, 5, 6, 7, 8],
        default: 8,
        factory: Some(|spec, kind, slot| {
            let layout = spec.dlive_layout.unwrap_or(DLiveLayout::FOH);
            match kind {
                StripKind::Channel => 8,
                StripKind::Aux => 6,
                StripKind::Bus if slot >= layout.fx_at() => 6,
                StripKind::Bus if slot >= layout.aux_at() => 7,
                StripKind::Bus => 5,
                StripKind::Matrix => 4,
                StripKind::Main => 2,
            }
        }),
    };
}

/// The Allen & Heath dLive (Firmware Reference Guide V2.12, the datasheets), as
/// its FoH Template Show configures it: 128 inputs on 124 channels, 8 Groups, 16
/// Aux, 8 FX sends, LR, 8 Matrices, 24 DCAs, 8 mute groups, 16 FX; its models
/// (`livedaw_dsp::dlive`) in the order of its block diagram (`chain::DLIVE_*`),
/// its 28-band GEQ after each mix's PEQ, its fader law, its sockets and names;
/// the FX sends feed FX 1-8. Mixer Config, its own effects and its surface follow;
/// its MIDI over TCP is `livedaw-ahmidi`.
static DLIVE: ProfileSpec = ProfileSpec {
    profile: Profile::AhDLive(DLiveLayout::FOH),
    name: "dLive",
    counts: Counts::DLIVE,
    templates: [chain::DLIVE_CHANNEL, chain::DLIVE_RETURN, chain::DLIVE_MIX],
    process_orders: chain::DLIVE_ORDERS,
    aux_order: b"",
    output_orders: &["DI"],
    fader: FaderLaw::DLIVE,
    main_fader: None,
    bus_fader: None,
    pan: PanLaw::WING,
    port_groups: DLIVE_PORT_GROUPS,
    models: &livedaw_dsp::dlive::MODELS,
    remotes: Remotes { midi: None, network: &[NetworkProtocol::AhMidiTcp] },
    palette: Palette::DLIVE,
    labels: ["IP", "FXR", "GRP", "LR", "MTX"],
    slots: &[
        SlotRange { kind: StripKind::Channel, first: dlive::STEREO, count: 4, chain: chain::DLIVE_CHANNEL, label: "ST" },
        SlotRange { kind: StripKind::Bus, first: dlive::ST_GRP, count: 4, chain: chain::DLIVE_MIX, label: "STGRP" },
        SlotRange { kind: StripKind::Bus, first: dlive::AUX, count: 8, chain: chain::DLIVE_MIX, label: "AUX" },
        SlotRange { kind: StripKind::Bus, first: dlive::ST_AUX, count: 8, chain: chain::DLIVE_MIX, label: "STAUX" },
        SlotRange { kind: StripKind::Bus, first: dlive::FX, count: 8, chain: chain::DLIVE_FX_SEND, label: "FX" },
        SlotRange { kind: StripKind::Matrix, first: dlive::ST_MTX, count: 4, chain: chain::DLIVE_MIX, label: "STMTX" },
    ],
    // The Groups start on LR at 0 dB, the Aux don't (Sidecar's reading).
    buses_to_main: 0..dlive::AUX,
    mono_mains: &[],
    source_link: false,
    sends_on: true,
    send_tap: -1,
    tap_point: crate::strip::tap::PFL as i32,
    factory: Some(dlive_factory),
    factory_name: Some(dlive_name),
    factory_outputs: Some(dlive_factory_outputs),
    dca_names: None,
    dca_fader_db: 0.0,
    // Which effects a new show holds, the guides don't say: Sidecar's choice.
    fx_factory: &["DLVSMRH", "DLVSMRE", "DLVTAP", "DLVCHOR"],
    factory_eq_block: Some("DLVGEQC"),
    factory_eq_after: true,
    factory_preamp_block: Some(livedaw_dsp::dlive::PREAMP_DEFAULT),
    dlive_layout: Some(DLiveLayout::FOH),
    // FX sends 1-8 feed FX 1-8 (a Mix->Return's default patch); the FoH template
    // has no send for FX 9-16.
    fx_send_buses: &[
        dlive::FX,
        dlive::FX + 1,
        dlive::FX + 2,
        dlive::FX + 3,
        dlive::FX + 4,
        dlive::FX + 5,
        dlive::FX + 6,
        dlive::FX + 7,
    ],
    attenuator: false,
    delay_samples: None,
};

/// Where the DM2000's buses of each sort start.
pub mod dm2000 {
    /// AUX 1-12 are bus slots 8-19 (after BUS 1-8).
    pub const AUX: usize = 8;
    /// The Input Channels the effect returns come back on (73-88), then 2TR (89-96).
    pub const FX_RETURNS: usize = 72;
    pub const TWO_TRACK: usize = 88;
}

/// The compressor PRE FADER and the insert PRE EQ on a new channel, both PRE FADER
/// (Comp->Ins) on a new output (the Insert page's initial data, p.136).
const DM2000_CHANNEL_ORDER: i32 = 3;
const DM2000_OUTPUT_ORDER: i32 = 4;

/// A new DM2000's strip, as Scene #0's initial data has it: every channel mono,
/// patched as p.289 lists (CH 1-24 AD 1-24; 25-48 slots 1-3 and 49-72 slots 4-6,
/// eight inputs of each; 73-88 the outputs of Effects 1-8; 89-92 2TR digital 1-2,
/// 93-96 2TR analog 1-2), its fader at -oo (p.186; the Initial Data Nominal preference
/// off), the delay in samples; BUS and AUX mono and MATRIX and STEREO stereo at 0 dB
/// (Sidecar's reading of the outputs' faders: p.186 speaks only of the inputs').
fn dm2000_factory(_spec: &ProfileSpec, p: &crate::params::StripParams, kind: StripKind, slot: usize) {
    use std::sync::atomic::Ordering::Relaxed;
    p.input.delay_mode.store(3, Relaxed); // SMP
    p.input.delay.store(0.0);
    match kind {
        StripKind::Channel => {
            p.process_order.store(DM2000_CHANNEL_ORDER, Relaxed);
            let source = match slot {
                0..=23 => slot as i32,
                24..=47 => dm2000_ports::SLOT + 16 * ((slot as i32 - 24) / 8) + (slot as i32 - 24) % 8,
                48..=71 => dm2000_ports::SLOT + 48 + 16 * ((slot as i32 - 48) / 8) + (slot as i32 - 48) % 8,
                72..=87 => crate::sources::FX_RETURN_SOURCE_BASE + (slot as i32 - 72),
                88..=91 => dm2000_ports::DIGITAL_2TR + (slot as i32 - 88),
                _ => dm2000_ports::ANALOG_2TR + (slot as i32 - 92),
            };
            p.input_left.store(source, Relaxed);
        }
        StripKind::Bus => {
            p.process_order.store(DM2000_OUTPUT_ORDER, Relaxed);
            p.stereo.store(false, Relaxed);
            p.fader_db.store(0.0);
        }
        StripKind::Matrix | StripKind::Main => {
            p.process_order.store(DM2000_OUTPUT_ORDER, Relaxed);
            p.fader_db.store(0.0);
        }
        StripKind::Aux => {}
    }
}

/// The DM2000's strip names (pp.298-299: CH01, BUS1, AUX1 ... AUX12, MATRIX1, ST).
fn dm2000_name(_spec: &ProfileSpec, kind: StripKind, slot: usize) -> Option<String> {
    match (kind, slot) {
        (StripKind::Channel, s) => Some(format!("CH{:02}", s + 1)),
        (StripKind::Bus, s) if s < dm2000::AUX => Some(format!("BUS{}", s + 1)),
        (StripKind::Bus, s) => Some(format!("AUX{}", s - dm2000::AUX + 1)),
        (StripKind::Matrix, s) => Some(format!("MATRIX{}", s + 1)),
        (StripKind::Main, 0) => Some("ST".into()),
        _ => None,
    }
}

/// A new DM2000's output patch (p.297): OMNI OUT 1-8 AUX 1-8, every slot's 16
/// outputs BUS 1-8 twice over, STEREO on STEREO OUT and (Sidecar's reading: the
/// table doesn't list them) the 2TR digital outs.
fn dm2000_factory_outputs(_spec: &ProfileSpec, patch: &crate::routing::OutputPatch) {
    use crate::routing::strip_source_input;
    use dm2000_ports::{DIGITAL_2TR, OMNI, SLOT};
    for side in 0..2 {
        let st = strip_source_input(StripKind::Main, 0, side);
        for port in [0, DIGITAL_2TR, DIGITAL_2TR + 2, DIGITAL_2TR + 4] {
            patch.set(port + side as i32, st);
        }
    }
    for aux in 0..8 {
        patch.set(OMNI + aux as i32, strip_source_input(StripKind::Bus, dm2000::AUX + aux, 0));
    }
    for port in 0..96 {
        patch.set(SLOT + port as i32, strip_source_input(StripKind::Bus, port % 8, 0));
    }
}

/// The Yamaha DM2000 (V2 Owner's Manual): 96 Input Channels, BUS 1-8 and AUX 1-12,
/// 4 stereo Matrix Sends, Stereo Out; its fader laws (the channels' +10 to -138 dB,
/// the outputs' 0 to -138 dB on 10-bit data), its labels and names, its port groups,
/// its factory strip and output patch; its models (`livedaw_dsp::dm2000`) in the
/// order of its block diagram (`chain::DM2000_*`); its eight effects fed by AUX 1-8.
/// Its surface and MIDI follow.
static DM2000: ProfileSpec = ProfileSpec {
    profile: Profile::Dm2000,
    name: "DM2000",
    counts: Counts::DM2000,
    templates: [chain::DM2000_CHANNEL, chain::DM2000_OUTPUT, chain::DM2000_OUTPUT],
    process_orders: chain::DM2000_ORDERS,
    aux_order: b"",
    output_orders: chain::DM2000_ORDERS,
    fader: FaderLaw::DM2000,
    main_fader: Some(FaderLaw::DM2000_MASTER),
    bus_fader: Some(FaderLaw::DM2000_MASTER),
    pan: PanLaw::WING,
    port_groups: DM2000_PORT_GROUPS,
    models: &livedaw_dsp::dm2000::MODELS,
    remotes: Remotes { midi: Some(MidiProtocol::Dm2000), network: &[] },
    palette: Palette::DM2000,
    labels: ["CH", "-", "BUS", "ST", "MTRX"],
    slots: &[SlotRange { kind: StripKind::Bus, first: dm2000::AUX, count: 12, chain: chain::DM2000_OUTPUT, label: "AUX" }],
    // Each Bus Out has its own Bus to Stereo switch, off at first (Sidecar's
    // reading: p.109 gives the switch, not its initial state).
    buses_to_main: 0..0,
    mono_mains: &[],
    source_link: false,
    sends_on: true,
    send_tap: -1,
    tap_point: crate::strip::tap::PFL as i32,
    factory: Some(dm2000_factory),
    factory_name: Some(dm2000_name),
    factory_outputs: Some(dm2000_factory_outputs),
    dca_names: None,
    dca_fader_db: 0.0,
    // Sidecar's choice: the manual doesn't say what the eight effects start on.
    fx_factory: &["DMHALL", "DMROOM", "DMSTAGE", "DMPLATE", "DMSTDLY", "DMCHO", "DMSYMP", "DMMONO"],
    factory_eq_block: None,
    factory_eq_after: false,
    factory_preamp_block: None,
    dlive_layout: None,
    attenuator: true,
    delay_samples: Some((0, 43400)),
    // AUX 1-8 feed Effects 1-8's first input (p.289).
    fx_send_buses: &[
        dm2000::AUX,
        dm2000::AUX + 1,
        dm2000::AUX + 2,
        dm2000::AUX + 3,
        dm2000::AUX + 4,
        dm2000::AUX + 5,
        dm2000::AUX + 6,
        dm2000::AUX + 7,
    ],
};

/// What a new XENYX 1204USB starts as (the panel): channels 1-4 mono on MIC/LINE 1-4, 5/6
/// and 7/8 stereo on the LINE jacks; the low cut at 75 Hz, 18 dB/oct (off), the soft clip
/// on, the compressor (channels 1-4) on at COMP 0 (flat), the EQ on and flat. The returns
/// stereo on the AUX RETURN jacks and the 2-TR/USB input stereo on the 2-TR input. The
/// AUX 1 and AUX 2 sends mono, ALT 3-4 and MAIN MIX stereo.
fn xenyx1204_factory(_spec: &ProfileSpec, p: &crate::params::StripParams, kind: StripKind, slot: usize) {
    use std::sync::atomic::Ordering::Relaxed;
    use xenyx1204_ports::{LINE, MIC_LINE, RETURN, TWO_TR_IN};
    match kind {
        StripKind::Channel => {
            let stereo = slot >= 4;
            p.stereo.store(stereo, Relaxed);
            let first = if stereo { LINE + 2 * (slot as i32 - 4) } else { MIC_LINE + slot as i32 };
            p.input_left.store(first, Relaxed);
            if stereo {
                p.input_right.store(first + 1, Relaxed);
            }
            let f = p.filter();
            f.low_cut_freq.store(livedaw_dsp::xenyx1204::LOW_CUT_HZ);
            f.low_cut_slope.store(livedaw_dsp::xenyx1204::LOW_CUT_SLOPE, Relaxed);
            p.gate().on.store(true, Relaxed);
            p.dynamics().on.store(slot < 4, Relaxed);
            p.eq().on.store(true, Relaxed);
        }
        StripKind::Aux => {
            p.stereo.store(true, Relaxed);
            let first = if slot < 2 { RETURN + 2 * slot as i32 } else { TWO_TR_IN };
            p.input_left.store(first, Relaxed);
            p.input_right.store(first + 1, Relaxed);
        }
        StripKind::Bus => p.stereo.store(slot == 2, Relaxed),
        StripKind::Main | StripKind::Matrix => {}
    }
}

/// The XENYX 1204USB's strip names, as the panel prints them.
fn xenyx1204_name(_spec: &ProfileSpec, kind: StripKind, slot: usize) -> Option<String> {
    match (kind, slot) {
        (StripKind::Channel, 4) => Some("5/6".into()),
        (StripKind::Channel, 5) => Some("7/8".into()),
        (StripKind::Channel, s) => Some(format!("{}", s + 1)),
        (StripKind::Aux, 2) => Some("2-TR/USB".into()),
        (StripKind::Aux, s) => Some(format!("RTN {}", s + 1)),
        (StripKind::Bus, 2) => Some("ALT 3-4".into()),
        (StripKind::Bus, s) => Some(format!("AUX {}", s + 1)),
        (StripKind::Main, 0) => Some("MAIN MIX".into()),
        _ => None,
    }
}

/// A new XENYX 1204USB's output patch: MAIN MIX on the main outputs, the 2-TR outputs and
/// the USB send; ALT 3-4 on its jacks; AUX 1 and AUX 2 on their jacks.
fn xenyx1204_factory_outputs(_spec: &ProfileSpec, patch: &crate::routing::OutputPatch) {
    use crate::routing::strip_source_input;
    use xenyx1204_ports::{ALT_OUT, AUX_OUT, MAIN_OUT, TWO_TR_OUT, USB_OUT};
    for side in 0..2 {
        let main = strip_source_input(StripKind::Main, 0, side);
        for first in [MAIN_OUT, TWO_TR_OUT, USB_OUT] {
            patch.set(first + side as i32, main);
        }
        patch.set(ALT_OUT + side as i32, strip_source_input(StripKind::Bus, 2, side));
    }
    for aux in 0..2 {
        patch.set(AUX_OUT + aux as i32, strip_source_input(StripKind::Bus, aux, 0));
    }
}

/// The Behringer XENYX 1204USB (the published panel and quick-start guide): 4 mono and 2
/// stereo channels, 2 stereo aux returns and the 2-TR/USB input, the AUX 1 (pre) and AUX 2
/// (post) sends, ALT 3-4, MAIN MIX; its fader law (a render's scale), its ports; its blocks
/// (`livedaw_dsp::xenyx1204`). The sends start on (at -oo); the surface sets their modes.
static XENYX1204: ProfileSpec = ProfileSpec {
    profile: Profile::Xenyx1204,
    name: "XENYX 1204USB",
    counts: Counts::XENYX1204,
    templates: [chain::XENYX1204_CHANNEL, chain::XENYX1204_PLAIN, chain::XENYX1204_PLAIN],
    process_orders: chain::XENYX1204_ORDERS,
    aux_order: b"",
    output_orders: chain::XENYX1204_OUTPUT_ORDERS,
    fader: FaderLaw::XENYX1204,
    main_fader: None,
    bus_fader: None,
    // Not in the guide (the pan knob's taper): the WING's.
    pan: PanLaw::WING,
    port_groups: XENYX1204_PORT_GROUPS,
    models: &livedaw_dsp::xenyx1204::MODELS,
    remotes: Remotes { midi: None, network: &[] },
    palette: Palette::XENYX1204,
    labels: ["CH", "RTN", "AUX", "MAIN", "MTX"],
    slots: &[],
    buses_to_main: 0..0,
    mono_mains: &[],
    source_link: false,
    sends_on: true,
    send_tap: -1,
    tap_point: crate::strip::tap::PFL as i32,
    factory: Some(xenyx1204_factory),
    factory_name: Some(xenyx1204_name),
    factory_outputs: Some(xenyx1204_factory_outputs),
    dca_names: None,
    dca_fader_db: 0.0,
    fx_factory: &[],
    factory_eq_block: None,
    factory_eq_after: false,
    factory_preamp_block: None,
    dlive_layout: None,
    attenuator: false,
    delay_samples: None,
    fx_send_buses: &[],
};

/// What a PRO2 starts as: its Normal (FOH) navigation mode's default state, the
/// safe scene (Owner's Manual pp.210-211). Channels mono; 1-48 on DL251's inputs;
/// 49-56, which the manual leaves unpatched, on the surface's 8 inputs (Sidecar's
/// reading, as the engine gives any unpatched channel its own input). Aux returns 1-8 on FX 1-4's stereo
/// outputs, panned odd left and even right. AUX 1-8 mono mixes; AUX 9-16 the
/// groups, routed to the stereo masters, at 0 dB, odd left and even right
/// (`buses_to_main`). Delays in milliseconds, compressors on the outputs' model.
/// Filter slopes on their switches' off positions (p.245: 12 and 6 dB/oct).
fn pro2_factory(_spec: &ProfileSpec, p: &crate::params::StripParams, kind: StripKind, slot: usize) {
    use std::sync::atomic::Ordering::Relaxed;
    p.input.delay_mode.store(2, Relaxed); // MS
    p.input.delay.store(0.3);
    match kind {
        StripKind::Channel => {
            p.stereo.store(false, Relaxed);
            p.input_left.store(pro2_ports::DL251_IN + slot as i32, Relaxed);
            let f = p.filter();
            f.low_cut_slope.store(1, Relaxed);
            f.high_cut_slope.store(0, Relaxed);
        }
        StripKind::Aux => {
            p.stereo.store(false, Relaxed);
            p.input_left.store(crate::sources::FX_RETURN_SOURCE_BASE + slot as i32, Relaxed);
            p.pan.store(if slot % 2 == 0 { -100.0 } else { 100.0 });
        }
        StripKind::Bus => {
            p.stereo.store(false, Relaxed);
            if slot >= pro2::GROUPS {
                p.pan.store(if slot % 2 == 0 { -100.0 } else { 100.0 });
            }
        }
        StripKind::Matrix => p.stereo.store(false, Relaxed),
        StripKind::Main => {}
    }
    if kind.is_output() {
        let d = p.dynamics();
        if let Some(model) = livedaw_dsp::model::model_index(d.table(), "MPCMPO") {
            d.set_model(model);
        }
    }
}

/// The PRO2's strip names (Owner's Manual: "Aux Send 3", and p.211's lists).
fn pro2_name(_spec: &ProfileSpec, kind: StripKind, slot: usize) -> Option<String> {
    match (kind, slot) {
        (StripKind::Channel, s) => Some(format!("Input {}", s + 1)),
        (StripKind::Aux, s) => Some(format!("Aux Ret {}", s + 1)),
        (StripKind::Bus, s) => Some(format!("Aux Send {}", s + 1)),
        (StripKind::Matrix, s) => Some(format!("Matrix {}", s + 1)),
        (StripKind::Main, 0) => Some("Master".into()),
        (StripKind::Main, 1) => Some("Mono".into()),
        _ => None,
    }
}

/// A new PRO2's output patch (p.211, Normal (FOH)): Aux Sends 1-8 on DL251's outputs
/// 1-8, Matrices 1-5 on 9-13, Master L, R and C on 14-16, and (Sidecar's reading: the
/// table doesn't list them) the masters on the surface's master XLRs.
fn pro2_factory_outputs(_spec: &ProfileSpec, patch: &crate::routing::OutputPatch) {
    use crate::routing::strip_source_input;
    use pro2_ports::{DL251_OUT, MASTER_OUT};
    for side in 0..2 {
        let st = strip_source_input(StripKind::Main, 0, side);
        patch.set(MASTER_OUT + side as i32, st);
        patch.set(DL251_OUT + 13 + side as i32, st);
    }
    patch.set(DL251_OUT + 15, strip_source_input(StripKind::Main, 1, 0));
    for aux in 0..8 {
        patch.set(DL251_OUT + aux as i32, strip_source_input(StripKind::Bus, aux, 0));
    }
    for matrix in 0..5 {
        patch.set(DL251_OUT + 8 + matrix as i32, strip_source_input(StripKind::Matrix, matrix, 0));
    }
}

/// Where the PRO2's aux sends of each sort start.
pub mod pro2 {
    /// Aux Sends 9-16 are the Normal (FOH) mode's stereo groups.
    pub const GROUPS: usize = 8;
}

/// The Midas PRO2 (Owner's Manual, DOC02-DL2 Issue A): 56 input channels, 8 aux returns,
/// 16 aux sends, 8 matrices, the stereo and mono masters, 8 VCAs, 6 auto-mutes and 6
/// FX; its fader law (a press render's scale), its port groups, its Normal (FOH)
/// mode's patch; its models (`livedaw_dsp::midas_pro`) in the order of its block
/// diagrams (`chain::PRO2_*`). FX 1-3 DN780, FX 4 Pitch Shifter, FX 1-4 fed by
/// Matrices 1-4 (p.211). Its surface follows.
static PRO2: ProfileSpec = ProfileSpec {
    profile: Profile::MidasPro2,
    name: "PRO2",
    counts: Counts::PRO2,
    templates: [chain::PRO2_CHANNEL, chain::PRO2_AUX, chain::PRO2_OUTPUT],
    process_orders: chain::PRO2_ORDERS,
    aux_order: b"",
    output_orders: chain::PRO2_OUTPUT_ORDERS,
    fader: FaderLaw::PRO2,
    main_fader: None,
    bus_fader: None,
    // Not in the manual; the WING's. p.262 says only "pan" and a "constant
    // power crossfade" for the SIS image.
    pan: PanLaw::WING,
    port_groups: PRO2_PORT_GROUPS,
    models: &livedaw_dsp::midas_pro::MODELS,
    remotes: Remotes { midi: None, network: &[] },
    palette: Palette::PRO2,
    labels: ["IN", "AR", "AUX", "MST", "MTX"],
    slots: &[],
    buses_to_main: pro2::GROUPS..16,
    mono_mains: &[1],
    source_link: false,
    sends_on: false,
    send_tap: -1,
    tap_point: crate::strip::tap::PFL as i32,
    factory: Some(pro2_factory),
    factory_name: Some(pro2_name),
    factory_outputs: Some(pro2_factory_outputs),
    dca_names: None,
    dca_fader_db: 0.0,
    fx_factory: &["MPDN780", "MPDN780", "MPDN780", "MPPIT"],
    factory_eq_block: None,
    factory_eq_after: false,
    factory_preamp_block: None,
    dlive_layout: None,
    attenuator: false,
    delay_samples: None,
    fx_send_buses: &[],
};

/// The first of the SQ-5's buses that is an FX send (Mix 1-12, then FX 1-4).
const SQ_FX_SENDS: usize = 12;

const SQ_LABELS: [&str; 5] = ["IP", "FXR", "MIX", "LR", "MTX"];

/// A new SQ-5 strip's name. Sidecar's reading of the guide's names (Ip, Aux, FX,
/// Fx1Rtn, Mtx).
fn sq5_name(_spec: &ProfileSpec, kind: StripKind, slot: usize) -> Option<String> {
    match (kind, slot) {
        (StripKind::Channel, s) => Some(format!("Ip{}", s + 1)),
        (StripKind::Aux, s) => Some(format!("Fx{}Rtn", s + 1)),
        (StripKind::Bus, s) if s < SQ_FX_SENDS => Some(format!("Aux{}", s + 1)),
        (StripKind::Bus, s) => Some(format!("FX{}", s - SQ_FX_SENDS + 1)),
        (StripKind::Main, 0) => Some("LR".into()),
        (StripKind::Matrix, s) => Some(format!("Mtx{}", s + 1)),
        _ => None,
    }
}

/// A new SQ-5's strips: inputs 17-22 stereo pairs on ST1-ST3 (Sidecar's
/// reading: the guide doesn't list the factory patch), the FX returns on
/// their RackFX.
fn sq5_factory(_spec: &ProfileSpec, p: &crate::params::StripParams, kind: StripKind, slot: usize) {
    use std::sync::atomic::Ordering::Relaxed;
    match kind {
        StripKind::Channel if (16..22).contains(&slot) => p.input_left.store(slot as i32, Relaxed),
        StripKind::Channel if slot >= 22 => p.input_left.store(-1, Relaxed),
        StripKind::Aux => {
            let fx = crate::sources::FX_RETURN_SOURCE_BASE + 2 * slot as i32;
            p.stereo.store(true, Relaxed);
            p.input_left.store(fx, Relaxed);
            p.input_right.store(fx + 1, Relaxed);
        }
        _ => {}
    }
}

/// A new SQ-5's outputs (Sidecar's reading): sockets 1-12 Mix 1-12, A and B and
/// the AES output LR.
fn sq5_factory_outputs(_spec: &ProfileSpec, patch: &crate::routing::OutputPatch) {
    use crate::routing::strip_source_input;
    for mix in 0..12 {
        patch.set(mix as i32, strip_source_input(StripKind::Bus, mix, 0));
    }
    for side in 0..2 {
        let lr = strip_source_input(StripKind::Main, 0, side);
        patch.set(12 + side as i32, lr);
        patch.set(14 + side as i32, lr);
    }
}

/// The SQ's strip colours (its LCD backlight): Sidecar's reading, to check
/// against the guide's colour picker when the surface is built.
const SQ_COLOURS: [(&str, u32); 8] = [
    ("Off", 0x000000),
    ("Red", 0xE02020),
    ("Green", 0x20C040),
    ("Yellow", 0xE8D020),
    ("Blue", 0x3060F0),
    ("Purple", 0xB040E0),
    ("Light Blue", 0x40C8E8),
    ("White", 0xF0F0F0),
];

impl Palette {
    // Default Green, as the guide's screens show a new console's channels.
    pub const SQ5: Palette = Palette { colours: &SQ_COLOURS, grid: &[1, 2, 3, 4, 5, 6, 7, 8], default: 3, factory: None };
}

/// The Allen & Heath SQ-5 (Reference Guide V1.6.0): 48 inputs, 12 mixes and 4 FX
/// sends, LR, 3 stereo matrices, 8 DCAs, 8 mute groups, 8 FX; its fader law
/// (`FaderLaw::SQ`), its port groups and names, its processing as blocks
/// (`livedaw_dsp::sq`) in the order of its block diagrams (`chain::SQ_*`). Its
/// RackFX and GEQ, its MIDI and its surface follow.
static SQ5: ProfileSpec = ProfileSpec {
    profile: Profile::AhSq5,
    name: "SQ-5",
    counts: Counts::SQ5,
    templates: [chain::SQ_CHANNEL, chain::SQ_RETURN, chain::SQ_MIX],
    process_orders: chain::SQ_ORDERS,
    aux_order: b"",
    output_orders: &["DI"],
    fader: FaderLaw::SQ,
    main_fader: None,
    bus_fader: None,
    pan: PanLaw::WING,
    port_groups: SQ5_PORT_GROUPS,
    models: &livedaw_dsp::sq::MODELS,
    remotes: Remotes { midi: Some(MidiProtocol::AhSq), network: &[NetworkProtocol::AhMidiTcp] },
    palette: Palette::SQ5,
    labels: SQ_LABELS,
    slots: &[SlotRange { kind: StripKind::Bus, first: SQ_FX_SENDS, count: 4, chain: chain::SQ_FX_SEND, label: "FX" }],
    // Its mixes start as Auxes, which don't feed LR.
    buses_to_main: 0..0,
    mono_mains: &[],
    source_link: false,
    sends_on: true,
    send_tap: -1,
    tap_point: crate::strip::tap::PFL as i32,
    factory: Some(sq5_factory),
    factory_name: Some(sq5_name),
    factory_outputs: Some(sq5_factory_outputs),
    dca_names: None,
    dca_fader_db: 0.0,
    // The first four FX engines start populated (p.60); which, the guide
    // doesn't say: Sidecar's choice.
    fx_factory: &["SQSMRH", "SQSMRE", "SQTAP", "SQCHOR"],
    factory_eq_block: Some("SQGEQ"),
    factory_eq_after: false,
    factory_preamp_block: Some("DLVTUBE"),
    dlive_layout: None,
    // FX sends 1-4 feed the first four FX engines (p.61).
    attenuator: false,
    delay_samples: None,
    fx_send_buses: &[SQ_FX_SENDS, SQ_FX_SENDS + 1, SQ_FX_SENDS + 2, SQ_FX_SENDS + 3],
};

#[cfg(test)]
mod dlive_tests {
    use super::*;
    use crate::config::ConsoleConfig;
    use crate::engine::Engine;
    use crate::params::StripParams;
    use crate::routing::strip_source_input;
    use std::sync::atomic::Ordering::Relaxed;

    fn console() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: Counts::DLIVE });
        e
    }

    #[test]
    fn its_foh_layout_is_its_static_spec() {
        let built = DLiveLayout::FOH.build_spec();
        let shape = |s: &ProfileSpec| s.slots.iter().map(|r| (r.kind, r.first, r.count, r.label)).collect::<Vec<_>>();
        assert_eq!(shape(&built), shape(&DLIVE));
        assert_eq!(built.counts, DLIVE.counts);
        assert_eq!(built.buses_to_main, DLIVE.buses_to_main);
        assert_eq!(built.fx_send_buses, DLIVE.fx_send_buses);
        assert_eq!(DLiveLayout::FOH.busses_used(), 60, "the LR's two and one stereo PAFL's two");
        assert_eq!(DLiveLayout::FOH.busses_remaining(), 4);
        assert!(DLiveLayout::FOH.is_valid());
    }

    #[test]
    fn stereo_inputs_set_the_channels() {
        // Inputs 1/2 stereo as well as the template's four pairs.
        let layout = DLiveLayout { stereo_inputs: DLiveLayout::FOH.stereo_inputs | 1, ..DLiveLayout::FOH };
        assert_eq!(layout.counts().channels, 123);
        let inputs = layout.channel_inputs();
        assert_eq!(inputs[0], (0, true));
        assert_eq!(inputs[1], (2, false));
        let spec = Profile::AhDLive(layout).spec();
        assert_eq!(dlive_name(spec, StripKind::Channel, 0).unwrap(), "1/2");
        assert_eq!(dlive_name(spec, StripKind::Channel, 1).unwrap(), "Ip3");
        assert_eq!(dlive_name(spec, StripKind::Channel, 122).unwrap(), "127/128");
        let p = StripParams::new_in_slot_of(StripKind::Channel, 0, spec);
        assert!(p.stereo.load(Relaxed));
        assert_eq!((p.input_left.load(Relaxed), p.input_right.load(Relaxed)), (0, 1));
        assert_eq!(spec.label_at(StripKind::Channel, 0), ("ST", 1));
        assert_eq!(DLiveLayout::from_text(&layout.to_text()), Some(layout));
    }

    #[test]
    fn a_mixer_config_must_fit() {
        let ok = DLiveLayout { grp: 2, st_grp: 2, aux: 16, st_aux: 4, fx: 4, st_fx: 2, mtx: 2, st_mtx: 2, ..DLiveLayout::FOH };
        assert!(ok.is_valid());
        assert_eq!(ok.busses_used(), 2 + 4 + 16 + 8 + 4 + 4 + 2 + 4 + 2 + 2, "the mixes, the LR and one stereo PAFL");
        assert!(!DLiveLayout { grp: 3, ..ok }.is_valid(), "mono busses change in pairs");
        assert!(!DLiveLayout { aux: 62, ..ok }.is_valid(), "more than 64 busses");
        assert!(!DLiveLayout { fx: 14, st_fx: 4, ..ok }.is_valid(), "more than 16 FX sends");
        assert_eq!(DLiveLayout::from_text(&ok.to_text()), Some(ok));
        assert_eq!(DLiveLayout::from_text("1,2,3"), None);
        assert_eq!(DLiveLayout::from_text("1,2,3,4,5,6,7,999"), None);
    }

    #[test]
    fn a_console_of_another_layout_names_and_counts_its_mixes() {
        let layout = DLiveLayout { grp: 2, st_grp: 1, aux: 2, st_aux: 2, fx: 2, st_fx: 1, mtx: 2, st_mtx: 1, ..DLiveLayout::FOH };
        let profile = Profile::AhDLive(layout);
        let spec = profile.spec();
        assert!(std::ptr::eq(spec, profile.spec()), "made once");
        assert_eq!(profile.id(), "dlive");
        assert!(profile.fits(&layout.counts()));
        assert!(!profile.fits(&Counts::DLIVE));
        assert_eq!(spec.counts.buses, 10);
        assert_eq!(spec.counts.matrices, 3);
        assert_eq!(spec.fx_send_buses, &[7, 8, 9]);
        assert_eq!(spec.buses_to_main, 0..3);
        let name = |slot| dlive_name(spec, StripKind::Bus, slot).unwrap();
        assert_eq!([name(0), name(2), name(3), name(5), name(7), name(9)], ["Grp1", "StGrp1", "Aux1", "StAux1", "FX1", "StFX1"]);
        assert_eq!(dlive_name(spec, StripKind::Matrix, 2).unwrap(), "StMtx1");
        assert_eq!(spec.label_at(StripKind::Bus, 5), ("STAUX", 1));
        assert_eq!(spec.label_at(StripKind::Bus, 0), ("GRP", 1));
    }

    #[test]
    fn the_dlive_is_its_foh_template() {
        let p = Profile::AhDLive(DLiveLayout::FOH).spec();
        assert_eq!(Profile::from_id("dlive"), Some(Profile::AhDLive(DLiveLayout::FOH)));
        assert!(Profile::AhDLive(DLiveLayout::FOH).fits(&Counts::DLIVE));
        assert!(!Profile::AhDLive(DLiveLayout::FOH).fits(&Counts { buses: 16, ..Counts::DLIVE }));
        assert_eq!(p.counts.channels, 124, "120 mono, 4 stereo: 128 inputs");
        assert_eq!(p.counts.buses, 32, "8 Groups, 16 Aux, 8 FX sends");
        assert_eq!((p.counts.aux, p.counts.matrices), (16, 8));
        assert_eq!((p.counts.dcas, p.counts.mute_groups, p.counts.fx), (24, 8, 24), "16 RackExtra and 8 RackUltra FX");
        assert_eq!(Counts::DLIVE.sanitised(), Counts::DLIVE);
    }

    #[test]
    fn the_dlive_chains_follow_its_block_diagram() {
        let p = Profile::AhDLive(DLiveLayout::FOH).spec();
        for chain in [p.template_at(StripKind::Channel, 0), p.template_at(StripKind::Channel, 123)] {
            let at = |n: Node| chain.iter().position(|&x| x == n).unwrap_or_else(|| panic!("{n:?}"));
            let order = [
                Node::Input,
                Node::Filter,
                Node::Gate,
                Node::PreInsert,
                Node::Slot(0),
                Node::Slot(1),
                Node::PostInsert,
                Node::Delay,
                Node::Pfl,
                Node::Fader,
            ];
            assert!(order.windows(2).all(|w| at(w[0]) < at(w[1])), "{chain:?}");
        }
        assert_eq!(p.pre_fader_order(StripKind::Channel, 0), b"ED", "the PEQ, then the compressor");
        assert_eq!(p.pre_fader_order(StripKind::Channel, 1), b"DE", "or the compressor first");
        let e = console();
        let ch = &e.channels()[0];
        assert_eq!((ch.slot_block(0), ch.slot_block(1)), (Some(Node::Eq), Some(Node::Dynamics)));
        ch.params.process_order.store(1, Relaxed);
        assert_eq!((ch.slot_block(0), ch.slot_block(1)), (Some(Node::Dynamics), Some(Node::Eq)), "per channel");
        let has = |kind, slot, n: Node| p.template_at(kind, slot).contains(&n);
        for (kind, slot) in
            [(StripKind::Bus, 0), (StripKind::Bus, 8), (StripKind::Bus, 16), (StripKind::Matrix, 7), (StripKind::Main, 0)]
        {
            for n in [Node::PreInsert, Node::Eq, Node::Dynamics, Node::Delay] {
                assert!(has(kind, slot, n), "{kind:?} {slot} {n:?}");
            }
        }
        for n in [Node::Eq, Node::Dynamics, Node::Delay] {
            assert!(!has(StripKind::Bus, dlive::FX, n), "an FX send has mute and fader alone");
        }
        assert!(has(StripKind::Aux, 15, Node::Eq), "an FX return's PEQ");
        assert!(!has(StripKind::Aux, 15, Node::Dynamics));
    }

    #[test]
    fn the_dlive_fader_is_its_midi_table() {
        let law = FaderLaw::DLIVE;
        let db = |step: f32| law.to_db(step / 127.0);
        for (step, expected) in
            [(127.0, 10.0), (117.0, 5.0), (107.0, 0.0), (97.0, -5.0), (87.0, -10.0), (47.0, -30.0), (17.0, -45.0)]
        {
            assert!((db(step) - expected).abs() < 0.01, "step {step}: {}", db(step));
        }
        assert_eq!(law.steps, 128);
        assert_eq!(law.to_db(0.0), livedaw_dsp::fader::FADER_MIN);
    }

    #[test]
    fn a_new_dlive_is_set_up_as_its_foh_template() {
        let e = console();
        let channels = e.channels();
        let input = |n: usize| channels[n].params.input_left.load(Relaxed);
        assert_eq!(channels.len(), 124);
        assert!(channels.iter().all(|c| c.params.mute.load(Relaxed)), "all channels muted");
        assert_eq!((input(0), input(63), input(64), input(95), input(96), input(113)), (0, 63, 64, 95, 96, 113));
        assert_eq!((input(114), input(119)), (128, 133), "115-120 on Surface 1-6");
        for (slot, first) in [(120, 134), (121, 136), (122, 138)] {
            assert!(channels[slot].params.stereo.load(Relaxed));
            assert_eq!((input(slot), channels[slot].params.input_right.load(Relaxed)), (first, first + 1));
        }
        assert_eq!(input(123), livedaw_recorder::USB_PLAYER_SOURCE_BASE, "127/128: the Surface's USB playback");
        assert_eq!(channels[0].name(), "Ip1");
        assert_eq!(channels[120].name(), "121/122");
        assert_eq!(channels[0].params.eq().model_spec().unwrap().token, "DLVPEQ");

        let returns = e.strips(StripKind::Aux);
        assert_eq!(returns.len(), 16);
        assert_eq!(returns[15].params.input_left.load(Relaxed), crate::sources::FX_RETURN_SOURCE_BASE + 30);
        assert!(crate::sources::fx_return_channel(crate::sources::FX_RETURN_SOURCE_BASE + 31).is_some(), "FX 16's right side");

        let buses = e.strips(StripKind::Bus);
        let stereo: Vec<bool> = buses.iter().map(|b| b.params.stereo.load(Relaxed)).collect();
        assert_eq!(&stereo[0..4], [false; 4], "Grp 1-4");
        assert_eq!(&stereo[4..8], [true; 4], "StGrp 1-4");
        assert_eq!(&stereo[8..16], [false; 8], "Aux 1-8");
        assert_eq!(&stereo[16..24], [true; 8], "StAux 1-8");
        assert_eq!(&stereo[24..32], [false; 8], "FX 1-8");
        assert_eq!(buses[4].name(), "StGrp1");
        assert_eq!(buses[31].name(), "FX8");
        let matrices = e.strips(StripKind::Matrix);
        assert!(!matrices[3].params.stereo.load(Relaxed) && matrices[4].params.stereo.load(Relaxed));
        assert_eq!(matrices[4].name(), "StMtx1");
    }

    #[test]
    fn the_dlive_fx_sends_feed_fx_1_to_8_past_the_wings_16_buses() {
        let e = console();
        let fx = &e.controls.fx;
        for (send, slot) in fx.iter().take(8).enumerate() {
            let source = strip_source_input(StripKind::Bus, dlive::FX + send, 0);
            assert!(source >= 0, "FX send {}", send + 1);
            assert_eq!(slot.source_left.load(Relaxed), source);
        }
        assert!(fx[8].source_left.load(Relaxed) < 0, "the FoH template has no send for FX 9");
    }

    #[test]
    fn a_new_dlive_starts_with_its_own_rackextra_fx() {
        let e = console();
        let models = e.fx_models();
        let token = |i: usize| models[e.controls.fx[i].model_index_in(models)].token;
        assert_eq!([token(0), token(1), token(2), token(3)], ["DLVSMRH", "DLVSMRE", "DLVTAP", "DLVCHOR"]);
        assert_eq!(token(4), "NONE");
        assert!(models.iter().all(|m| m.token == "NONE" || m.token == "EXT" || m.token.starts_with("DLV")));
    }

    #[test]
    fn the_dlive_outputs_reach_past_the_wings_16_buses() {
        let e = console();
        let patch = e.output_patch();
        assert_eq!(patch.get(0), strip_source_input(StripKind::Main, 0, 0));
        assert_eq!(patch.get(2), strip_source_input(StripKind::Bus, dlive::AUX, 0));
        assert_eq!(patch.get(25), strip_source_input(StripKind::Bus, dlive::ST_AUX + 7, 1));
        assert!(patch.get(25) >= 0, "StAux 8 is bus 24");
        assert_eq!(patch.get(26), strip_source_input(StripKind::Matrix, 0, 0));
    }

    #[test]
    fn the_dlive_geq_follows_each_mixes_peq() {
        use crate::blocks::Step;
        let e = console();
        let (buses, mains, matrices) = (e.strips(StripKind::Bus), e.strips(StripKind::Main), e.strips(StripKind::Matrix));
        for strip in buses.iter().take(24).chain(mains.iter()).chain(matrices.iter()) {
            let steps = strip.steps();
            let eq = steps.iter().position(|s| matches!(s, Step::Node(Node::Eq))).unwrap();
            let comp = steps.iter().position(|s| matches!(s, Step::Node(Node::Dynamics))).unwrap();
            let geq = steps.iter().position(|s| !matches!(s, Step::Node(_))).unwrap();
            assert!(eq < geq && geq < comp, "PEQ, GEQ, compressor");
        }
        assert!(buses[dlive::FX].steps().iter().all(|s| matches!(s, Step::Node(_))), "an FX send has no GEQ");
    }

    #[test]
    fn the_dlive_palette_and_port_groups() {
        let p = Profile::AhDLive(DLiveLayout::FOH).spec();
        assert_eq!(p.palette.colours.len(), 8, "7 colours and off");
        let g = p.port_groups;
        assert_eq!(g.len(), WING_PORT_GROUPS.len());
        assert_eq!(crate::ports::group_of(g, false, 95).map(|(g, n)| (g.name, n)), Some(("DX 1/2", 31)));
        assert_eq!(crate::ports::group_of(g, false, 128).map(|(g, n)| (g.prefix, n)), Some(("S", 0)));
        let aux9 = strip_source_input(StripKind::Bus, 16, 0);
        assert_eq!(crate::ports::group_of(g, false, aux9).map(|(g, n)| (g.name, n)), Some(("MIX 17-32", 0)));
    }
}

#[cfg(test)]
mod y01v_tests {
    use super::*;
    use crate::config::ConsoleConfig;
    use crate::engine::Engine;

    fn console() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::Yamaha01V, counts: Counts::Y01V });
        e
    }

    #[test]
    fn the_01v_is_selectable_and_counts_its_inputs() {
        let p = Profile::Yamaha01V.spec();
        assert_eq!(Profile::from_id("01v"), Some(Profile::Yamaha01V));
        assert!(Profile::Yamaha01V.fits(&Counts::Y01V));
        assert!(!Profile::Yamaha01V.fits(&Counts { buses: 16, ..Counts::Y01V }));
        assert_eq!(p.counts.channels, 22, "12 mono, 2 stereo, 8 Option I/O: 24 inputs");
        assert_eq!(p.counts.buses, 10, "BUS 1-4, AUX 1-4, EFFECT 1-2");
        assert_eq!(p.counts.aux, 2);
        assert_eq!(p.counts.fx, 2);
        assert_eq!(p.fx_send_buses, &[8, 9]);
    }

    #[test]
    fn the_01v_chains_follow_its_block_diagram() {
        let p = Profile::Yamaha01V.spec();
        let has = |kind, slot, n: Node| p.template_at(kind, slot).contains(&n);
        for slot in [0, 11, 12, 13] {
            assert!(has(StripKind::Channel, slot, Node::Eq));
            assert!(has(StripKind::Channel, slot, Node::Dynamics), "CH {slot}");
            assert!(has(StripKind::Channel, slot, Node::Delay), "CH {slot}");
        }
        for slot in 14..22 {
            assert!(has(StripKind::Channel, slot, Node::Eq));
            assert!(!has(StripKind::Channel, slot, Node::Dynamics), "CH {slot}: no dynamics");
            assert!(!has(StripKind::Channel, slot, Node::Delay), "CH {slot}: no delay");
        }
        for slot in 0..2 {
            assert!(has(StripKind::Aux, slot, Node::Eq));
            assert!(!has(StripKind::Aux, slot, Node::Dynamics));
        }
        for slot in (0..4).chain(8..10) {
            assert!(!has(StripKind::Bus, slot, Node::Eq), "BUS and EFFECT: the master alone");
            assert!(!has(StripKind::Bus, slot, Node::Dynamics));
        }
        for slot in 4..8 {
            assert!(has(StripKind::Bus, slot, Node::Eq), "AUX");
            assert!(has(StripKind::Bus, slot, Node::Dynamics), "AUX");
        }
        assert!(has(StripKind::Main, 0, Node::Delay), "STEREO's output delay");
    }

    #[test]
    fn a_new_01v_is_set_up_as_its_manual_says() {
        let e = console();
        let channels = e.channels();
        let (ch, st, opt) = (&channels[0], &channels[12], &channels[14]);
        assert_eq!(ch.params.eq().model_spec().unwrap().token, "Y2EQ");
        assert!(st.params.stereo.load(std::sync::atomic::Ordering::Relaxed));
        assert_eq!(st.params.input_left.load(std::sync::atomic::Ordering::Relaxed), 12);
        assert_eq!(channels[13].params.input_left.load(std::sync::atomic::Ordering::Relaxed), 14);
        assert_eq!(opt.params.input_left.load(std::sync::atomic::Ordering::Relaxed), 16);
        assert_eq!(opt.params.eq().model_spec().unwrap().token, "O1EQ2");
        assert_eq!(channels[21].params.eq().model_spec().unwrap().token, "O1EQ2");
        let p = Profile::Yamaha01V.spec();
        assert_eq!(p.fader_for(StripKind::Channel), FaderLaw::Y01V);
        assert_eq!(p.fader_for(StripKind::Bus), FaderLaw::Y01V_MASTER);
        assert_eq!(p.fader_for(StripKind::Main), FaderLaw::Y01V_STEREO);
        assert_eq!(p.label(StripKind::Aux), "RTN");
        let name = |kind, slot| (p.factory_name.unwrap())(p, kind, slot);
        assert_eq!(name(StripKind::Channel, 0), None);
        assert_eq!(name(StripKind::Channel, 12).as_deref(), Some("13/14"));
        assert_eq!(name(StripKind::Channel, 13).as_deref(), Some("15/16"));
        assert_eq!(name(StripKind::Channel, 14).as_deref(), Some("CH 17"));
        assert_eq!(name(StripKind::Channel, 21).as_deref(), Some("CH 24"));
        assert_eq!(name(StripKind::Bus, 4).as_deref(), Some("AUX 1"));
        assert_eq!(name(StripKind::Bus, 9).as_deref(), Some("EFFECT 2"));
    }

    #[test]
    fn the_01v_faders_read_like_its_panel() {
        let db = |law: FaderLaw, step: f32| law.to_db(step / 127.0);
        assert!((db(FaderLaw::Y01V, 127.0) - 6.0).abs() < 0.01);
        assert!((db(FaderLaw::Y01V, 102.0)).abs() < 0.01);
        assert!((db(FaderLaw::Y01V, 1.0) + 72.0).abs() < 0.01);
        assert!((db(FaderLaw::Y01V_MASTER, 127.0)).abs() < 0.01);
        assert!((db(FaderLaw::Y01V_MASTER, 1.0) + 72.0).abs() < 0.01);
        assert!((db(FaderLaw::Y01V_STEREO, 127.0)).abs() < 0.01);
        assert!((db(FaderLaw::Y01V_STEREO, 1.0) + 96.0).abs() < 0.01);
        assert_eq!(FaderLaw::Y01V.steps, 128);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_new_sq5s_mixes_have_a_geq_before_their_peq_and_its_fx_are_populated() {
        use crate::blocks::{BlockParams, Step};
        use crate::chain::Node;
        use crate::{ConsoleConfig, Engine};
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::AhSq5, counts: Counts::SQ5 });
        assert_eq!((e.strips(StripKind::Channel).len(), e.strips(StripKind::Bus).len()), (48, 16));
        let geq_at = |s: &crate::Strip| {
            let steps = s.steps();
            let block = steps.iter().position(|x| matches!(x, Step::Block(_)));
            let eq = steps.iter().position(|x| matches!(x, Step::Node(Node::Eq)));
            (block, eq)
        };
        let mixes: Vec<_> = e
            .strips(StripKind::Bus)
            .into_iter()
            .take(12)
            .chain(e.strips(StripKind::Main))
            .chain(e.strips(StripKind::Matrix))
            .collect();
        assert_eq!(mixes.len(), 16);
        for s in &mixes {
            let (block, eq) = geq_at(s);
            let (block, eq) = (block.expect("a GEQ block"), eq.expect("a PEQ"));
            assert!(block < eq, "the GEQ runs first");
            let blocks = s.blocks();
            let BlockParams::Eq(p) = &blocks[0].params else { panic!("an EQ block") };
            assert_eq!(p.model_spec().unwrap().token, "SQGEQ");
        }
        // The FX sends have no EQ, so no GEQ; the inputs have none either.
        assert!(e.strips(StripKind::Bus)[12].blocks().is_empty());
        // The inputs' one block is the DEEP preamp's (p.32), and it starts out.
        let blocks = e.strips(StripKind::Channel)[0].blocks();
        assert_eq!(blocks.len(), 1);
        let BlockParams::Dynamics(preamp) = &blocks[0].params else { panic!("a dynamics block") };
        assert!(!preamp.on.load(std::sync::atomic::Ordering::Relaxed));
        // The first four FX engines start with a device, the others empty.
        let models = e.fx_models();
        let token = |i: usize| models[e.controls.fx[i].model_index_in(models)].token;
        assert_eq!([token(0), token(1), token(2), token(3)], ["SQSMRH", "SQSMRE", "SQTAP", "SQCHOR"]);
        assert_eq!(token(4), "NONE");
    }

    #[test]
    fn the_sq5() {
        let p = Profile::AhSq5.spec();
        assert_eq!(Profile::from_id("sq5"), Some(Profile::AhSq5));
        assert!(Profile::AhSq5.fits(&Counts::SQ5));
        assert!(!Profile::AhSq5.fits(&Counts { channels: 32, ..Counts::SQ5 }));
        assert_eq!((p.counts.channels, p.counts.buses, p.counts.matrices, p.counts.fx), (48, 16, 3, 8));
        assert_eq!(p.name, "SQ-5");
        assert_eq!(p.models.gate_default, "SQGATE");
        assert_eq!(sq5_name(&SQ5, StripKind::Channel, 47).as_deref(), Some("Ip48"));
        assert_eq!(sq5_name(&SQ5, StripKind::Bus, 11).as_deref(), Some("Aux12"));
        assert_eq!(sq5_name(&SQ5, StripKind::Bus, 12).as_deref(), Some("FX1"));
    }

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
        let factory = |kind, slot| (x.palette.factory.unwrap())(x, kind, slot);
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
    fn the_promix01() {
        let p = Profile::ProMix01.spec();
        assert_eq!(Profile::from_id("promix01"), Some(Profile::ProMix01));
        assert_eq!((p.name, p.counts), ("ProMix 01", Counts::PROMIX01));
        assert!(!Profile::ProMix01.fits(&Counts { fx: 4, ..Counts::PROMIX01 }));
        assert!(!Profile::X32.fits(&Counts::PROMIX01) && !Profile::Wing.fits(&Counts::PROMIX01));
        assert_eq!(Profile::Wing.or_universal(&Counts::PROMIX01), Profile::Universal);
        assert_eq!((p.fader.steps, p.fader.max_db()), (128, 6.0));
        assert_eq!(p.port_groups, PROMIX01_PORT_GROUPS);
        assert!(p.remotes.midi == Some(MidiProtocol::ProMix01) && p.remotes.network.is_empty(), "MIDI only");
        assert_eq!(p.label_at(StripKind::Aux, 0), ("ST IN", 1));
        assert_eq!(p.label_at(StripKind::Aux, 2), ("RTN", 2));
        assert_eq!((p.label(StripKind::Bus), p.label(StripKind::Main)), ("SEND", "ST OUT"));
        let name = |kind, slot| (p.factory_name.unwrap())(p, kind, slot);
        assert_eq!((name(StripKind::Aux, 1), name(StripKind::Bus, 3)), (Some("RTN1".into()), Some("SEND4".into())));
        assert_eq!(name(StripKind::Channel, 0), None);
        let colour = |kind, slot| (p.palette.factory.unwrap())(p, kind, slot);
        assert_eq!(p.palette.colour(colour(StripKind::Aux, 0)), Some(("YELLOW", 0xe8d020)));
        assert_eq!(p.palette.colour(colour(StripKind::Main, 0)).map(|c| c.0), Some("RED"));
        // A new strip as scene 00 has it.
        use crate::params::StripParams;
        use std::sync::atomic::Ordering::Relaxed;
        let rtn2 = StripParams::new_in_slot_of(StripKind::Aux, 2, p);
        assert_eq!(rtn2.fader_db.load(), 0.0);
        assert_eq!(rtn2.input_left.load(Relaxed), crate::sources::FX_RETURN_SOURCE_BASE + 2, "FX 2 L");
        let send1 = StripParams::new_in_slot_of(StripKind::Bus, 0, p);
        assert!(!send1.stereo.load(Relaxed), "the sends are mono");
        assert_eq!(send1.fader_db.load(), 0.0, "SEND1 has no master");
    }

    #[test]
    fn the_02r() {
        let p = Profile::Yamaha02R.spec();
        assert_eq!(Profile::from_id("02r"), Some(Profile::Yamaha02R));
        assert_eq!((p.name, p.counts), ("02R", Counts::Y02R));
        assert!(!Profile::Yamaha02R.fits(&Counts { channels: 40, ..Counts::Y02R }));
        assert!(!Profile::ProMix01.fits(&Counts::Y02R) && !Profile::Wing.fits(&Counts::Y02R));
        assert_eq!(Profile::Wing.or_universal(&Counts::Y02R), Profile::Universal);
        // The channels' fader and STEREO's.
        assert_eq!((p.fader.steps, p.fader.max_db()), (128, 10.0));
        assert_eq!(p.fader_for(StripKind::Main), FaderLaw::Y02R_STEREO);
        assert_eq!(p.fader_for(StripKind::Bus), FaderLaw::Y02R);
        assert_eq!(Profile::X32.spec().fader_for(StripKind::Main), FaderLaw::X32, "one law elsewhere");
        assert_eq!(p.port_groups, Y02R_PORT_GROUPS);
        assert_eq!(p.label_at(StripKind::Channel, 15), ("CH", 16));
        assert_eq!(p.label_at(StripKind::Channel, 17), ("LINE", 2));
        assert_eq!(p.label_at(StripKind::Channel, 35), ("TAPE", 16));
        assert_eq!(p.label_at(StripKind::Bus, 7), ("BUS", 8));
        assert_eq!(p.label_at(StripKind::Bus, 8), ("AUX", 1));
        assert_eq!((p.label(StripKind::Aux), p.label(StripKind::Main)), ("EFF", "STEREO"));
        let name = |kind, slot| (p.factory_name.unwrap())(p, kind, slot);
        assert_eq!(name(StripKind::Channel, 19), Some("LINE 23/24".into()));
        assert_eq!(name(StripKind::Channel, 20), Some("TAPE 1".into()));
        assert_eq!((name(StripKind::Aux, 1), name(StripKind::Bus, 15)), (Some("EFF2".into()), Some("AUX 8".into())));
        assert_eq!(name(StripKind::Channel, 0), None);
        let colour = |kind, slot| (p.palette.factory.unwrap())(p, kind, slot);
        assert_eq!(p.palette.colour(colour(StripKind::Channel, 20)).map(|c| c.0), Some("GREEN"));
        assert_eq!(p.palette.colour(colour(StripKind::Bus, 8)).map(|c| c.0), Some("BLUE"));
        assert_eq!(p.fx_send_buses, &[14, 15], "AUX 7-8 feed the effects");
        // A new strip as scene 0 has it.
        use crate::params::StripParams;
        use std::sync::atomic::Ordering::Relaxed;
        let line = StripParams::new_in_slot_of(StripKind::Channel, 17, p);
        assert!(line.stereo.load(Relaxed));
        assert_eq!((line.input_left.load(Relaxed), line.input_right.load(Relaxed)), (18, 19), "LINE 19/20");
        let tape16 = StripParams::new_in_slot_of(StripKind::Channel, 35, p);
        assert_eq!(tape16.input_left.load(Relaxed), 47, "card slot 2's 8th input");
        let eff2 = StripParams::new_in_slot_of(StripKind::Aux, 1, p);
        assert_eq!(eff2.input_left.load(Relaxed), crate::sources::FX_RETURN_SOURCE_BASE + 2, "FX 2 L");
        let aux8 = StripParams::new_in_slot_of(StripKind::Bus, 15, p);
        assert!(!aux8.stereo.load(Relaxed));
        assert_eq!(aux8.fader_db.load(), 0.0, "the masters at 0 dB");
    }

    #[test]
    fn the_promix01s_chains_and_models() {
        use crate::params::StripParams;
        let p = Profile::ProMix01.spec();
        assert_eq!(p.template_at(StripKind::Channel, 0), chain::PROMIX01_CHANNEL);
        assert_eq!(p.template_at(StripKind::Aux, 2), chain::X32_AUX);
        assert_eq!(p.template_at(StripKind::Bus, 3), chain::PROMIX01_SEND);
        assert_eq!(p.template_at(StripKind::Main, 0), chain::PROMIX01_MAIN);
        // The dynamics after the fader at first (POST), before it as order 1.
        assert_eq!(p.pre_fader_order(StripKind::Bus, 0), b"-D");
        assert_eq!(p.pre_fader_order(StripKind::Main, 1), b"D-");
        assert_eq!(p.pre_fader_order(StripKind::Main, 9), b"D-", "clamped");
        for c in [chain::PROMIX01_CHANNEL, chain::PROMIX01_SEND, chain::PROMIX01_MAIN] {
            let count = |n: Node| c.iter().filter(|&&x| x == n).count();
            for n in [Node::Gate, Node::Filter, Node::Delay, Node::PreInsert, Node::PostInsert] {
                assert_eq!(count(n), 0, "{n:?}");
            }
            for n in [Node::Input, Node::Pfl, Node::Fader, Node::Post] {
                assert_eq!(count(n), 1, "{n:?}");
            }
        }
        assert!(!chain::PROMIX01_SEND.contains(&Node::Eq), "the sends have no EQ");
        // A new strip holds the ProMix's models.
        let ch = StripParams::new_with(StripKind::Channel, p.models);
        assert_eq!(ch.eq().model_spec().unwrap().token, "PMEQ");
        assert_eq!(ch.dynamics().model_spec().unwrap().token, "PMCOMP");
        assert_eq!(ch.dynamics().param("ratio").unwrap().load(), 5.0, "2:1");
        // The WING's and the X32's outputs keep their one order.
        assert_eq!(Profile::Wing.spec().pre_fader_order(StripKind::Bus, 1), b"DI");
        assert_eq!(Profile::X32.spec().pre_fader_order(StripKind::Matrix, 3), b"EDI");
    }

    #[test]
    fn the_wings_labels() {
        for p in [Profile::Universal, Profile::Wing, Profile::X32] {
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
            if p == Profile::ProMix01 || p == Profile::Yamaha02R || p == Profile::Yamaha01V {
                assert_eq!(s.fader.steps, 128, "a Yamaha's");
                continue;
            }
            if p == Profile::Dm2000 {
                assert_eq!(s.fader, FaderLaw::DM2000);
                continue;
            }
            if p == Profile::Xenyx1204 {
                assert_eq!(s.fader, FaderLaw::XENYX1204);
                continue;
            }
            if p == Profile::MidasPro2 {
                assert_eq!(s.fader, FaderLaw::PRO2);
                continue;
            }
            if p == Profile::AhSq5 {
                assert_eq!(s.fader.steps, 256, "the SQ's");
                continue;
            }
            if p == Profile::AhDLive(DLiveLayout::FOH) {
                assert_eq!(s.fader, FaderLaw::DLIVE);
                continue;
            }
            // The X32's faders run the same law on 1024 steps.
            assert_eq!(s.fader.points, FaderLaw::WING.points);
            assert_eq!(s.fader.steps, if p == Profile::X32 { 1024 } else { 0 });
            assert_eq!(s.pan.gains(true, -1.0), livedaw_dsp::util::wing_mono_pan(-1.0));
            assert_eq!(s.pan.gains(false, 0.5), livedaw_dsp::util::stereo_balance(0.5));
            let groups = if p == Profile::X32 { X32_PORT_GROUPS } else { WING_PORT_GROUPS };
            assert_eq!(s.port_groups, groups);
        }
    }

    #[test]
    fn the_wings_models() {
        use livedaw_dsp::wing::{AUX_DYN_MODELS, DYN_MODELS, EQ_MODELS_BUS, EQ_MODELS_CH, FILTER_TOOLS, FX_MODELS};
        let m = Profile::Wing.spec().models;
        assert_eq!(m.table(0), DYN_MODELS);
        assert_eq!(m.table(1), EQ_MODELS_CH);
        assert_eq!(m.table(2), EQ_MODELS_BUS);
        assert_eq!(m.table(3), FILTER_TOOLS);
        assert_eq!(m.table(4), FX_MODELS);
        assert_eq!(m.table(9), DYN_MODELS, "out of range: dynamics");
        // The Universal's tables start with the WING's, in its order.
        let u = Profile::Universal.spec().models;
        for t in 0..5 {
            assert_eq!(&u.table(t)[..m.table(t).len()], m.table(t), "table {t}");
        }
        for m in [m, u] {
            assert_eq!(m.aux_dynamics, AUX_DYN_MODELS);
            assert!(AUX_DYN_MODELS.iter().all(|t| m.aux_may_hold(t)));
            assert!(!m.aux_may_hold("NO SUCH MODEL"));
        }
    }

    #[test]
    fn a_universal_strip_takes_the_x32s_blocks() {
        use crate::params::StripParams;
        use livedaw_dsp::model::model_index;
        let u = Profile::Universal.spec().models;
        let ch = StripParams::new_with(StripKind::Channel, u);
        // A new strip starts on the WING's blocks.
        assert_eq!(ch.gate().model_spec().unwrap().token, "GATE");
        assert_eq!(ch.dynamics().model_spec().unwrap().token, "COMP");
        // Any X32 model goes on it, and takes its own defaults.
        ch.gate().set_model(model_index(u.dynamics, "X32DUCK").unwrap());
        ch.dynamics().set_model(model_index(u.dynamics, "X32COMP").unwrap());
        ch.eq().set_model(model_index(u.eq_channel, "X32EQ6").unwrap());
        assert_eq!(ch.gate().model_spec().unwrap().token, "X32DUCK");
        assert_eq!(ch.dynamics().model_spec().unwrap().token, "X32COMP");
        assert_eq!(ch.dynamics().param("ratio").unwrap().load(), 5.0, "the X32's 3.0:1 (item 5)");
        assert_eq!(ch.eq().model_spec().unwrap().token, "X32EQ6");
        assert!(ch.eq().param("6q").is_some(), "six bands on an input");
        let bus = StripParams::new_with(StripKind::Bus, u);
        bus.eq().set_model(model_index(u.eq_bus, "X32EQ4").unwrap());
        assert_eq!(bus.eq().model_spec().unwrap().token, "X32EQ4");
        // The ProMix 01's and the 02R's too: a compander, their EQs.
        ch.dynamics().set_model(model_index(u.dynamics, "Y2CPH").unwrap());
        assert_eq!(ch.dynamics().model_spec().unwrap().token, "Y2CPH");
        ch.gate().set_model(model_index(u.dynamics, "PMGATE").unwrap());
        assert_eq!(ch.gate().model_spec().unwrap().token, "PMGATE");
        bus.eq().set_model(model_index(u.eq_bus, "PMEQ").unwrap());
        assert_eq!(bus.eq().model_spec().unwrap().token, "PMEQ");
        ch.eq().set_model(model_index(u.eq_channel, "Y2EQ").unwrap());
        assert_eq!(ch.eq().model_spec().unwrap().token, "Y2EQ");
        // The X32's profile itself stays the X32's alone.
        let x = Profile::X32.spec().models;
        assert!(model_index(x.dynamics, "GATE").is_none() && model_index(x.fx, "HALL").is_none());
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
