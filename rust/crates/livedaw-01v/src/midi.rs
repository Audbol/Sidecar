//! The 01V's MIDI (Owner's Manual chapter 17, appendix C): its remote protocol,
//! on one port.
//!
//! * Program changes recall scenes through a table (`PcTable`, MIDI page 2:
//!   programs #1-99 scenes 1-99, #100 scene 0, p.273); recalling one on the
//!   console sends its program change, or a function call if it has none.
//! * Control changes move the parameters of the assign table (`CcMap`, MIDI
//!   page 3: faders, ON keys, pans, the effect sends; pp.274-279 with its
//!   three arrangements), both ways, each value by the manual's equation
//!   (p.280).
//! * System Exclusive, model `3E 04` (pp.281-284):
//!   * parameter changes on the edit buffer (`F0 43 1n 3E 04 tt`) as bytes,
//!     7-bit values or bits, by Sidecar's addresses (`params`), and requests of
//!     them (`3n`), the system memory (the setup) too;
//!   * function calls (type 2): scene recall and store, the effects library's
//!     recall, titles;
//!   * key remote (type 0x43): the ON and SOLO keys here, the others to the app.
//!   * Yamaha bulk dumps and requests (`F0 43 0n 7E`, `F0 43 2n 7E`, the
//!     header `LM  8B34` and a type letter, p.286): scene memories (127 the
//!     edit buffer), the setup, both tables, a channel's current EQ and
//!     dynamics, an effect's current settings.
//! * MIDI SETUP (p.224): Tx and Rx channels; Tx, Rx, OMNI and ECHO of program
//!   and control changes; parameter change Tx, Rx, ECHO; BULK.
//!
//! The bytes inside a dump are Sidecar's layout (the manual gives only the
//! sizes), so a dump comes back into Sidecar, not into a 01V. A value that
//! arrived isn't sent back; console changes go out once the first poll has
//! taken the console's values.

use crate::params::{self, is_stereo_input, BUSES, EFFECTS, OPTION_CH, RETURNS, SIZE, STEREO};
use livedaw_engine::Engine;
use livedaw_promix::midi::{frame_dump, parse_frame};
use std::collections::VecDeque;
use std::sync::atomic::Ordering::Relaxed;

const YAMAHA: u8 = 0x43;
/// The digital mixers' group id and the 01V's model id (p.281).
const GROUP: u8 = 0x3e;
const MODEL: u8 = 0x04;
/// The bulk dumps' header (p.286).
const HEADER: &[u8; 8] = b"LM  8B34";
/// A scene dump's id and title bytes before its data (p.286): two id bytes and
/// the 8-character title (the id's two bytes are Sidecar's reading: zero).
const SCENE_TITLE: usize = 10;
/// A library or current-settings dump's title (pp.287-288).
const PROGRAM_TITLE: usize = 12;

/// The scenes: 0 the read-only initial data, 1-99.
pub const MAX_SCENE: u8 = 99;
/// MIDI's number of the edit buffer in a scene dump and request (p.286).
const EDIT_BUFFER: u8 = 127;
/// A function call's channel byte when it names none (p.283).
const NO_CHANNEL: u8 = 0x7f;

/// The switches of MIDI SETUP (p.224), by their position in `Setup::values`.
pub const SETUP_VALUES: usize = 15;

/// MIDI SETUP. Its initial values are the ones p.224 pictures.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Setup {
    pub tx_channel: u8,
    pub rx_channel: u8,
    pub pc_tx: bool,
    pub pc_rx: bool,
    pub pc_omni: bool,
    pub pc_echo: bool,
    pub cc_tx: bool,
    pub cc_rx: bool,
    pub cc_omni: bool,
    pub cc_echo: bool,
    pub param_tx: bool,
    pub param_rx: bool,
    pub param_echo: bool,
    pub bulk_rx: bool,
    /// TRANSMIT INTERVAL: the bulk blocks' spacing in tens of ms (p.233).
    pub interval: u8,
}

impl Default for Setup {
    fn default() -> Self {
        Setup {
            tx_channel: 0,
            rx_channel: 0,
            pc_tx: true,
            pc_rx: true,
            pc_omni: false,
            pc_echo: false,
            cc_tx: false,
            cc_rx: false,
            cc_omni: false,
            cc_echo: false,
            param_tx: false,
            param_rx: true,
            param_echo: false,
            bulk_rx: true,
            interval: 0,
        }
    }
}

impl Setup {
    /// As numbers, in MIDI SETUP's order (the channels 0-15, the switches 0 / 1, the interval).
    pub fn values(&self) -> [u8; SETUP_VALUES] {
        let s = self;
        [
            s.tx_channel,
            s.rx_channel,
            s.pc_tx as u8,
            s.pc_rx as u8,
            s.pc_omni as u8,
            s.pc_echo as u8,
            s.cc_tx as u8,
            s.cc_rx as u8,
            s.cc_omni as u8,
            s.cc_echo as u8,
            s.param_tx as u8,
            s.param_rx as u8,
            s.param_echo as u8,
            s.bulk_rx as u8,
            s.interval,
        ]
    }

    pub fn set_value(&mut self, i: usize, v: u8) {
        let on = v != 0;
        match i {
            0 => self.tx_channel = v & 0xf,
            1 => self.rx_channel = v & 0xf,
            2 => self.pc_tx = on,
            3 => self.pc_rx = on,
            4 => self.pc_omni = on,
            5 => self.pc_echo = on,
            6 => self.cc_tx = on,
            7 => self.cc_rx = on,
            8 => self.cc_omni = on,
            9 => self.cc_echo = on,
            10 => self.param_tx = on,
            11 => self.param_rx = on,
            12 => self.param_echo = on,
            13 => self.bulk_rx = on,
            14 => self.interval = v.min(127),
            _ => {}
        }
    }
}

/// The setup dump's 128 bytes: `Setup::values`, then zeros (Sidecar's layout).
pub fn setup_bytes(s: &Setup) -> Vec<u8> {
    let mut out = s.values().to_vec();
    out.resize(128, 0);
    out
}

fn setup_of_bytes(d: &[u8]) -> Setup {
    let mut s = Setup::default();
    for (i, &v) in d.iter().enumerate().take(SETUP_VALUES) {
        s.set_value(i, v);
    }
    s
}

/// What a control change moves (the table's first two columns, p.229).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CcTarget {
    /// A strip's fader (FADER CHANNEL, FADER MASTER).
    Fader(usize),
    /// A strip's ON.
    On(usize),
    /// A strip's pan (a stereo strip's L or R, the side), STEREO's balance.
    Pan(usize, usize),
    /// A send to AUX 1-4 / EFFECT 1-2 (0-5) of an input.
    Send(usize, usize),
}

impl CcTarget {
    /// As the table's three bytes (Sidecar's layout: the kind 1 fader, 2 ON,
    /// 3 pan, 4 send, 0 NO ASSIGN; the strip; the side or the send).
    pub fn to_bytes(t: Option<CcTarget>) -> [u8; 3] {
        match t {
            None => [0, 0, 0],
            Some(CcTarget::Fader(s)) => [1, s as u8, 0],
            Some(CcTarget::On(s)) => [2, s as u8, 0],
            Some(CcTarget::Pan(s, side)) => [3, s as u8, side as u8],
            Some(CcTarget::Send(s, n)) => [4, s as u8, n as u8],
        }
    }

