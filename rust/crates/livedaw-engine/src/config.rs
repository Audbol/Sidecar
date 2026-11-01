//! The console's configuration: how many strips and groups of each kind it
//! has. The Universal core has no fixed counts; an emulation (the WING
//! first) is one configuration of it (`docs/UNIVERSAL_MIXER.md`).

use crate::params::{
    MAX_DCAS, MAX_FX, MAX_MUTE_GROUPS, NUM_AUX, NUM_MAINS, NUM_MATRICES, WING_BUSES, WING_CHANNELS, WING_DCAS, WING_FX,
    WING_MUTE_GROUPS,
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
    /// Up to the WING's 4 for now: sends to mains are still a fixed block
    /// of every strip (Phase 1, step 2 makes them general).
    pub mains: usize,
    /// Up to the WING's 8 for now (as `mains`).
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
        aux: NUM_AUX,
        buses: WING_BUSES,
        mains: NUM_MAINS,
        matrices: NUM_MATRICES,
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
            mains: self.mains.clamp(1, NUM_MAINS),
            matrices: self.matrices.min(NUM_MATRICES),
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
    pub counts: Counts,
}

impl ConsoleConfig {
    /// The Behringer WING.
    pub fn wing() -> Self {
        Self { counts: Counts::WING }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn counts_stay_within_what_the_engine_holds() {
        let c = Counts { channels: 99999, mains: 0, matrices: 99, dcas: 1000, mute_groups: 1000, fx: 1000, ..Counts::WING };
        let s = c.sanitised();
        assert_eq!((s.channels, s.mains, s.matrices), (MAX_CHANNELS, 1, NUM_MATRICES));
        assert_eq!((s.dcas, s.mute_groups, s.fx), (MAX_DCAS, MAX_MUTE_GROUPS, MAX_FX));
        assert_eq!(Counts::WING.sanitised(), Counts::WING);
        assert_eq!(ConsoleConfig::default(), ConsoleConfig::wing());
    }
}
