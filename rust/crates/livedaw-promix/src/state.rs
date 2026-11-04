//! What the ProMix 01 has and the engine doesn't: the console's own state.
//!
//! The engine's strips hold the mix (levels, ON, pans, sends, phase, EQ, the
//! dynamics blocks); this holds what ties them together the ProMix's way: the
//! three dynamics processors (where each is patched, its key, its program),
//! the channel pairs, the fader groups A-D, SEND3-4 as a stereo pair and its
//! pans, the stereo strips' dual pans, the oscillator and the CUE mode
//! (User's Guide chapters 4-8, 10). [`State::to_text`] and
//! [`State::from_text`] write and read it as `key=value` lines, so a show
//! (and a scene) can keep it.

/// The ProMix 01's input channels.
pub const CHANNELS: usize = 16;
/// The channel pairs (1/2 ... 15/16).
pub const PAIRS: usize = 8;
/// The dynamics processors (COMP1-3).
pub const PROCESSORS: usize = 3;
/// The fader groups A-D.
pub const GROUPS: usize = 4;
/// The strips that take a group: channels 1-16 and ST IN.
pub const GROUPED: usize = CHANNELS + 1;
/// A dynamics program's parameters.
pub const COMP_PARAMS: usize = 5;

/// Where a dynamics processor is patched (`Comp n Patch Point`, 20 values,
/// p.72): nowhere, a channel (with its pair partner), SEND3, SEND4 or ST OUT.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Patch {
    #[default]
    Off,
    Channel(usize),
    Send3,
    Send4,
    Stereo,
}

impl Patch {
    /// The internal value (0 OFF, 1-16 CH1-16, 17 SEND3, 18 SEND4, 19 STEREO).
    pub fn value(self) -> u16 {
        match self {
            Patch::Off => 0,
            Patch::Channel(c) => 1 + c as u16,
            Patch::Send3 => 17,
            Patch::Send4 => 18,
            Patch::Stereo => 19,
        }
    }

    pub fn of_value(v: u16) -> Patch {
        match v {
            1..=16 => Patch::Channel(v as usize - 1),
            17 => Patch::Send3,
            18 => Patch::Send4,
            19 => Patch::Stereo,
            _ => Patch::Off,
        }
    }

    pub fn name(self) -> String {
        match self {
            Patch::Off => "OFF".into(),
            Patch::Channel(c) => format!("CH{}", c + 1),
            Patch::Send3 => "SEND3".into(),
            Patch::Send4 => "SEND4".into(),
            Patch::Stereo => "STEREO".into(),
        }
    }
}

/// What triggers a dynamics processor (`Comp n Keyin Channel`, 21 values,
/// p.73): itself, a channel (post-EQ, pre-fader), SEND3, SEND4, or one side
/// of the stereo output.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum KeyIn {
    #[default]
    SelfKey,
    Channel(usize),
    Send3,
    Send4,
    StereoL,
    StereoR,
}

impl KeyIn {
    /// The internal value (0 SELF, 1-16 CH1-16, 17 SEND3, 18 SEND4, 19 ST-L, 20 ST-R).
    pub fn value(self) -> u16 {
        match self {
            KeyIn::SelfKey => 0,
            KeyIn::Channel(c) => 1 + c as u16,
            KeyIn::Send3 => 17,
            KeyIn::Send4 => 18,
            KeyIn::StereoL => 19,
            KeyIn::StereoR => 20,
        }
    }

    pub fn of_value(v: u16) -> KeyIn {
        match v {
            1..=16 => KeyIn::Channel(v as usize - 1),
            17 => KeyIn::Send3,
            18 => KeyIn::Send4,
            19 => KeyIn::StereoL,
            20 => KeyIn::StereoR,
            _ => KeyIn::SelfKey,
        }
    }
}

/// A dynamics processor (COMP1-3, pp.68-77): its program and settings, and
/// where it is patched. ON, PATCH, KEY IN and IN are kept in mix scenes, not in
/// programs (p.73).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Processor {
    pub on: bool,
    pub patch: Patch,
    pub key_in: KeyIn,
    /// IN (a patch on a pair or ST OUT): 0 BOTH, 1 ODD / L, 2 EVEN / R.
    pub in_mode: u8,
    /// The program last recalled (0-based: 0-9 the presets, 10-19 the user programs).
    pub program: u8,
    /// Its type: the index of its model in `livedaw_dsp::promix::DYN_MODELS`.
    pub model: u8,
    /// Its parameters' values, in the model's units.
    pub values: [f32; COMP_PARAMS],
}