    pub fn of_bytes(b: [u8; 3]) -> Option<CcTarget> {
        let (s, x) = (b[1] as usize, b[2] as usize);
        if s > STEREO {
            return None;
        }
        Some(match b[0] {
            1 => CcTarget::Fader(s),
            2 => CcTarget::On(s),
            3 => CcTarget::Pan(s, x.min(1)),
            4 => CcTarget::Send(s, x.min(5)),
            _ => return None,
        })
    }

    /// The edit buffer's address (`params`) it sits at.
    fn address(self) -> Option<usize> {
        Some(match self {
            CcTarget::Fader(s) => params::common(s, 0),
            CcTarget::On(s) => params::common(s, 1),
            CcTarget::Pan(s, side) => params::common(s, 2 + side.min(1)),
            CcTarget::Send(s, n) => params::send_level_address(s, n)?,
        })
    }

    /// How many steps its parameter has (faders and sends 128, a pan 33, ON 2).
    fn steps(self) -> u32 {
        match self {
            CcTarget::Fader(_) | CcTarget::Send(..) => 128,
            CcTarget::Pan(..) => 33,
            CcTarget::On(_) => 2,
        }
    }

    /// The table's three columns as MIDI page 3 prints them.
    pub fn describe(t: Option<CcTarget>) -> [String; 3] {
        let channel_or_master = |s: usize| if s < BUSES { "CHANNEL" } else { "MASTER" };
        let Some(t) = t else { return ["NO ASSIGN".into(), String::new(), String::new()] };
        match t {
            CcTarget::Fader(s) => ["FADER".into(), channel_or_master(s).into(), params::strip_name(s)],
            CcTarget::On(s) => ["ON".into(), channel_or_master(s).into(), params::strip_name(s)],
            CcTarget::Pan(STEREO, _) => ["PAN".into(), "BALANCE".into(), params::strip_name(STEREO)],
            CcTarget::Pan(s, side) if is_stereo_input(s) && s >= RETURNS => {
                ["PAN".into(), "CHANNEL".into(), format!("{} {}", params::strip_name(s), if side == 0 { "L" } else { "R" })]
            }
            CcTarget::Pan(s, side) if is_stereo_input(s) => {
                let first = 13 + 2 * (s - params::STEREO_CH) + side;
                ["PAN".into(), "CHANNEL".into(), format!("CH{first}")]
            }
            CcTarget::Pan(s, _) => ["PAN".into(), "CHANNEL".into(), params::strip_name(s)],
            CcTarget::Send(s, n) => {
                let bus = if n < 4 { format!("AUX{} SEND", n + 1) } else { format!("EFF{} SEND", n - 3) };
                ["FADER".into(), bus, params::strip_name(s)]
            }
        }
    }
}

/// The manual's mapping of a control change's value onto a parameter of
/// `steps` steps (p.280: C=128, S=steps, X=C/S, remainder Y, Z=INT((Y+1)/2)).
pub fn cc_to_step(data: u8, steps: u32) -> u32 {
    let (x, y) = (128 / steps, 128 % steps);
    let z = y.div_ceil(2);
    let data = data as u32;
    if data < z {
        0
    } else {
        ((data - z) / x).min(steps - 1)
    }
}

/// What a parameter at `step` sends: the middle of what comes back as it (the
/// reverse of `cc_to_step`, Sidecar's reading: the manual gives only that way).
pub fn step_to_cc(step: u32, steps: u32) -> u8 {
    let (x, y) = (128 / steps, 128 % steps);
    let z = y.div_ceil(2);
    (z + step.min(steps - 1) * x) as u8
}

/// The assignable control changes: 0-95 and 102-119 (p.229).
fn assignable() -> impl Iterator<Item = usize> {
    (0..120).filter(|cc| !(96..102).contains(cc))
}

/// MIDI page 3's three arrangements (pp.274-279).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Arrangement {
    Default,
    ProMix,
    O3d,
}

/// The assign table: controllers 0-95 and 102-119.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CcMap {
    pub map: [Option<CcTarget>; 128],
}

impl Default for CcMap {
    fn default() -> Self {
        CcMap::of(Arrangement::Default)
    }
}

impl CcMap {
    /// INITIALIZE (p.229): the table of an arrangement.
    pub fn of(a: Arrangement) -> Self {
        use CcTarget::*;
        let mut m = [None; 128];
        let (ch1, ch13, rtn, aux, bus, eff) = (0usize, params::STEREO_CH, RETURNS, params::AUXES, BUSES, EFFECTS);
        let opt = OPTION_CH;
        let mut range = |from: usize, n: usize, f: &dyn Fn(usize) -> CcTarget| {
            for k in 0..n {
                m[from + k] = Some(f(k));
            }
        };
        match a {
            // pp.274-276: the channel fader in CC 1-16, the masters, the ON keys, the pans, the effect sends.
            Arrangement::Default => {
                range(1, 12, &|k| Fader(ch1 + k));
                range(13, 2, &|k| Fader(ch13 + k));
                range(15, 2, &|k| Fader(rtn + k));
                range(17, 4, &|k| Fader(aux + k));
                range(21, 4, &|k| Fader(bus + k));
                range(25, 2, &|k| Fader(eff + k));
                range(27, 1, &|_| Fader(STEREO));
                range(28, 4, &|k| On(ch1 + k));
                range(33, 8, &|k| On(ch1 + 4 + k));
                range(41, 2, &|k| On(ch13 + k));
                range(43, 2, &|k| On(rtn + k));
                range(45, 4, &|k| On(aux + k));
                range(49, 4, &|k| On(bus + k));
                range(53, 2, &|k| On(eff + k));
                range(55, 1, &|_| On(STEREO));
                range(56, 12, &|k| Pan(ch1 + k, 0));
                range(68, 2, &|k| Pan(ch13, k));
                range(70, 2, &|k| Pan(ch13 + 1, k));
                range(72, 2, &|k| Pan(rtn, k));
                range(74, 2, &|k| Pan(rtn + 1, k));
                range(76, 1, &|_| Pan(STEREO, 0));
                range(77, 12, &|k| Send(ch1 + k, 4));
                range(89, 2, &|k| Send(ch13 + k, 4));
                range(91, 5, &|k| Send(ch1 + k, 5));
                range(102, 7, &|k| Send(ch1 + 5 + k, 5));
                range(109, 2, &|k| Send(ch13 + k, 5));
                range(111, 8, &|k| Fader(opt + k));
            }
            // Programmable Mixer 01's (pp.277-279).
            Arrangement::ProMix => {
                range(0, 12, &|k| Fader(ch1 + k));
                range(12, 4, &|k| Fader(opt + k));
                range(16, 1, &|_| Fader(ch13));
                range(17, 2, &|k| Fader(rtn + k));
                range(19, 2, &|k| Fader(aux + k));
                range(21, 1, &|_| Fader(STEREO));
                range(22, 12, &|k| On(ch1 + k));
                range(34, 4, &|k| On(opt + k));
                range(38, 1, &|_| On(ch13));
                range(39, 2, &|k| On(rtn + k));
                range(41, 2, &|k| On(aux + k));
                range(43, 1, &|_| On(STEREO));
                range(44, 12, &|k| Pan(ch1 + k, 0));
                range(56, 4, &|k| Pan(opt + k, 0));
                range(60, 2, &|k| Pan(ch13, k));
                range(62, 2, &|k| Pan(rtn, k));
                range(64, 2, &|k| Pan(rtn + 1, k));
                range(67, 1, &|_| Pan(STEREO, 0));
                range(68, 12, &|k| Send(ch1 + k, 4));
                range(80, 4, &|k| Send(opt + k, 4));
                range(84, 1, &|_| Send(ch13, 4));
                range(85, 11, &|k| Send(ch1 + k, 5));
                range(102, 12, &|k| Send(ch1 + k, 0));
                range(114, 4, &|k| Send(opt + k, 0));
                range(118, 1, &|_| Send(ch13, 0));
            }
            // 03D's (pp.277-279).
            Arrangement::O3d => {
                range(1, 12, &|k| Fader(ch1 + k));
                range(17, 8, &|k| Fader(opt + k));
                range(25, 1, &|_| Fader(ch13));
                range(26, 2, &|k| Fader(rtn + k));
                range(28, 4, &|k| Fader(aux + k));
                range(33, 4, &|k| Fader(bus + k));
                range(37, 1, &|_| Fader(STEREO));
                range(38, 12, &|k| Pan(ch1 + k, 0));
                range(54, 8, &|k| Pan(opt + k, 0));
                range(62, 2, &|k| Pan(ch13, k));
                range(64, 12, &|k| On(ch1 + k));
                range(80, 8, &|k| On(opt + k));
                range(88, 1, &|_| On(ch13));
                range(89, 2, &|k| On(rtn + k));
                range(91, 4, &|k| On(aux + k));
                range(95, 1, &|_| On(STEREO));
                range(102, 12, &|k| Send(ch1 + k, 4));
                range(118, 1, &|_| Send(ch13, 4));
                range(119, 1, &|_| Pan(STEREO, 0));
            }
        }
        CcMap { map: m }
    }

