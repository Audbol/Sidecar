//! Console profiles: the console a configuration emulates and what that
//! console is (its counts, its strips' chains and process orders, its fader
//! and pan laws). The
//! Universal core takes any configuration; a profile limits and dresses it
//! (`docs/UNIVERSAL_MIXER.md`, Phase 3). The WING is the first; the Universal
//! is no console's limits.

use crate::chain::{self, Node};
use crate::config::Counts;
use crate::params::StripKind;
use livedaw_dsp::fader::FaderLaw;
use livedaw_dsp::util::PanLaw;
use livedaw_dsp::wing::PROCESS_ORDERS;

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
};

/// No console's limits. Its strips are the WING's until the WING's blocks
/// leave the strip (Phase 3, step 5); a new one starts with the WING's counts.
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
        }
    }
}
