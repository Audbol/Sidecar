//! What the 02R has and the engine doesn't: the console's own state.
//!
//! The engine's strips hold the mix (levels, ON, pans, attenuation, phase,
//! delay, sends and routing, EQ, the dynamics blocks); this holds what ties
//! them together the 02R's way: the channel pairs (MIC and TAPE odd / even,
//! V2 p.184) and the aux and bus pairs (V2 p.185), the fader groups A-D and
//! mute groups E-H (MAN pp.148-149), the pan gangs (MAN p.37), the stereo
//! strips' two pans, the direct outs (MAN p.39), the dynamics processors'
//! KEY IN, ST.LINK and IN (MAN p.70), fader recall safe and fade times (MAN
//! pp.120-121), the oscillator (MAN p.165) and the effects programs last
//! recalled. [`State::to_text`] and [`State::from_text`] write and read it as
//! `key=value` lines, so a show (and a scene) can keep it.

/// MIC/LINE 1-16.
pub const MICS: usize = 16;
/// LINE 17/18-23/24 (stereo channels).
pub const LINES: usize = 4;
/// TAPE 1-16.
pub const TAPES: usize = 16;
/// EFF1-2 (stereo effect returns).
pub const EFFS: usize = 2;
/// The input channels (MIC, LINE, TAPE: the engine's channels 1-36).
pub const CHANNELS: usize = MICS + LINES + TAPES;
/// The strips that take a fader or mute group: the channels and EFF1-2
/// (MAN p.148), numbered as the engine's channels, then EFF1-2.
pub const MEMBERS: usize = CHANNELS + EFFS;
/// BUS 1-8 and AUX 1-8.
pub const BUSES: usize = 8;
pub const AUXES: usize = 8;
/// The channel pairs: MIC 1/2 ... 15/16, then TAPE 1/2 ... 15/16.
pub const PAIRS: usize = MICS / 2 + TAPES / 2;
/// The strips with a dynamics processor: the channels, BUS 1-8, STEREO.
pub const DYN_STRIPS: usize = CHANNELS + BUSES + 1;
/// The faders recall safe and fade times cover: the members and STEREO.
pub const FADERS: usize = MEMBERS + 1;

/// What keys a dynamics processor (KEY IN, MAN p.70): the strip itself or
/// its odd neighbour (LEFT), before or after their EQ, AUX 1 or 2; an output
/// processor also BUS 1-8 or STEREO (MAN p.72).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum KeyIn {
    #[default]
    SelfPostEq,
    SelfPreEq,
    LeftPostEq,
    LeftPreEq,
    Aux1,
    Aux2,
    /// BUS 1-8 (0-based).
    Bus(usize),
    Stereo,
}

impl KeyIn {
    /// The value (0 SELF-POST EQ ... 5 AUX 2, 6-13 BUS 1-8, 14 STEREO).
    pub fn value(self) -> u16 {
        match self {
            KeyIn::SelfPostEq => 0,
            KeyIn::SelfPreEq => 1,
            KeyIn::LeftPostEq => 2,
            KeyIn::LeftPreEq => 3,
            KeyIn::Aux1 => 4,
            KeyIn::Aux2 => 5,
            KeyIn::Bus(b) => 6 + b.min(7) as u16,
            KeyIn::Stereo => 14,
        }
    }

    pub fn of_value(v: u16) -> KeyIn {
        match v {
            1 => KeyIn::SelfPreEq,
            2 => KeyIn::LeftPostEq,
            3 => KeyIn::LeftPreEq,
            4 => KeyIn::Aux1,
            5 => KeyIn::Aux2,
            6..=13 => KeyIn::Bus(v as usize - 6),
            14 => KeyIn::Stereo,
            _ => KeyIn::SelfPostEq,
        }
    }

    pub fn name(self) -> String {
        match self {
            KeyIn::SelfPostEq => "SELF-POST EQ".into(),
            KeyIn::SelfPreEq => "SELF-PRE EQ".into(),
            KeyIn::LeftPostEq => "LEFT-POST EQ".into(),
            KeyIn::LeftPreEq => "LEFT-PRE EQ".into(),
            KeyIn::Aux1 => "AUX 1".into(),
            KeyIn::Aux2 => "AUX 2".into(),
            KeyIn::Bus(b) => format!("BUS {}", b + 1),
            KeyIn::Stereo => "STEREO".into(),
        }
    }
}