    /// The table's bytes (114 x 3, 7 bit: p.289).
    fn bytes(&self) -> Vec<u8> {
        assignable().flat_map(|cc| CcTarget::to_bytes(self.map[cc])).collect()
    }

    fn take_bytes(&mut self, data: &[u8]) {
        for (cc, b) in assignable().zip(data.chunks_exact(3)) {
            self.map[cc] = CcTarget::of_bytes([b[0], b[1], b[2]]);
        }
    }
}

/// The program change table: program 0-127 (MIDI's numbering) to scene 0-99, or none.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PcTable {
    pub scenes: [Option<u8>; 128],
}

impl Default for PcTable {
    /// Programs #1-99 recall scenes 1-99, #100 scene 00 (p.273).
    fn default() -> Self {
        let mut scenes = [None; 128];
        for (p, s) in scenes.iter_mut().enumerate().take(99) {
            *s = Some(p as u8 + 1);
        }
        scenes[99] = Some(0);
        PcTable { scenes }
    }
}

impl PcTable {
    /// The lowest program that recalls `scene` (p.227).
    pub fn program_of(&self, scene: u8) -> Option<u8> {
        self.scenes.iter().position(|s| *s == Some(scene)).map(|p| p as u8)
    }

    fn bytes(&self) -> Vec<u8> {
        self.scenes.iter().map(|s| s.unwrap_or(0x7f)).collect()
    }

    fn take_bytes(&mut self, data: &[u8]) {
        for (s, &d) in self.scenes.iter_mut().zip(data) {
            *s = (d <= MAX_SCENE).then_some(d);
        }
    }
}

/// What the app does on the console's behalf.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Event {
    /// Recall scene 1-99 (the protocol recalls scene 00 itself).
    RecallScene(u8),
    /// Scene 00, the initial data, is about to load: the app marks the undo.
    RecallInitial,
    /// Store the console into scene 1-99.
    StoreScene(u8),
    /// Bulk: scene `n` (1-99) requested; answered with `scene_dump`.
    SceneRequest(u8),
    /// A dump of scene 1-99 to store: the title and the edit buffer's bytes.
    SceneDump(u8, String, Vec<u8>),
    /// A scene's title (1-99).
    SetTitle(u8, String),
    /// A scene's title requested (0-99, 127 the edit buffer); answered with `title`.
    TitleRequest(u8),
    /// An effects library recall (`program` 0-98) into EFFECT 1 or 2 (0-1).
    RecallEffect(u8, u8),
    /// A key the protocol doesn't handle: its key number and bit (SEL, the function keys).
    Key(u8, u8),
}

/// The formats a dump can have: its 7-bit bytes first, then its nibble-pair
/// bytes (pp.286-289). Library programs (Q, Y, E 0-98) aren't answered: Sidecar's
/// 01V has no user library yet; their current-settings forms are.
fn dump_shape(kind: u8) -> Option<(usize, usize)> {
    Some(match kind {
        b'M' => (SCENE_TITLE, SIZE),
        b'S' => (0, 128),
        b'P' => (128, 0),
        b'C' => (342, 0),
        b'Q' => (PROGRAM_TITLE, 12),
        b'Y' => (PROGRAM_TITLE, 7),
        b'E' => (PROGRAM_TITLE, 41),
        _ => return None,
    })
}

fn numbered(kind: u8) -> bool {
    matches!(kind, b'M' | b'Q' | b'Y' | b'E')
}

/// A bulk dump of `kind` on `channel`: its number where it has one, the
/// 7-bit bytes as they are and the rest as nibble pairs.
pub fn dump(channel: u8, kind: u8, number: u8, seven: &[u8], data: &[u8]) -> Vec<u8> {
    let mut body = HEADER.to_vec();
    body.push(kind);
    body.push(if numbered(kind) { number & 0x7f } else { b' ' });
    body.extend_from_slice(seven);
    for &d in data {
        body.push(d >> 4);
        body.push(d & 0xf);
    }
    frame_dump(channel, &body)
}

/// A bulk request of `kind`.
pub fn request(channel: u8, kind: u8, number: u8) -> Vec<u8> {
    let mut out = vec![0xf0, YAMAHA, 0x20 | (channel & 0xf), 0x7e];
    out.extend_from_slice(HEADER);
    out.push(kind);
    out.push(if numbered(kind) { number } else { b' ' });
    out.push(0xf7);
    out
}

/// A parsed dump: channel, kind, number, its 7-bit bytes, its data.
pub type ParsedDump = (u8, u8, u8, Vec<u8>, Vec<u8>);

/// A dump's parts, if well-formed.
pub fn parse_dump(msg: &[u8]) -> Option<ParsedDump> {
    let (channel, body) = parse_frame(msg, HEADER)?;
    let kind = *body.get(8)?;
    let (seven_len, data_len) = dump_shape(kind)?;
    let seven = body.get(10..10 + seven_len)?.to_vec();
    let nibbles = body.get(10 + seven_len..)?;
    if nibbles.len() != data_len * 2 || nibbles.iter().any(|&n| n > 0xf) || seven.iter().any(|&b| b > 0x7f) {
        return None;
    }
    Some((channel, kind, body[9], seven, nibbles.chunks(2).map(|p| p[0] << 4 | p[1]).collect()))
}

/// A bulk request's (channel, kind, number).
pub fn parse_request(msg: &[u8]) -> Option<(u8, u8, u8)> {
    if msg.len() < 15 || msg[0] != 0xf0 || msg[1] != YAMAHA || msg[2] & 0xf0 != 0x20 || msg[3] != 0x7e || &msg[4..12] != HEADER {
        return None;
    }
    Some((msg[2] & 0xf, msg[12], msg[13]))
}

/// A parameter change message of type `tt` carrying `data`.
fn param_message(channel: u8, tt: u8, data: &[u8]) -> Vec<u8> {
    let mut out = vec![0xf0, YAMAHA, 0x10 | (channel & 0xf), GROUP, MODEL, tt];
    out.extend_from_slice(data);
    out.push(0xf7);
    out
}

/// A byte-operation parameter change of address `a` to `v` (type 0, p.281).
pub fn param_change(channel: u8, a: usize, v: u8) -> Vec<u8> {
    param_message(channel, 0x00, &[(a >> 7) as u8 & 0x7f, a as u8 & 0x7f, v >> 4, v & 0xf])
}

