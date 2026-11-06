//! The 02R's libraries (MAN pp.52-60, 73-81, 95-111): 128 EQ, dynamics and
//! effects programs each, the presets first (EQ 1-40, of which 33-40 may be
//! overwritten; dynamics and effects 1-40), the rest user programs. A
//! program is a model of `livedaw_dsp::y02r` and its values, as the guide's
//! preset tables list them (the dynamics' times at 44.1 kHz as printed, on
//! the nearest of Sidecar's 48 kHz steps).
//!
//! Two presets are printed with the same values (EQ 24 Brass Section and 29
//! Chorus & Harmony): kept as printed.

use livedaw_dsp::model::{ModelSpec, ParamKind};
use livedaw_dsp::y02r::{fx::FX_MODELS, DYN_MODELS, EQ};

/// The programs of each library, and the first one a user may store into
/// (0-based): EQ 33, dynamics and effects 41 (MAN p.211).
pub const PROGRAMS: usize = 128;
pub const EQ_FIRST_USER: usize = 32;
pub const DYN_FIRST_USER: usize = 40;
pub const FX_FIRST_USER: usize = 40;

/// A program: its title (16 characters at most), its model's token and its
/// values in the model's order and units (a `Values` parameter's index).
#[derive(Clone, Debug, PartialEq)]
pub struct Program {
    pub name: String,
    pub token: &'static str,
    pub values: Vec<f32>,
}

impl Program {
    /// A program of `spec` on its defaults with `set` (id, value) over them;
    /// a `Values` or `Choice` parameter takes the nearest of its values.
    fn of(spec: &'static ModelSpec, name: &str, set: &[(&str, f32)]) -> Program {
        let mut values: Vec<f32> = spec.params.iter().map(|p| p.default).collect();
        for &(id, v) in set {
            let Some(i) = spec.param_index(id) else { panic!("{} has no {id}", spec.token) };
            values[i] = match spec.params[i].kind {
                ParamKind::Values(table) => nearest(table, v) as f32,
                _ => v,
            };
        }
        Program { name: name.chars().take(16).collect(), token: spec.token, values }
    }
}

fn nearest(table: &[f32], v: f32) -> usize {
    table.iter().enumerate().min_by(|a, b| (a.1 - v).abs().total_cmp(&(b.1 - v).abs())).map_or(0, |(i, _)| i)
}

// ---- EQ ------------------------------------------------------------------------------

/// A band of an EQ preset.
#[derive(Clone, Copy)]
enum Band {
    /// Peaking: Q, frequency (Hz), gain (dB).
    Pk(f32, f32, f32),
    /// A shelf (LOW's low shelf, HIGH's high shelf).
    Sh(f32, f32),
    /// LPF (HIGH) at a frequency, on.
    Lpf(f32),
}
use Band::{Lpf, Pk, Sh};

