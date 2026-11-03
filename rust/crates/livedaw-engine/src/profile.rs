//! Console profiles: the console a configuration emulates and what that
//! console is (its counts, its strips' chains and process orders, its fader
//! and pan laws, its port groups, its processing models, its remote
//! protocols, its colours). The
//! Universal core takes any configuration; a profile limits and dresses it
//! (`docs/UNIVERSAL_MIXER.md`, Phase 3). The WING is the first; the Universal
//! is no console's limits.

use crate::chain::{self, Node};
use crate::config::Counts;
use crate::params::{StripKind, DEFAULT_COLOUR, WING_COLOURS, WING_COLOUR_GRID};
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
}

impl Profile {
    pub const ALL: [Profile; 2] = [Profile::Universal, Profile::Wing];

    /// The id shows store (`<Console profile="...">`).
    pub fn id(self) -> &'static str {
        match self {
            Profile::Universal => "universal",
            Profile::Wing => "wing",
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
        }
    }

    /// The name the interface shows.
    pub fn name(self) -> &'static str {
        self.spec().name
    }

    /// Whether a console of `counts` is one of this profile's: the WING's
    /// aux, mains, matrices, DCAs and mute groups (what its surface
    /// addresses; channels, buses and FX slots can grow on a WING too).
    pub fn fits(self, counts: &Counts) -> bool {
        match self {
            Profile::Universal => true,
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
}

impl NetworkProtocol {
    /// The protocol's bit in the C ABI's set (`LdRemotes::network`): the WING
    /// remote's `enable` bits.
    pub fn bit(self) -> u32 {
        match self {
            Self::WingDiscovery => 1,
            Self::WingOsc => 2,
            Self::WingNative => 4,
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
}

impl Palette {
    /// The WING's 18 (`params::WING_COLOURS`) and ICON/COLOR's grid.
    pub const WING: Palette = Palette { colours: &WING_COLOURS, grid: &WING_COLOUR_GRID, default: DEFAULT_COLOUR };

    /// Colour `index` (1-based), if the palette has it.
    pub fn colour(&self, index: i32) -> Option<(&'static str, u32)> {
        usize::try_from(index - 1).ok().and_then(|i| self.colours.get(i)).copied()
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
        assert_eq!(Profile::from_id("x32"), None);
        assert!(Profile::Wing.fits(&Counts { channels: 64, buses: 24, fx: 32, ..Counts::WING }));
        for c in [Counts { mains: 2, ..Counts::WING }, Counts { buses: 8, ..Counts::WING }, Counts { dcas: 24, ..Counts::WING }] {
            assert!(!Profile::Wing.fits(&c));
            assert_eq!(Profile::Wing.or_universal(&c), Profile::Universal);
        }
    }

    #[test]
    fn the_wings_colours() {
        for p in Profile::ALL {
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
        for p in Profile::ALL {
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
            assert_eq!(s.fader, FaderLaw::WING);
            assert_eq!(s.pan.gains(true, -1.0), livedaw_dsp::util::wing_mono_pan(-1.0));
            assert_eq!(s.pan.gains(false, 0.5), livedaw_dsp::util::stereo_balance(0.5));
            assert_eq!(s.port_groups, WING_PORT_GROUPS);
        }
    }

    #[test]
    fn the_wings_models() {
        use livedaw_dsp::wing::{AUX_DYN_MODELS, DYN_MODELS, EQ_MODELS_BUS, EQ_MODELS_CH, FILTER_TOOLS, FX_MODELS};
        for p in Profile::ALL {
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
}
