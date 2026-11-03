//! The console's configuration: how many strips and groups of each kind it
//! has. The Universal core has no fixed counts; an emulation (the WING
//! first) is one configuration of it (`docs/UNIVERSAL_MIXER.md`).

use crate::params::{
    MAX_DCAS, MAX_FX, MAX_MAINS, MAX_MATRICES, MAX_MUTE_GROUPS, WING_AUX, WING_BUSES, WING_CHANNELS, WING_DCAS, WING_FX,
    WING_MAINS, WING_MATRICES, WING_MUTE_GROUPS,
};
use crate::profile::Profile;
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

/// A console configuration (`Engine::configure`).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ConsoleConfig {
    pub profile: Profile,
    pub counts: Counts,
}

impl ConsoleConfig {
    /// The Behringer WING.
    pub fn wing() -> Self {
        let profile = Profile::Wing;
        Self { profile, counts: profile.spec().counts }
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
    fn of_counts() {
        assert_eq!(ConsoleConfig::of_counts(Counts { mains: 2, ..Counts::WING }).profile, Profile::Universal);
        assert_eq!(ConsoleConfig::of_counts(Counts::WING), ConsoleConfig::wing());
    }
}
