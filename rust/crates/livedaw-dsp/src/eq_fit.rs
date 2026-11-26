//! Fitting one EQ model's settings to another's curve (COPY FROM's FIT TO THIS CONSOLE).
//!
//! The source EQ's response is measured with its own [`design`] at [`POINTS`] log-spaced
//! frequencies from 20 Hz to 20 kHz. The target model's parameters are then searched to match
//! it: every parameter in turn, a coarse scan of its whole range first, then a finer search
//! around the best value, until a pass no longer improves the match. Switches and lists are
//! tried value by value. The same search fits any model to any other, so no table per pair of
//! consoles is needed.

use crate::model::{ModelSpec, ParamKind};
use crate::wing::eq::{design, EqDesign};
use crate::wing::MAX_EQ_PARAMS;
use std::f64::consts::PI;

/// Points the curves are compared at.
pub const POINTS: usize = 40;
const SR: f64 = 48_000.0;

/// An EQ's response in dB at the fit's frequencies.
pub type Curve = [f64; POINTS];

/// The frequencies the curves are compared at: 20 Hz to 20 kHz, log-spaced.
pub fn frequencies() -> [f64; POINTS] {
    std::array::from_fn(|i| 20.0 * 1000f64.powf(i as f64 / (POINTS - 1) as f64))
}

/// `e^-jw` and `e^-2jw` at each frequency, worked out once.
struct Grid {
    z: [(f64, f64, f64, f64); POINTS],
}

impl Grid {
    fn new() -> Self {
        let f = frequencies();
        Self {
            z: std::array::from_fn(|i| {
                let w = 2.0 * PI * f[i] / SR;
                (w.cos(), -w.sin(), (2.0 * w).cos(), -(2.0 * w).sin())
            }),
        }
    }

    fn curve(&self, d: &EqDesign) -> Curve {
        std::array::from_fn(|i| {
            let (c1, s1, c2, s2) = self.z[i];
            let mut mag2 = 1.0;
            for c in &d.sections[..d.len] {
                let (nr, ni) = (c.b0 + c.b1 * c1 + c.b2 * c2, c.b1 * s1 + c.b2 * s2);
                let (dr, di) = (1.0 + c.a1 * c1 + c.a2 * c2, c.a1 * s1 + c.a2 * s2);
                mag2 *= (nr * nr + ni * ni) / (dr * dr + di * di).max(1e-30);
            }
            10.0 * mag2.max(1e-18).log10() + d.out_gain_db as f64
        })
    }
}

/// The response of `spec` set to `values`, with MIX at `mix` percent.
pub fn curve(spec: &ModelSpec, values: &[f32], mix: f32) -> Curve {
    Grid::new().curve(&design(spec, values, SR, mix / 100.0))
}

/// The largest difference between two curves, in dB.
pub fn worst_db(a: &Curve, b: &Curve) -> f64 {
    a.iter().zip(b).map(|(x, y)| (x - y).abs()).fold(0.0, f64::max)
}

/// Whether a curve is flat (within 0.1 dB): nothing worth fitting.
pub fn is_flat(c: &Curve) -> bool {
    c.iter().all(|x| x.abs() < 0.1)
}

/// A fitted setting: the target model's values and how close it came.
#[derive(Clone, Debug)]
pub struct Fit {
    pub values: Vec<f32>,
    /// The largest difference from the wanted curve, in dB.
    pub worst_db: f64,
}

