//! The 02R's edit buffer as bytes: what its SysEx parameter changes (type 0)
//! address and its scene dumps carry (2016 data bytes, MAN p.213).
//!
//! The guides give the size, not the addresses: this is Sidecar's layout, so
//! a parameter change or a dump comes back into Sidecar, not into an 02R
//! (`docs/UNIVERSAL_MIXER.md`, Phase 6). Address `a`:
//!
//! * 0-1539: the 55 strips (`STRIPS`) 28 bytes each, strip `a / 28`, item `a
//!   % 28`: 0 the fader step, 1 ON, 2-3 the pans (a mono strip's in 2; the
//!   stereo strips' L and R), 4 the attenuator (+96: 0-108), 5 the switches
//!   (bit 0 phase, 1 delay on, 2 EQ on, 3 HPF, 4 LPF, 5 dynamics on, 6 to
//!   ST), 6-7 the delay in samples (high 7 bits, low 7 bits), 8-19 the EQ
//!   (LOW, L-MID, H-MID, HIGH: F, G, Q steps), 20 the dynamics type, 21-26
//!   its parameters' steps, 27 the routing to BUS 1-8 (a bit each).
//! * 1540-1919: the inputs' (the channels and EFF1-2) aux sends, 10 bytes
//!   each: the 8 levels as fader steps, then ON and PRE as a bit per aux.
//! * 1920-2015: zero.
//!
//! A value doesn't hold more than 7 bits except the routing byte and the
//! switches (8); the dump carries bytes as two nibbles anyway.

use crate::console::{Console, Origin, Y2Strip};
use crate::state::{AUXES, BUSES, MEMBERS};

/// The edit buffer's bytes (a scene's data).
pub const SIZE: usize = 2016;
/// The strips, in their order here: the channels, EFF1-2, BUS 1-8, AUX 1-8, STEREO.
pub const STRIPS: usize = MEMBERS + BUSES + AUXES + 1;
/// Each strip's bytes, and where the aux sends start.
pub const PER_STRIP: usize = 28;
pub const SENDS: usize = STRIPS * PER_STRIP;
pub const PER_SEND_BLOCK: usize = 10;

/// Strip `i` of the layout.
pub fn strip(i: usize) -> Option<Y2Strip> {
    match i {
        i if i < MEMBERS => Y2Strip::of_member(i),
        i if i < MEMBERS + BUSES => Some(Y2Strip::Bus(i - MEMBERS)),
        i if i < MEMBERS + BUSES + AUXES => Some(Y2Strip::Aux(i - MEMBERS - BUSES)),
        i if i < STRIPS => Some(Y2Strip::Stereo),
        _ => None,
    }
}

/// A strip's number in the layout.
pub fn index_of(s: Y2Strip) -> usize {
    match s {
        Y2Strip::Bus(b) => MEMBERS + b,
        Y2Strip::Aux(a) => MEMBERS + BUSES + a,
        Y2Strip::Stereo => STRIPS - 1,
        s => s.member().unwrap_or(0),
    }
}

/// The byte at address `a`.
pub fn get(c: &Console, a: usize) -> u8 {
    if a < SENDS {
        let Some(s) = strip(a / PER_STRIP) else { return 0 };
        return strip_byte(c, s, a % PER_STRIP);
    }
    let a = a - SENDS;
    let Some(s) = Y2Strip::of_member(a / PER_SEND_BLOCK) else { return 0 };
    match a % PER_SEND_BLOCK {
        k @ 0..=7 => c.aux_level(s, k) as u8,
        8 => (0..8).fold(0, |b, k| b | (c.aux_on(s, k) as u8) << k),
        _ => (0..8).fold(0, |b, k| b | (c.aux_pre(s, k) as u8) << k),
    }
}

fn strip_byte(c: &Console, s: Y2Strip, item: usize) -> u8 {
    let input = s.member().is_some();
    match item {
        0 => c.level(s) as u8,
        1 => c.on(s) as u8,
        2 | 3 if s.stereo_index().is_some() => c.stereo_pan(s, item - 2) as u8,
        2 => c.pan(s) as u8,
        4 => (c.att(s) + 96) as u8,
        5 => {
            let (delay_on, _) = c.delay(s);
            [c.phase(s), delay_on, c.eq_on(s), c.eq_filter(s, false), c.eq_filter(s, true), c.dyn_on(s), input && c.stereo_on(s)]
                .iter()
                .enumerate()
                .fold(0, |b, (k, &on)| b | (on as u8) << k)
        }
        6 => (c.delay(s).1 >> 7) as u8,
        7 => (c.delay(s).1 & 0x7f) as u8,
        8..=19 => c.eq_value(s, (item - 8) / 3, (item - 8) % 3) as u8,
        20 => c.dyn_type(s) as u8,
        21..=26 => c.dyn_param(s, item - 21) as u8,
        27 if input => (0..8).fold(0, |b, k| b | (c.bus_on(s, k) as u8) << k),
        _ => 0,
    }
}

