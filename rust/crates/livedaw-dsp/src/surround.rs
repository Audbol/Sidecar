//! Surround panning (the 02R's SURROUND, V2 MAN pp.64-73; the DM2000's too):
//! a position on a two-dimensional pan graph to a gain per speaker, in the
//! console's bus order.
//!
//! The guides give the modes, the speakers' buses, the trajectories and what
//! CSR does at 0, 0.5 and 1, not the law: this one is constant power. Left /
//! right and front / rear each split the power between their two sides as
//! `sqrt`; the front speakers of a mode with a centre pan L - C - R pairwise
//! for CSR 1, L - R for CSR 0, and between by CSR (0.5: the centre as loud as
//! the sides for a sound at the front centre). The subwoofer takes its own
//! level, wherever the sound is.

/// A surround mode and its speakers on buses 1-6 (V2 MAN p.64).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    /// FL, FR, RL, RR.
    TwoTwo,
    /// L, R, C, S.
    ThreeOne,
    /// FL, FR, RL, RR, FC, SW.
    ThreeTwoOne,
}

impl Mode {
    pub const ALL: [Mode; 3] = [Mode::TwoTwo, Mode::ThreeOne, Mode::ThreeTwoOne];

    pub fn name(self) -> &'static str {
        match self {
            Mode::TwoTwo => "2+2",
            Mode::ThreeOne => "3+1",
            Mode::ThreeTwoOne => "3+2+1",
        }
    }

    /// The buses it takes (1-4, or 1-6).
    pub fn buses(self) -> usize {
        match self {
            Mode::ThreeTwoOne => 6,
            _ => 4,
        }
    }

    /// Its speakers' names, by bus.
    pub fn speakers(self) -> &'static [&'static str] {
        match self {
            Mode::TwoTwo => &["FL", "FR", "RL", "RR"],
            Mode::ThreeOne => &["L", "R", "C", "S"],
            Mode::ThreeTwoOne => &["FL", "FR", "RL", "RR", "FC", "SW"],
        }
    }

    /// Where speaker `bus` sits on the pan graph (x -1 left .. 1 right, y -1
    /// rear .. 1 front); the subwoofer has no place.
    pub fn place(self, bus: usize) -> Option<(f32, f32)> {
        let corners = [(-1.0, 1.0), (1.0, 1.0), (-1.0, -1.0), (1.0, -1.0)];
        match (self, bus) {
            (Mode::ThreeOne, 0) => Some((-1.0, 1.0)),
            (Mode::ThreeOne, 1) => Some((1.0, 1.0)),
            (Mode::ThreeOne, 2) => Some((0.0, 1.0)),
            (Mode::ThreeOne, 3) => Some((0.0, -1.0)),
            (Mode::ThreeTwoOne, 4) => Some((0.0, 1.0)),
            (Mode::ThreeTwoOne, 5) => None,
            (_, b) => corners.get(b).copied(),
        }
    }
}

/// A sound's place and its levels: x -1 (left) .. 1 (right), y -1 (rear) ..
/// 1 (front), CSR 0 .. 1, the subwoofer's gain (3+2+1).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Position {
    pub x: f32,
    pub y: f32,
    pub csr: f32,
    pub sub: f32,
}

/// The gains of the mode's speakers, by bus (the rest 0).
pub fn gains(mode: Mode, p: Position) -> [f32; 6] {
    let (x, y) = (p.x.clamp(-1.0, 1.0), p.y.clamp(-1.0, 1.0));
    let (left, right) = (((1.0 - x) * 0.5).sqrt(), ((1.0 + x) * 0.5).sqrt());
    let (front, rear) = (((1.0 + y) * 0.5).sqrt(), ((1.0 - y) * 0.5).sqrt());
    let mut g = [0.0; 6];
    match mode {
        Mode::TwoTwo => {
            g[..4].copy_from_slice(&[left * front, right * front, left * rear, right * rear]);
        }
        Mode::ThreeOne => {
            let [l, c, r] = front_three(x, p.csr);
            g[..4].copy_from_slice(&[l * front, r * front, c * front, rear]);
        }
        Mode::ThreeTwoOne => {
            let [l, c, r] = front_three(x, p.csr);
            g = [l * front, r * front, left * rear, right * rear, c * front, p.sub.max(0.0)];
        }
    }
    g
}

/// The front speakers L, C, R for a sound at `x`, by CSR.
fn front_three(x: f32, csr: f32) -> [f32; 3] {
    // CSR 1: pairwise L - C or C - R; CSR 0: L - R (no centre).
    let three = if x < 0.0 { [(-x).sqrt(), (1.0 + x).sqrt(), 0.0] } else { [0.0, (1.0 - x).sqrt(), x.sqrt()] };
    let two = [((1.0 - x) * 0.5).sqrt(), 0.0, ((1.0 + x) * 0.5).sqrt()];
    // The centre's share of a front-centre sound's power: 0, a third at 0.5, all.
    let csr = csr.clamp(0.0, 1.0);
    let w = if csr <= 0.5 { csr * 2.0 / 3.0 } else { 1.0 / 3.0 + (csr - 0.5) * 4.0 / 3.0 };
    std::array::from_fn(|i| (w * three[i] * three[i] + (1.0 - w) * two[i] * two[i]).sqrt())
}

