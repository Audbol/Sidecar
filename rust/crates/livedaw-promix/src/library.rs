//! The ProMix 01's program libraries: the dynamics processors' 10 preset
//! programs (User's Guide p.78) and 10 user programs. The effects' and the
//! EQ library's follow with the effects (Phase 5 step 4).

/// A dynamics program: its name, its type (an index of
/// `livedaw_dsp::promix::DYN_MODELS`) and its five values in the type's
/// units (THRESHOLD, RATIO's index, ATTACK, RELEASE s, OUT GAIN for a
/// compressor; THRESHOLD, ATTACK, HOLD, DECAY, RANGE for a gate or ducking).
#[derive(Clone, Debug, PartialEq)]
pub struct DynProgram {
    pub name: String,
    pub model: u8,
    pub values: [f32; 5],
}

const fn hold(steps: f32) -> f32 {
    steps * livedaw_dsp::promix::HOLD_STEP_MS
}

/// RATIO's index of `r`:1 (`livedaw_dsp::promix::RATIOS`).
const fn ratio(r: f32) -> f32 {
    let table = livedaw_dsp::promix::RATIOS;
    let mut i = 0;
    while i < table.len() {
        if table[i] == r {
            return i as f32;
        }
        i += 1;
    }
    panic!("not a ratio");
}

/// The preset programs 1-10 (p.78; HOLD and DECAY on their steps: 101 ms is
/// step 38, 1.3 s step 15).
pub const DYN_PRESETS: [(&str, u8, [f32; 5]); 10] = [
    ("Total Comp", 0, [0.0, ratio(2.0), 50.0, 3.0, 1.0]),
    ("Radio Comp", 0, [6.0, ratio(4.0), 18.0, 5.0, 3.0]),
    ("Comp Fast", 0, [2.0, ratio(4.0), 5.0, 2.5, 3.0]),
    ("Comp Slow", 0, [-2.0, ratio(4.0), 70.0, 5.0, 1.0]),
    ("Comp Heavy1", 0, [-3.0, ratio(10.0), 14.0, 0.5, 2.0]),
    ("Comp Heavy2", 0, [0.0, ratio(6.0), 18.0, 1.5, 3.0]),
    ("Gate Fast", 1, [-6.0, 1.0, hold(38.0), 15.0 * 85.0, -20.0]),
    ("Gate Slow", 1, [-2.0, 5.0, hold(118.0), 96.0 * 85.0, -25.0]),
    ("Ducking", 2, [-8.0, 9.0, hold(111.0), 76.0 * 85.0, -9.0]),
    ("BGM Ducking", 2, [-18.0, 9.0, hold(128.0), 128.0 * 85.0, -14.0]),
];

/// The dynamics library: presets 1-10 and user programs 11-20 (a user
/// program starts as an empty "Initial Data" compressor).
#[derive(Clone, Debug, PartialEq)]
pub struct DynLibrary {
    pub user: Vec<DynProgram>,
}

impl Default for DynLibrary {
    fn default() -> Self {
        let first = &DYN_PRESETS[0];
        let user = (0..10).map(|_| DynProgram { name: "Initial Data".into(), model: first.1, values: first.2 }).collect();
        DynLibrary { user }
    }
}

impl DynLibrary {
    /// Program `n` (0-based: 0-9 the presets, 10-19 the user programs).
    pub fn program(&self, n: usize) -> Option<DynProgram> {
        match n {
            0..=9 => {
                let (name, model, values) = DYN_PRESETS[n];
                Some(DynProgram { name: name.into(), model, values })
            }
            10..=19 => self.user.get(n - 10).cloned(),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_dsp::promix::DYN_MODELS;

    #[test]
    fn the_presets_sit_on_their_types_steps() {
        let lib = DynLibrary::default();
        for n in 0..20 {
            let p = lib.program(n).unwrap();
            let spec = &DYN_MODELS[p.model as usize];
            for (v, s) in p.values.iter().zip(spec.params) {
                assert!((s.lo()..=s.hi()).contains(v), "{} {}: {v}", p.name, s.id);
            }
        }
        assert_eq!(lib.program(20), None);
        let gate_slow = lib.program(7).unwrap();
        assert_eq!((gate_slow.values[2].round(), (gate_slow.values[3] / 100.0).round() / 10.0), (315.0, 8.2));
        assert_eq!(DYN_MODELS[lib.program(8).unwrap().model as usize].token, "PMDUCK");
    }
}