/// A strip's dynamics processor settings that its programs don't hold (MAN
/// p.70): KEY IN, ST.LINK (a pair's two processors as one) and IN (which
/// side of a link triggers it: 0 BOTH, 1 L, 2 R).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DynSettings {
    pub key_in: KeyIn,
    pub st_link: bool,
    pub in_mode: u8,
}

/// The oscillator (MAN p.165): ON, its level (0 to -96 dB, as dB below 0),
/// its wave (0 sine 100 Hz, 1 sine 1 kHz, 2 sine 10 kHz, 3 noise) and where
/// it plays: bits 0-7 BUS 1-8, 8 STEREO, 9-16 AUX 1-8.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Oscillator {
    pub on: bool,
    pub level: u8,
    pub wave: u8,
    pub assign: u32,
}

impl Default for Oscillator {
    fn default() -> Self {
        // Off, -20 dB, 1 kHz, to STEREO: Sidecar's choice (the guide doesn't say).
        Oscillator { on: false, level: 20, wave: 1, assign: 1 << 8 }
    }
}

/// The 02R's own state (see the module).
#[derive(Clone, Debug, PartialEq)]
pub struct State {
    /// MIC 1/2 ... 15/16, then TAPE 1/2 ... 15/16 (V2 p.184).
    pub pairs: [bool; PAIRS],
    /// MS decoding (V2 MAN p.186) of a pair: its odd channel M, its even S,
    /// the faders apart.
    pub ms: [bool; PAIRS],
    /// AUX 1/2 ... 7/8 and BUS 1/2 ... 7/8 (V2 p.185).
    pub aux_pairs: [bool; AUXES / 2],
    pub bus_pairs: [bool; BUSES / 2],
    /// Pan gangs of the same odd / even neighbours as `pairs` (MAN p.37).
    pub gangs: [bool; PAIRS],
    /// The fader group (0 none, 1-4 A-D) and mute group (0 none, 1-4 E-H) of
    /// each member (MAN pp.148-149).
    pub fader_groups: [u8; MEMBERS],
    pub mute_groups: [u8; MEMBERS],
    /// Groups A-D, then E-H, on (a group that is off moves its members alone).
    pub groups_on: [bool; 8],
    /// LINE 17/18-23/24's and EFF1-2's two pans (L then R, 0-32).
    pub stereo_pans: [[u8; 2]; LINES + EFFS],
    /// MIC 1-16's direct outs (MAN p.39).
    pub direct: [bool; MICS],
    /// The dynamics processors of the channels, BUS 1-8 and STEREO.
    pub dynamics: [DynSettings; DYN_STRIPS],
    /// Fader recall safe: ENABLE, and each member's and STEREO's flag.
    pub recall_safe_on: bool,
    pub recall_safe: [bool; FADERS],
    /// Fade times in tenths of a second (0-100: 0.0-10.0 s).
    pub fade_times: [u8; FADERS],
    pub oscillator: Oscillator,
    /// The effects programs last recalled into Effect 1-2 (0-127).
    pub fx_programs: [u8; 2],
    /// The DELAY page's unit: samples (false) or ms.
    pub delay_ms: bool,
}

impl Default for State {
    fn default() -> Self {
        let mut dynamics = [DynSettings::default(); DYN_STRIPS];
        // STEREO's processor keys itself before its EQ (MAN-GS p.56).
        dynamics[DYN_STRIPS - 1].key_in = KeyIn::SelfPreEq;
        State {
            pairs: [false; PAIRS],
            ms: [false; PAIRS],
            aux_pairs: [false; AUXES / 2],
            bus_pairs: [false; BUSES / 2],
            gangs: [false; PAIRS],
            fader_groups: [0; MEMBERS],
            mute_groups: [0; MEMBERS],
            groups_on: [true; 8],
            // The stereo strips' L hard left, R hard right.
            stereo_pans: [[0, 32]; LINES + EFFS],
            direct: [false; MICS],
            dynamics,
            recall_safe_on: false,
            recall_safe: [false; FADERS],
            fade_times: [0; FADERS],
            oscillator: Oscillator::default(),
            // REVERB HALL1 and DELAY L-C-R 1 (Sidecar's choice for scene 0).
            fx_programs: [0, 22],
            delay_ms: false,
        }
    }
}