/// MAN pp.56-60.
const EQ_PRESETS: [(&str, [Band; 4]); 40] = [
    ("Bass Drum 1", [Pk(1.2, 99.0, 3.5), Pk(10.0, 265.0, -3.5), Pk(0.9, 1059.0, 0.0), Sh(5339.0, 4.0)]),
    ("Bass Drum 2", [Pk(1.4, 79.0, 8.0), Pk(4.5, 397.0, -7.0), Pk(2.2, 2520.0, 6.0), Lpf(12699.0)]),
    ("Snare Drum 1", [Pk(1.2, 132.0, -0.5), Pk(4.5, 1000.0, 0.0), Pk(0.11, 3175.0, 3.0), Sh(5040.0, 4.5)]),
    ("Snare Drum 2", [Sh(177.0, 1.5), Pk(10.0, 334.0, -8.5), Pk(0.7, 2378.0, 2.5), Pk(0.1, 4000.0, 4.0)]),
    ("Tom-tom 1", [Pk(1.4, 210.0, 2.0), Pk(10.0, 667.0, -7.5), Pk(1.2, 4490.0, 2.0), Pk(0.28, 6350.0, 1.0)]),
    ("Cymbal", [Sh(105.0, -2.0), Pk(8.0, 420.0, 0.0), Pk(0.9, 1059.0, 0.0), Sh(13454.0, 3.0)]),
    ("High Hat", [Sh(94.0, -4.0), Pk(0.5, 420.0, -2.5), Pk(1.0, 2828.0, 1.0), Sh(7551.0, 0.5)]),
    ("Percussion", [Sh(99.0, -4.5), Pk(4.5, 397.0, 0.0), Pk(0.56, 2828.0, 2.0), Sh(16951.0, 0.0)]),
    ("E.Bass 1", [Sh(35.0, -7.5), Pk(5.0, 111.0, 4.5), Pk(4.5, 2000.0, 2.5), Sh(4000.0, 0.0)]),
    ("E.Bass 2", [Pk(0.1, 111.0, 3.0), Pk(5.0, 111.0, 0.0), Pk(6.3, 2245.0, 2.5), Sh(4000.0, 0.5)]),
    ("Syn.Bass 1", [Pk(0.1, 83.0, 3.5), Pk(8.0, 944.0, 8.5), Pk(4.5, 4000.0, 0.0), Sh(12699.0, 0.0)]),
    ("Syn.Bass 2", [Pk(1.6, 125.0, 2.5), Pk(8.0, 177.0, 0.0), Pk(2.2, 1122.0, 1.5), Sh(12699.0, 0.0)]),
    ("Piano 1", [Sh(94.0, -6.0), Pk(8.0, 944.0, 0.0), Pk(0.9, 3175.0, 2.0), Sh(7551.0, 4.0)]),
    ("Piano 2", [Pk(5.6, 223.0, 3.5), Pk(10.0, 595.0, -8.5), Pk(0.7, 3175.0, 1.5), Sh(5339.0, 3.0)]),
    ("E.G.Clean", [Pk(0.18, 265.0, 2.0), Pk(10.0, 397.0, -5.5), Pk(6.3, 1335.0, 0.5), Sh(4490.0, 2.5)]),
    ("E.G.Crunch 1", [Pk(8.0, 140.0, 4.5), Pk(4.5, 1000.0, 0.0), Pk(0.63, 1888.0, 4.0), Pk(9.0, 5657.0, 2.0)]),
    ("E.G.Crunch 2", [Pk(8.0, 125.0, 2.5), Pk(0.4, 445.0, 1.5), Pk(0.16, 3364.0, 2.5), Sh(19027.0, 0.0)]),
    ("E.G.Distortion 1", [Sh(354.0, 5.0), Pk(9.0, 944.0, 0.0), Pk(10.0, 3364.0, 3.5), Sh(12699.0, 0.0)]),
    ("E.G.Distortion 2", [Sh(315.0, 6.0), Pk(10.0, 1059.0, -8.5), Pk(4.0, 4238.0, 4.5), Sh(12699.0, 4.0)]),
    ("A.G.Stroke 1", [Pk(0.9, 105.0, -2.0), Pk(4.5, 1000.0, 0.0), Pk(3.5, 1888.0, 1.0), Sh(5339.0, 4.0)]),
    ("A.G.Stroke 2", [Sh(297.0, -3.5), Pk(9.0, 749.0, -2.0), Pk(4.5, 2000.0, 0.0), Sh(3564.0, 2.0)]),
    ("A.G.Arpeggio 1", [Sh(223.0, -0.5), Pk(4.5, 1000.0, 0.0), Pk(4.5, 4000.0, 0.0), Pk(0.12, 6727.0, 2.0)]),
    ("A.G.Arpeggio 2", [Sh(177.0, 0.0), Pk(7.0, 354.0, -5.5), Pk(4.5, 4000.0, 0.0), Sh(4238.0, 4.0)]),
    ("Brass Section", [Pk(2.8, 88.0, -2.0), Pk(2.0, 841.0, 1.0), Pk(0.7, 2119.0, 1.5), Pk(7.0, 4490.0, 3.0)]),
    ("Male Vocal 1", [Pk(0.11, 187.0, -0.5), Pk(4.5, 1000.0, 0.0), Pk(0.56, 2000.0, 2.0), Pk(0.11, 6727.0, 3.5)]),
    ("Male Vocal 2", [Pk(0.11, 167.0, 2.0), Pk(10.0, 236.0, -5.0), Pk(5.6, 2670.0, 2.5), Sh(6727.0, 4.0)]),
    ("Female Vocal 1", [Pk(0.18, 118.0, -1.0), Pk(0.45, 397.0, 1.0), Pk(0.56, 2670.0, 1.5), Pk(0.14, 5993.0, 2.0)]),
    ("Female Vocal 2", [Sh(111.0, -7.0), Pk(0.16, 334.0, 1.5), Pk(0.2, 2000.0, 1.5), Sh(6727.0, 2.5)]),
    ("Chorus & Harmony", [Pk(2.8, 88.0, -2.0), Pk(2.0, 841.0, 1.0), Pk(0.7, 2119.0, 1.5), Pk(7.0, 4490.0, 3.0)]),
    ("Total EQ 1", [Pk(7.0, 94.0, -0.5), Pk(2.2, 944.0, 0.0), Pk(5.6, 2119.0, 3.0), Sh(16000.0, 6.5)]),
    ("Total EQ 2", [Pk(7.0, 94.0, 4.0), Pk(2.8, 749.0, 1.5), Pk(5.6, 1782.0, 2.0), Sh(17959.0, 6.0)]),
    ("Total EQ 3", [Sh(66.0, 1.5), Pk(0.28, 841.0, 0.5), Pk(0.7, 1888.0, 2.0), Sh(15102.0, 4.0)]),
    ("Bass Drum 3", [Pk(2.0, 118.0, 3.5), Pk(10.0, 315.0, -10.0), Pk(0.4, 4238.0, 3.5), Pk(0.4, 20159.0, 0.0)]),
    ("Snare Drum 3", [Sh(223.0, 0.0), Pk(4.5, 561.0, 2.0), Pk(2.8, 4238.0, 3.5), Pk(0.1, 4000.0, 0.0)]),
    ("Tom-tom 2", [Sh(88.0, -9.0), Pk(4.5, 210.0, 1.5), Pk(1.2, 5339.0, 2.0), Sh(16951.0, 0.0)]),
    ("Piano 3", [Pk(8.0, 99.0, 4.5), Pk(10.0, 472.0, -13.0), Pk(9.0, 2378.0, 4.5), Sh(10079.0, 2.5)]),
    ("Piano Low", [Pk(10.0, 187.0, -5.5), Pk(6.3, 397.0, 1.5), Pk(2.2, 6727.0, 6.0), Sh(12699.0, 0.0)]),
    ("Piano High", [Pk(10.0, 187.0, -5.5), Pk(6.3, 397.0, 1.5), Pk(2.2, 6727.0, 5.0), Pk(0.1, 5657.0, 3.0)]),
    ("Fine-EQ(Cass)", [Sh(74.0, -1.5), Pk(4.5, 1000.0, 0.0), Pk(1.8, 4000.0, 1.0), Sh(12699.0, 3.0)]),
    ("Narrator", [Pk(4.0, 105.0, -4.0), Pk(7.0, 707.0, -1.0), Pk(0.63, 2520.0, 2.0), Sh(10079.0, 0.0)]),
];

