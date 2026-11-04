//! The ProMix 01's 531 mix parameters (User's Guide, the Control Change to
//! Parameter Assignment table, Add-5 to Add-15): each an internal value
//! 0..N-1, read from and written to the console. They are what its control
//! changes carry (`midi`), what Memory Control Change Out sends (p.87), and
//! an address for every control of its panel.
//!
//! Numbers as the guide's table: 0-21 levels, 22-43 ON, 44-67 pans and
//! balances, 68-135 sends, 136-139 send PRE / POST, 140-156 SEND3-4 pans,
//! 157-178 CUE, 179-195 phase, 196-395 EQ, 396-403 pair flags, 404-445 the
//! effects, 446-475 the dynamics processors, 476-514 configuration, 515-530
//! the faders' own values in Local OFF.

use crate::console::{Console, Origin, PmStrip};
use crate::state::{KeyIn, Patch, CHANNELS};
use livedaw_dsp::promix::DYN_MODELS;

/// How many parameters the console has.
pub const COUNT: u16 = 531;

/// What parameter `n` addresses.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Param {
    Level(PmStrip),
    On(PmStrip),
    /// A mono channel's pan, ST OUT's balance, SEND3-4's output balance.
    Pan(PmStrip),
    /// ST IN (0), RTN1 (1), RTN2 (2): side 0 L, 1 R.
    StereoPan(usize, usize),
    /// An input's send level to SEND1-4 (0-3).
    Send(PmStrip, usize),
    SendPre(usize),
    Send34Pan(PmStrip),
    Cue(PmStrip),
    Phase(PmStrip),
    EqOn(PmStrip),
    /// Band 0-2 (LOW, MID, HIGH), 0 F / 1 G / 2 Q.
    Eq(PmStrip, usize, usize),
    PairFlag(usize),
    /// Effect 1-2 (0-1): type (its program), parameters 1-10 as high and low bytes.
    FxType(usize),
    FxParam(usize, usize, bool),
    CompType(usize),
    CompPatch(usize),
    CompKey(usize),
    CompIn(usize),
    CompOn(usize),
    /// Processor, parameter 0-4.
    CompParam(usize, usize),
    Send34Stereo,
    OscOn,
    OscLevel,
    OscFreq,
    /// SEND3, SEND4, ST OUT.
    OscAssign(usize),
    CueMode,
    Group(usize),
    PanIndividual,
    GroupSelect,
    PairExecute(usize),
    /// SEND3 (0), SEND4 (1), ST OUT (2)... as the table orders them: 511 ST
    /// OUT, 512 SEND3, 513 SEND4.
    PatchPoint(usize),
    Send34Execute,
    LocalFader(usize),
}

/// The 22 strips of the levels and ON (CH1-16, ST IN, RTN1-2, SEND3-4, ST OUT).
fn mix_strip(i: u16) -> PmStrip {
    match i {
        0..=15 => PmStrip::Ch(i as usize),
        16 => PmStrip::StIn,
        17 | 18 => PmStrip::Rtn(i as usize - 17),
        19 | 20 => PmStrip::Send(i as usize - 17),
        _ => PmStrip::StOut,
    }
}

/// An input: CH1-16 (0-15), ST IN (16).
fn input(i: u16) -> PmStrip {
    if i < 16 {
        PmStrip::Ch(i as usize)
    } else {
        PmStrip::StIn
    }
}

