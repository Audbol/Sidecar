//! Matching one console's gate or compressor settings to another's model (COPY FROM's FIT
//! TO THIS CONSOLE for dynamics).
//!
//! Dynamics are matched by meaning, not by curve: each parameter is given a [`Role`] from its
//! label and unit (threshold, ratio, attack, hold, release, range, knee, make-up gain,
//! detector), read from the source in plain units (dB, ms, a ratio as a number, a knee as
//! 0 hard to 1 softest), and written to the target parameter of the same role, converted
//! (seconds and milliseconds, a list of values, a ratio as a list position or as text) and
//! held to the target's range. A target parameter with no counterpart keeps its default.
//!
//! Models whose controls have no units (a vintage compressor's INPUT 0..10) can't be
//! matched this way; [`map`] gives `None` for them and they stay as they were.

use crate::model::{ModelCategory, ModelSpec, ParamKind, ParamSpec};

/// What a dynamics parameter does.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Role {
    Threshold,
    Ratio,
    Attack,
    Hold,
    Release,
    Range,
    Knee,
    Gain,
    Detector,
}

impl Role {
    /// The role's name in a report.
    pub fn name(self) -> &'static str {
        match self {
            Role::Threshold => "threshold",
            Role::Ratio => "ratio",
            Role::Attack => "attack",
            Role::Hold => "hold",
            Role::Release => "release",
            Role::Range => "range",
            Role::Knee => "knee",
            Role::Gain => "make-up gain",
            Role::Detector => "detector",
        }
    }
}

/// A number in a list entry: "4", ".3", "1.5:1" or "1:1.5" (a ratio, as a number above 1).
fn number(text: &str) -> Option<f64> {
    let t = text.trim();
    if let Some((a, b)) = t.split_once(':') {
        let (a, b) = (a.trim().parse::<f64>().ok()?, b.trim().parse::<f64>().ok()?);
        return (a > 0.0 && b > 0.0).then(|| if a >= b { a / b } else { b / a });
    }
    t.parse().ok()
}

fn is_time(unit: &str) -> bool {
    matches!(unit, "ms" | "s")
}

/// The parameter's role, when it has one this module can read and write.
pub fn role(p: &ParamSpec) -> Option<Role> {
    let label = p.label.to_ascii_uppercase();
    let numeric = match p.kind {
        ParamKind::Choice(list) => list.iter().all(|c| number(c).is_some()),
        _ => true,
    };
    let db = matches!(p.unit, "dB" | "dBu");
    let r = match label.as_str() {
        "THR" | "THRESHOLD" if db && p.is_continuous() => Role::Threshold,
        "RATIO" | "COMPRESSION" if numeric && p.unit.is_empty() && !matches!(p.kind, ParamKind::Linear) => Role::Ratio,
        "ATTACK" if is_time(p.unit) && numeric => Role::Attack,
        "HOLD" if is_time(p.unit) && numeric => Role::Hold,
        "RELEASE" | "DECAY" if is_time(p.unit) && numeric => Role::Release,
        "RANGE" | "DEPTH" if db => Role::Range,
        "KNEE" | "SOFT KNEE" => Role::Knee,
        "GAIN" | "OUT GAIN" | "MAKE-UP" if db => Role::Gain,
        "DETECTOR" | "DETECT" | "MODE" if detector_index(p, false).is_some() => Role::Detector,
        _ => return None,
    };
    Some(r)
}

/// Which of a detector list's entries is peak (`rms` false) or RMS / average (`rms` true).
fn detector_index(p: &ParamSpec, rms: bool) -> Option<usize> {
    let ParamKind::Choice(list) = p.kind else { return None };
    let is = |c: &str, rms: bool| {
        let c = c.to_ascii_uppercase();
        if rms {
            c == "RMS" || c == "AVG"
        } else {
            c == "PEAK" || c == "PK"
        }
    };
    let peak = list.iter().position(|c| is(c, false))?;
    let avg = list.iter().position(|c| is(c, true))?;
    Some(if rms { avg } else { peak })
}