fn eq_program(name: &str, bands: &[Band; 4]) -> Program {
    use livedaw_dsp::y02r::{FILTER, Q_VALUES, SHELF};
    let ids = [("lf", "lg", "lq"), ("lmf", "lmg", "lmq"), ("hmf", "hmg", "hmq"), ("hf", "hg", "hq")];
    let mut set = Vec::new();
    for (band, (f_id, g_id, q_id)) in bands.iter().zip(ids) {
        let (f, g, q) = match *band {
            Pk(q, f, g) => (f, g, nearest(&Q_VALUES, q) as f32),
            Sh(f, g) => (f, g, SHELF),
            Lpf(f) => (f, 0.0, FILTER),
        };
        set.extend([(f_id, f), (g_id, g), (q_id, q)]);
    }
    Program::of(&EQ[0], name, &set)
}

// ---- dynamics --------------------------------------------------------------------------

/// A dynamics preset (MAN pp.76-81): its type's token and values in order:
/// CMP / EXP threshold, ratio, attack, out gain, knee (0 hard), release
/// (ms); CPH / CPS threshold, ratio, attack, out gain, width, release; GAT /
/// DUK threshold, range, attack, hold (ms), decay (ms).
const DYN_PRESETS: [(&str, &str, [f32; 6]); 40] = [
    ("A.Dr.BD", "Y2CMP", [-24.0, 3.0, 9.0, 5.5, 2.0, 58.0]),
    ("A.Dr.BD", "Y2EXP", [-23.0, 1.7, 1.0, 3.5, 2.0, 70.0]),
    ("A.Dr.BD", "Y2GAT", [-11.0, -53.0, 0.0, 1.93, 400.0, 0.0]),
    ("A.Dr.BD", "Y2CPH", [-11.0, 3.5, 1.0, -1.5, 7.0, 192.0]),
    ("A.Dr.SN", "Y2CMP", [-17.0, 2.5, 8.0, 3.5, 2.0, 12.0]),
    ("A.Dr.SN", "Y2EXP", [-23.0, 2.0, 0.0, 0.5, 2.0, 151.0]),
    ("A.Dr.SN", "Y2GAT", [-8.0, -23.0, 1.0, 0.63, 238.0, 0.0]),
    ("A.Dr.SN", "Y2CPS", [-8.0, 1.7, 11.0, 0.0, 10.0, 128.0]),
    ("A.Dr.Tom", "Y2EXP", [-20.0, 2.0, 2.0, 5.0, 2.0, 749.0]),
    ("A.Dr.OverTop", "Y2CPS", [-24.0, 2.0, 38.0, -3.5, 54.0, 842.0]),
    ("E.B.finger", "Y2CMP", [-12.0, 2.0, 15.0, 4.5, 2.0, 470.0]),
    ("E.B.slap", "Y2CMP", [-12.0, 1.7, 6.0, 4.0, 0.0, 133.0]),
    ("Syn.Bass", "Y2CMP", [-10.0, 3.5, 9.0, 3.0, 0.0, 250.0]),
    ("Piano1", "Y2CMP", [-9.0, 2.5, 17.0, 1.0, 0.0, 238.0]),
    ("Piano2", "Y2CMP", [-18.0, 3.5, 7.0, 6.0, 2.0, 174.0]),
    ("E.Guitar", "Y2CMP", [-8.0, 3.5, 7.0, 2.5, 4.0, 261.0]),
    ("A.Guitar", "Y2CMP", [-10.0, 2.5, 5.0, 1.5, 2.0, 238.0]),
    ("Strings1", "Y2CMP", [-11.0, 2.0, 33.0, 1.5, 2.0, 749.0]),
    ("Strings2", "Y2CMP", [-12.0, 1.5, 93.0, 1.5, 4.0, 1350.0]),
    ("Strings3", "Y2CMP", [-17.0, 1.5, 76.0, 2.5, 2.0, 186.0]),
    ("BrassSection", "Y2CMP", [-18.0, 1.7, 18.0, 4.0, 1.0, 226.0]),
    ("Syn.Pad", "Y2CMP", [-13.0, 2.0, 58.0, 2.0, 1.0, 238.0]),
    ("SamplingPerc", "Y2CPS", [-18.0, 1.7, 8.0, -2.5, 18.0, 238.0]),
    ("Sampling BD", "Y2CMP", [-14.0, 2.0, 2.0, 3.5, 4.0, 35.0]),
    ("Sampling SN", "Y2CMP", [-18.0, 4.0, 8.0, 8.0, 0.0, 354.0]),
    ("Hip Comp", "Y2CPS", [-23.0, 20.0, 15.0, 0.0, 15.0, 163.0]),
    ("Solo Vocal1", "Y2CMP", [-20.0, 2.5, 31.0, 2.0, 1.0, 342.0]),
    ("Solo Vocal2", "Y2CMP", [-8.0, 2.5, 26.0, 1.5, 3.0, 331.0]),
    ("Chorus", "Y2CMP", [-9.0, 1.7, 39.0, 2.5, 2.0, 226.0]),
    ("Compander(H)", "Y2CPH", [-10.0, 3.5, 1.0, 0.0, 6.0, 250.0]),
    ("Compander(S)", "Y2CPS", [-8.0, 4.0, 25.0, 0.0, 24.0, 180.0]),
    ("Click Erase", "Y2EXP", [-33.0, 2.0, 1.0, 2.0, 2.0, 284.0]),
    ("Announcer", "Y2CPH", [-14.0, 2.5, 1.0, -2.5, 18.0, 180.0]),
    ("Easy Gate", "Y2GAT", [-26.0, -56.0, 0.0, 2.56, 331.0, 0.0]),
    ("BGM Ducking", "Y2DUK", [-19.0, -22.0, 93.0, 1200.0, 6320.0, 0.0]),
    ("Limiter1", "Y2CPS", [-8.0, 4.0, 25.0, 0.0, 24.0, 180.0]),
    ("Limiter2", "Y2CMP", [0.0, 1000.0, 0.0, 0.0, 0.0, 319.0]),
    ("Total Comp1", "Y2CMP", [-8.0, 2.5, 60.0, 0.0, 2.0, 1120.0]),
    ("Total Comp2", "Y2CMP", [-18.0, 3.5, 94.0, 2.5, 0.0, 447.0]),
    ("Total Comp3", "Y2CMP", [-16.0, 6.0, 11.0, 6.0, 1.0, 180.0]),
];

