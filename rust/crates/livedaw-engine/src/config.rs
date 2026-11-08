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

impl Counts {
    /// The X32's: 32 channels, 16 aux (8 aux in and, from Phase 4 step 2, 8 FX
    /// returns), 16 buses, 2 mains (LR and M/C), 6 matrices, 8 DCAs, 6 mute
    /// groups, 8 FX slots.
    pub const X32: Counts = Counts { channels: 32, aux: 16, buses: 16, mains: 2, matrices: 6, dcas: 8, mute_groups: 6, fx: 8 };

    /// The Yamaha ProMix 01's: 16 channels, 3 aux (the stereo input ST IN and
    /// the effect returns RTN1-2), 4 buses (SEND1-4), 1 main (ST OUT), no
    /// matrices, DCAs or mute groups, 2 FX slots (its internal effects).
    pub const PROMIX01: Counts = Counts { channels: 16, aux: 3, buses: 4, mains: 1, matrices: 0, dcas: 0, mute_groups: 0, fx: 2 };

    /// The Yamaha 02R's: 36 channels (MIC/LINE 1-16, LINE 17/18-23/24 stereo,
    /// TAPE 1-16), 2 aux (the effect returns EFF1-2), 16 buses (BUS 1-8, AUX
    /// 1-8), 1 main (STEREO), no matrices, DCAs or mute groups (its fader and
    /// mute groups are its own), 2 FX slots (its internal effects).
    pub const Y02R: Counts = Counts { channels: 36, aux: 2, buses: 16, mains: 1, matrices: 0, dcas: 0, mute_groups: 0, fx: 2 };

    /// The Allen & Heath SQ-5's (Reference Guide V1.6.0, section 18): 48 input
    /// channels, 8 aux (its stereo FX returns), 16 buses (12 mixes, then the 4 FX
    /// sends), 1 main (LR), 3 stereo matrices, 8 DCAs, 8 mute groups, 8 FX slots.
    pub const SQ5: Counts = Counts { channels: 48, aux: 8, buses: 16, mains: 1, matrices: 3, dcas: 8, mute_groups: 8, fx: 8 };
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