impl Default for Processor {
    fn default() -> Self {
        let spec = &livedaw_dsp::promix::DYN_MODELS[0];
        let mut values = [0.0; COMP_PARAMS];
        for (v, p) in values.iter_mut().zip(spec.params) {
            *v = p.default;
        }
        Processor { on: false, patch: Patch::Off, key_in: KeyIn::SelfKey, in_mode: 0, program: 0, model: 0, values }
    }
}

/// The oscillator (p.90): ON, its level (a fader position 0-127), its
/// frequency (0-3) and where it plays (SEND3, SEND4, ST OUT).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Oscillator {
    pub on: bool,
    pub level: u8,
    pub freq: u8,
    pub to_send3: bool,
    pub to_send4: bool,
    pub to_stereo: bool,
}

/// The ProMix 01's own state (see the module).
#[derive(Clone, Debug, PartialEq)]
pub struct State {
    pub processors: [Processor; PROCESSORS],
    /// Channel pairs 1/2 ... 15/16 (p.58).
    pub pairs: [bool; PAIRS],
    /// The group (0 none, 1-4 A-D) of channels 1-16 and ST IN (p.56).
    pub groups: [u8; GROUPED],
    /// The group the GROUP function shows (0-3: A-D).
    pub group_select: u8,
    /// Pair pans: GANG (false) or INDIVIDUAL (p.27).
    pub pan_individual: bool,
    /// SEND3-4 as a stereo pair (p.46): SEND3's bus is stereo then, playing
    /// on SEND3's and SEND4's outputs; the channels' SEND3-4 pans and ST IN's
    /// balance are their sends' pans to it, its output balance its pan.
    pub send34_stereo: bool,
    /// The dual pans of ST IN, RTN1 and RTN2 (L then R, 0-32).
    pub stereo_pans: [[u8; 2]; 3],
    pub oscillator: Oscillator,
    /// CUE mode: 0 MIX, 1 LAST CUE, 2 GROUP CUE, 3 STEREO OUT FIX.
    pub cue_mode: u8,
    /// The effects programs last recalled into effects 1-2 (0-39).
    pub fx_programs: [u8; 2],
}

impl Default for State {
    fn default() -> Self {
        State {
            processors: [Processor::default(); PROCESSORS],
            pairs: [false; PAIRS],
            groups: [0; GROUPED],
            group_select: 0,
            pan_individual: false,
            send34_stereo: false,
            // Stereo strips' L hard left, R hard right.
            stereo_pans: [[0, 32]; 3],
            oscillator: Oscillator::default(),
            cue_mode: 1,
            // REVERB HALL 1 and DELAY L-C-R (the factory rack: Sidecar's choice).
            fx_programs: [0, 13],
        }
    }
}

impl State {
    /// The pair partner of channel `c` (0-based), if its pair is on.
    pub fn partner(&self, c: usize) -> Option<usize> {
        (c < CHANNELS && self.pairs[c / 2]).then_some(c ^ 1)
    }

    /// The processor patched into `patch`, if one is.
    pub fn processor_at(&self, patch: Patch) -> Option<usize> {
        self.processors.iter().position(|p| p.patch == patch && patch != Patch::Off)
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
        for (i, p) in self.processors.iter().enumerate() {
            let values: Vec<String> = p.values.iter().map(|v| format!("{v}")).collect();
            put(
                &format!("comp{}", i + 1),
                format!(
                    "{} {} {} {} {} {} {}",
                    p.on as u8,
                    p.patch.value(),
                    p.key_in.value(),
                    p.in_mode,
                    p.program,
                    p.model,
                    values.join(" ")
                ),
            );
        }
        let list = |v: &[u8]| v.iter().map(|x| x.to_string()).collect::<Vec<_>>().join(" ");
        put("pairs", list(&self.pairs.map(u8::from)));
        put("groups", list(&self.groups));
        put("group_select", self.group_select.to_string());
        put("pan_individual", (self.pan_individual as u8).to_string());
        put("send34_stereo", (self.send34_stereo as u8).to_string());
        put("stereo_pans", list(&self.stereo_pans.concat()));
        let o = &self.oscillator;
        put("osc", list(&[o.on as u8, o.level, o.freq, o.to_send3 as u8, o.to_send4 as u8, o.to_stereo as u8]));
        put("cue_mode", self.cue_mode.to_string());
        put("fx_programs", list(&self.fx_programs));
        out
    }