fn dyn_program(name: &str, token: &str, v: &[f32; 6]) -> Program {
    let spec = DYN_MODELS.iter().find(|m| m.token == token).expect("a 02R dynamics type");
    let set: Vec<(&str, f32)> = spec.params.iter().zip(v).map(|(p, &x)| (p.id, x)).collect();
    Program::of(spec, name, &set)
}

// ---- effects -----------------------------------------------------------------------------

/// The effects presets (MAN pp.98-111): their type's token and the values
/// that differ from the type's defaults (its first preset's). THRU is 20 kHz
/// (LPF) or 20 Hz (HPF).
/// An effects preset: its name, its type's token, and (id, value) pairs.
type FxPreset = (&'static str, &'static str, &'static [(&'static str, f32)]);

const FX_PRESETS: [FxPreset; 40] = [
    ("REVERB HALL1", "Y2REV", &[]),
    ("REVERB HALL2", "Y2REV", &[("rt", 3.2), ("hr", 0.7), ("dif", 8.0), ("idly", 38.0), ("lpf", 6300.0), ("rdly", 17.0)]),
    ("REVERB ROOM1", "Y2REV", &[("rt", 1.4), ("dif", 7.0), ("idly", 5.0), ("lpf", 20000.0), ("hpf", 280.0), ("rdly", 1.0)]),
    ("REVERB ROOM2", "Y2REV", &[("rt", 1.8), ("hr", 0.6), ("idly", 17.0), ("lpf", 9000.0), ("hpf", 80.0), ("rdly", 4.0)]),
    (
        "REVERB STAGE",
        "Y2REV",
        &[("rt", 3.4), ("hr", 0.9), ("dif", 8.0), ("idly", 45.0), ("lpf", 20000.0), ("hpf", 70.0), ("rdly", 9.0)],
    ),
    ("REVERB PLATE", "Y2REV", &[("rt", 2.4), ("hr", 0.7), ("dif", 8.0), ("idly", 16.0), ("lpf", 8000.0), ("rdly", 2.0)]),
    (
        "REV AMBIENCE1",
        "Y2REV",
        &[("rt", 1.2), ("hr", 1.0), ("dif", 8.0), ("idly", 19.0), ("lpf", 9000.0), ("hpf", 45.0), ("rdly", 4.0)],
    ),
    (
        "REV AMBIENCE2",
        "Y2REV",
        &[("rt", 0.8), ("hr", 0.6), ("dif", 8.0), ("idly", 0.1), ("lpf", 20000.0), ("hpf", 56.0), ("rdly", 0.1)],
    ),
    ("REV LIVE ROOM1", "Y2REV", &[("rt", 2.4), ("dif", 7.0), ("idly", 0.1), ("rdly", 0.1)]),
    ("REV LIVE ROOM2", "Y2REV", &[("rt", 2.2), ("hr", 0.5), ("idly", 12.0), ("lpf", 4000.0), ("rdly", 3.0)]),
    (
        "REVERB VOCAL1",
        "Y2REV",
        &[
            ("rt", 1.8),
            ("hr", 0.4),
            ("dif", 3.0),
            ("idly", 19.0),
            ("lpf", 6300.0),
            ("hpf", 110.0),
            ("rdly", 10.0),
            ("rbal", 100.0),
        ],
    ),
    (
        "REVERB VOCAL2",
        "Y2REV",
        &[("hr", 0.3), ("idly", 35.0), ("dens", 2.0), ("lpf", 6300.0), ("hpf", 80.0), ("rdly", 30.0), ("rbal", 100.0)],
    ),
    (
        "REV SN.ROOM",
        "Y2REV",
        &[
            ("rt", 1.3),
            ("hr", 0.9),
            ("dif", 8.0),
            ("idly", 26.0),
            ("dens", 1.0),
            ("lpf", 10000.0),
            ("hpf", 180.0),
            ("rdly", 4.0),
            ("rbal", 90.0),
        ],
    ),
    (
        "REV SN.PLATE",
        "Y2REV",
        &[
            ("rt", 1.7),
            ("hr", 0.9),
            ("dif", 2.0),
            ("idly", 20.0),
            ("dens", 1.0),
            ("lpf", 11000.0),
            ("hpf", 140.0),
            ("rdly", 6.0),
            ("rbal", 90.0),
        ],
    ),
    ("CHORUS->REVERB", "Y2CHREV", &[]),
    ("FLANGE->REVERB1", "Y2FLREV", &[]),
    (
        "FLANGE->REVERB2",
        "Y2FLREV",
        &[("mf", 3.3), ("md", 9.0), ("fbg", -63.0), ("mdl", 0.4), ("rt", 1.4), ("idly", 9.1), ("rdep", 56.0)],
    ),
    ("EARLY REF.1", "Y2ER", &[]),
    ("EARLY REF.2", "Y2ER", &[("type", 0.0), ("size", 2.0), ("live", 7.0), ("dif", 7.0), ("idly", 4.8), ("lpf", 11000.0)]),
    ("GATE REVERB", "Y2GATE", &[]),
    ("REVERSE GATE", "Y2GATE", &[("type", 0.0), ("size", 2.8), ("dif", 5.0)]),
    ("DELAY->EARLY REF", "Y2DLER", &[]),
    ("DELAY L-C-R 1", "Y2DLCR", &[]),
    ("DELAY L-C-R 2", "Y2DLCR", &[("dl", 250.0), ("dr", 500.0), ("dc", 1000.0), ("fbd", 1000.0), ("fbg", 17.0)]),
    ("CHORUS->DLY LCR", "Y2CHDLY", &[]),
    ("MONODLY->CHORUS", "Y2MDCHO", &[]),
    ("STEREO INI.DLY ECHO", "Y2STECHO", &[]),
    ("ECHO", "Y2ECHO", &[]),
    (
        "CROSS ECHO",
        "Y2ECHO",
        &[("dl", 200.0), ("fgl", 40.0), ("dr", 350.0), ("fgr", 40.0), ("hr", 0.4), ("l2r", 34.0), ("r2l", 34.0)],
    ),
    ("CROSS DELAY", "Y2ECHO", &[("l2r", 100.0), ("r2l", 100.0)]),
    ("CHORUS1", "Y2CHO", &[]),
    ("CHORUS2", "Y2CHO", &[("lsfg", 0.0), ("eqg", 0.0), ("hsfg", 0.0)]),
    (
        "BROAD CHORUS",
        "Y2CHO",
        &[
            ("mf", 0.5),
            ("am", 64.0),
            ("pm", 90.0),
            ("lsff", 180.0),
            ("lsfg", 2.0),
            ("mdl", 38.0),
            ("eqg", 0.0),
            ("hsff", 3200.0),
            ("hsfg", 2.0),
        ],
    ),
    ("FLANGE", "Y2FLG", &[]),
    ("SYMPHONIC", "Y2SYMP", &[]),
    (
        "TREMOLO",
        "Y2CHO",
        &[("mf", 3.1), ("am", 84.0), ("pm", 6.0), ("lsff", 250.0), ("lsfg", 2.0), ("eqf", 630.0), ("eqg", 1.0), ("hsff", 2800.0)],
    ),
    ("AUTO PAN", "Y2APAN", &[]),
    ("PHASING", "Y2PHAS", &[]),
    ("ST.PITCH CHANGE", "Y2PITCH", &[]),
    (
        "VOCAL DOUBLER",
        "Y2PITCH",
        &[("f1", 5.0), ("f2", -10.0), ("p1", -6.0), ("p2", 6.0), ("fb1", 10.0), ("fb2", -10.0), ("fbd", 60.0)],
    ),
];