/// The pair (an index into `State::pairs`) a channel (0-based, the engine's
/// numbering) is in, if it can pair: MIC 1-16 and TAPE 1-16.
pub fn pair_of(channel: usize) -> Option<usize> {
    match channel {
        c if c < MICS => Some(c / 2),
        c if (MICS + LINES..CHANNELS).contains(&c) => Some(MICS / 2 + (c - MICS - LINES) / 2),
        _ => None,
    }
}

impl State {
    /// The pair partner of channel `c` (0-based), if its pair is made.
    pub fn partner(&self, c: usize) -> Option<usize> {
        pair_of(c).filter(|&p| self.pairs[p]).map(|_| c ^ 1)
    }

    /// The gang partner of channel `c`, if its pans are ganged.
    pub fn gang_partner(&self, c: usize) -> Option<usize> {
        pair_of(c).filter(|&p| self.gangs[p]).map(|_| c ^ 1)
    }

    /// The pair partner of bus slot `b` (0-15: BUS 1-8, AUX 1-8), if paired.
    pub fn bus_partner(&self, b: usize) -> Option<usize> {
        match b {
            0..=7 if self.bus_pairs[b / 2] => Some(b ^ 1),
            8..=15 if self.aux_pairs[(b - 8) / 2] => Some(b ^ 1),
            _ => None,
        }
    }

    /// The other members of `member`'s fader group (`mute`: its mute group),
    /// while that group is on.
    pub fn group_members(&self, member: usize, mute: bool) -> Vec<usize> {
        let groups = if mute { &self.mute_groups } else { &self.fader_groups };
        let g = groups.get(member).copied().unwrap_or(0);
        if g == 0 || !self.groups_on[(g as usize - 1) + if mute { 4 } else { 0 }] {
            return vec![];
        }
        (0..MEMBERS).filter(|&m| m != member && groups[m] == g).collect()
    }

    /// As `key=value` lines.
    pub fn to_text(&self) -> String {
        let mut out = String::new();
        let mut put = |k: &str, v: String| {
            out.push_str(k);
            out.push('=');
            out.push_str(&v);
            out.push('\n');
        };
        let list = |v: &[u8]| v.iter().map(|x| x.to_string()).collect::<Vec<_>>().join(" ");
        let bits = |v: &[bool]| list(&v.iter().map(|&b| b as u8).collect::<Vec<_>>());
        put("pairs", bits(&self.pairs));
        put("ms", bits(&self.ms));
        put("aux_pairs", bits(&self.aux_pairs));
        put("bus_pairs", bits(&self.bus_pairs));
        put("gangs", bits(&self.gangs));
        put("fader_groups", list(&self.fader_groups));
        put("mute_groups", list(&self.mute_groups));
        put("groups_on", bits(&self.groups_on));
        put("stereo_pans", list(&self.stereo_pans.concat()));
        put("direct", bits(&self.direct));
        let dyns: Vec<u8> = self.dynamics.iter().flat_map(|d| [d.key_in.value() as u8, d.st_link as u8, d.in_mode]).collect();
        put("dynamics", list(&dyns));
        put("recall_safe_on", (self.recall_safe_on as u8).to_string());
        put("recall_safe", bits(&self.recall_safe));
        put("fade_times", list(&self.fade_times));
        let o = &self.oscillator;
        put("osc", format!("{} {} {} {}", o.on as u8, o.level, o.wave, o.assign));
        put("fx_programs", list(&self.fx_programs));
        put("delay_ms", (self.delay_ms as u8).to_string());
        out
    }

