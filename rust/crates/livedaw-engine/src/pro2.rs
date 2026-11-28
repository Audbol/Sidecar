//! What only a Midas PRO2 has (Owner's Manual DOC02-DL2 Issue A, "OM", pages as
//! printed): the aux sends' three mix modes, stereo-linked mixes, MCA groups, the input
//! channels' direct outputs and SIS panning. The summing that uses them is in
//! `engine.rs` (`pro2_bus_feed`, `pro2_direct_out`, `sum_main`).
//!
//! Clean-room: from the manual's words only. What it doesn't say, and is Sidecar's
//! reading, is said where it is used.

use crate::atomic::AtomicF32;
use crate::engine::Engine;
use crate::params::{StripKind, StripParams};
use crate::profile::{pro2, Profile, ProfileSpec};
use crate::strip::Strip;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering::Relaxed};

/// An aux send's mix mode (OM pp.77, 260): MODE cycles mix, group, mix minus.
pub mod mix_mode {
    /// A mix: ON, PRE and a level per input.
    pub const MIX: i32 = 0;
    /// A group: ON alone, the level the input's fader (and, stereo, its pan).
    pub const GROUP: i32 = 1;
    /// Mix minus: as a group, but every input is routed until its switch (minus) takes it out.
    pub const MIX_MINUS: i32 = 2;
}

/// The direct output's tap-off point (OM pp.249, 341), in the MODE switch's order.
pub mod direct_mode {
    /// "Post-fade and mute".
    pub const POST_FADE: i32 = 0;
    /// "Pre-mute, pre-processing": after the input's gain, before its filters.
    pub const PRE_PROCESSING: i32 = 1;
    /// "Pre-mute, post-processing": where PFL listens.
    pub const POST_PROCESSING: i32 = 2;
}

/// A strip's channel safes but AUTO (OM pp.174-175, Appendix H pp.385-430), bits of
/// [`Pro2StripParams::safes`]: what of the strip a scene recall leaves as it is. AUTO
/// (everything) is the strip's recall safe (`StripParams::recall_safe`). Which settings
/// each keeps is the session's (`livedaw_session::pro2_safes`).
pub mod safe {
    /// EQ: the EQ, the filters and (inputs) the C/O order.
    pub const EQ: i32 = 1 << 0;
    /// DYN: the gate and the compressor (aux returns have none).
    pub const DYN: i32 = 1 << 1;
    /// MIC: inputs' gain, 48V, trim, phase, delay, insert and direct output; outputs'
    /// insert and direct input.
    pub const MIC: i32 = 1 << 2;
    /// MUTE: the mute (and an input's direct output's).
    pub const MUTE: i32 = 1 << 3;
    /// FADER: the fader, ST and MON, SIS and the pan; outputs' trim, delay and mode too.
    pub const FADER: i32 = 1 << 4;
    /// All five.
    pub const ALL: i32 = EQ | DYN | MIC | MUTE | FADER;
}

/// MCA groups per mix: one for each VCA (OM p.44: they have the VCAs' names and members).
pub const NUM_MCAS: usize = 8;
/// The direct output's level, -oo to +10 dB (OM p.249).
pub const DIRECT_MAX_DB: f32 = 10.0;

/// A strip's PRO2 settings. `#[repr(C)]`, mirrored by `ld::Pro2StripParams`.
#[repr(C)]
#[derive(Debug)]
pub struct Pro2StripParams {
    /// Aux sends (buses): [`mix_mode`]. A linked pair takes the odd one's (OM p.78).
    pub mix_mode: AtomicI32,
    /// Aux sends and matrices: one of a stereo pair, odd with even (OM p.78); set on both.
    pub linked: AtomicBool,
    /// Input channels: SIS on, the LCR master bus enable (OM p.261).
    pub sis: AtomicBool,
    /// Input channels: the direct output's MUTE (OM p.248).
    pub direct_mute: AtomicBool,
    /// Input channels: the direct output's tap-off point, [`direct_mode`].
    pub direct_mode: AtomicI32,
    /// Input channels: the direct output's level, dB.
    pub direct_db: AtomicF32,
    /// Input channels: the SIS image, 0 (stereo) to 100 (full LCR) % (OM p.98).
    pub sis_image: AtomicF32,
    /// Aux sends and matrices: the MCA levels, one for each VCA's members, dB (OM p.44).
    pub mca_db: [AtomicF32; NUM_MCAS],
    /// The channel safes but AUTO, [`safe`] bits. Not part of a scene (a recall, a reset
    /// and a copy leave them); saved with the show.
    pub safes: AtomicI32,
}

