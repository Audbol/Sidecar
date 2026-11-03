//! The WING as a profile over the core (Phase 3 step 8,
//! `docs/UNIVERSAL_MIXER.md`): what this crate is to the console the engine
//! emulates.
//!
//! The WING's description (counts, chains, laws, port groups, models,
//! colours, labels) is the engine's [`Profile::Wing`] spec, which the
//! Universal still shares; it moves here once a second console shows what is
//! shared and what is the WING's. This crate is the WING's protocols: the
//! network servers its profile's [`Remotes`] name, the parameter tree they
//! serve, and its snapshot and show files.

use crate::server::Config;
use livedaw_engine::{NetworkProtocol, Profile, ProfileSpec, Remotes};

/// The profile this crate speaks for.
pub const PROFILE: Profile = Profile::Wing;

/// What the WING is (the engine's description of it).
pub fn spec() -> &'static ProfileSpec {
    PROFILE.spec()
}

/// Whether this crate serves network protocol `p`.
pub fn serves(p: NetworkProtocol) -> bool {
    matches!(p, NetworkProtocol::WingDiscovery | NetworkProtocol::WingOsc | NetworkProtocol::WingNative)
}

impl Config {
    /// The servers `remotes` names (the console's profile's) and no others:
    /// a server the profile doesn't name stays off whatever the settings say.
    pub fn limited_to(mut self, remotes: &Remotes) -> Self {
        let named = |p: NetworkProtocol| remotes.network.contains(&p);
        self.discovery &= named(NetworkProtocol::WingDiscovery);
        self.osc &= named(NetworkProtocol::WingOsc);
        self.native &= named(NetworkProtocol::WingNative);
        self
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_wing_serves_its_profiles_protocols() {
        // Everything the WING's profile (and the Universal's, the WING's for
        // now) names, this crate serves; the X32's is `livedaw-x32`'s.
        for p in [Profile::Universal, Profile::Wing] {
            assert!(p.spec().remotes.network.iter().all(|&n| serves(n)), "{p:?}");
        }
        assert!(!serves(NetworkProtocol::X32Osc));
        assert_eq!(spec().profile, Profile::Wing);

        let all = Config::default().limited_to(&spec().remotes);
        assert!(all.discovery && all.osc && all.native);
        let osc_only = Config::default().limited_to(&Remotes { midi: None, network: &[NetworkProtocol::WingOsc] });
        assert!(!osc_only.discovery && osc_only.osc && !osc_only.native);
        let off = Config { osc: false, ..Config::default() }.limited_to(&spec().remotes);
        assert!(!off.osc, "the settings still turn a server off");
    }
}
