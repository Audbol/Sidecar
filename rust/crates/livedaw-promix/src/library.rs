//! The ProMix 01's program libraries (User's Guide): the effects' 30 preset
//! programs and 10 user programs (pp.30, 37-44), the dynamics processors' 10
//! and 10 (p.78) and the EQ library's 30 and 20 (pp.22-24). A program is a
//! name, a type (a model) and its values; recalling one sets a slot, a
//! processor or a strip's EQ. The user programs are the console's, not a
//! scene's (p.34, p.75).

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

/// An effects program: its name, its type (a token of
/// `livedaw_dsp::promix::FX_MODELS`) and its values (the type's order).
#[derive(Clone, Debug, PartialEq)]
pub struct FxProgram {
    pub name: String,
    pub token: &'static str,
    pub values: Vec<f32>,
}

/// LPF / HPF's index of a frequency (`livedaw_dsp::promix::fx`), THRU the last / first.
const fn lpf(hz: f32) -> f32 {
    index(livedaw_dsp::promix::fx::LPF_FREQS, hz)
}
const fn hpf(hz: f32) -> f32 {
    index(livedaw_dsp::promix::fx::HPF_FREQS, hz)
}
const THRU_LPF: f32 = livedaw_dsp::promix::fx::LPF_FREQS.len() as f32 - 1.0;
const THRU_HPF: f32 = 0.0;

const fn index(table: &[f32], v: f32) -> f32 {
    let mut i = 0;
    while i < table.len() {
        if table[i] == v {
            return i as f32;
        }
        i += 1;
    }
    panic!("not in the table");
}

/// The preset effects programs 1-30 (pp.37-44): name, type, values.
pub const FX_PRESETS: [(&str, &str, &[f32]); 30] = [
    ("Reverb Hall 1", "PMREV", &[2.8, 0.8, 6.0, 40.0, lpf(7000.0), THRU_HPF]),
    ("Reverb Hall 2", "PMREV", &[3.2, 0.7, 8.0, 38.0, lpf(6300.0), THRU_HPF]),
    ("Reverb Room 1", "PMREV", &[1.4, 0.8, 7.0, 5.0, THRU_LPF, hpf(280.0)]),
    ("Reverb Room 2", "PMREV", &[1.8, 0.6, 6.0, 17.0, lpf(9000.0), hpf(80.0)]),
    ("Reverb Stage", "PMREV", &[3.4, 0.9, 8.0, 45.0, THRU_LPF, hpf(70.0)]),
    ("Reverb Plate", "PMREV", &[2.4, 0.7, 8.0, 16.0, lpf(8000.0), THRU_HPF]),
    ("Rev Ambience 1", "PMREV", &[1.2, 1.0, 8.0, 19.0, lpf(9000.0), hpf(45.0)]),
    ("Rev Ambience 2", "PMREV", &[0.8, 0.6, 8.0, 0.1, THRU_LPF, hpf(56.0)]),
    ("Rev Live Room 1", "PMREV", &[2.4, 0.8, 7.0, 0.1, lpf(7000.0), THRU_HPF]),
    ("Rev Live Room 2", "PMREV", &[2.2, 0.5, 6.0, 12.0, lpf(4000.0), THRU_HPF]),
    ("Reverb Vocal", "PMREV", &[1.9, 0.5, 6.0, 16.0, lpf(12000.0), hpf(100.0)]),
    ("Chorus->Reverb", "PMCHREV", &[0.8, 40.0, 1.3, 2.4, 0.7, 7.0, 30.0, lpf(6300.0), THRU_HPF, 24.0]),
    ("Flange->Reverb", "PMFLREV", &[1.4, 22.0, 45.0, 13.0, 2.4, 8.0, 26.0, lpf(4500.0), hpf(45.0), 30.0]),
    ("Delay L-C-R", "PMDLCR", &[250.0, 500.0, 125.0, 70.0, 500.0, 40.0, 0.8]),
    ("MonoDly->Chorus", "PMMDCHO", &[400.0, 32.0, 0.6, 0.4, 10.0, 0.1]),
    ("Chorus->Dly LCR", "PMCHDLY", &[0.8, 24.0, 5.9, 26.4, 33.2, 13.1, 60.0, 40.5, -48.0, 0.1]),
    ("Delay->Chorus", "PMDLCHO", &[250.0, 500.0, 500.0, 33.0, 0.7, 1.2, 25.0, 10.0]),
    ("Karaoke Echo 1", "PMECHO", &[220.0, 40.0, 223.0, 40.0, 0.4]),
    ("Karaoke Echo 2", "PMECHO", &[220.0, 44.0, 180.0, -55.0, 0.2]),
    ("St.Pitch Change", "PMPITCH", &[0.0, 10.0, -10.0, 100.0, 100.0, -100.0, 100.0, 28.0, -28.0, 25.0]),
    ("Vocal Doubler", "PMPITCH", &[0.0, 8.0, -8.0, 100.0, 100.0, -6.0, 6.0, 20.0, -20.0, 50.0]),
    ("Funny Pitch", "PMPITCH", &[-2.0, 8.0, -8.0, 100.0, 100.0, -90.0, 90.0, 90.0, -90.0, 4.0]),
    ("Chorus", "PMCHO", &[0.6, 49.0, 35.0, 12.1]),
    ("Broad Chorus", "PMCHO", &[0.5, 64.0, 90.0, 38.0]),
    ("Symphonic", "PMSYMP", &[0.6, 8.0, 2.4]),
    ("Flange", "PMFLG", &[1.2, 25.0, 45.0, 14.6]),
    ("Super Flange", "PMFLG", &[0.2, 80.0, 83.0, 0.4]),
    ("Phasing", "PMPHAS", &[0.5, 58.0, 47.0, 24.0]),
    ("Tremolo", "PMTREM", &[2.8, 86.0, 0.0]),
    ("Auto Pan", "PMAPAN", &[0.8, 98.0, 15.0, 0.0]),
];