/// The value of `p` set to `v`, in plain units: dB, ms, a ratio, a knee 0..1, detector 0 peak
/// or 1 RMS.
fn read(p: &ParamSpec, role: Role, v: f32) -> Option<f64> {
    let v = p.sanitise(v);
    let index = v.round().max(0.0) as usize;
    let scale = if p.unit == "s" { 1000.0 } else { 1.0 };
    match role {
        Role::Knee => {
            let span = (p.hi() - p.lo()).max(1.0) as f64;
            Some(match p.kind {
                ParamKind::Choice(list) => index as f64 / (list.len().max(2) - 1) as f64,
                _ => (v - p.lo()) as f64 / span,
            })
        }
        Role::Detector => Some(if detector_index(p, true) == Some(index) { 1.0 } else { 0.0 }),
        Role::Range => Some((v as f64).abs()),
        _ => match p.kind {
            ParamKind::Values(list) => list.get(index).map(|x| *x as f64 * scale),
            ParamKind::Choice(list) => list.get(index).and_then(|c| number(c)).map(|x| x * scale),
            _ => Some(v as f64 * scale),
        },
    }
}

/// The value of `p` closest to `x` (plain units, as [`read`] gives them).
fn write(p: &ParamSpec, role: Role, x: f64) -> f32 {
    let scale = if p.unit == "s" { 1000.0 } else { 1.0 };
    // Times and ratios compare by proportion, the rest by difference.
    let log = matches!(role, Role::Ratio | Role::Attack | Role::Hold | Role::Release);
    let distance = |a: f64, b: f64| if log { (a.max(1e-6) / b.max(1e-6)).ln().abs() } else { (a - b).abs() };
    let nearest = |values: &mut dyn Iterator<Item = Option<f64>>| -> f32 {
        let mut best = (f64::INFINITY, 0usize);
        for (i, y) in values.enumerate() {
            if let Some(y) = y {
                let d = distance(y, x);
                if d < best.0 {
                    best = (d, i);
                }
            }
        }
        best.1 as f32
    };
    let v = match role {
        Role::Knee => match p.kind {
            ParamKind::Choice(list) => (x * (list.len().max(2) - 1) as f64).round() as f32,
            _ => p.lo() + (x * (p.hi() - p.lo()) as f64).round() as f32,
        },
        Role::Detector => detector_index(p, x > 0.5).map_or(p.default, |i| i as f32),
        Role::Range => (if p.hi() <= 0.0 { -x } else { x }) as f32,
        _ => match p.kind {
            ParamKind::Values(list) => nearest(&mut list.iter().map(|y| Some(*y as f64 * scale))),
            ParamKind::Choice(list) => nearest(&mut list.iter().map(|c| number(c).map(|y| y * scale))),
            _ => (x / scale) as f32,
        },
    };
    p.sanitise(v)
}

/// Settings moved onto another model.
#[derive(Clone, Debug)]
pub struct Mapped {
    /// The target model's values.
    pub values: Vec<f32>,
    /// The source's roles the target has no control for (their settings are lost).
    pub unmatched: Vec<Role>,
}

fn roles(spec: &ModelSpec) -> Vec<(usize, Role)> {
    let mut seen = Vec::new();
    for (i, p) in spec.params.iter().enumerate() {
        if let Some(r) = role(p) {
            if !seen.iter().any(|(_, s)| *s == r) {
                seen.push((i, r));
            }
        }
    }
    seen
}

/// Whether a model can be matched at all: it has a threshold in dB.
pub fn mappable(spec: &ModelSpec) -> bool {
    roles(spec).iter().any(|(_, r)| *r == Role::Threshold)
}

/// `from` set to `values`, as settings of `to` (the target's defaults where nothing matches);
/// `None` when either model can't be matched.
pub fn map(from: &ModelSpec, values: &[f32], to: &ModelSpec) -> Option<Mapped> {
    if !mappable(from) || !mappable(to) {
        return None;
    }
    let mut out = vec![0.0f32; to.params.len()];
    for (o, p) in out.iter_mut().zip(to.params) {
        *o = p.default;
    }
    let target = roles(to);
    let mut unmatched = Vec::new();
    for (i, r) in roles(from) {
        let Some(x) = values.get(i).and_then(|v| read(&from.params[i], r, *v)) else { continue };
        match target.iter().find(|(_, t)| *t == r) {
            Some((j, _)) => out[*j] = write(&to.params[*j], r, x),
            None => unmatched.push(r),
        }
    }
    Some(Mapped { values: out, unmatched })
}

