//! The 02R's own state as numbered values, for the host's panel (`ld_y02r_state`):
//! what the edit buffer's bytes don't hold (the pairs, groups, gangs, the
//! dynamics' keys, recall safe, fade times, the oscillator, the direct outs).

use crate::console::{Console, Y2Strip};
use crate::params;
use crate::state::{DynSettings, KeyIn, SurroundPan, FADERS, MEMBERS, MICS, PAIRS};

/// What a value is (`get` / `set`'s `what`), and what its index counts.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum What {
    /// Channel pair 0-15 (MIC 1/2 ..., TAPE 1/2 ...): 0 / 1; making one copies
    /// the odd channel (`set_pair`).
    Pair = 0,
    /// AUX pair 0-3, BUS pair 0-3.
    AuxPair = 1,
    BusPair = 2,
    /// Pan gang 0-15 (the channel pairs' neighbours).
    Gang = 3,
    /// A member's (0-37: the channels, EFF1-2) fader group 0-4 (0 none, 1-4
    /// A-D), mute group 0-4 (E-H).
    FaderGroup = 4,
    MuteGroup = 5,
    /// Group 0-7 (A-D, E-H) on.
    GroupOn = 6,
    /// MIC 1-16's DIRECT.
    Direct = 7,
    /// A strip's (the edit buffer's numbering) dynamics KEY IN (`KeyIn::value`),
    /// ST.LINK and IN.
    KeyIn = 8,
    StLink = 9,
    InMode = 10,
    /// Fader recall safe of fader 0-38 (the members, STEREO), and its ENABLE.
    RecallSafe = 11,
    RecallSafeOn = 12,
    /// Fade time of fader 0-38, in tenths of a second (0-100).
    FadeTime = 13,
    /// The oscillator: ON, level (dB below 0, 0-96), wave (0-3), assign (bits).
    OscOn = 14,
    OscLevel = 15,
    OscWave = 16,
    OscAssign = 17,
    /// Channel pair 0-15's MS decoding (V2 MAN p.186): on, it pairs them.
    Ms = 18,
    /// SURROUND's mode (0 off, 1 2+2, 2 3+1, 3 3+2+1).
    Surround = 19,
    /// Member `i / 11`'s surround pan value `i % 11` (`SurroundPan::field`:
    /// X, Y, CSR, SW, ON, trajectory, WIDTH, DEPTH, OFFSET X, OFFSET Y, the
    /// place along it).
    SurroundPan = 20,
}

impl What {
    pub fn of(n: i32) -> Option<What> {
        use What::*;
        [
            Pair,
            AuxPair,
            BusPair,
            Gang,
            FaderGroup,
            MuteGroup,
            GroupOn,
            Direct,
            KeyIn,
            StLink,
            InMode,
            RecallSafe,
            RecallSafeOn,
            FadeTime,
            OscOn,
            OscLevel,
            OscWave,
            OscAssign,
            Ms,
            Surround,
            SurroundPan,
        ]
        .into_iter()
        .find(|w| *w as i32 == n)
    }
}

/// The dynamics settings of the edit buffer's strip `i`, if it has a processor.
fn dyn_strip(i: usize) -> Option<Y2Strip> {
    params::strip(i).filter(|s| s.dyn_index().is_some())
}

/// Value `what` number `i`, if there is one.
pub fn get(c: &Console, what: What, i: usize) -> Option<i32> {
    let s = &c.state;
    let flag = |v: &[bool]| v.get(i).map(|&b| b as i32);
    Some(match what {
        What::Pair => flag(&s.pairs)?,
        What::Ms if i < PAIRS => c.ms(i) as i32,
        What::Ms => return None,
        What::Surround => s.surround as i32,
        What::SurroundPan => c.surround_pan(i / SurroundPan::FIELDS)?.field(i % SurroundPan::FIELDS)? as i32,
        What::AuxPair => flag(&s.aux_pairs)?,
        What::BusPair => flag(&s.bus_pairs)?,
        What::Gang => flag(&s.gangs)?,
        What::FaderGroup => *s.fader_groups.get(i)? as i32,
        What::MuteGroup => *s.mute_groups.get(i)? as i32,
        What::GroupOn => flag(&s.groups_on)?,
        What::Direct => flag(&s.direct)?,
        What::KeyIn => c.dyn_settings(dyn_strip(i)?).key_in.value() as i32,
        What::StLink => c.dyn_settings(dyn_strip(i)?).st_link as i32,
        What::InMode => c.dyn_settings(dyn_strip(i)?).in_mode as i32,
        What::RecallSafe => flag(&s.recall_safe)?,
        What::RecallSafeOn => s.recall_safe_on as i32,
        What::FadeTime => *s.fade_times.get(i)? as i32,
        What::OscOn => s.oscillator.on as i32,
        What::OscLevel => s.oscillator.level as i32,
        What::OscWave => s.oscillator.wave as i32,
        What::OscAssign => s.oscillator.assign as i32,
    })
}