fn fx_program(name: &str, token: &str, set: &[(&str, f32)]) -> Program {
    let spec = FX_MODELS.iter().find(|m| m.token == token).expect("a 02R effect type");
    Program::of(spec, name, set)
}

/// The channel library's programs (V2 MAN pp.48-50): 64, all a user's.
pub const CHANNEL_PROGRAMS: usize = 64;

/// A channel program: its title and an input strip's settings as the edit
/// buffer has them (`params::channel_bytes`: its 28 bytes, then its sends' 10).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ChannelProgram {
    pub name: String,
    pub bytes: Vec<u8>,
}

/// The libraries (`None`: an empty user program).
#[derive(Clone, Debug, PartialEq)]
pub struct Library {
    pub eq: Vec<Option<Program>>,
    pub dynamics: Vec<Option<Program>>,
    pub effects: Vec<Option<Program>>,
    pub channel: Vec<Option<ChannelProgram>>,
    /// The channel library's UNDO ("U"): the channel's settings before the
    /// last recall, or the program the last store or clear overwrote. Not kept.
    pub channel_undo: Option<ChannelProgram>,
}

impl Default for Library {
    fn default() -> Self {
        let fill = |presets: Vec<Program>| {
            let mut v: Vec<Option<Program>> = presets.into_iter().map(Some).collect();
            v.resize(PROGRAMS, None);
            v
        };
        Library {
            eq: fill(EQ_PRESETS.iter().map(|(n, b)| eq_program(n, b)).collect()),
            dynamics: fill(DYN_PRESETS.iter().map(|(n, t, v)| dyn_program(n, t, v)).collect()),
            effects: fill(FX_PRESETS.iter().map(|(n, t, s)| fx_program(n, t, s)).collect()),
            channel: vec![None; CHANNEL_PROGRAMS],
            channel_undo: None,
        }
    }
}