/// What parameter `n` is.
pub fn param(n: u16) -> Option<Param> {
    use Param::*;
    Some(match n {
        0..=21 => Level(mix_strip(n)),
        22..=43 => On(mix_strip(n - 22)),
        44..=59 => Pan(PmStrip::Ch(n as usize - 44)),
        60..=65 => StereoPan((n as usize - 60) / 2, (n as usize - 60) % 2),
        66 => Pan(PmStrip::Send(2)),
        67 => Pan(PmStrip::StOut),
        68..=135 => {
            let k = n - 68;
            Send(input(k % 17), (k / 17) as usize)
        }
        136..=139 => SendPre(n as usize - 136),
        140..=156 => Send34Pan(input(n - 140)),
        157..=178 => Cue(mix_strip(n - 157)),
        179..=195 => Phase(input(n - 179)),
        196..=215 => EqOn(PmStrip::eq_strip(n as usize - 196)?),
        216..=395 => {
            // Nine blocks of 20 (LOW F, LOW G, LOW Q, MID F ... HIGH Q).
            let k = (n - 216) as usize;
            Eq(PmStrip::eq_strip(k % 20)?, k / 60, (k / 20) % 3)
        }
        396..=403 => PairFlag(n as usize - 396),
        404 | 405 => FxType(n as usize - 404),
        406..=445 => {
            let k = (n - 406) as usize;
            FxParam(k / 20, (k % 20) / 2, k % 2 == 0)
        }
        446..=448 => CompType(n as usize - 446),
        449..=451 => CompPatch(n as usize - 449),
        452..=454 => CompKey(n as usize - 452),
        455..=457 => CompIn(n as usize - 455),
        458..=460 => CompOn(n as usize - 458),
        461..=475 => CompParam((n as usize - 461) / 5, (n as usize - 461) % 5),
        476 => Send34Stereo,
        477 => OscOn,
        478 => OscLevel,
        479 => OscFreq,
        480..=482 => OscAssign(n as usize - 480),
        483 => CueMode,
        484..=500 => Group(n as usize - 484),
        501 => PanIndividual,
        502 => GroupSelect,
        503..=510 => PairExecute(n as usize - 503),
        511 => PatchPoint(2),
        512 | 513 => PatchPoint(n as usize - 512),
        514 => Send34Execute,
        515..=530 => LocalFader(n as usize - 515),
        _ => return None,
    })
}

/// Whether parameter `n` has a control change in the default map (CUE ON
/// and the CUE mode have none, Add-9 / Add-15).
pub fn has_cc(n: u16) -> bool {
    n < COUNT && !(157..=178).contains(&n) && n != 483
}

/// How many values parameter `n` has (N: its internal range is 0..N-1); a
/// dynamics parameter's depends on its processor's type, an effect's are
/// 14-bit bytes (128 each).
pub fn steps(c: &Console, n: u16) -> u16 {
    use Param::*;
    match param(n) {
        Some(Level(_) | Send(..) | OscLevel | LocalFader(_)) => 128,
        Some(Pan(_) | StereoPan(..) | Send34Pan(_)) => 33,
        Some(Eq(_, band, what)) => match (band, what) {
            (0, 0) => 31,
            (1, 0) => 56,
            (2, 0) => 26,
            (_, 1) => 31,
            (1, 2) => 9,
            _ => 10,
        },
        Some(FxType(_)) => 40,
        Some(FxParam(..)) => 128,
        Some(CompType(_) | CompPatch(_)) => 20,
        Some(CompKey(_)) => 21,
        Some(CompIn(_)) => 3,
        Some(CompParam(i, k)) => {
            let model = c.state.processors[i].model as usize;
            DYN_MODELS[model].params.get(k).map_or(1, |p| p.steps as u16)
        }
        Some(OscFreq | CueMode | GroupSelect | PairExecute(_)) => 4,
        Some(Group(_)) => 5,
        Some(_) => 2,
        None => 0,
    }
}