/// The current EQ / dynamics dump's channel numbers (pp.287-288): what strip
/// `m - 96` is. EQ: CH 1-12, 13/14, 15/16, RTN 1-2, AUX 1-4, ST, CH 17-24.
fn eq_channel(m: u8) -> Option<usize> {
    Some(match m.checked_sub(96)? as usize {
        k @ 0..=11 => k,
        k @ 12..=13 => params::STEREO_CH + k - 12,
        k @ 14..=15 => RETURNS + k - 14,
        k @ 16..=19 => params::AUXES + k - 16,
        20 => STEREO,
        k @ 21..=28 => OPTION_CH + k - 21,
        _ => return None,
    })
}

/// Dynamics: CH 1-12, 13/14, 15/16, AUX 1-4, ST.
fn dyn_channel(m: u8) -> Option<usize> {
    Some(match m.checked_sub(96)? as usize {
        k @ 0..=11 => k,
        k @ 12..=13 => params::STEREO_CH + k - 12,
        k @ 14..=17 => params::AUXES + k - 14,
        18 => STEREO,
        _ => return None,
    })
}

/// The key remote's keys (p.284): the strip of an ON or SOLO key (key number
/// and bit): `Some((strip, on_key))`.
fn key_strip(key: u8, bit: u8) -> Option<(usize, bool)> {
    let b = bit as usize;
    Some(match (key, b) {
        (0, 0..=7) => (b, true),
        (1, 0..=3) => (8 + b, true),
        (1, 4 | 5) => (params::STEREO_CH + b - 4, true),
        (1, 6) => (STEREO, true),
        (2, 0..=7) => (b, false),
        (3, 0..=3) => (8 + b, false),
        (3, 4 | 5) => (params::STEREO_CH + b - 4, false),
        (6, 0 | 1) => (RETURNS + b, true),
        (6, 2 | 3) => (RETURNS + b - 2, false),
        // The LCD's solo keys.
        (100, 0..=7) => (b, false),
        (101, 0..=3) => (8 + b, false),
        (101, 4 | 5) => (params::STEREO_CH + b - 4, false),
        (101, 6 | 7) => (RETURNS + b - 6, false),
        (102, 0..=7) => (OPTION_CH + b, false),
        _ => return None,
    })
}

/// A key's number and bit, for the solo key of `strip` (what p.233 sends when
/// a channel is soloed), if it has one on the panel.
fn solo_key(strip: usize) -> Option<(u8, u8)> {
    Some(match strip {
        s if s < 8 => (2, s as u8),
        s if s < params::STEREO_CH => (3, (s - 8) as u8),
        s if s < OPTION_CH => (3, (s - params::STEREO_CH + 4) as u8),
        s if s < RETURNS => (102, (s - OPTION_CH) as u8),
        s if s < BUSES => (6, (s - RETURNS + 2) as u8),
        _ => return None,
    })
}

/// A key remote message: `key` number, bit `bit`, pressed.
pub fn key_message(channel: u8, key: u8, bit: u8) -> Vec<u8> {
    param_message(channel, 0x43, &[0, key & 0x7f, 8 | (bit & 7)])
}

/// The 01V's MIDI: its setup and tables, the edit buffer as last sent, and
/// what waits to go out.
pub struct Midi {
    pub setup: Setup,
    pub cc_map: CcMap,
    pub pc_table: PcTable,
    last: Vec<u8>,
    last_solo: [bool; params::BUSES],
    primed: bool,
    out: VecDeque<Vec<u8>>,
    events: VecDeque<Event>,
}

impl Default for Midi {
    fn default() -> Self {
        Midi {
            setup: Setup::default(),
            cc_map: CcMap::default(),
            pc_table: PcTable::default(),
            last: vec![0; SIZE],
            last_solo: [false; params::BUSES],
            primed: false,
            out: VecDeque::new(),
            events: VecDeque::new(),
        }
    }
}

impl Midi {
    /// The next message to send.
    pub fn take_out(&mut self) -> Option<Vec<u8>> {
        self.out.pop_front()
    }

    pub fn push_out(&mut self, msg: Vec<u8>) {
        self.out.push_back(msg);
    }

    /// The next thing for the app to do.
    pub fn take_event(&mut self) -> Option<Event> {
        self.events.pop_front()
    }

    /// Handles one received message (a channel message or a whole SysEx).
    pub fn receive(&mut self, c: &Engine, msg: &[u8]) {
        let Some(&status) = msg.first() else { return };
        match status & 0xf0 {
            0xf0 if status == 0xf0 => self.receive_sysex(c, msg),
            0xc0 if msg.len() >= 2 => {
                if self.setup.pc_echo {
                    self.out.push_back(msg.to_vec());
                }
                if self.setup.pc_rx && (self.setup.pc_omni || status & 0xf == self.setup.rx_channel) {
                    if let Some(scene) = self.pc_table.scenes[(msg[1] & 0x7f) as usize] {
                        self.recall(scene);
                    }
                }
            }
            0xb0 if msg.len() >= 3 => {
                if self.setup.cc_echo {
                    self.out.push_back(msg.to_vec());
                }
                if self.setup.cc_rx && (self.setup.cc_omni || status & 0xf == self.setup.rx_channel) {
                    if let Some(t) = self.cc_map.map[(msg[1] & 0x7f) as usize] {
                        self.receive_cc(c, t, msg[2] & 0x7f);
                        self.remember(c);
                    }
                }
            }
            _ => {}
        }
    }

    /// The one recalled scene: 00 is the initial data, here, the rest the show's.
    fn recall(&mut self, scene: u8) {
        self.events.push_back(if scene == 0 { Event::RecallInitial } else { Event::RecallScene(scene) });
    }

    /// Loads scene 00, the initial data (the app has taken its undo first).
    pub fn recall_initial(&mut self, c: &Engine) {
        params::load(c, params::initial_bytes());
        self.remember(c);
    }

    fn receive_cc(&mut self, c: &Engine, t: CcTarget, v: u8) {
        let Some(a) = t.address() else { return };
        params::set(c, a, cc_to_step(v, t.steps()) as u8);
    }

    /// Takes the edit buffer as it is (a received change, or a scene recalled
    /// for a message, isn't sent back).
    pub fn remember(&mut self, c: &Engine) {
        if self.primed {
            self.last = params::bytes(c);
            self.last_solo = solo_state(c);
        }
    }

    fn receive_sysex(&mut self, c: &Engine, msg: &[u8]) {
        if let Some((ch, kind, number)) = parse_request(msg) {
            if ch == self.setup.rx_channel && self.setup.bulk_rx {
                self.answer_request(c, kind, number);
            }
            return;
        }
        if let Some((ch, kind, number, seven, data)) = parse_dump(msg) {
            if ch == self.setup.rx_channel && self.setup.bulk_rx {
                self.take_dump(c, kind, number, seven, data);
            }
            return;
        }
        if msg.len() < 7 || msg[1] != YAMAHA || msg[3] != GROUP || msg[4] != MODEL || *msg.last().unwrap_or(&0) != 0xf7 {
            return;
        }
        let d = &msg[5..msg.len() - 1];
        let (mode, channel) = (msg[2] & 0xf0, msg[2] & 0xf);
        let ours = channel == self.setup.rx_channel;
        match mode {
            0x10 => {
                if self.setup.param_echo {
                    self.out.push_back(msg.to_vec());
                }
                if self.setup.param_rx && ours {
                    self.parameter_change(c, d);
                }
            }
            0x30 => {
                if self.setup.param_echo && !ours {
                    self.out.push_back(msg.to_vec());
                }
                if self.setup.param_rx && ours {
                    self.parameter_request(c, d, channel);
                }
            }
            _ => {}
        }
    }