/// The libraries.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Eq,
    Dynamics,
    Effects,
}

impl Library {
    fn programs(&self, kind: Kind) -> &Vec<Option<Program>> {
        match kind {
            Kind::Eq => &self.eq,
            Kind::Dynamics => &self.dynamics,
            Kind::Effects => &self.effects,
        }
    }

    /// Program `n` (0-based), if it holds one.
    pub fn get(&self, kind: Kind, n: usize) -> Option<&Program> {
        self.programs(kind).get(n)?.as_ref()
    }

    /// The first program a user may store into (0-based).
    pub fn first_user(kind: Kind) -> usize {
        match kind {
            Kind::Eq => EQ_FIRST_USER,
            Kind::Dynamics => DYN_FIRST_USER,
            Kind::Effects => FX_FIRST_USER,
        }
    }

    /// Stores `program` as number `n`; the read-only presets refuse (false).
    pub fn store(&mut self, kind: Kind, n: usize, program: Program) -> bool {
        if n < Self::first_user(kind) || n >= PROGRAMS {
            return false;
        }
        let list = match kind {
            Kind::Eq => &mut self.eq,
            Kind::Dynamics => &mut self.dynamics,
            Kind::Effects => &mut self.effects,
        };
        list[n] = Some(Program { name: program.name.chars().take(16).collect(), ..program });
        true
    }