/// Finds the settings of `spec` (MIX at 100 %) whose response comes closest to `wanted`.
pub fn fit(spec: &ModelSpec, wanted: &Curve) -> Fit {
    let grid = Grid::new();
    let n = spec.params.len().min(MAX_EQ_PARAMS);
    let mut values = vec![0.0f32; MAX_EQ_PARAMS];
    spec.defaults_into(&mut values);
    let cost = |v: &[f32]| {
        let got = grid.curve(&design(spec, v, SR, 1.0));
        let (mut sum, mut worst) = (0.0, 0.0f64);
        for (g, w) in got.iter().zip(wanted) {
            let e = g - w;
            sum += e * e;
            worst = worst.max(e.abs());
        }
        // Mostly the average error (smooth to search), a little of the worst.
        sum / POINTS as f64 + 0.1 * worst * worst
    };
    let mut best = cost(&values);

    for pass in 0..12 {
        let before = best;
        for i in 0..n {
            let p = &spec.params[i];
            let count = (p.hi() - p.lo()).round() as i64 + 1;
            let discrete = !p.is_continuous() || matches!(p.kind, ParamKind::Toggle);
            if discrete && count <= 32 {
                // Every value of a switch or a list.
                for k in 0..count {
                    let mut v = values.clone();
                    v[i] = p.sanitise(p.lo() + k as f32);
                    let c = cost(&v);
                    if c < best - 1e-12 {
                        best = c;
                        values = v;
                    }
                }
                continue;
            }
            let mut pos = p.to_normalised(values[i]);
            if pass < 3 {
                // Coarse: the whole range in sixteenths.
                for k in 0..=16 {
                    let mut v = values.clone();
                    v[i] = p.from_normalised(k as f32 / 16.0);
                    let c = cost(&v);
                    if c < best - 1e-12 {
                        best = c;
                        values = v;
                        pos = k as f32 / 16.0;
                    }
                }
            }
            // Fine: step either way, halving the step when neither helps.
            let mut step = if pass < 3 { 1.0 / 32.0 } else { 1.0 / 64.0 };
            while step > 1.0 / 4096.0 {
                let mut moved = false;
                for dir in [-1.0f32, 1.0] {
                    let to = (pos + dir * step).clamp(0.0, 1.0);
                    let mut v = values.clone();
                    v[i] = p.from_normalised(to);
                    let c = cost(&v);
                    if c < best - 1e-12 {
                        best = c;
                        values = v;
                        pos = to;
                        moved = true;
                        break;
                    }
                }
                if !moved {
                    step *= 0.5;
                }
            }
        }
        if pass >= 3 && before - best < 1e-6 {
            break;
        }
    }

    let got = grid.curve(&design(spec, &values, SR, 1.0));
    values.truncate(n);
    Fit { values, worst_db: worst_db(&got, wanted) }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::biquad::BiquadCoeffs;
    use crate::model::ModelTables;

    /// A curve made of plain filters, as an EQ setting would make it.
    fn made_of(sections: &[BiquadCoeffs]) -> Curve {
        let mut d = EqDesign::default();
        for s in sections {
            d.push(*s);
        }
        Grid::new().curve(&d)
    }

    /// The typical curves: a low shelf and two peaks; a presence lift and a cut in the mud;
    /// a bright top.
    fn typical() -> Vec<(&'static str, Curve)> {
        vec![
            (
                "low shelf and two peaks",
                made_of(&[
                    BiquadCoeffs::low_shelf(SR, 100.0, 0.7, 4.0),
                    BiquadCoeffs::peak(SR, 400.0, 1.4, -3.0),
                    BiquadCoeffs::peak(SR, 3000.0, 1.0, 3.0),
                ]),
            ),
            (
                "mud cut and presence",
                made_of(&[BiquadCoeffs::peak(SR, 300.0, 1.0, -4.0), BiquadCoeffs::peak(SR, 5000.0, 0.8, 4.0)]),
            ),
            ("bright top", made_of(&[BiquadCoeffs::high_shelf(SR, 8000.0, 0.7, 5.0)])),
        ]
    }

    fn consoles() -> Vec<(&'static str, &'static ModelTables)> {
        vec![
            ("WING", &crate::wing::MODELS),
            ("X32", &crate::x32::MODELS),
            ("dLive", &crate::dlive::MODELS),
            ("SQ", &crate::sq::MODELS),
            ("DM2000", &crate::dm2000::MODELS),
            ("01V", &crate::y01v::MODELS),
            ("02R", &crate::y02r::MODELS),
            ("ProMix", &crate::promix::MODELS),
            ("PRO", &crate::midas_pro::MODELS),
            ("1202", &crate::xenyx1202::MODELS),
        ]
    }

    #[test]
    fn a_flat_curve_is_flat() {
        let spec = &crate::dm2000::MODELS.eq_channel[0];
        let mut v = vec![0.0; MAX_EQ_PARAMS];
        spec.defaults_into(&mut v);
        assert!(is_flat(&curve(spec, &v, 100.0)));
    }

    #[test]
    fn every_consoles_channel_eq_fits_the_typical_curves() {
        let mut lines = Vec::new();
        for (console, tables) in consoles() {
            let spec = &tables.eq_channel[0];
            for (name, wanted) in typical() {
                let f = fit(spec, &wanted);
                // The fitted values give the curve they claim to.
                assert!((worst_db(&curve(spec, &f.values, 100.0), &wanted) - f.worst_db).abs() < 1e-6);
                lines.push(format!("{console:7} {:8} {name:26} {:.2} dB", spec.token, f.worst_db));
                // A full parametric EQ within 1 dB; the ProMix's and the 1202's fewer, fixed
                // bands as close as they reach, and always closer than leaving the EQ flat.
                let (limit, share) = match spec.token {
                    "PMEQ" => (1.6, 0.5),
                    "XYEQ" => (4.5, 0.9),
                    _ => (1.0, 0.5),
                };
                let rms = |c: &Curve| (c.iter().map(|x| x * x).sum::<f64>() / POINTS as f64).sqrt();
                let got = curve(spec, &f.values, 100.0);
                let off: Curve = std::array::from_fn(|i| got[i] - wanted[i]);
                assert!(f.worst_db < limit && rms(&off) < rms(&wanted) * share, "{console} {name}: {:.2} dB", f.worst_db);
            }
        }
        println!("{}", lines.join("\n"));
    }
}