    /// A parameter change's bytes after the header (`tt` first, `F7` gone).
    fn parameter_change(&mut self, c: &Engine, d: &[u8]) {
        let Some((&tt, data)) = d.split_first() else { return };
        match tt {
            // A byte operation: an address and nibble pairs for it and the next ones.
            0x00 if data.len() >= 4 => {
                let a = addr(data);
                for (k, pair) in data[2..].chunks_exact(2).enumerate() {
                    if a + k < SIZE {
                        params::set(c, a + k, pair[0] << 4 | (pair[1] & 0xf));
                    }
                }
                self.remember(c);
            }
            // A 7-bit operation.
            0x10 if data.len() >= 3 => {
                let a = addr(data);
                for (k, &v) in data[2..].iter().enumerate() {
                    if a + k < SIZE {
                        params::set(c, a + k, v & 0x7f);
                    }
                }
                self.remember(c);
            }
            // A bit operation (bits 0-2 the bit, bit 3 set or reset), address and data repeated.
            0x40 => {
                for t in data.chunks_exact(3) {
                    let a = addr(t);
                    if a < SIZE {
                        let bit = t[2] & 7;
                        let byte = params::get(c, a);
                        let v = if t[2] & 8 != 0 { byte | 1 << bit } else { byte & !(1 << bit) };
                        params::set(c, a, v);
                    }
                }
                self.remember(c);
            }
            // The system memory: the setup (p.282).
            0x01 if data.len() >= 4 => {
                let a = addr(data);
                for (k, pair) in data[2..].chunks_exact(2).enumerate() {
                    self.setup.set_value(a + k, pair[0] << 4 | (pair[1] & 0xf));
                }
            }
            0x41 => {
                for t in data.chunks_exact(3) {
                    let a = addr(t);
                    if a < SETUP_VALUES {
                        let bit = t[2] & 7;
                        let byte = self.setup.values()[a];
                        self.setup.set_value(a, if t[2] & 8 != 0 { byte | 1 << bit } else { byte & !(1 << bit) });
                    }
                }
            }
            0x02 if data.len() >= 2 => self.function_call(data),
            0x43 if data.len() >= 3 && data[0] == 0 => self.key_remote(c, data),
            _ => {}
        }
    }

    /// Type 2 (p.283): recall, store, titles.
    fn function_call(&mut self, d: &[u8]) {
        let (function, number) = (d[0], d[1]);
        let channel = d.get(2).copied().unwrap_or(NO_CHANNEL);
        match function {
            0x00 if number <= MAX_SCENE => self.recall(number),
            0x10 if (1..=MAX_SCENE).contains(&number) => self.events.push_back(Event::StoreScene(number)),
            0x03 if channel < 2 && number < 99 => self.events.push_back(Event::RecallEffect(number, channel)),
            0x30 if (1..=MAX_SCENE).contains(&number) => {
                let title: String = d[2..].iter().map(|&b| b as char).collect();
                self.events.push_back(Event::SetTitle(number, title.trim_end().to_string()));
            }
            // The EQ and dynamics libraries (0x01, 0x02, 0x11-0x13) aren't built
            // on Sidecar's 01V yet.
            _ => {}
        }
    }

    /// Type 0x43 (p.284): a key pressed (bit 3 set); released keys are ignored
    /// since a press is a press and release (the manual's note).
    fn key_remote(&mut self, c: &Engine, d: &[u8]) {
        for t in d.chunks_exact(3).filter(|t| t[0] == 0) {
            let (key, data) = (t[1], t[2]);
            if data & 8 == 0 {
                continue;
            }
            let bit = data & 7;
            match key_strip(key, bit) {
                Some((s, true)) => {
                    let a = params::common(s, 1);
                    params::set(c, a, 1 - params::get(c, a));
                }
                Some((s, false)) => {
                    if let Some(st) = strip_of(c, s) {
                        st.params.solo.fetch_xor(true, Relaxed);
                    }
                }
                None => self.events.push_back(Event::Key(key, bit)),
            }
        }
        self.remember(c);
    }

    /// A `3n` request's bytes after the header.
    fn parameter_request(&mut self, c: &Engine, d: &[u8], channel: u8) {
        let Some((&tt, data)) = d.split_first() else { return };
        match tt {
            0x00 if data.len() >= 3 => {
                let a = addr(data);
                for k in a..(a + data[2].max(1) as usize).min(SIZE) {
                    self.out.push_back(param_change(channel, k, params::get(c, k)));
                }
            }
            0x01 if data.len() >= 3 => {
                let a = addr(data);
                let bytes = setup_bytes(&self.setup);
                for (k, &v) in bytes.iter().enumerate().skip(a).take(data[2].max(1) as usize) {
                    self.out.push_back(param_message(channel, 0x01, &[(k >> 7) as u8, k as u8 & 0x7f, v >> 4, v & 0xf]));
                }
            }
            0x02 if data.len() >= 2 && data[0] == 0x30 => self.events.push_back(Event::TitleRequest(data[1])),
            _ => {}
        }
    }

    fn answer_request(&mut self, c: &Engine, kind: u8, number: u8) {
        let ch = self.setup.rx_channel;
        self.send(c, ch, kind, number);
    }

    /// A dump of `kind`/`number` queued on `channel` (a scene's goes through the app).
    fn send(&mut self, c: &Engine, channel: u8, kind: u8, number: u8) {
        let msg = match kind {
            b'M' if number == EDIT_BUFFER => dump(channel, b'M', EDIT_BUFFER, &scene_title(""), &params::bytes(c)),
            b'M' if number <= MAX_SCENE => {
                self.events.push_back(Event::SceneRequest(number));
                return;
            }
            b'S' => dump(channel, b'S', 0, &[], &setup_bytes(&self.setup)),
            b'P' => dump(channel, b'P', 0, &self.pc_table.bytes(), &[]),
            b'C' => dump(channel, b'C', 0, &self.cc_map.bytes(), &[]),
            b'Q' => {
                let Some(s) = eq_channel(number) else { return };
                let Some(a) = params::eq_address(s) else { return };
                let mut data: Vec<u8> = (a..a + params::eq_len(s)).map(|k| params::get(c, k)).collect();
                data.resize(12, 0);
                dump(channel, b'Q', number, &program_title(""), &data)
            }
            b'Y' => {
                let Some(a) = dyn_channel(number).and_then(params::dyn_address) else { return };
                dump(
                    channel,
                    b'Y',
                    number,
                    &program_title(""),
                    &(a..a + params::DYN_LEN).map(|k| params::get(c, k)).collect::<Vec<_>>(),
                )
            }
            b'E' if (112..=113).contains(&number) => {
                let a = params::fx_address((number - 112) as usize);
                dump(
                    channel,
                    b'E',
                    number,
                    &program_title(""),
                    &(a..a + params::FX_LEN).map(|k| params::get(c, k)).collect::<Vec<_>>(),
                )
            }
            _ => return,
        };
        self.out.push_back(msg);
    }