/// Sets the byte at address `a`, as the console's control would (a pair's
/// partner follows; a group doesn't: `Origin::Remote`).
pub fn set(c: &mut Console, a: usize, v: u8) {
    if a < SENDS {
        if let Some(s) = strip(a / PER_STRIP) {
            set_strip_byte(c, s, a % PER_STRIP, v);
        }
        return;
    }
    let a = a - SENDS;
    let Some(s) = Y2Strip::of_member(a / PER_SEND_BLOCK) else { return };
    match a % PER_SEND_BLOCK {
        k @ 0..=7 => c.set_aux_level(s, k, v as u16),
        8 => (0..8).for_each(|k| c.set_aux_on(s, k, v >> k & 1 != 0)),
        _ => (0..8).for_each(|k| c.set_aux_pre(s, k, v >> k & 1 != 0)),
    }
}

fn set_strip_byte(c: &mut Console, s: Y2Strip, item: usize, v: u8) {
    let bit = |k: u8| v >> k & 1 != 0;
    match item {
        0 => c.set_level(s, v as u16, Origin::Remote),
        1 => c.set_on(s, v != 0, Origin::Remote),
        2 | 3 if s.stereo_index().is_some() => c.set_stereo_pan(s, item - 2, v as u16),
        2 => c.set_pan(s, v as u16, Origin::Remote),
        4 => c.set_att(s, v as i32 - 96),
        5 => {
            c.set_phase(s, bit(0));
            let (_, samples) = c.delay(s);
            c.set_delay(s, bit(1), samples);
            c.set_eq_on(s, bit(2));
            c.set_eq_filter(s, false, bit(3));
            c.set_eq_filter(s, true, bit(4));
            c.set_dyn_on(s, bit(5));
            c.set_stereo_on(s, bit(6));
        }
        6 | 7 => {
            let (on, samples) = c.delay(s);
            let samples = if item == 6 { (v as u16 & 0x7f) << 7 | samples & 0x7f } else { samples & !0x7f | v as u16 & 0x7f };
            c.set_delay(s, on, samples);
        }
        8..=19 => c.set_eq_value(s, (item - 8) / 3, (item - 8) % 3, v as u16),
        20 => {
            if c.dyn_type(s) != v as usize {
                c.set_dyn_type(s, v as usize);
            }
        }
        21..=26 => c.set_dyn_param(s, item - 21, v as u16),
        27 => (0..8).for_each(|k| c.set_bus_on(s, k, bit(k as u8))),
        _ => {}
    }
}

/// The whole edit buffer (`SIZE` bytes).
pub fn bytes(c: &Console) -> Vec<u8> {
    (0..SIZE).map(|a| get(c, a)).collect()
}

/// Loads an edit buffer (a scene's data): every address, the dynamics type
/// before its parameters.
pub fn load(c: &mut Console, data: &[u8]) {
    for (a, &v) in data.iter().enumerate().take(SIZE) {
        set(c, a, v);
    }
}

/// Scene 0's bytes: the edit buffer of a new 02R (its initial data, MAN
/// p.115; V2 p.129).
pub fn initial_bytes() -> &'static [u8] {
    static BYTES: std::sync::OnceLock<Vec<u8>> = std::sync::OnceLock::new();
    BYTES.get_or_init(|| {
        let e = livedaw_engine::Engine::new(0);
        e.configure(&livedaw_engine::ConsoleConfig {
            profile: livedaw_engine::Profile::Yamaha02R,
            counts: livedaw_engine::Counts::Y02R,
        });
        let mut s = crate::State::default();
        bytes(&Console::new(&e, &mut s))
    })
}