/// The kind of dynamics a model's token names, beyond its category: an expander or a ducker.
fn flavour(token: &str) -> u8 {
    let t = token.to_ascii_uppercase();
    if t.contains("EXP") {
        1
    } else if t.contains("DUC") || t.contains("DUK") {
        2
    } else {
        0
    }
}

/// The model of `candidates` that takes `from`'s settings best: same category and kind
/// (expander, ducker), the most roles in common; on a tie the first of `preferred` (the
/// strip's own model, the console's default), then the earliest candidate.
pub fn choose(from: &ModelSpec, candidates: &[&'static ModelSpec], preferred: &[&str]) -> Option<&'static ModelSpec> {
    if !mappable(from) {
        return None;
    }
    let wanted: Vec<Role> = roles(from).into_iter().map(|(_, r)| r).collect();
    let category = |c: ModelCategory| match c {
        ModelCategory::Gate => 0,
        _ => 1,
    };
    let score = |m: &ModelSpec| {
        let have = roles(m);
        let common = wanted.iter().filter(|r| have.iter().any(|(_, h)| h == *r)).count() as i32;
        let same_kind = i32::from(category(m.category) == category(from.category)) * 100;
        let same_flavour = i32::from(flavour(m.token) == flavour(from.token)) * 20;
        let pref = preferred.iter().position(|p| *p == m.token).map_or(0, |i| 10 - i as i32);
        same_kind + same_flavour + common * 2 + pref
    };
    candidates.iter().copied().filter(|m| mappable(m) && m.category != ModelCategory::Special).max_by_key(|m| {
        // max_by_key keeps the last of equals: the index breaks ties towards the first.
        let index = candidates.iter().position(|c| c.token == m.token).unwrap_or(0) as i32;
        score(m) * 1000 - index
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{model_index, ModelTables};

    fn model(t: &'static ModelTables, token: &str) -> &'static ModelSpec {
        &t.dynamics[model_index(t.dynamics, token).unwrap_or_else(|| panic!("{token}"))]
    }

    fn set(spec: &ModelSpec, pairs: &[(&str, f32)]) -> Vec<f32> {
        let mut v = vec![0.0; spec.params.len()];
        for (o, p) in v.iter_mut().zip(spec.params) {
            *o = p.default;
        }
        for (id, x) in pairs {
            v[spec.param_index(id).unwrap_or_else(|| panic!("{id}"))] = *x;
        }
        v
    }

    fn plain(spec: &ModelSpec, values: &[f32], r: Role) -> f64 {
        let i = spec.params.iter().position(|p| role(p) == Some(r)).unwrap_or_else(|| panic!("{} has no {r:?}", spec.token));
        read(&spec.params[i], r, values[i]).unwrap()
    }

    #[test]
    fn list_entries_read_as_numbers() {
        assert_eq!(number("4"), Some(4.0));
        assert_eq!(number(".3"), Some(0.3));
        assert_eq!(number("1.5:1"), Some(1.5));
        assert_eq!(number("1:1.5"), Some(1.5));
        assert_eq!(number("FAST"), None);
    }

    #[test]
    fn a_wing_compressor_onto_a_dm2000() {
        let from = model(&crate::wing::MODELS, "COMP");
        // Ratio 4:1 is the WING's list entry for 4.0.
        let ParamKind::Values(ratios) = from.params[from.param_index("ratio").unwrap()].kind else { panic!() };
        let four = ratios.iter().position(|r| *r == 4.0).unwrap() as f32;
        let v = set(from, &[("thr", -24.0), ("ratio", four), ("att", 10.0), ("rel", 250.0), ("knee", 2.0)]);
        let dm = &crate::dm2000::MODELS;
        let mine: Vec<&'static ModelSpec> = dm.dynamics.iter().collect();
        let to = choose(from, &mine, &[dm.comp_default]).unwrap();
        assert_eq!(to.token, "DMCMP");
        let m = map(from, &v, to).unwrap();
        assert_eq!(plain(to, &m.values, Role::Threshold), -24.0);
        assert_eq!(plain(to, &m.values, Role::Ratio), 4.0);
        assert_eq!(plain(to, &m.values, Role::Attack), 10.0);
        let rel = plain(to, &m.values, Role::Release);
        assert!((rel / 250.0 - 1.0).abs() < 0.06, "the DM2000's nearest release step: {rel}");
        assert!((plain(to, &m.values, Role::Knee) - 0.4).abs() < 0.21, "a medium knee stays medium");
    }

    #[test]
    fn a_dm2000_gate_onto_a_wing_and_range_keeps_its_size() {
        let from = model(&crate::dm2000::MODELS, "DMGAT");
        let v = set(from, &[("thr", -30.0), ("range", -40.0), ("att", 2.0)]);
        let wing = &crate::wing::MODELS;
        let mine: Vec<&'static ModelSpec> = wing.dynamics.iter().collect();
        let to = choose(from, &mine, &[wing.gate_default]).unwrap();
        assert_eq!(to.token, "GATE");
        let m = map(from, &v, to).unwrap();
        assert_eq!(plain(to, &m.values, Role::Threshold), -30.0);
        assert_eq!(m.values[to.param_index("range").unwrap()], 40.0, "the WING's range counts up");
        assert_eq!(plain(to, &m.values, Role::Attack), 2.0);
    }

    #[test]
    fn seconds_become_milliseconds_and_ranges_hold() {
        // The ProMix's release is in seconds, the X32's in ms.
        let from = model(&crate::promix::MODELS, "PMCOMP");
        let v = set(from, &[("thr", 10.0), ("rel", 0.5)]);
        let to = model(&crate::x32::MODELS, "X32COMP");
        let m = map(from, &v, to).unwrap();
        assert_eq!(plain(to, &m.values, Role::Release), 500.0);
        assert_eq!(plain(to, &m.values, Role::Threshold), 0.0, "held to the X32's top");
    }

    #[test]
    fn an_expander_goes_to_an_expander_and_a_ducker_to_a_ducker() {
        let x32: Vec<&'static ModelSpec> = crate::x32::MODELS.dynamics.iter().collect();
        assert_eq!(choose(model(&crate::wing::MODELS, "EXP"), &x32, &[]).unwrap().token, "X32EXP");
        let dm: Vec<&'static ModelSpec> = crate::dm2000::MODELS.dynamics.iter().collect();
        assert_eq!(choose(model(&crate::x32::MODELS, "X32DUCK"), &dm, &[]).unwrap().token, "DMDUK");
    }

    #[test]
    fn a_model_without_units_isnt_matched() {
        let la = model(&crate::wing::MODELS, "LA");
        let dm: Vec<&'static ModelSpec> = crate::dm2000::MODELS.dynamics.iter().collect();
        assert!(choose(la, &dm, &[]).is_none());
        assert!(map(la, &set(la, &[]), model(&crate::dm2000::MODELS, "DMCMP")).is_none());
    }

    #[test]
    fn every_consoles_compressor_and_gate_land_on_every_others() {
        let all: [&'static ModelTables; 10] = [
            &crate::wing::MODELS,
            &crate::x32::MODELS,
            &crate::dlive::MODELS,
            &crate::sq::MODELS,
            &crate::dm2000::MODELS,
            &crate::y01v::MODELS,
            &crate::y02r::MODELS,
            &crate::promix::MODELS,
            &crate::midas_pro::MODELS,
            &crate::xenyx1202::MODELS,
        ];
        for from_t in all {
            for (token, kind) in [(from_t.comp_default, 1), (from_t.gate_default, 0)] {
                let from = model(from_t, token);
                if !mappable(from) {
                    continue; // the 1202's one-knob compressor
                }
                let v = set(from, &[]);
                let thr = plain(from, &v, Role::Threshold);
                for to_t in all {
                    let mine: Vec<&'static ModelSpec> = to_t.dynamics.iter().collect();
                    let preferred = [if kind == 1 { to_t.comp_default } else { to_t.gate_default }];
                    let Some(to) = choose(from, &mine, &preferred) else { continue };
                    let m = map(from, &v, to).unwrap();
                    let got = plain(to, &m.values, Role::Threshold);
                    let p = &to.params[to.params.iter().position(|p| role(p) == Some(Role::Threshold)).unwrap()];
                    let want = thr.clamp(p.lo() as f64, p.hi() as f64);
                    assert!((got - want).abs() < 1e-3, "{token} -> {}: threshold {got} for {want}", to.token);
                }
            }
        }
    }
}