/// The trajectories of ROUTING 4/4 (V2 MAN pp.69-71).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Trajectory {
    LeftRight,
    FrontRear,
    RearLeftFrontRight,
    FrontLeftRearRight,
    LeftRightSemicircle,
    FrontRearSemicircle,
    Circle,
}

impl Trajectory {
    pub const ALL: [Trajectory; 7] = [
        Trajectory::LeftRight,
        Trajectory::FrontRear,
        Trajectory::RearLeftFrontRight,
        Trajectory::FrontLeftRearRight,
        Trajectory::LeftRightSemicircle,
        Trajectory::FrontRearSemicircle,
        Trajectory::Circle,
    ];

    pub fn name(self) -> &'static str {
        match self {
            Trajectory::LeftRight => "L <> R",
            Trajectory::FrontRear => "F <> R",
            Trajectory::RearLeftFrontRight => "RL <> FR",
            Trajectory::FrontLeftRearRight => "FL <> RR",
            Trajectory::LeftRightSemicircle => "L <> R SEMI",
            Trajectory::FrontRearSemicircle => "F <> R SEMI",
            Trajectory::Circle => "CIRCLE",
        }
    }

    /// The place at `t` (0 .. 1 along it) of the trajectory `width` and
    /// `depth` wide and deep (0 .. 1 of the graph's half) and offset by
    /// (`ox`, `oy`); clamped to the graph.
    pub fn at(self, t: f32, width: f32, depth: f32, ox: f32, oy: f32) -> (f32, f32) {
        let t = t.clamp(0.0, 1.0);
        let s = 2.0 * t - 1.0;
        let pi = std::f32::consts::PI;
        let (x, y) = match self {
            Trajectory::LeftRight => (s * width, 0.0),
            Trajectory::FrontRear => (0.0, -s * depth),
            Trajectory::RearLeftFrontRight => (s * width, s * depth),
            Trajectory::FrontLeftRearRight => (s * width, -s * depth),
            // Over the front, left to right; down the left, front to rear.
            Trajectory::LeftRightSemicircle => (-(pi * t).cos() * width, (pi * t).sin() * depth),
            Trajectory::FrontRearSemicircle => (-(pi * t).sin() * width, (pi * t).cos() * depth),
            // From the front, clockwise.
            Trajectory::Circle => ((2.0 * pi * t).sin() * width, (2.0 * pi * t).cos() * depth),
        };
        ((x + ox).clamp(-1.0, 1.0), (y + oy).clamp(-1.0, 1.0))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn power(g: &[f32]) -> f32 {
        g.iter().map(|x| x * x).sum()
    }

    #[test]
    fn constant_power_everywhere() {
        for mode in Mode::ALL {
            for &(x, y) in &[(0.0, 0.0), (-1.0, 1.0), (0.3, -0.7), (1.0, -1.0), (-0.5, 0.5)] {
                for csr in [0.0, 0.5, 1.0] {
                    let g = gains(mode, Position { x, y, csr, sub: 0.0 });
                    assert!((power(&g) - 1.0).abs() < 1e-5, "{mode:?} {x} {y} {csr}: {g:?}");
                }
            }
        }
    }

    #[test]
    fn corners_and_csr() {
        let at = |mode, x, y, csr| gains(mode, Position { x, y, csr, sub: 0.0 });
        assert_eq!(at(Mode::TwoTwo, -1.0, 1.0, 0.0)[..4], [1.0, 0.0, 0.0, 0.0], "FL");
        assert!((at(Mode::TwoTwo, 1.0, -1.0, 0.0)[3] - 1.0).abs() < 1e-6, "RR");
        // 3+1 at the front centre: CSR 0 L and R alone, 0.5 all three alike, 1 C alone.
        let g = at(Mode::ThreeOne, 0.0, 1.0, 0.0);
        assert!(g[2].abs() < 1e-6 && (g[0] - g[1]).abs() < 1e-6);
        let g = at(Mode::ThreeOne, 0.0, 1.0, 0.5);
        assert!((g[0] - g[2]).abs() < 1e-5 && (g[1] - g[2]).abs() < 1e-5, "{g:?}");
        let g = at(Mode::ThreeOne, 0.0, 1.0, 1.0);
        assert!((g[2] - 1.0).abs() < 1e-6);
        // At the rear, S alone; the subwoofer at its level.
        assert!((at(Mode::ThreeOne, 0.4, -1.0, 0.5)[3] - 1.0).abs() < 1e-6);
        assert_eq!(gains(Mode::ThreeTwoOne, Position { x: 0.0, y: 0.0, csr: 0.5, sub: 0.5 })[5], 0.5);
    }

    #[test]
    fn trajectories() {
        assert_eq!(Trajectory::LeftRight.at(0.0, 1.0, 1.0, 0.0, 0.0), (-1.0, 0.0));
        assert_eq!(Trajectory::LeftRight.at(1.0, 1.0, 1.0, 0.0, 0.0), (1.0, 0.0));
        let (x, y) = Trajectory::Circle.at(0.25, 0.5, 0.5, 0.0, 0.0);
        assert!((x - 0.5).abs() < 1e-5 && y.abs() < 1e-5, "a quarter round: right");
        let (x, y) = Trajectory::LeftRightSemicircle.at(0.5, 1.0, 1.0, 0.0, 0.5);
        assert!(x.abs() < 1e-5 && (y - 1.0).abs() < 1e-5, "the top, offset to the front and clamped");
    }
}