/// The values a strip starts with: see [`Pro2StripParams::factory`].
#[derive(Clone, Copy, Debug, PartialEq)]
struct Values {
    mix_mode: i32,
    linked: bool,
    sis: bool,
    direct_mute: bool,
    direct_mode: i32,
    direct_db: f32,
    sis_image: f32,
    mca_db: [f32; NUM_MCAS],
}

impl Values {
    /// Any console's: a mono mix, the direct output after the processing at 0 dB (the
    /// mode is Sidecar's reading: the manual names no default; 0 dB is what "Set All
    /// Dir Out Gains to 0dB" sets, OM p.207), SIS off with the image half way (equal
    /// power on all three at centre pan, OM p.99), MCAs at 0 dB.
    const DEFAULT: Self = Self {
        mix_mode: mix_mode::MIX,
        linked: false,
        sis: false,
        direct_mute: false,
        direct_mode: direct_mode::POST_PROCESSING,
        direct_db: 0.0,
        sis_image: 50.0,
        mca_db: [0.0; NUM_MCAS],
    };

    /// A PRO2's strip of `kind` in `slot`: Normal (FOH) mode's aux sends 9-16 are stereo
    /// groups (OM p.211); everything else as [`Self::DEFAULT`].
    fn factory(spec: &ProfileSpec, kind: StripKind, slot: usize) -> Self {
        let group = spec.profile == Profile::MidasPro2 && kind == StripKind::Bus && (pro2::GROUPS..16).contains(&slot);
        Self { mix_mode: if group { mix_mode::GROUP } else { mix_mode::MIX }, linked: group, ..Self::DEFAULT }
    }
}

impl Default for Pro2StripParams {
    fn default() -> Self {
        Self::from_values(&Values::DEFAULT)
    }
}

impl Pro2StripParams {
    fn from_values(v: &Values) -> Self {
        Self {
            mix_mode: AtomicI32::new(v.mix_mode),
            linked: AtomicBool::new(v.linked),
            sis: AtomicBool::new(v.sis),
            direct_mute: AtomicBool::new(v.direct_mute),
            direct_mode: AtomicI32::new(v.direct_mode),
            direct_db: AtomicF32::new(v.direct_db),
            sis_image: AtomicF32::new(v.sis_image),
            mca_db: std::array::from_fn(|i| AtomicF32::new(v.mca_db[i])),
            safes: AtomicI32::new(0),
        }
    }

    fn values(&self) -> Values {
        Values {
            mix_mode: self.mix_mode.load(Relaxed),
            linked: self.linked.load(Relaxed),
            sis: self.sis.load(Relaxed),
            direct_mute: self.direct_mute.load(Relaxed),
            direct_mode: self.direct_mode.load(Relaxed),
            direct_db: self.direct_db.load(),
            sis_image: self.sis_image.load(),
            mca_db: std::array::from_fn(|i| self.mca_db[i].load()),
        }
    }

    fn store(&self, v: &Values) {
        self.mix_mode.store(v.mix_mode, Relaxed);
        self.linked.store(v.linked, Relaxed);
        self.sis.store(v.sis, Relaxed);
        self.direct_mute.store(v.direct_mute, Relaxed);
        self.direct_mode.store(v.direct_mode, Relaxed);
        self.direct_db.store(v.direct_db);
        self.sis_image.store(v.sis_image);
        for (m, db) in self.mca_db.iter().zip(v.mca_db) {
            m.store(db);
        }
    }

    /// What a strip of `kind` in `slot` on a console of `spec` starts with.
    pub fn factory(spec: &ProfileSpec, kind: StripKind, slot: usize) -> Self {
        Self::from_values(&Values::factory(spec, kind, slot))
    }

    /// Everything is as such a new strip's.
    pub fn is_factory(&self, spec: &ProfileSpec, kind: StripKind, slot: usize) -> bool {
        self.values() == Values::factory(spec, kind, slot)
    }

    /// Back to such a new strip's.
    pub fn reset(&self, spec: &ProfileSpec, kind: StripKind, slot: usize) {
        self.store(&Values::factory(spec, kind, slot));
    }

    /// Copies `other`'s settings.
    pub fn copy_from(&self, other: &Self) {
        self.store(&other.values());
    }