/// Parameter `n`'s value now (None: not a parameter, or one with no state:
/// the Execute parameters, Local OFF's faders, the effects until step 4).
pub fn get(c: &Console, n: u16) -> Option<u16> {
    use Param::*;
    let s = &c.state;
    Some(match param(n)? {
        Level(st) => c.level(st),
        On(st) => c.on(st) as u16,
        Pan(st) => c.pan(st),
        StereoPan(i, side) => c.stereo_pan(i, side),
        Send(st, bus) => c.send_level(st, bus),
        SendPre(bus) => !c.send_pre(bus) as u16,
        Send34Pan(st) => c.send34_pan(st),
        Cue(st) => c.cue(st) as u16,
        Phase(st) => c.phase(st) as u16,
        EqOn(st) => c.eq_on(st) as u16,
        Eq(st, band, what) => c.eq_value(st, band, what),
        PairFlag(p) => s.pairs[p] as u16,
        FxType(_) | FxParam(..) => return None,
        CompType(i) => s.processors[i].program as u16,
        CompPatch(i) => s.processors[i].patch.value(),
        CompKey(i) => s.processors[i].key_in.value(),
        CompIn(i) => s.processors[i].in_mode as u16,
        CompOn(i) => s.processors[i].on as u16,
        CompParam(i, k) => c.processor_param(i, k),
        Send34Stereo => s.send34_stereo as u16,
        OscOn => s.oscillator.on as u16,
        OscLevel => s.oscillator.level as u16,
        OscFreq => s.oscillator.freq as u16,
        OscAssign(k) => [s.oscillator.to_send3, s.oscillator.to_send4, s.oscillator.to_stereo][k] as u16,
        CueMode => s.cue_mode as u16,
        Group(m) => s.groups[m] as u16,
        PanIndividual => s.pan_individual as u16,
        GroupSelect => s.group_select as u16,
        PatchPoint(k) => !c.patch_point_pre(k) as u16,
        PairExecute(_) | Send34Execute | LocalFader(_) => return None,
    })
}

/// Sets parameter `n` to internal value `v` (clamped to its range). A pair
/// flag or the SEND3-4 configuration flag received over MIDI changes the
/// flag alone; their Execute parameters do what the panel does (Add-17).
pub fn set(c: &mut Console, n: u16, v: u16, origin: Origin) {
    use Param::*;
    let Some(p) = param(n) else { return };
    let v = v.min(steps(c, n).max(1) - 1);
    let on = v != 0;
    match p {
        Level(st) => c.set_level(st, v, origin),
        On(st) => c.set_on(st, on),
        Pan(st) => c.set_pan(st, v),
        StereoPan(i, side) => c.set_stereo_pan(i, side, v),
        Send(st, bus) => c.set_send_level(st, bus, v),
        SendPre(bus) => c.set_send_pre(bus, !on),
        Send34Pan(st) => c.set_send34_pan(st, v),
        Cue(st) => c.set_cue(st, on),
        Phase(st) => c.set_phase(st, on),
        EqOn(st) => c.set_eq_on(st, on),
        Eq(st, band, what) => c.set_eq_value(st, band, what, v),
        PairFlag(pair) => c.state.pairs[pair] = on,
        FxType(_) | FxParam(..) => {}
        CompType(i) => c.recall_dyn_program(i, v as usize),
        CompPatch(i) => {
            c.set_patch(i, Patch::of_value(v));
        }
        CompKey(i) => c.set_key_in(i, KeyIn::of_value(v)),
        CompIn(i) => c.set_in_mode(i, v),
        CompOn(i) => c.set_processor_on(i, on),
        CompParam(i, k) => c.set_processor_param(i, k, v),
        Send34Stereo => c.state.send34_stereo = on,
        OscOn => c.state.oscillator.on = on,
        OscLevel => c.state.oscillator.level = v as u8,
        OscFreq => c.state.oscillator.freq = v as u8,
        OscAssign(k) => {
            let o = &mut c.state.oscillator;
            *[&mut o.to_send3, &mut o.to_send4, &mut o.to_stereo][k] = on;
        }
        CueMode => c.state.cue_mode = v as u8,
        Group(m) => c.set_group(m, v),
        PanIndividual => c.state.pan_individual = on,
        GroupSelect => c.state.group_select = v as u8,
        PairExecute(pair) => c.execute_pair(pair, v),
        PatchPoint(k) => c.set_patch_point_pre(k, !on),
        Send34Execute => c.set_send34_stereo(on),
        LocalFader(_) => {}
    }
}

