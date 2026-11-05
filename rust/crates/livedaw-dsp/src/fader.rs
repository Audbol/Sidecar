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

    /// The Yamaha ProMix 01's: 128 steps, +6 dB at the top, -66 dB on the
    /// step above -oo (User's Guide p.25, p.105). The guide gives no table of
    /// the steps between, so the breakpoints are where the panel prints its
    /// scale (6, 0, 5, 10, 20, 40, 60, measured on the drawing on p.2, the
    /// travel from 6 to -oo): 0 dB on step 100, -10 dB on 57, -20 dB on 36,
    /// -40 dB on 14, -60 dB on 7. A photo of a console's panel (November
    /// 2026) prints its scale where the drawing does, within a step or two;
    /// the steps' levels themselves are still to be measured. Every
    /// breakpoint sits on a step (`k / 127`).
    pub const PROMIX01: FaderLaw = FaderLaw {
        points: &[
            (1.0 / 127.0, -66.0),
            (7.0 / 127.0, -60.0),
            (14.0 / 127.0, -40.0),
            (36.0 / 127.0, -20.0),
            (57.0 / 127.0, -10.0),
            (100.0 / 127.0, 0.0),
            (1.0, 6.0),
        ],
        steps: 128,
    };

    /// The Yamaha 02R's channel faders: 128 steps, +10 dB at the top, -90 dB
    /// on the step above -oo (02R User's Guide p.192). The guide gives no table
    /// of the steps between, so the breakpoints are where the panel prints its
    /// scale (10, 5, 0, 5, 10, 15, 20, 30, 40, 50, measured on the Getting
    /// Started drawing, p.4): 0 dB on step 101; to be measured on a console.
    pub const Y02R: FaderLaw = FaderLaw {
        points: &[
            (1.0 / 127.0, -90.0),
            (6.0 / 127.0, -50.0),
            (12.0 / 127.0, -40.0),
            (21.0 / 127.0, -30.0),
            (39.0 / 127.0, -20.0),
            (71.0 / 127.0, -10.0),
            (101.0 / 127.0, 0.0),
            (1.0, 10.0),
        ],
        steps: 128,
    };

    /// The 02R's STEREO fader: 128 steps, 0 dB at the top, -120 dB on the step
    /// above -oo (p.192); the steps between are where the panel prints its
    /// scale (0, 5, 10, 15, 20, 30, 40, 50, 60, 70).
    pub const Y02R_STEREO: FaderLaw = FaderLaw {
        points: &[
            (1.0 / 127.0, -120.0),
            (11.0 / 127.0, -70.0),
            (15.0 / 127.0, -60.0),
            (24.0 / 127.0, -50.0),
            (36.0 / 127.0, -40.0),
            (51.0 / 127.0, -30.0),
            (72.0 / 127.0, -20.0),
            (1.0, 0.0),
        ],
        steps: 128,
    };

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

    /// The position (0..1) of a level in dB; below the lowest point is the
    /// bottom (the lowest point is its own position: a ProMix 01's step 1).
    pub fn to_pos(&self, db: f32) -> f32 {
        let pts = self.points;
        // NaN too: the bottom.
        if pts.is_empty() || db.is_nan() || db < pts[0].1 {
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
        if pts.is_empty() || p <= 0.0 || p < pts[0].0 {
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
    fn the_promix01s_law() {
        let law = FaderLaw::PROMIX01;
        assert!(law.points.len() <= FaderLaw::MAX_POINTS);
        assert_eq!((law.steps, law.max_db()), (128, 6.0));
        let step = |k: i32| law.to_db(k as f32 / 127.0);
        assert_eq!(step(0), FADER_MIN, "the bottom is -oo");
        assert!((step(1) + 66.0).abs() < 1e-4);
        assert!(step(100).abs() < 1e-4, "0 dB");
        assert!((step(57) + 10.0).abs() < 1e-3);
        // Every step is a level of its own, rising.
        assert!((1..128).all(|k| step(k) > step(k - 1)));
        assert_eq!((law.to_pos(0.0) * 127.0).round(), 100.0);
        assert_eq!((law.to_pos(-66.0) * 127.0).round(), 1.0, "-66 dB is step 1");
        assert_eq!(law.to_pos(-66.5), 0.0, "below it: -oo");
    }

    #[test]
    fn the_02rs_laws() {
        for (law, top, bottom, zero) in [(FaderLaw::Y02R, 10.0, -90.0, 101.0), (FaderLaw::Y02R_STEREO, 0.0, -120.0, 127.0)] {
            assert!(law.points.len() <= FaderLaw::MAX_POINTS);
            assert_eq!((law.steps, law.max_db()), (128, top));
            let step = |k: i32| law.to_db(k as f32 / 127.0);
            assert_eq!(step(0), FADER_MIN, "the bottom is -oo");
            assert!((step(1) - bottom).abs() < 1e-3);
            assert!((1..128).all(|k| step(k) > step(k - 1)), "every step its own level");
            assert_eq!((law.to_pos(0.0) * 127.0).round(), zero, "0 dB");
            // Every breakpoint on a step.
            assert!(law.points.iter().all(|p| ((p.0 * 127.0).round() - p.0 * 127.0).abs() < 1e-4));
        }
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