    /// The MCA levels of this mix that reach an input with `p`'s VCAs: the sum of its
    /// VCAs', as a VCA's level adds to its members' (OM p.44: "on top of the local mix
    /// level controls of the group members").
    pub fn mca_offset_db(&self, p: &StripParams) -> f32 {
        (0..NUM_MCAS).filter(|&v| p.dca_mask.contains(v)).map(|v| self.mca_db[v].load()).sum()
    }
}

/// The slot whose send settings a mix in `slot` uses: the odd one (0-based even) of a
/// linked pair, else its own.
pub fn leader_slot(slot: usize, linked: bool) -> usize {
    if linked {
        slot & !1
    } else {
        slot
    }
}

/// SIS (OM pp.98-99): a mono channel's gains to the left, centre (mono master) and right
/// at `pan` (-1..1), the SIS image `image` 0 (stereo) to 1 (full LCR). Stereo is `law`
/// (the profile's mono pan law: C silent); LCR is the three-way law (centre pan all in C,
/// `dlive::lcr_gains`). Between them each output's power is blended by the knob, so the
/// ends are the two laws as they are. Sidecar's reading: the manual describes the ends and
/// that half way, at centre pan, all three are equal, which this gives with a pan law at
/// unity in its centre (the profile's).
pub fn sis_gains(law: fn(f32) -> (f32, f32), pan: f32, image: f32) -> (f32, f32, f32) {
    let w = image.clamp(0.0, 1.0);
    let (sl, sr) = law(pan);
    let (ll, lc, lr) = crate::dlive::lcr_gains(pan);
    let blend = |s: f32, l: f32| ((1.0 - w) * s * s + w * l * l).sqrt();
    (blend(sl, ll), w.sqrt() * lc, blend(sr, lr))
}

impl Engine {
    /// The strip of `kind` whose id is `id`.
    fn pro2_strip(&self, kind: StripKind, id: i32) -> Option<std::sync::Arc<Strip>> {
        self.strips(kind).into_iter().find(|s| s.id() == id)
    }

    /// Message thread: links mix `mix` (an aux send or a matrix) with its neighbour, odd
    /// with even, or unlinks the pair (OM p.78: "Pairs can only be created from adjacent mix
    /// buses"). Linking gives the even one the odd one's mode and MCA levels (p.78: "the mode
    /// of the odd channel is used on both"). A linked pair of aux sends reaches the stereo
    /// master as its two sides (`engine::pro2_master_sides`). False: not a mix, or no
    /// neighbour.
    pub fn pro2_set_linked(&self, mix: &Strip, on: bool) -> bool {
        if !matches!(mix.kind(), StripKind::Bus | StripKind::Matrix) || mix.id() < 0 {
            return false;
        }
        let odd = mix.id() & !1;
        let (Some(a), Some(b)) = (self.pro2_strip(mix.kind(), odd), self.pro2_strip(mix.kind(), odd + 1)) else {
            return false;
        };
        a.pro2.linked.store(on, Relaxed);
        b.pro2.linked.store(on, Relaxed);
        if on {
            b.pro2.mix_mode.store(a.pro2.mix_mode.load(Relaxed), Relaxed);
            for (to, from) in b.pro2.mca_db.iter().zip(&a.pro2.mca_db) {
                to.store(from.load());
            }
        }
        true
    }