    fn take_dump(&mut self, c: &Engine, kind: u8, number: u8, seven: Vec<u8>, data: Vec<u8>) {
        match kind {
            b'M' if number == EDIT_BUFFER => {
                params::load(c, &data);
                self.remember(c);
            }
            b'M' if (1..=MAX_SCENE).contains(&number) => {
                let title: String = seven[2..].iter().map(|&b| b as char).collect();
                self.events.push_back(Event::SceneDump(number, title.trim_end().to_string(), data));
            }
            b'S' => self.setup = setup_of_bytes(&data),
            b'P' => self.pc_table.take_bytes(&seven),
            b'C' => self.cc_map.take_bytes(&seven),
            b'Q' => {
                if let Some(s) = eq_channel(number) {
                    if let Some(a) = params::eq_address(s) {
                        for (k, &v) in data.iter().take(params::eq_len(s)).enumerate() {
                            params::set(c, a + k, v);
                        }
                        self.remember(c);
                    }
                }
            }
            b'Y' => {
                if let Some(a) = dyn_channel(number).and_then(params::dyn_address) {
                    for (k, &v) in data.iter().enumerate() {
                        params::set(c, a + k, v);
                    }
                    self.remember(c);
                }
            }
            b'E' if (112..=113).contains(&number) => {
                let a = params::fx_address((number - 112) as usize);
                for (k, &v) in data.iter().enumerate() {
                    params::set(c, a + k, v);
                }
                self.remember(c);
            }
            _ => {}
        }
    }

    /// Sends the console's changes since the last poll: a control change for
    /// what the table assigns (with Tx on), a parameter change for the rest
    /// (p.281, with its Tx on), and a key remote for a solo (p.233). The
    /// first poll takes the values and sends nothing.
    pub fn poll(&mut self, c: &Engine) {
        let now = params::bytes(c);
        let solo = solo_state(c);
        if self.primed {
            let channel = self.setup.tx_channel;
            let mut assigned = vec![false; SIZE];
            for cc in assignable() {
                let Some(t) = self.cc_map.map[cc] else { continue };
                let Some(a) = t.address() else { continue };
                // A parameter on two controllers goes out on the lowest.
                if !std::mem::replace(&mut assigned[a], true) && self.setup.cc_tx && now[a] != self.last[a] {
                    self.out.push_back(vec![0xb0 | channel, cc as u8, step_to_cc(now[a] as u32, t.steps())]);
                }
            }
            if self.setup.param_tx {
                for a in 0..SIZE {
                    if !assigned[a] && now[a] != self.last[a] {
                        self.out.push_back(param_change(channel, a, now[a]));
                    }
                }
                for (s, (&on, &was)) in solo.iter().zip(&self.last_solo).enumerate() {
                    if on != was {
                        if let Some((key, bit)) = solo_key(s) {
                            self.out.push_back(key_message(channel, key, bit));
                        }
                    }
                }
            }
        }
        self.last = now;
        self.last_solo = solo;
        self.primed = true;
    }

    /// Scene `scene` (0-99) was recalled on the console: its program change,
    /// or a function call if the table has none for it (p.227). The console's
    /// new values are the scene's, not edits to send.
    pub fn scene_recalled(&mut self, c: &Engine, scene: u8) {
        if let Some(p) = self.pc_table.program_of(scene) {
            if self.setup.pc_tx {
                self.out.push_back(vec![0xc0 | self.setup.tx_channel, p]);
            }
        } else if self.setup.param_tx {
            self.out.push_back(param_message(self.setup.tx_channel, 0x02, &[0x00, scene, NO_CHANNEL]));
        }
        self.remember(c);
    }

    /// A scene's dump (answering `Event::SceneRequest`), on the channel of its request (Rx).
    pub fn scene_dump(&mut self, scene: u8, title: &str, data: &[u8]) {
        self.out.push_back(dump(self.setup.rx_channel, b'M', scene, &scene_title(title), data));
    }

    /// A scene's title (answering `Event::TitleRequest`), on Rx CH (p.282).
    pub fn title(&mut self, number: u8, title: &str) {
        let mut d = vec![0x30, number];
        d.extend(title.bytes().map(seven_bit).take(8));
        self.out.push_back(param_message(self.setup.rx_channel, 0x02, &d));
    }

    /// MIDI page 4's TRANSMIT (on Tx CH; scenes through `Event::SceneRequest`).
    pub fn transmit(&mut self, c: &Engine, kind: u8, number: u8) {
        let ch = self.setup.tx_channel;
        self.send(c, ch, kind, number);
    }

    /// MIDI page 4's REQUEST: asks the other device for a dump (on Tx CH).
    pub fn send_request(&mut self, kind: u8, number: u8) {
        self.out.push_back(request(self.setup.tx_channel, kind, number));
    }
}

fn addr(d: &[u8]) -> usize {
    (d[0] as usize) << 7 | d[1] as usize
}

fn seven_bit(b: u8) -> u8 {
    if (0x20..0x7f).contains(&b) {
        b
    } else {
        b' '
    }
}

fn scene_title(title: &str) -> Vec<u8> {
    let mut t = vec![0, 0];
    t.extend(title.bytes().map(seven_bit).take(8));
    t.resize(SCENE_TITLE, b' ');
    t
}

fn program_title(title: &str) -> Vec<u8> {
    let mut t: Vec<u8> = title.bytes().map(seven_bit).take(PROGRAM_TITLE).collect();
    t.resize(PROGRAM_TITLE, b' ');
    t
}

fn strip_of(c: &Engine, s: usize) -> Option<std::sync::Arc<livedaw_engine::Strip>> {
    let (kind, n) = params::engine_ref(s)?;
    c.strip_at(kind, n)
}