/// Parameter `n`'s name, in the table's words ("Channel Input 3 Level").
pub fn name(n: u16) -> String {
    use Param::*;
    let strip = |s: PmStrip| match s {
        PmStrip::Ch(c) => format!("Channel Input {}", c + 1),
        PmStrip::StIn => "Stereo Input".into(),
        PmStrip::Rtn(r) => format!("Effect Return {}", r + 1),
        PmStrip::Send(b) => format!("Send {}", b + 1),
        PmStrip::StOut => "Stereo Output".into(),
    };
    const BANDS: [&str; 3] = ["Low", "Mid", "High"];
    const WHAT: [&str; 3] = ["Frequency", "Gain", "Q"];
    let Some(p) = param(n) else { return String::new() };
    match p {
        Level(s) => format!("{} Level", strip(s)),
        On(s) => format!("{} On/Off", strip(s)),
        Pan(PmStrip::Send(_)) => "Send 3/4 Balance".into(),
        Pan(PmStrip::StOut) => "Stereo Output Balance".into(),
        Pan(s) => format!("{} Panpot", strip(s)),
        StereoPan(i, side) => {
            let s = if i == 0 { PmStrip::StIn } else { PmStrip::Rtn(i - 1) };
            format!("{} {} Panpot", strip(s), ["Lch", "Rch"][side])
        }
        Send(s, b) if b < 2 => format!("{} to Effect Send {} Level", strip(s), b + 1),
        Send(s, b) => format!("{} to Send {} Level", strip(s), b + 1),
        SendPre(b) if b < 2 => format!("Effect Send {} Pre/Post", b + 1),
        SendPre(b) => format!("Send {} Pre/Post", b + 1),
        Send34Pan(s) => format!("{} to Send3/4 Panpot", strip(s)),
        Cue(s) => format!("{} to Cue On/Off", strip(s)),
        Phase(s) => format!("{} Phase", strip(s)),
        EqOn(s) => format!("{} Equalizer On/Off", strip(s)),
        Eq(s, band, what) => format!("{} {} Equalizer {}", strip(s), BANDS[band], WHAT[what]),
        PairFlag(p) => format!("Channel Input {}/{} Stereo Pair Flag On/Off", 2 * p + 1, 2 * p + 2),
        FxType(f) => format!("Internal Effect {} Type", f + 1),
        FxParam(f, k, high) => {
            format!("Internal Effect {} Parameter {} {} Byte", f + 1, k + 1, if high { "High" } else { "Low" })
        }
        CompType(i) => format!("Comp {} Type", i + 1),
        CompPatch(i) => format!("Comp {} Patch Point", i + 1),
        CompKey(i) => format!("Comp {} Keyin Channel", i + 1),
        CompIn(i) => format!("Comp {} In Mode", i + 1),
        CompOn(i) => format!("Comp {} On/Off", i + 1),
        CompParam(i, k) => format!("Comp {} Parameter {}", i + 1, k + 1),
        Send34Stereo => "Send 3/4 Configuration Flag".into(),
        OscOn => "Oscillator On/Off".into(),
        OscLevel => "Oscillator Level".into(),
        OscFreq => "Oscillator Frequency".into(),
        OscAssign(k) => format!("Oscillator {} Assign", ["SEND3", "SEND4", "ST OUT"][k]),
        CueMode => "Cue Mode".into(),
        Group(m) if m < CHANNELS => format!("Channel Input {} Group No.", m + 1),
        Group(_) => "Stereo Input Group No.".into(),
        PanIndividual => "Panpot Control Mode".into(),
        GroupSelect => "Group Select".into(),
        PairExecute(p) => format!("Ch Input {}/{} Pair Execute", 2 * p + 1, 2 * p + 2),
        PatchPoint(2) => "Stereo Master Patch Point Pre/Post".into(),
        PatchPoint(k) => format!("Send{} Master Patch Point Pre/Post", k + 3),
        Send34Execute => "Send3,4 Configuration Execute".into(),
        LocalFader(f) => format!("MIDI Local Off Fader No.{}", f + 1),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::library::DynLibrary;
    use crate::state::State;
    use livedaw_engine::config::{ConsoleConfig, Counts};
    use livedaw_engine::{Engine, Profile};

    #[test]
    fn every_parameter_is_one_of_the_tables() {
        assert!((0..COUNT).all(|n| param(n).is_some()), "531 parameters");
        assert_eq!(param(COUNT), None);
        // Samples of the guide's table (Add-5 to Add-15).
        let samples: [(u16, &str); 16] = [
            (0, "Channel Input 1 Level"),
            (21, "Stereo Output Level"),
            (43, "Stereo Output On/Off"),
            (61, "Stereo Input Rch Panpot"),
            (66, "Send 3/4 Balance"),
            (84, "Stereo Input to Effect Send 1 Level"),
            (102, "Channel Input 1 to Send 3 Level"),
            (156, "Stereo Input to Send3/4 Panpot"),
            (195, "Stereo Input Phase"),
            (215, "Stereo Output Equalizer On/Off"),
            (235, "Stereo Output Low Equalizer Frequency"),
            (336, "Channel Input 1 High Equalizer Frequency"),
            (395, "Stereo Output High Equalizer Q"),
            (445, "Internal Effect 2 Parameter 10 Low Byte"),
            (475, "Comp 3 Parameter 5"),
            (530, "MIDI Local Off Fader No.16"),
        ];
        for (n, text) in samples {
            assert_eq!(name(n), text, "{n}");
        }
        assert!(!has_cc(157) && !has_cc(178) && !has_cc(483) && has_cc(156) && has_cc(530));
    }

    #[test]
    fn values_reach_the_engine_and_read_back() {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::ProMix01, counts: Counts::PROMIX01 });
        let (mut s, lib) = (State::default(), DynLibrary::default());
        let mut c = Console::new(&e, &mut s, &lib);
        let mut check = |n: u16, v: u16| {
            set(&mut c, n, v, Origin::Midi);
            assert_eq!(get(&c, n), Some(v), "{} = {v}", name(n));
        };
        for (n, v) in [
            (3, 115),  // CH4 at 0 dB
            (21, 90),  // ST OUT
            (23, 0),   // CH2 OFF
            (50, 4),   // CH7 pan L12
            (62, 10),  // RTN1 L pan
            (67, 20),  // ST OUT balance
            (90, 100), // CH7 to SEND2
            (136, 0),  // SEND1 PRE
            (139, 1),  // SEND4 POST
            (181, 1),  // CH3 phase reversed
            (214, 1),  // RTN2 EQ on
            (235, 30), // ST OUT LOW F 1 kHz
            (296, 0),  // CH1 MID G -15 dB
            (335, 8),  // ST OUT MID Q 3 oct
            (339, 25), // CH4 HIGH F 18 kHz
            (379, 9),  // CH4 HIGH Q SHELF
            (450, 17), // COMP2 on SEND3
            (453, 5),  // COMP2 keyed from CH5
            (459, 1),  // COMP2 ON
            (488, 3),  // CH5 in group C
            (511, 0),  // ST OUT's patch point PRE
        ] {
            check(n, v);
        }
        // Values beyond a parameter's range clamp to it.
        set(&mut c, 44, 99, Origin::Midi);
        assert_eq!(get(&c, 44), Some(32));
        // A type changes its parameters' steps: COMP1 as GATE FAST.
        set(&mut c, 446, 6, Origin::Midi);
        assert_eq!((get(&c, 446), steps(&c, 461 + 2)), (Some(6), 128), "HOLD's 128 steps");
        assert_eq!(get(&c, 461 + 2), Some(37), "101 ms is step 38 (0-based 37)");
        // Executes: a pair, SEND3-4 stereo.
        set(&mut c, 503, 3, Origin::Midi);
        assert_eq!(get(&c, 396), Some(1));
        assert_eq!(get(&c, 503), None, "an Execute has no value");
        set(&mut c, 514, 1, Origin::Midi);
        assert_eq!(get(&c, 476), Some(1));
        // The flag alone changes only the flag.
        set(&mut c, 397, 1, Origin::Midi);
        assert_eq!(get(&c, 397), Some(1));
        assert!(e.channel_at(2).unwrap().params.pan.load() == 0.0, "nothing reset");
    }
}