    /// Clears channel program `n` (0-63) into the UNDO (V2 MAN p.49).
    pub fn clear_channel(&mut self, n: usize) -> bool {
        let Some(old) = self.channel.get_mut(n).and_then(Option::take) else { return false };
        self.channel_undo = Some(old);
        true
    }

    /// The programs a user may store into, as text (one per line: `eq.N`,
    /// `dyn.N` or `fx.N`, the title, a tab, the model's token and the values),
    /// for the host to keep: the console's memory, not a scene's.
    pub fn user_text(&self) -> String {
        let mut out = String::new();
        for (kind, key) in [(Kind::Eq, "eq"), (Kind::Dynamics, "dyn"), (Kind::Effects, "fx")] {
            for n in Self::first_user(kind)..PROGRAMS {
                if let Some(p) = self.get(kind, n) {
                    let name = p.name.replace(['\t', '\n', '\r', '='], " ");
                    let values: Vec<String> = p.values.iter().map(|v| format!("{v}")).collect();
                    out.push_str(&format!("{key}.{}={name}\t{} {}\n", n + 1, p.token, values.join(" ")));
                }
            }
        }
        for (n, p) in self.channel.iter().enumerate() {
            if let Some(p) = p {
                let name = p.name.replace(['\t', '\n', '\r', '='], " ");
                let hex: String = p.bytes.iter().map(|b| format!("{b:02x}")).collect();
                out.push_str(&format!("ch.{}={name}\t{hex}\n", n + 1));
            }
        }
        out
    }