    /// Message thread: an aux send's mix mode (`mix_mode`), on both of a linked pair. Into
    /// mix minus, every input is routed (OM p.77: "In mix minus mode all buses are initially
    /// routed"): its send's switch, which takes it out, goes off.
    pub fn pro2_set_mix_mode(&self, bus: &Strip, mode: i32) {
        if bus.kind() != StripKind::Bus || !(mix_mode::MIX..=mix_mode::MIX_MINUS).contains(&mode) {
            return;
        }
        let slot = bus.id().max(0);
        let slots = if bus.pro2.linked.load(Relaxed) { vec![slot & !1, (slot & !1) + 1] } else { vec![slot] };
        let into_minus = mode == mix_mode::MIX_MINUS && bus.pro2.mix_mode.load(Relaxed) != mode;
        bus.pro2.mix_mode.store(mode, Relaxed);
        for b in slots.iter().filter_map(|&id| self.pro2_strip(StripKind::Bus, id)) {
            b.pro2.mix_mode.store(mode, Relaxed);
        }
        if !into_minus {
            return;
        }
        for kind in [StripKind::Channel, StripKind::Aux] {
            for s in self.strips(kind) {
                for &id in &slots {
                    if let Some(send) = s.sends.get_or_create(id as usize) {
                        send.enabled.store(false, Relaxed);
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_dsp::util::wing_mono_pan;

    #[test]
    fn sis_ends_are_stereo_and_lcr_and_half_way_is_equal() {
        // Fully anti-clockwise: the stereo pan, C silent.
        let (l, c, r) = sis_gains(wing_mono_pan, 0.0, 0.0);
        assert!((l - 1.0).abs() < 1e-6 && c == 0.0 && (r - 1.0).abs() < 1e-6);
        // Fully clockwise: centre pan is C alone (OM p.98).
        let (l, c, r) = sis_gains(wing_mono_pan, 0.0, 1.0);
        assert!(l.abs() < 1e-6 && (c - 1.0).abs() < 1e-6 && r.abs() < 1e-6);
        // Both centred: the same power to all three (OM p.99).
        let (l, c, r) = sis_gains(wing_mono_pan, 0.0, 0.5);
        assert!((l - c).abs() < 1e-6 && (r - c).abs() < 1e-6);
        // Hard left in LCR: left alone.
        let (l, c, r) = sis_gains(wing_mono_pan, -1.0, 1.0);
        assert!((l - 1.0).abs() < 1e-6 && c.abs() < 1e-6 && r.abs() < 1e-6);
    }

    #[test]
    fn mca_levels_add_for_each_vca_an_input_is_in() {
        let mix = Pro2StripParams::default();
        mix.mca_db[0].store(-6.0);
        mix.mca_db[2].store(-4.0);
        let p = StripParams::new_in_slot(StripKind::Channel, 0);
        assert_eq!(mix.mca_offset_db(&p), 0.0);
        p.dca_mask.set(0, true);
        assert_eq!(mix.mca_offset_db(&p), -6.0);
        p.dca_mask.set(2, true);
        p.dca_mask.set(9, true); // past the eight: no MCA
        assert_eq!(mix.mca_offset_db(&p), -10.0);
    }

    #[test]
    fn a_pro2_starts_with_aux_sends_9_to_16_as_stereo_groups() {
        let spec = Profile::MidasPro2.spec();
        for slot in 0..16 {
            let p = Pro2StripParams::factory(spec, StripKind::Bus, slot);
            let group = slot >= 8;
            assert_eq!(p.linked.load(Relaxed), group, "aux {}", slot + 1);
            assert_eq!(p.mix_mode.load(Relaxed), if group { mix_mode::GROUP } else { mix_mode::MIX });
            assert!(p.is_factory(spec, StripKind::Bus, slot));
        }
        // Another console's buses are mono mixes.
        assert!(!Pro2StripParams::factory(Profile::Wing.spec(), StripKind::Bus, 9).linked.load(Relaxed));
        let p = Pro2StripParams::factory(spec, StripKind::Bus, 9);
        p.mca_db[3].store(-3.0);
        assert!(!p.is_factory(spec, StripKind::Bus, 9));
        p.reset(spec, StripKind::Bus, 9);
        assert!(p.is_factory(spec, StripKind::Bus, 9));
        // The safes are no setting of a scene: a reset and a copy leave them.
        p.safes.store(safe::EQ | safe::FADER, Relaxed);
        p.reset(spec, StripKind::Bus, 9);
        p.copy_from(&Pro2StripParams::factory(spec, StripKind::Bus, 9));
        assert!(p.is_factory(spec, StripKind::Bus, 9));
        assert_eq!(p.safes.load(Relaxed), safe::EQ | safe::FADER);
    }

    #[test]
    fn the_layout_is_the_headers() {
        // `ld::Pro2StripParams` asserts the same.
        assert_eq!(std::mem::size_of::<Pro2StripParams>(), 56);
        assert_eq!(std::mem::offset_of!(Pro2StripParams, direct_mode), 8);
        assert_eq!(std::mem::offset_of!(Pro2StripParams, safes), 52);
    }

    #[test]
    fn leaders_are_the_odd_ones_of_linked_pairs() {
        assert_eq!(leader_slot(9, true), 8);
        assert_eq!(leader_slot(8, true), 8);
        assert_eq!(leader_slot(9, false), 9);
    }
}