/// Sets value `what` number `i` as the console's panel does.
pub fn set(c: &mut Console, what: What, i: usize, v: i32) {
    let on = v != 0;
    match what {
        What::Pair if i < PAIRS => c.set_pair(i, on, true),
        What::Ms if i < PAIRS => c.set_ms(i, on),
        What::Surround => c.set_surround(v.clamp(0, 3) as u8),
        What::SurroundPan => {
            if let Some(p) = c.surround_pan(i / SurroundPan::FIELDS) {
                c.set_surround_pan(i / SurroundPan::FIELDS, p.with_field(i % SurroundPan::FIELDS, v.max(0) as u32));
            }
        }
        What::AuxPair => c.set_output_pair(true, i, on),
        What::BusPair => c.set_output_pair(false, i, on),
        What::Gang => c.set_gang(i, on),
        What::FaderGroup if i < MEMBERS => c.set_group(i, false, v.clamp(0, 4) as u8),
        What::MuteGroup if i < MEMBERS => c.set_group(i, true, v.clamp(0, 4) as u8),
        What::GroupOn => {
            if let Some(g) = c.state.groups_on.get_mut(i) {
                *g = on;
            }
        }
        What::Direct if i < MICS => c.set_direct(i, on),
        What::KeyIn | What::StLink | What::InMode => {
            let Some(s) = dyn_strip(i) else { return };
            let mut d: DynSettings = c.dyn_settings(s);
            match what {
                What::KeyIn => d.key_in = KeyIn::of_value(v.clamp(0, 14) as u16),
                What::StLink => d.st_link = on,
                _ => d.in_mode = v.clamp(0, 2) as u8,
            }
            c.set_dyn_settings(s, d);
        }
        What::RecallSafe if i < FADERS => c.state.recall_safe[i] = on,
        What::RecallSafeOn => c.state.recall_safe_on = on,
        What::FadeTime if i < FADERS => c.state.fade_times[i] = v.clamp(0, 100) as u8,
        What::OscOn => c.state.oscillator.on = on,
        What::OscLevel => c.state.oscillator.level = v.clamp(0, 96) as u8,
        What::OscWave => c.state.oscillator.wave = v.clamp(0, 3) as u8,
        What::OscAssign => c.state.oscillator.assign = v as u32 & 0x1ffff,
        _ => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::State;
    use livedaw_engine::config::{ConsoleConfig, Counts};
    use livedaw_engine::{Engine, Profile};

    #[test]
    fn values_by_number() {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::Yamaha02R, counts: Counts::Y02R });
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        for n in 0..21 {
            assert_eq!(What::of(n).map(|w| w as i32), Some(n));
        }
        assert_eq!(What::of(21), None);
        set(&mut c, What::Pair, 8, 1);
        assert_eq!((get(&c, What::Pair, 8), c.partner(Y2Strip::Tape(0))), (Some(1), Some(Y2Strip::Tape(1))));
        set(&mut c, What::FaderGroup, 37, 9);
        assert_eq!(get(&c, What::FaderGroup, 37), Some(4));
        // KEY IN of BUS 3 (strip 40): BUS 5.
        set(&mut c, What::KeyIn, 40, KeyIn::Bus(4).value() as i32);
        assert_eq!(get(&c, What::KeyIn, 40), Some(10));
        assert_eq!(get(&c, What::KeyIn, 46), None, "AUX 1 has no processor");
        set(&mut c, What::FadeTime, 38, 250);
        assert_eq!(get(&c, What::FadeTime, 38), Some(100));
        set(&mut c, What::OscAssign, 0, -1);
        assert_eq!(get(&c, What::OscAssign, 0), Some(0x1ffff));
        assert_eq!(get(&c, What::Pair, 16), None);
    }
}
