//! The console's configuration: how many strips and groups of each kind it
//! has. The Universal core has no fixed counts; an emulation (the WING
//! first) is one configuration of it (`docs/UNIVERSAL_MIXER.md`).

use crate::params::{
    MAX_DCAS, MAX_FX, MAX_MAINS, MAX_MATRICES, MAX_MUTE_GROUPS, WING_AUX, WING_BUSES, WING_CHANNELS, WING_DCAS, WING_FX,
    WING_MAINS, WING_MATRICES, WING_MUTE_GROUPS,
};
use crate::sends::MAX_BUSES;

/// The most input channels a console can have (a bound for sanity, not a
/// realtime limit: CPU is).
pub const MAX_CHANNELS: usize = 4096;
/// The most aux (stereo input) channels.
pub const MAX_AUX: usize = 512;

/// How many of each kind a console has.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Counts {
    pub channels: usize,
    pub aux: usize,
    pub buses: usize,
    /// At least one (the console's main), up to `MAX_MAINS`.
    pub mains: usize,
    pub matrices: usize,
    pub dcas: usize,
    pub mute_groups: usize,
    pub fx: usize,
}

impl Counts {
    /// The WING's: 40 channels, 8 aux, 16 buses, 4 mains, 8 matrices, 16
    /// DCAs, 8 mute groups, 16 FX slots.
    pub const WING: Counts = Counts {
        channels: WING_CHANNELS,
        aux: WING_AUX,
        buses: WING_BUSES,
        mains: WING_MAINS,
        matrices: WING_MATRICES,
        dcas: WING_DCAS,
        mute_groups: WING_MUTE_GROUPS,
        fx: WING_FX,
    };

    /// Within what the engine can hold.
    pub fn sanitised(self) -> Self {
        Self {
            channels: self.channels.min(MAX_CHANNELS),
            aux: self.aux.min(MAX_AUX),
            buses: self.buses.min(MAX_BUSES),
            mains: self.mains.clamp(1, MAX_MAINS),
            matrices: self.matrices.min(MAX_MATRICES),
            dcas: self.dcas.min(MAX_DCAS),
            mute_groups: self.mute_groups.min(MAX_MUTE_GROUPS),
            fx: self.fx.min(MAX_FX),
        }
    }
}

impl Default for Counts {
    fn default() -> Self {
        Self::WING
    }
}

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

    /// The name the interface shows.
    pub fn name(self) -> &'static str {
        match self {
            Profile::Universal => "Universal",
            Profile::Wing => "WING",
        }
    }

    /// Whether a console of `counts` is one of this profile's: the WING's
    /// aux, mains, matrices, DCAs and mute groups (what its surface
    /// addresses; channels, buses and FX slots can grow on a WING too).
    pub fn fits(self, counts: &Counts) -> bool {
        match self {
            Profile::Universal => true,
            Profile::Wing => {
                let w = Counts::WING;
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

/// A console configuration (`Engine::configure`).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ConsoleConfig {
    pub profile: Profile,
    pub counts: Counts,
}

impl ConsoleConfig {
    /// The Behringer WING.
    pub fn wing() -> Self {
        Self { profile: Profile::Wing, counts: Counts::WING }
    }

    /// A console of `counts`: a WING if they are the WING's, else the Universal.
    pub fn of_counts(counts: Counts) -> Self {
        Self { profile: Profile::Wing.or_universal(&counts), counts }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn counts_stay_within_what_the_engine_holds() {
        let c = Counts { channels: 99999, mains: 0, matrices: 9999, dcas: 1000, mute_groups: 1000, fx: 1000, ..Counts::WING };
        let s = c.sanitised();
        assert_eq!((s.channels, s.mains, s.matrices), (MAX_CHANNELS, 1, MAX_MATRICES));
        assert_eq!((s.dcas, s.mute_groups, s.fx), (MAX_DCAS, MAX_MUTE_GROUPS, MAX_FX));
        assert_eq!(Counts::WING.sanitised(), Counts::WING);
        assert_eq!(ConsoleConfig::default(), ConsoleConfig::wing());
    }

    #[test]
    fn profiles() {
        for p in Profile::ALL {
            assert_eq!(Profile::from_id(p.id()), Some(p));
        }
        assert_eq!(Profile::from_id("x32"), None);
        assert!(Profile::Wing.fits(&Counts::WING));
        assert!(Profile::Wing.fits(&Counts { channels: 64, buses: 24, fx: 32, ..Counts::WING }));
        for c in [Counts { mains: 2, ..Counts::WING }, Counts { buses: 8, ..Counts::WING }, Counts { dcas: 24, ..Counts::WING }] {
            assert!(!Profile::Wing.fits(&c));
            assert_eq!(ConsoleConfig::of_counts(c).profile, Profile::Universal);
        }
        assert_eq!(ConsoleConfig::of_counts(Counts::WING), ConsoleConfig::wing());
    }
}