/// An EQ band: gain (dB), frequency (Hz), Q (0-8 the octave values, 9 SHELF).
pub type EqBand = (f32, f32, u8);

/// An EQ program: LOW, MID, HIGH.
#[derive(Clone, Debug, PartialEq)]
pub struct EqProgram {
    pub name: String,
    pub bands: [EqBand; 3],
}

const SH: u8 = 9;
const O16: u8 = 0;
const O14: u8 = 1;
const O13: u8 = 2;
const O34: u8 = 4;
const O1: u8 = 5;
const O32: u8 = 6;
const O2: u8 = 7;
const O3: u8 = 8;

/// The preset EQ programs 1-30 (pp.23-24): LOW, MID, HIGH as (G, F, Q). The
/// guide prints each column's 15 triples of a page in program order; read
/// so, every program matches its description (NOTCH 4kHz: HIGH -10 dB at
/// 4.0 kHz, 1/6 oct; TELEPHONE VOICE: LOW and HIGH cut).
pub const EQ_PRESETS: [(&str, [EqBand; 3]); 30] = [
    ("RESET", [(0.0, 80.0, SH), (0.0, 2000.0, O32), (0.0, 10000.0, SH)]),
    ("LOUDNESS", [(5.0, 80.0, SH), (3.0, 200.0, O2), (0.0, 10000.0, SH)]),
    ("EQ DISCO", [(5.0, 90.0, O3), (-4.0, 700.0, O16), (4.0, 12000.0, O3)]),
    ("EQ POPS", [(0.0, 100.0, SH), (2.0, 2800.0, O3), (2.0, 10000.0, O3)]),
    ("EQ ROCK", [(4.0, 80.0, O1), (2.0, 2500.0, O1), (4.0, 12000.0, SH)]),
    ("EQ LIVE", [(3.0, 125.0, SH), (1.0, 700.0, O2), (2.0, 12000.0, SH)]),
    ("TIGHT DRUMS", [(5.0, 80.0, O34), (-5.0, 400.0, O13), (4.0, 2500.0, O32)]),
    ("LOUD DRUMS", [(5.0, 110.0, SH), (2.0, 2000.0, O32), (1.0, 12000.0, O3)]),
    ("KICK DRUM", [(8.0, 80.0, O34), (-7.0, 400.0, O16), (5.0, 2500.0, O13)]),
    ("SNARE 1", [(2.0, 200.0, SH), (1.0, 1400.0, O32), (3.0, 5600.0, O3)]),
    ("SNARE 2", [(5.0, 200.0, O34), (-6.0, 700.0, O16), (5.0, 3200.0, O3)]),
    ("CYMBALS", [(2.0, 200.0, SH), (0.0, 2000.0, O32), (4.0, 12000.0, SH)]),
    ("HI-HATS", [(-3.0, 80.0, SH), (-2.0, 250.0, O16), (3.0, 8000.0, SH)]),
    ("TOMS", [(4.0, 200.0, SH), (-1.0, 900.0, O32), (3.0, 4000.0, O3)]),
    ("E.BASS", [(5.0, 90.0, O1), (-1.0, 450.0, O14), (4.0, 2500.0, O1)]),
    ("WOOD BASS", [(2.0, 80.0, O3), (1.0, 315.0, O32), (2.0, 2200.0, SH)]),
    ("ACOUSTIC GUITAR", [(2.0, 180.0, O2), (3.0, 4000.0, O2), (4.0, 7000.0, SH)]),
    ("TRUMPET", [(1.0, 360.0, O3), (1.0, 1400.0, O1), (4.0, 5600.0, O2)]),
    ("SAXOPHONE", [(3.0, 315.0, O2), (1.0, 900.0, O14), (3.0, 3600.0, SH)]),
    ("PIANO", [(2.0, 140.0, SH), (1.0, 2000.0, O2), (1.0, 5000.0, O3)]),
    ("MALE VOCAL", [(3.0, 280.0, O3), (1.0, 1800.0, O34), (2.0, 5000.0, SH)]),
    ("FEMALE VOCAL", [(-1.0, 220.0, SH), (1.0, 2000.0, O3), (2.0, 7000.0, O3)]),
    ("CHORUS", [(1.0, 280.0, O3), (2.0, 1400.0, O32), (5.0, 5600.0, SH)]),
    ("MALE ANNOUNCER", [(-3.0, 100.0, O1), (2.0, 4500.0, O3), (-4.0, 7000.0, SH)]),
    ("FEMALE ANNOUNCER", [(-3.0, 200.0, SH), (3.0, 2000.0, O1), (-1.0, 8000.0, SH)]),
    ("TELEPHONE VOICE", [(-15.0, 500.0, SH), (12.0, 1100.0, O2), (-10.0, 9000.0, O34)]),
    ("NOTCH 4kHz", [(0.0, 80.0, SH), (0.0, 2000.0, O32), (-10.0, 4000.0, O16)]),
    ("HUM REDUCE 50Hz", [(-9.0, 50.0, O16), (-10.0, 160.0, O16), (0.0, 10000.0, SH)]),
    ("HUM REDUCE 60Hz", [(-9.0, 60.0, O16), (-10.0, 180.0, O16), (0.0, 10000.0, SH)]),
    ("W. NOISE REDUCE", [(0.0, 80.0, SH), (0.0, 2000.0, O32), (-13.0, 16000.0, SH)]),
];