    /// Sets the user programs from `user_text` (lines it doesn't know: ignored).
    pub fn apply_text(&mut self, text: &str) {
        for line in text.lines() {
            let Some((key, rest)) = line.split_once('=') else { continue };
            let Some((kind, n)) = key.split_once('.') else { continue };
            let (Ok(n), Some((name, data))) = (n.trim().parse::<usize>(), rest.split_once('\t')) else { continue };
            if kind == "ch" {
                let data = data.trim();
                let bytes: Option<Vec<u8>> = (0..data.len() / 2)
                    .map(|i| data.get(i * 2..i * 2 + 2).and_then(|h| u8::from_str_radix(h, 16).ok()))
                    .collect();
                if let (Some(bytes), Some(slot)) = (bytes, self.channel.get_mut(n.wrapping_sub(1))) {
                    *slot = Some(ChannelProgram { name: name.trim().chars().take(16).collect(), bytes });
                }
                continue;
            }
            let (kind, models): (Kind, &'static [ModelSpec]) = match kind {
                "eq" => (Kind::Eq, EQ),
                "dyn" => (Kind::Dynamics, DYN_MODELS),
                "fx" => (Kind::Effects, &FX_MODELS),
                _ => continue,
            };
            let mut words = data.split_whitespace();
            let Some(spec) = words.next().and_then(|t| models.iter().find(|m| m.token == t)) else { continue };
            let values = words.filter_map(|x| x.parse().ok()).collect();
            self.store(kind, n.saturating_sub(1), Program { name: name.trim().into(), token: spec.token, values });
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_presets() {
        let lib = Library::default();
        for kind in [Kind::Eq, Kind::Dynamics, Kind::Effects] {
            assert_eq!(lib.programs(kind).len(), PROGRAMS);
            assert!((0..40).all(|n| lib.get(kind, n).is_some()) && lib.get(kind, 40).is_none(), "{kind:?}");
        }
        // EQ 2 Bass Drum 2: LOW 79 Hz Q 1.4 +8 dB, HIGH an LPF at 12.6 kHz.
        let bd2 = lib.get(Kind::Eq, 1).unwrap();
        let eq = &EQ[0];
        let v = |id: &str| bd2.values[eq.param_index(id).unwrap()];
        assert_eq!((v("lg"), livedaw_dsp::y02r::Q_VALUES[v("lq") as usize]), (8.0, 1.4));
        assert!((livedaw_dsp::y02r::FREQS[v("lf") as usize] - 79.0).abs() < 0.5);
        assert_eq!(v("hq"), livedaw_dsp::y02r::FILTER);
        // Dynamics 35 BGM Ducking, 37 Limiter2 (oo:1).
        let duck = lib.get(Kind::Dynamics, 34).unwrap();
        assert_eq!((duck.name.as_str(), duck.token, duck.values[1]), ("BGM Ducking", "Y2DUK", -22.0));
        let lim = lib.get(Kind::Dynamics, 36).unwrap();
        assert_eq!(livedaw_dsp::y02r::RATIOS[lim.values[1] as usize], 1000.0);
        // Effects 30 CROSS DELAY: ECHO's values, its sides crossed all the way.
        let cross = lib.get(Kind::Effects, 29).unwrap();
        let spec = FX_MODELS.iter().find(|m| m.token == "Y2ECHO").unwrap();
        assert_eq!(cross.values[spec.param_index("l2r").unwrap()], 100.0);
        assert_eq!(lib.get(Kind::Effects, 22).unwrap().name, "DELAY L-C-R 1");
    }

    #[test]
    fn user_programs_keep_as_text() {
        let mut lib = Library::default();
        let eq = Program { name: "My Kick".into(), ..lib.get(Kind::Eq, 0).unwrap().clone() };
        let fx = Program { name: "Long Hall".into(), ..lib.get(Kind::Effects, 2).unwrap().clone() };
        assert!(lib.store(Kind::Eq, 50, eq.clone()) && lib.store(Kind::Effects, 127, fx.clone()));
        let mut back = Library::default();
        back.apply_text(&lib.user_text());
        assert_eq!((back.get(Kind::Eq, 50), back.get(Kind::Effects, 127)), (Some(&eq), Some(&fx)));
        assert_eq!(back, lib);
    }

    #[test]
    fn channel_programs_keep_as_text() {
        let mut lib = Library::default();
        lib.channel[0] = Some(ChannelProgram { name: "Lead Vox".into(), bytes: vec![100, 1, 16, 0, 255] });
        lib.channel[63] = Some(ChannelProgram { name: "Kick".into(), bytes: vec![0; 38] });
        let mut back = Library::default();
        back.apply_text(&lib.user_text());
        assert_eq!(back.channel, lib.channel);
    }

    #[test]
    fn user_programs() {
        let mut lib = Library::default();
        let p = lib.get(Kind::Eq, 0).unwrap().clone();
        assert!(!lib.store(Kind::Eq, 31, p.clone()), "a preset");
        assert!(lib.store(Kind::Eq, 32, Program { name: "MY VERY LONG EQ NAME".into(), ..p }), "33-40 may be overwritten");
        assert_eq!(lib.get(Kind::Eq, 32).unwrap().name, "MY VERY LONG EQ ");
        let fx = lib.get(Kind::Effects, 0).unwrap().clone();
        assert!(!lib.store(Kind::Effects, 39, fx.clone()) && lib.store(Kind::Effects, 127, fx));
    }
}