    /// Reads `to_text`'s lines; what is missing or unreadable keeps its default.
    pub fn from_text(text: &str) -> State {
        let mut s = State::default();
        let nums = |v: &str| v.split_whitespace().filter_map(|x| x.parse::<u32>().ok()).collect::<Vec<u32>>();
        let fill = |dst: &mut [u8], v: &str, max: u8| {
            for (d, x) in dst.iter_mut().zip(nums(v)) {
                *d = x.min(max as u32) as u8;
            }
        };
        let flags = |dst: &mut [bool], v: &str| {
            for (d, x) in dst.iter_mut().zip(nums(v)) {
                *d = x != 0;
            }
        };
        for line in text.lines() {
            let Some((k, v)) = line.split_once('=') else { continue };
            match k.trim() {
                "pairs" => flags(&mut s.pairs, v),
                "ms" => flags(&mut s.ms, v),
                "aux_pairs" => flags(&mut s.aux_pairs, v),
                "bus_pairs" => flags(&mut s.bus_pairs, v),
                "gangs" => flags(&mut s.gangs, v),
                "fader_groups" => fill(&mut s.fader_groups, v, 4),
                "mute_groups" => fill(&mut s.mute_groups, v, 4),
                "groups_on" => flags(&mut s.groups_on, v),
                "stereo_pans" => {
                    let mut flat = [0u8; 2 * (LINES + EFFS)];
                    fill(&mut flat, v, 32);
                    for (i, pan) in s.stereo_pans.iter_mut().enumerate() {
                        *pan = [flat[2 * i], flat[2 * i + 1]];
                    }
                }
                "direct" => flags(&mut s.direct, v),
                "dynamics" => {
                    for (d, x) in s.dynamics.iter_mut().zip(nums(v).chunks_exact(3)) {
                        *d = DynSettings { key_in: KeyIn::of_value(x[0] as u16), st_link: x[1] != 0, in_mode: x[2].min(2) as u8 };
                    }
                }
                "recall_safe_on" => s.recall_safe_on = nums(v).first() == Some(&1),
                "recall_safe" => flags(&mut s.recall_safe, v),
                "fade_times" => fill(&mut s.fade_times, v, 100),
                "osc" => {
                    let n = nums(v);
                    if n.len() == 4 {
                        s.oscillator = Oscillator {
                            on: n[0] != 0,
                            level: n[1].min(96) as u8,
                            wave: n[2].min(3) as u8,
                            assign: n[3] & 0x1ffff,
                        };
                    }
                }
                "fx_programs" => fill(&mut s.fx_programs, v, 127),
                "delay_ms" => s.delay_ms = nums(v).first() == Some(&1),
                _ => {}
            }
        }
        s
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keys_by_value() {
        for v in 0..15 {
            assert_eq!(KeyIn::of_value(v).value(), v);
        }
        assert_eq!(KeyIn::of_value(8).name(), "BUS 3");
        assert_eq!(KeyIn::of_value(99), KeyIn::SelfPostEq);
    }

    #[test]
    fn pairs_gangs_and_groups() {
        let mut s = State::default();
        assert_eq!((pair_of(0), pair_of(15), pair_of(16), pair_of(20), pair_of(35)), (Some(0), Some(7), None, Some(8), Some(15)));
        s.pairs[8] = true; // TAPE 1/2
        assert_eq!((s.partner(20), s.partner(21), s.partner(0)), (Some(21), Some(20), None));
        assert_eq!(s.partner(17), None, "LINE channels are stereo already");
        s.gangs[0] = true;
        assert_eq!(s.gang_partner(1), Some(0));
        s.aux_pairs[3] = true;
        assert_eq!((s.bus_partner(15), s.bus_partner(14), s.bus_partner(0)), (Some(14), Some(15), None));
        s.fader_groups[3] = 2;
        s.fader_groups[36] = 2; // EFF1
        s.fader_groups[5] = 1;
        assert_eq!(s.group_members(3, false), vec![36]);
        s.groups_on[1] = false;
        assert!(s.group_members(3, false).is_empty(), "group B off");
        s.mute_groups[0] = 1;
        s.mute_groups[1] = 1;
        assert_eq!(s.group_members(0, true), vec![1]);
    }

    #[test]
    fn the_state_round_trips_as_text() {
        let mut s = State::default();
        s.pairs[3] = true;
        s.bus_pairs[1] = true;
        s.gangs[9] = true;
        s.fader_groups[37] = 4;
        s.mute_groups[2] = 3;
        s.groups_on[6] = false;
        s.stereo_pans[4] = [4, 30];
        s.direct[15] = true;
        s.dynamics[40] = DynSettings { key_in: KeyIn::Bus(4), st_link: true, in_mode: 2 };
        s.recall_safe_on = true;
        s.recall_safe[38] = true;
        s.fade_times[10] = 100;
        s.oscillator = Oscillator { on: true, level: 96, wave: 3, assign: 0x1ffff };
        s.fx_programs = [40, 127];
        s.delay_ms = true;
        assert_eq!(State::from_text(&s.to_text()), s);
        assert_eq!(State::from_text(""), State::default());
        assert_eq!(State::default().dynamics[DYN_STRIPS - 1].key_in, KeyIn::SelfPreEq, "STEREO's");
        let odd = State::from_text("pairs=1 0 1\nfader_groups=9\nnonsense");
        assert!(odd.pairs[0] && odd.pairs[2] && !odd.pairs[1]);
        assert_eq!(odd.fader_groups[0], 4);
    }
}