/// The libraries the console keeps (its backup memory, not a scene's): the
/// user programs of the effects (31-40), the dynamics (11-20) and the EQ
/// (31-50). A user program starts as "Initial Data": the first preset.
#[derive(Clone, Debug, PartialEq)]
pub struct Library {
    pub fx_user: Vec<FxProgram>,
    pub dyn_user: Vec<DynProgram>,
    pub eq_user: Vec<EqProgram>,
}

impl Default for Library {
    fn default() -> Self {
        let named = |p: &FxProgram| FxProgram { name: "Initial Data".into(), ..p.clone() };
        let fx = named(&Self::fx_preset(0));
        let (_, model, values) = DYN_PRESETS[0];
        let eq = EqProgram { name: "Initial Data".into(), bands: EQ_PRESETS[0].1 };
        Library {
            fx_user: vec![fx; 10],
            dyn_user: vec![DynProgram { name: "Initial Data".into(), model, values }; 10],
            eq_user: vec![eq; 20],
        }
    }
}

impl Library {
    fn fx_preset(n: usize) -> FxProgram {
        let (name, token, values) = FX_PRESETS[n];
        FxProgram { name: name.into(), token, values: values.to_vec() }
    }

    /// Effects program `n` (0-based: 0-29 the presets, 30-39 the user programs).
    pub fn fx_program(&self, n: usize) -> Option<FxProgram> {
        match n {
            0..=29 => Some(Self::fx_preset(n)),
            _ => self.fx_user.get(n - 30).cloned(),
        }
    }