    /// Reads `to_text`'s lines; what is missing or unreadable keeps its
    /// default.
    pub fn from_text(text: &str) -> State {
        let mut s = State::default();
        let nums = |v: &str| v.split_whitespace().filter_map(|x| x.parse::<f32>().ok()).collect::<Vec<f32>>();
        let fill = |dst: &mut [u8], v: &str, max: u8| {
            for (d, x) in dst.iter_mut().zip(nums(v)) {
                *d = (x.max(0.0) as u8).min(max);
            }
        };
        for line in text.lines() {
            let Some((k, v)) = line.split_once('=') else { continue };
            match k.trim() {
                k if k.starts_with("comp") => {
                    let Some(i) = k[4..].parse::<usize>().ok().and_then(|n| n.checked_sub(1)).filter(|&i| i < PROCESSORS) else {
                        continue;
                    };
                    let n = nums(v);
                    if n.len() < 6 {
                        continue;
                    }
                    let p = &mut s.processors[i];
                    p.on = n[0] != 0.0;
                    p.patch = Patch::of_value(n[1] as u16);
                    p.key_in = KeyIn::of_value(n[2] as u16);
                    p.in_mode = (n[3] as u8).min(2);
                    p.program = (n[4] as u8).min(19);
                    p.model = (n[5] as u8).min(livedaw_dsp::promix::DYN_MODELS.len() as u8 - 1);
                    for (d, x) in p.values.iter_mut().zip(&n[6..]) {
                        *d = *x;
                    }
                }
                "pairs" => {
                    let mut pairs = [0u8; PAIRS];
                    fill(&mut pairs, v, 1);
                    s.pairs = pairs.map(|x| x != 0);
                }
                "groups" => fill(&mut s.groups, v, 4),
                "group_select" => fill(std::slice::from_mut(&mut s.group_select), v, 3),
                "pan_individual" => s.pan_individual = nums(v).first() == Some(&1.0),
                "send34_stereo" => s.send34_stereo = nums(v).first() == Some(&1.0),
                "stereo_pans" => {
                    let mut flat = [0u8; 6];
                    fill(&mut flat, v, 32);
                    for (i, pan) in s.stereo_pans.iter_mut().enumerate() {
                        *pan = [flat[2 * i], flat[2 * i + 1]];
                    }
                }
                "osc" => {
                    let mut o = [0u8; 6];
                    fill(&mut o, v, 127);
                    s.oscillator = Oscillator {
                        on: o[0] != 0,
                        level: o[1],
                        freq: o[2].min(3),
                        to_send3: o[3] != 0,
                        to_send4: o[4] != 0,
                        to_stereo: o[5] != 0,
                    };
                }
                "cue_mode" => fill(std::slice::from_mut(&mut s.cue_mode), v, 3),
                "fx_programs" => fill(&mut s.fx_programs, v, 39),
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
    fn patches_and_keys_by_value() {
        for v in 0..20 {
            assert_eq!(Patch::of_value(v).value(), v);
        }
        for v in 0..21 {
            assert_eq!(KeyIn::of_value(v).value(), v);
        }
        assert_eq!(Patch::of_value(5).name(), "CH5");
        assert_eq!(Patch::of_value(99), Patch::Off);
    }

    #[test]
    fn the_state_round_trips_as_text() {
        let mut s = State::default();
        s.processors[1] = Processor {
            on: true,
            patch: Patch::Channel(4),
            key_in: KeyIn::StereoR,
            in_mode: 2,
            program: 12,
            model: 1,
            values: [-6.0, 1.0, 101.33, 1275.0, -20.0],
        };
        s.pairs[3] = true;
        s.groups[16] = 2;
        s.send34_stereo = true;
        s.stereo_pans[2] = [4, 30];
        s.oscillator = Oscillator { on: true, level: 100, freq: 2, to_send3: false, to_send4: true, to_stereo: true };
        s.cue_mode = 3;
        s.fx_programs = [22, 39];
        assert_eq!(State::from_text(&s.to_text()), s);
        assert_eq!(State::from_text(""), State::default());
        assert_eq!(
            State::from_text("pairs=1 0 1\ngroups=9\nnonsense"),
            State {
                pairs: [true, false, true, false, false, false, false, false],
                groups: [4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                ..State::default()
            }
        );
        assert_eq!(s.partner(6), Some(7));
        assert_eq!(s.partner(7), Some(6));
        assert_eq!(s.partner(0), None);
        assert_eq!(s.processor_at(Patch::Channel(4)), Some(1));
        assert_eq!(s.processor_at(Patch::Off), None);
    }
}