fn solo_state(c: &Engine) -> [bool; params::BUSES] {
    let mut out = [false; params::BUSES];
    for (s, o) in out.iter_mut().enumerate() {
        *o = strip_of(c, s).is_some_and(|st| st.params.solo.load(Relaxed));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::config::{ConsoleConfig, Counts};
    use livedaw_engine::Profile;

    fn y01v() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::Yamaha01V, counts: Counts::Y01V });
        e
    }

    fn drain(m: &mut Midi) -> Vec<Vec<u8>> {
        std::iter::from_fn(|| m.take_out()).collect()
    }

    fn all_on() -> Midi {
        let mut m = Midi::default();
        m.setup.cc_tx = true;
        m.setup.cc_rx = true;
        m.setup.param_tx = true;
        m
    }

    #[test]
    fn dumps_frame_as_the_manual_says() {
        let d = dump(3, b'P', 0, &[1; 128], &[]);
        assert_eq!(&d[..4], &[0xf0, 0x43, 0x03, 0x7e]);
        assert_eq!(((d[4] as usize) << 7) | d[5] as usize, 138, "p.289's count");
        assert_eq!(&d[6..14], b"LM  8B34");
        assert_eq!(parse_dump(&d), Some((3, b'P', b' ', vec![1; 128], vec![])));
        let scene = dump(0, b'M', 5, &scene_title("SONG 1"), &[9; SIZE]);
        assert_eq!(((scene[4] as usize) << 7) | scene[5] as usize, 2048, "p.286's count");
        let (ch, kind, n, seven, data) = parse_dump(&scene).unwrap();
        assert_eq!((ch, kind, n, data.len()), (0, b'M', 5, SIZE));
        assert_eq!(&seven[2..8], b"SONG 1");
        let s = dump(0, b'S', 0, &[], &[0; 128]);
        assert_eq!(((s[4] as usize) << 7) | s[5] as usize, 266);
        let eq = dump(0, b'Q', 96, &program_title(""), &[0; 12]);
        assert_eq!(((eq[4] as usize) << 7) | eq[5] as usize, 46);
        let ef = dump(0, b'E', 112, &program_title(""), &[0; 41]);
        assert_eq!(((ef[4] as usize) << 7) | ef[5] as usize, 104);
        let mut bad = eq.clone();
        bad[20] ^= 1;
        assert_eq!(parse_dump(&bad), None, "the checksum");
        assert_eq!(parse_request(&request(2, b'M', 127)), Some((2, b'M', 127)));
    }

    #[test]
    fn the_manuals_value_equation() {
        // Faders: 128 steps, the value as it is.
        assert_eq!((cc_to_step(0, 128), cc_to_step(127, 128)), (0, 127));
        // A pan's 33 steps: X 3, Y 29, Z 15.
        assert_eq!((cc_to_step(0, 33), cc_to_step(14, 33), cc_to_step(15, 33), cc_to_step(18, 33)), (0, 0, 0, 1));
        assert_eq!(cc_to_step(127, 33), 32);
        // ON: two steps.
        assert_eq!((cc_to_step(63, 2), cc_to_step(64, 2)), (0, 1));
        for steps in [2, 33, 128] {
            for step in 0..steps {
                assert_eq!(cc_to_step(step_to_cc(step, steps), steps), step, "{steps}");
            }
        }
    }

    #[test]
    fn program_changes_recall_through_the_table_and_go_out() {
        let e = y01v();
        let mut m = Midi::default();
        m.poll(&e);
        m.receive(&e, &[0xc0, 4]);
        assert_eq!(m.take_event(), Some(Event::RecallScene(5)), "program change #5, MIDI's 4");
        m.receive(&e, &[0xc1, 4]);
        assert_eq!(m.take_event(), None, "another channel");
        m.setup.pc_omni = true;
        m.receive(&e, &[0xc1, 4]);
        assert_eq!(m.take_event(), Some(Event::RecallScene(5)), "OMNI");
        m.receive(&e, &[0xc0, 99]);
        assert_eq!(m.take_event(), Some(Event::RecallInitial), "program change #100: scene 00");
        m.pc_table.scenes[2] = Some(50);
        m.pc_table.scenes[9] = Some(50);
        m.scene_recalled(&e, 50);
        assert_eq!(drain(&mut m), vec![vec![0xc0, 2]], "the lowest");
        m.pc_table.scenes[2] = None;
        m.pc_table.scenes[9] = None;
        m.pc_table.scenes[49] = None;
        m.scene_recalled(&e, 50);
        assert!(drain(&mut m).is_empty(), "no program change, parameter change TX off");
        m.setup.param_tx = true;
        m.scene_recalled(&e, 50);
        assert_eq!(drain(&mut m), vec![vec![0xf0, 0x43, 0x10, 0x3e, 0x04, 0x02, 0x00, 50, 0x7f, 0xf7]], "a function call");
    }

    #[test]
    fn the_assign_table_arrangements_are_the_manuals() {
        let d = CcMap::default();
        assert_eq!(d.map[0], None);
        assert_eq!(d.map[1], Some(CcTarget::Fader(0)));
        assert_eq!(d.map[13], Some(CcTarget::Fader(12)));
        assert_eq!(d.map[27], Some(CcTarget::Fader(STEREO)));
        assert_eq!(d.map[32], None);
        assert_eq!(d.map[33], Some(CcTarget::On(4)), "CH5");
        assert_eq!(d.map[55], Some(CcTarget::On(STEREO)));
        assert_eq!(d.map[69], Some(CcTarget::Pan(12, 1)), "CH14");
        assert_eq!(d.map[76], Some(CcTarget::Pan(STEREO, 0)));
        assert_eq!(d.map[85], Some(CcTarget::Send(8, 4)), "p.230: CC 85 is CH9's effect 1 send");
        assert_eq!(d.map[102], Some(CcTarget::Send(5, 5)), "EFF2 CH6");
        assert_eq!(d.map[111], Some(CcTarget::Fader(14)), "CH17");
        assert_eq!(d.map[119], None);
        assert_eq!(assignable().count(), 114);

        let p = CcMap::of(Arrangement::ProMix);
        assert_eq!(p.map[0], Some(CcTarget::Fader(0)));
        assert_eq!(p.map[12], Some(CcTarget::Fader(14)), "CH17");
        assert_eq!(p.map[21], Some(CcTarget::Fader(STEREO)));
        assert_eq!(p.map[66], None);
        assert_eq!(p.map[67], Some(CcTarget::Pan(STEREO, 0)));
        assert_eq!(p.map[102], Some(CcTarget::Send(0, 0)), "AUX1 CH1");
        assert_eq!(p.map[119], None);

        let t = CcMap::of(Arrangement::O3d);
        assert_eq!(t.map[0], None);
        assert_eq!(t.map[17], Some(CcTarget::Fader(14)));
        assert_eq!(t.map[37], Some(CcTarget::Fader(STEREO)));
        assert_eq!(t.map[95], Some(CcTarget::On(STEREO)));
        assert_eq!(t.map[119], Some(CcTarget::Pan(STEREO, 0)), "FADER BALANCE ST OUT");
        assert_eq!(CcTarget::describe(d.map[1]), ["FADER".to_string(), "CHANNEL".into(), "CH1".into()]);
        assert_eq!(CcTarget::describe(d.map[69])[2], "CH14");
        assert_eq!(CcTarget::describe(d.map[73])[2], "RETURN1 R");
        assert_eq!(CcTarget::describe(None)[0], "NO ASSIGN");
    }

    #[test]
    fn control_changes_move_the_table_both_ways() {
        let e = y01v();
        let mut m = all_on();
        m.poll(&e);
        assert!(drain(&mut m).is_empty(), "nothing at first");
        m.receive(&e, &[0xb0, 3, 90]); // CC 3: CH3's fader
        assert_eq!(params::get(&e, params::common(2, 0)), 90);
        m.receive(&e, &[0xb0, 41, 0]); // CC 41: CH13/14 off
        assert_eq!(params::get(&e, params::common(12, 1)), 0);
        m.poll(&e);
        assert!(drain(&mut m).is_empty(), "received, not echoed");

        // A console change of an assigned parameter is a control change, on Tx CH.
        m.setup.tx_channel = 4;
        params::set(&e, params::common(0, 0), 77);
        m.poll(&e);
        assert_eq!(drain(&mut m), vec![vec![0xb4, 1, 77]]);
        // An unassigned one a parameter change.
        params::set(&e, params::eq_address(0).unwrap(), 11);
        m.poll(&e);
        assert_eq!(drain(&mut m), vec![param_change(4, params::eq_address(0).unwrap(), 11)]);
        // CC Rx off: nothing. Another channel: nothing.
        m.setup.cc_rx = false;
        m.receive(&e, &[0xb0, 3, 10]);
        assert_eq!(params::get(&e, params::common(2, 0)), 90);
        m.setup.cc_rx = true;
        m.receive(&e, &[0xb7, 3, 10]);
        assert_eq!(params::get(&e, params::common(2, 0)), 90);
        // ECHO passes it through.
        m.setup.cc_echo = true;
        m.receive(&e, &[0xb7, 3, 10]);
        assert_eq!(drain(&mut m), vec![vec![0xb7, 3, 10]]);
    }

    #[test]
    fn parameter_changes_by_byte_seven_bit_and_bit() {
        let e = y01v();
        let mut m = all_on();
        m.poll(&e);
        let a = params::common(3, 0);
        // Type 0: nibbles, continuing at the next address.
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x00, (a >> 7) as u8, a as u8, 5, 0, 0, 1, 0xf7]);
        assert_eq!((params::get(&e, a), params::get(&e, a + 1)), (80, 1));
        // Type 0x10: 7 bit.
        let pan = params::common(3, 2);
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x10, (pan >> 7) as u8, pan as u8, 8, 0xf7]);
        assert_eq!(params::get(&e, pan), 8);
        // Type 0x40: a bit, set then reset.
        let sw = params::common(3, 5);
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x40, (sw >> 7) as u8, sw as u8, 8, 0xf7]);
        assert_eq!(params::get(&e, sw) & 1, 1, "phase");
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x40, (sw >> 7) as u8, sw as u8, 0, 0xf7]);
        assert_eq!(params::get(&e, sw) & 1, 0);
        // Its own model id only.
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3d, 0x00, 0x00, 0, (a as u8) & 0x7f, 7, 0, 0xf7]);
        m.receive(&e, &[0xf0, 0x43, 0x11, 0x3e, 0x04, 0x00, (a >> 7) as u8, a as u8, 7, 0, 0xf7]);
        assert_eq!(params::get(&e, a), 80, "02R's model, and another channel");
        m.poll(&e);
        assert!(drain(&mut m).is_empty(), "not sent back");
        // A request of two bytes, answered on its channel even with Tx off.
        m.setup.param_tx = false;
        m.receive(&e, &[0xf0, 0x43, 0x30, 0x3e, 0x04, 0x00, (a >> 7) as u8, a as u8, 2, 0xf7]);
        assert_eq!(drain(&mut m), vec![param_change(0, a, 80), param_change(0, a + 1, 1)]);
        // Rx off: not read.
        m.setup.param_rx = false;
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x10, (pan >> 7) as u8, pan as u8, 20, 0xf7]);
        assert_eq!(params::get(&e, pan), 8);
        // The setup by the system memory type.
        m.setup.param_rx = true;
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x01, 0, 1, 0, 3, 0xf7]);
        assert_eq!(m.setup.rx_channel, 3);
    }

    #[test]
    fn function_calls_and_keys() {
        let e = y01v();
        let mut m = Midi::default();
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x02, 0x00, 7, 0x7f, 0xf7]);
        assert_eq!(m.take_event(), Some(Event::RecallScene(7)));
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x02, 0x10, 8, 0x7f, 0xf7]);
        assert_eq!(m.take_event(), Some(Event::StoreScene(8)));
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x02, 0x03, 20, 1, 0xf7]);
        assert_eq!(m.take_event(), Some(Event::RecallEffect(20, 1)));
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x02, 0x30, 3, b'S', b'O', b'N', b'G', 0xf7]);
        assert_eq!(m.take_event(), Some(Event::SetTitle(3, "SONG".into())));
        m.receive(&e, &[0xf0, 0x43, 0x30, 0x3e, 0x04, 0x02, 0x30, 3, 0xf7]);
        assert_eq!(m.take_event(), Some(Event::TitleRequest(3)));
        m.title(3, "SONG");
        assert_eq!(drain(&mut m), vec![vec![0xf0, 0x43, 0x10, 0x3e, 0x04, 0x02, 0x30, 3, b'S', b'O', b'N', b'G', 0xf7]]);

        // The ON and SOLO keys are the console's; SEL goes to the app.
        let on = params::common(9, 1);
        assert_eq!(params::get(&e, on), 1);
        m.receive(&e, &key_message(0, 1, 1)); // ON10
        assert_eq!(params::get(&e, on), 0);
        m.receive(&e, &[0xf0, 0x43, 0x10, 0x3e, 0x04, 0x43, 0, 1, 1, 0xf7]);
        assert_eq!(params::get(&e, on), 0, "a release does nothing");
        m.receive(&e, &key_message(0, 2, 3)); // SOLO4
        assert!(strip_of(&e, 3).unwrap().params.solo.load(Relaxed));
        m.receive(&e, &key_message(0, 4, 0)); // SEL1
        assert_eq!(m.take_event(), Some(Event::Key(4, 0)));
        // A solo goes out as a key remote.
        let mut tx = Midi::default();
        tx.setup.param_tx = true;
        tx.poll(&e);
        strip_of(&e, 8).unwrap().params.solo.store(true, Relaxed);
        tx.poll(&e);
        assert_eq!(drain(&mut tx), vec![key_message(0, 3, 0)], "SOLO9");
    }

    #[test]
    fn bulk_dumps_round_trip() {
        let e = y01v();
        let mut m = Midi::default();
        params::set(&e, params::common(0, 0), 77);
        params::set(&e, params::eq_address(1).unwrap() + 1, 33);
        params::set(&e, params::dyn_address(2).unwrap() + 2, 3);
        params::set(&e, params::fx_address(0) + 2, 5);
        m.setup.pc_tx = false;
        m.cc_map = CcMap::of(Arrangement::O3d);
        m.pc_table.scenes[5] = Some(40);
        for (kind, n) in [(b'M', 127), (b'S', 32), (b'P', 32), (b'C', 32), (b'Q', 97), (b'Y', 98), (b'E', 112)] {
            m.receive(&e, &request(0, kind, n));
        }
        let dumps = drain(&mut m);
        assert_eq!(dumps.len(), 7);
        assert_eq!(dumps[0].len(), 6 + 2048 + 2, "the edit buffer's dump");
        let e2 = y01v();
        let mut m2 = Midi::default();
        for d in &dumps {
            m2.receive(&e2, d);
        }
        assert_eq!(params::get(&e2, params::common(0, 0)), 77);
        assert_eq!(params::get(&e2, params::eq_address(1).unwrap() + 1), 33);
        assert_eq!(params::get(&e2, params::dyn_address(2).unwrap() + 2), 3);
        assert_eq!(params::get(&e2, params::fx_address(0) + 2), 5);
        assert!(!m2.setup.pc_tx, "the setup");
        assert_eq!(m2.cc_map, CcMap::of(Arrangement::O3d));
        assert_eq!(m2.pc_table.scenes[5], Some(40));
        // A scene: through the app, both ways.
        m.receive(&e, &request(0, b'M', 12));
        assert_eq!(m.take_event(), Some(Event::SceneRequest(12)));
        m.scene_dump(12, "VERSE", &[3; SIZE]);
        let d = drain(&mut m).remove(0);
        m2.receive(&e2, &d);
        match m2.take_event() {
            Some(Event::SceneDump(12, title, data)) => assert_eq!((title.as_str(), data.len()), ("VERSE", SIZE)),
            other => panic!("{other:?}"),
        }
        // BULK off: nothing heard, nothing answered.
        m2.setup.bulk_rx = false;
        m2.receive(&e2, &d);
        m2.receive(&e2, &request(0, b'S', 32));
        assert_eq!((m2.take_event(), m2.take_out()), (None, None));
        // Scene 0 can't be written, and the recall's values aren't sent back.
        let d0 = dump(0, b'M', 0, &scene_title(""), &[0; SIZE]);
        m2.setup.bulk_rx = true;
        m2.receive(&e2, &d0);
        assert_eq!(m2.take_event(), None);
        let mut tx = all_on();
        tx.poll(&e);
        params::set(&e, params::common(1, 0), 12);
        tx.scene_recalled(&e, 3);
        assert_eq!(drain(&mut tx), vec![vec![0xc0, 2]]);
        tx.poll(&e);
        assert!(drain(&mut tx).is_empty(), "a recall's values are the scene's");
    }

    #[test]
    fn page_4s_transmit_and_request_use_tx_ch() {
        let e = y01v();
        let mut m = Midi::default();
        m.setup.tx_channel = 2;
        m.transmit(&e, b'S', 0);
        let d = m.take_out().unwrap();
        assert_eq!(parse_dump(&d).map(|p| (p.0, p.1)), Some((2, b'S')));
        m.send_request(b'M', 3);
        assert_eq!(parse_request(&m.take_out().unwrap()), Some((2, b'M', 3)));
    }
}