/// What address `a` is, for a display ("CH 3 FADER", "TAPE 1 EQ L-MID G").
pub fn name(a: usize) -> String {
    const ITEMS: [&str; PER_STRIP] = [
        "FADER",
        "ON",
        "PAN",
        "PAN R",
        "ATT",
        "SWITCHES",
        "DELAY H",
        "DELAY L",
        "EQ LOW F",
        "EQ LOW G",
        "EQ LOW Q",
        "EQ L-MID F",
        "EQ L-MID G",
        "EQ L-MID Q",
        "EQ H-MID F",
        "EQ H-MID G",
        "EQ H-MID Q",
        "EQ HIGH F",
        "EQ HIGH G",
        "EQ HIGH Q",
        "DYN TYPE",
        "DYN 1",
        "DYN 2",
        "DYN 3",
        "DYN 4",
        "DYN 5",
        "DYN 6",
        "ROUTING",
    ];
    let label = |s: Y2Strip| match s {
        Y2Strip::Mic(m) => format!("CH {}", m + 1),
        Y2Strip::Line(l) => format!("LINE {}/{}", 17 + 2 * l, 18 + 2 * l),
        Y2Strip::Tape(t) => format!("TAPE {}", t + 1),
        Y2Strip::Eff(e) => format!("EFF{}", e + 1),
        Y2Strip::Bus(b) => format!("BUS {}", b + 1),
        Y2Strip::Aux(x) => format!("AUX {}", x + 1),
        Y2Strip::Stereo => "STEREO".into(),
    };
    if a < SENDS {
        return strip(a / PER_STRIP).map_or_else(String::new, |s| format!("{} {}", label(s), ITEMS[a % PER_STRIP]));
    }
    let k = (a - SENDS) % PER_SEND_BLOCK;
    match Y2Strip::of_member((a - SENDS) / PER_SEND_BLOCK) {
        Some(s) if k < 8 => format!("{} AUX {}", label(s), k + 1),
        Some(s) => format!("{} AUX {}", label(s), if k == 8 { "ON" } else { "PRE" }),
        None => String::new(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::State;
    use livedaw_engine::config::{ConsoleConfig, Counts};
    use livedaw_engine::{Engine, Profile};

    fn y02r() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::Yamaha02R, counts: Counts::Y02R });
        e
    }

    #[test]
    fn the_layout() {
        assert_eq!((STRIPS, SENDS), (55, 1540));
        const { assert!(SENDS + MEMBERS * PER_SEND_BLOCK <= SIZE) };
        for i in 0..STRIPS {
            assert_eq!(index_of(strip(i).unwrap()), i);
        }
        assert_eq!(name(0), "CH 1 FADER");
        assert_eq!(name(20 * PER_STRIP + 9), "TAPE 1 EQ LOW G");
        assert_eq!(name(SENDS + 7), "CH 1 AUX 8");
        assert_eq!(name((STRIPS - 1) * PER_STRIP), "STEREO FADER");
        assert_eq!(crate::state::CHANNELS, 36);
    }

    #[test]
    fn bytes_round_trip_through_another_console() {
        let e = y02r();
        let mut s = State::default();
        let mut c = Console::new(&e, &mut s);
        c.set_level(Y2Strip::Tape(4), 77, Origin::Panel);
        c.set_on(Y2Strip::Mic(2), false, Origin::Panel);
        c.set_stereo_pan(Y2Strip::Eff(1), 1, 20);
        c.set_att(Y2Strip::Stereo, 6);
        c.set_delay(Y2Strip::Mic(9), true, 2000);
        c.set_eq_value(Y2Strip::Line(2), 2, 1, 60);
        c.set_dyn_type(Y2Strip::Bus(3), 5);
        c.set_dyn_param(Y2Strip::Bus(3), 4, 100);
        c.set_bus_on(Y2Strip::Mic(0), 6, true);
        c.set_aux_level(Y2Strip::Tape(0), 3, 90);
        c.set_aux_pre(Y2Strip::Tape(0), 3, true);
        let data = bytes(&c);
        assert_eq!(data.len(), SIZE);
        let e2 = y02r();
        let mut s2 = State::default();
        let mut c2 = Console::new(&e2, &mut s2);
        load(&mut c2, &data);
        assert_eq!(bytes(&c2), data);
        assert_eq!((c2.level(Y2Strip::Tape(4)), c2.on(Y2Strip::Mic(2)), c2.att(Y2Strip::Stereo)), (77, false, 6));
        assert_eq!(
            (c2.delay(Y2Strip::Mic(9)), c2.dyn_type(Y2Strip::Bus(3)), c2.dyn_param(Y2Strip::Bus(3), 4)),
            ((true, 2000), 5, 100)
        );
        assert!(c2.bus_on(Y2Strip::Mic(0), 6) && c2.aux_pre(Y2Strip::Tape(0), 3));
        assert_eq!(c2.aux_level(Y2Strip::Tape(0), 3), 90);
    }
}
