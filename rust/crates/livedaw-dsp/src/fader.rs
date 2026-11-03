//! Fader laws: where a level in dB sits on a fader's travel. A console's
//! profile names its law ([`FaderLaw`]); the WING's is also kept as the
//! functions its protocols use (`fader_to_pos`, `pos_to_fader`).

/// The fader's silence (-oo).
pub const FADER_MIN: f32 = -144.0;
pub const FADER_MAX: f32 = 10.0;

/// WING fader law (position 0..1 for a level in dB), as on the X32: four
/// linear segments meeting at -60, -30 and -10 dB, 0 dB at 0.75, +10 dB at 1
/// and -90 dB at the bottom, which is -oo. The two upper segments match the
/// protocol document's examples (0 dB = 0.75, -3 dB = 0.675, +4 dB = 0.85),
/// the lowest a WING's reply (-67.38 dB = 0.04712).
pub fn fader_to_pos(db: f32) -> f32 {
    let db = db.clamp(FADER_MIN, FADER_MAX);
    let pos = if db >= -10.0 {
        (db + 30.0) / 40.0
    } else if db >= -30.0 {
        (db + 50.0) / 80.0
    } else if db >= -60.0 {
        (db + 70.0) / 160.0
    } else {
        (db + 90.0) / 480.0
    };
    pos.clamp(0.0, 1.0)
}

pub fn pos_to_fader(pos: f32) -> f32 {
    let p = pos.clamp(0.0, 1.0);
    if p >= 0.5 {
        p * 40.0 - 30.0
    } else if p >= 0.25 {
        p * 80.0 - 50.0
    } else if p >= 0.0625 {
        p * 160.0 - 70.0
    } else if p > 0.0 {
        p * 480.0 - 90.0
    } else {
        FADER_MIN
    }
}

/// A fader law: where a level in dB sits on a fader's travel (0..1), as
/// straight segments between breakpoints. The bottom of the travel is
/// silence ([`FADER_MIN`]); a stepped fader (128 positions, say) lands on its
/// steps.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FaderLaw {
    /// (position, dB) breakpoints, rising in both: the first is the lowest
    /// level above silence, the last the top of the travel. At most
    /// [`FaderLaw::MAX_POINTS`].
    pub points: &'static [(f32, f32)],
    /// The positions the fader has (0: continuous).
    pub steps: u32,
}

impl FaderLaw {
    pub const MAX_POINTS: usize = 8;

    /// The WING's: `fader_to_pos`.
    pub const WING: FaderLaw =
        FaderLaw { points: &[(0.0, -90.0), (0.0625, -60.0), (0.25, -30.0), (0.5, -10.0), (1.0, 10.0)], steps: 0 };

    /// The X32's: the WING's four segments (0 dB at 0.75, -60 dB at 0.0625), on
    /// its faders' 1024 steps.
    pub const X32: FaderLaw = FaderLaw { points: Self::WING.points, steps: 1024 };

    /// The level at the top of the travel.
    pub fn max_db(&self) -> f32 {
        self.points.last().map_or(FADER_MAX, |p| p.1)
    }

    /// The nearest position the fader has.
    pub fn snap(&self, pos: f32) -> f32 {
        if self.steps > 1 {
            let n = (self.steps - 1) as f32;
            (pos * n).round() / n
        } else {
            pos
        }
    }

    /// The position (0..1) of a level in dB; the lowest point and below are
    /// the bottom.
    pub fn to_pos(&self, db: f32) -> f32 {
        let pts = self.points;
        // NaN too: the bottom.
        if pts.is_empty() || db.is_nan() || db <= pts[0].1 {
            return 0.0;
        }
        let pos =
            pts.windows(2).find(|w| db <= w[1].1).map_or(1.0, |w| w[0].0 + (db - w[0].1) / (w[1].1 - w[0].1) * (w[1].0 - w[0].0));
        self.snap(pos.clamp(0.0, 1.0))
    }

    /// The level in dB at a position; the bottom is silence ([`FADER_MIN`]).
    pub fn to_db(&self, pos: f32) -> f32 {
        let pts = self.points;
        let p = self.snap(pos.clamp(0.0, 1.0));
        if pts.is_empty() || p <= pts[0].0 {
            return FADER_MIN;
        }
        pts.windows(2)
            .find(|w| p <= w[1].0)
            .map_or(self.max_db(), |w| w[0].1 + (p - w[0].0) / (w[1].0 - w[0].0) * (w[1].1 - w[0].1))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_wings_law_as_a_value() {
        let w = FaderLaw::WING;
        assert!(w.points.len() <= FaderLaw::MAX_POINTS);
        assert_eq!(w.max_db(), 10.0);
        assert_eq!(w.to_pos(0.0), 0.75);
        assert_eq!(w.to_db(0.0), FADER_MIN);
        assert_eq!(w.to_pos(FADER_MIN), 0.0);
        for i in 0..=1000 {
            let p = i as f32 / 1000.0;
            assert!((w.to_db(p) - pos_to_fader(p)).abs() < 1e-4, "{p}");
            let db = -95.0 + i as f32 * 0.105;
            assert!((w.to_pos(db) - fader_to_pos(db)).abs() < 1e-6, "{db} dB");
        }
    }

    #[test]
    fn a_stepped_law() {
        // 128 positions, +6 dB to -66 dB (a ProMix 01-like fader).
        let law = FaderLaw { points: &[(1.0 / 127.0, -66.0), (1.0, 6.0)], steps: 128 };
        assert_eq!(law.max_db(), 6.0);
        assert_eq!(law.to_db(0.001), FADER_MIN);
        assert_eq!(law.to_db(1.0), 6.0);
        let p = law.to_pos(0.0);
        assert_eq!((p * 127.0).round(), p * 127.0, "on a step");
        assert!((law.to_db(p) - 0.0).abs() < 0.3);
    }

    #[test]
    fn positions_round_trip() {
        for db in [FADER_MIN, -80.0, -60.0, -45.0, -30.0, -20.0, -10.0, -3.0, 0.0, 4.0, 10.0] {
            assert!((pos_to_fader(fader_to_pos(db)) - db).abs() < 1e-3, "{db} dB");
        }
        assert_eq!(fader_to_pos(0.0), 0.75);
        // A WING reply: -67.38281 dB at 0.04711914; -90 dB and below is the bottom.
        assert!((fader_to_pos(-67.38281) - 0.04711914).abs() < 1e-6);
        assert_eq!(fader_to_pos(-90.0), 0.0);
    }
}