    /// Dynamics program `n` (0-based: 0-9 the presets, 10-19 the user programs).
    pub fn program(&self, n: usize) -> Option<DynProgram> {
        match n {
            0..=9 => {
                let (name, model, values) = DYN_PRESETS[n];
                Some(DynProgram { name: name.into(), model, values })
            }
            _ => self.dyn_user.get(n - 10).cloned(),
        }
    }

    /// EQ program `n` (0-based: 0-29 the presets, 30-49 the user programs).
    pub fn eq_program(&self, n: usize) -> Option<EqProgram> {
        match n {
            0..=29 => Some(EqProgram { name: EQ_PRESETS[n].0.into(), bands: EQ_PRESETS[n].1 }),
            _ => self.eq_user.get(n - 30).cloned(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_dsp::promix::DYN_MODELS;

    #[test]
    fn the_effects_presets_are_their_types_values() {
        use livedaw_dsp::promix::FX_MODELS;
        let lib = Library::default();
        for n in 0..40 {
            let p = lib.fx_program(n).unwrap();
            let spec = FX_MODELS.iter().find(|m| m.token == p.token).unwrap();
            assert_eq!(p.values.len(), spec.params.len(), "{}", p.name);
            for (v, s) in p.values.iter().zip(spec.params) {
                assert!((s.lo()..=s.hi()).contains(v), "{} {}: {v}", p.name, s.id);
            }
        }
        assert_eq!(lib.fx_program(40), None);
        // Each type's first program is its defaults (the type's model).
        for (name, token, values) in FX_PRESETS {
            let spec = FX_MODELS.iter().find(|m| m.token == token).unwrap();
            if FX_PRESETS.iter().find(|p| p.1 == token).unwrap().0 == name {
                let defaults: Vec<f32> = spec.params.iter().map(|p| p.default).collect();
                assert_eq!(defaults, values, "{name}");
            }
        }
    }

    #[test]
    fn the_eq_presets_are_on_the_eqs_steps() {
        use livedaw_dsp::promix::{HIGH_FREQS, LOW_FREQS, MID_FREQS};
        let lib = Library::default();
        for n in 0..50 {
            let p = lib.eq_program(n).unwrap();
            for (band, (g, f, q)) in p.bands.iter().enumerate() {
                assert!((-15.0..=15.0).contains(g) && g.fract() == 0.0, "{} G", p.name);
                assert!([LOW_FREQS, MID_FREQS, HIGH_FREQS][band].contains(f), "{} {band}: {f} Hz", p.name);
                assert!(*q <= if band == 1 { 8 } else { 9 }, "{} Q", p.name);
            }
        }
        assert_eq!(lib.eq_program(26).unwrap().bands[2], (-10.0, 4000.0, 0), "NOTCH 4kHz");
        assert_eq!(lib.eq_program(50), None);
    }

    #[test]
    fn the_presets_sit_on_their_types_steps() {
        let lib = Library::default();
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
