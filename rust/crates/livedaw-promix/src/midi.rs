//! The ProMix 01's MIDI (User's Guide chapter 9, Add-4 to Add-29): its only
//! remote protocol, on one port.
//!
//! * Control changes carry the 531 parameters (`params`), each on a bank and
//!   a controller (`CcMap`, user-assignable; the default puts parameter p on
//!   bank p / 96, controller p % 96, CUE ON and the CUE mode on none). In
//!   Channel mode a bank is a MIDI channel (the Tx / Rx channel plus the
//!   bank, past 16 from 1 again); in Register mode everything is on the Tx /
//!   Rx channel and NRPN LSB (CC 98) names the bank first (Add-16). A value's
//!   internal range 0..N-1 spans MIDI 0-127 (Add-17: N's buckets of 128 / N,
//!   the rest split on both ends so the middle lands on 64); the effects'
//!   parameters are 14 bits over two parameters, high and low byte.
//! * Program changes recall scenes through a table (`PcTable`: 1-50 the
//!   scenes, 128 scene 00 at first, Add-4); recalling one on the console sends
//!   its program change.
//! * System Exclusive (Add-18 to Add-29): Yamaha's bulk dumps `F0 43 0n 7E`
//!   and requests `F0 43 2n 7E`, an ASCII header "LM  8A41" and a type letter,
//!   data bytes sent as two hex digits, a checksum. Sidecar sends and takes
//!   the Edit Buffer (`M`, memory 127), the program change table (`P`), the
//!   control change table (`C`), the MIDI setup (`S`) and the effects, dynamics
//!   and EQ user programs (`E`, `O`, `Q`); scenes 1-50 come with the scene
//!   memories (`Event::SceneDumpRequest`, `SceneDump`). Key Remote (`K`)
//!   presses a panel button. The guide gives the formats, not the bytes inside
//!   a dump: their layout is Sidecar's ([`scene_bytes`]), so a dump comes back
//!   into Sidecar, not into a ProMix 01.
//! * Setup (p.81): Tx and Rx channels; per kind (program, control, bulk) TX,
//!   RX, OMNI and ECHO.
//!
//! Received values aren't sent back; the console's changes are (`poll`), once
//! the first poll has taken the console's values.

use crate::console::{Console, Origin, PmStrip};
use crate::library::{EqProgram, FxProgram};
use crate::params::{self, Param, COUNT};
use std::collections::VecDeque;

/// Banks of controllers (16 of 96, Add-16).
pub const BANKS: u8 = 16;
pub const CCS_PER_BANK: u8 = 96;
/// NRPN LSB: Register mode's bank.
pub const CC_BANK: u8 = 98;

/// A message kind's switches (MIDI SETUP, p.81).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Switches {
    pub tx: bool,
    pub rx: bool,
    pub omni: bool,
    pub echo: bool,
}

impl Switches {
    const ON: Switches = Switches { tx: true, rx: true, omni: false, echo: false };
}

/// MIDI SETUP (p.81) and the control change mode (p.83).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Setup {
    /// MIDI channels, 0-15.
    pub tx_channel: u8,
    pub rx_channel: u8,
    pub program: Switches,
    pub control: Switches,
    pub bulk: Switches,
    /// Register mode (NRPN banks on one channel), else Channel mode.
    pub register_mode: bool,
    /// Bulk interval, in 100 ms (0-10).
    pub interval: u8,
}

impl Default for Setup {
    fn default() -> Self {
        Setup {
            tx_channel: 0,
            rx_channel: 0,
            program: Switches::ON,
            control: Switches::ON,
            bulk: Switches::ON,
            register_mode: false,
            interval: 3,
        }
    }
}

/// The control change table: each parameter's (bank, controller), if any.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CcMap {
    pub map: Vec<Option<(u8, u8)>>,
}

impl Default for CcMap {
    /// The guide's initial assignments (Add-5 to Add-15).
    fn default() -> Self {
        let map = (0..COUNT)
            .map(|n| params::has_cc(n).then_some(((n / CCS_PER_BANK as u16) as u8, (n % CCS_PER_BANK as u16) as u8)))
            .collect();
        CcMap { map }
    }
}

impl CcMap {
    /// The parameter on (bank, cc): the lowest-numbered if several are (p.84).
    pub fn lookup(&self, bank: u8, cc: u8) -> Option<u16> {
        self.map.iter().position(|m| *m == Some((bank, cc))).map(|n| n as u16)
    }

    pub fn of(&self, n: u16) -> Option<(u8, u8)> {
        self.map.get(n as usize).copied().flatten()
    }
}

/// The program change table: program 0-127 (MIDI's numbering; the guide's
/// 1-128) to scene 0-50 (0: scene 00), or none.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PcTable {
    pub scenes: [Option<u8>; 128],
}

impl Default for PcTable {
    /// Programs 1-50 recall scenes 1-50, 128 scene 00 (p.82, Add-4).
    fn default() -> Self {
        let mut scenes = [None; 128];
        for (p, s) in scenes.iter_mut().enumerate().take(50) {
            *s = Some(p as u8 + 1);
        }
        scenes[127] = Some(0);
        PcTable { scenes }
    }
}

impl PcTable {
    /// The first program that recalls `scene`.
    pub fn program_of(&self, scene: u8) -> Option<u8> {
        self.scenes.iter().position(|s| *s == Some(scene)).map(|p| p as u8)
    }
}

/// An internal value `v` of `n` values as MIDI 0..=`top` (127, or 16383 for
/// 14 bits): the ends on the ends, the rest in the middle of its bucket.
pub fn to_midi(v: u16, n: u16, top: u16) -> u16 {
    let n = n.max(1);
    let v = v.min(n - 1);
    if n == 1 || v == 0 {
        return 0;
    }
    if v == n - 1 {
        return top;
    }
    let range = top as u32 + 1;
    let (width, rest) = (range / n as u32, range % n as u32);
    let offset = rest / 2 + rest % 2;
    (offset + v as u32 * width + (width - 1) / 2).min(top as u32) as u16
}

/// A MIDI value as an internal value of `n` values (Add-17: `(x - offset) /
/// width`, held within the range).
pub fn from_midi(x: u16, n: u16, top: u16) -> u16 {
    let n = n.max(1);
    let range = top as u32 + 1;
    let (width, rest) = (range / n as u32, range % n as u32);
    let offset = rest / 2 + rest % 2;
    ((x.min(top) as u32).saturating_sub(offset) / width.max(1)).min(n as u32 - 1) as u16
}

/// What the app should do on the console's behalf.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Event {
    /// Recall scene 1-50 (the protocol recalls scene 00, the initial data, itself).
    RecallScene(u8),
    /// A bulk request for scene 1-50: the app answers with `scene_dump`.
    SceneDumpRequest(u8),
    /// A dump of scene 1-50: its bytes (`scene_bytes`' layout) to store there.
    SceneDump(u8, Vec<u8>),
    /// A Key Remote button (Add-28) the app's panel handles: its number and
    /// data (127 a press of a switch).
    Key(u8, u8),
}

/// The type letters of the bulk formats (Add-19 to Add-27).
const HEADER: &[u8; 8] = b"LM  8A41";

/// The data bytes of a dump type (Add-29's D0..De).
fn data_len(kind: u8) -> Option<usize> {
    Some(match kind {
        b'M' => 408,
        b'P' => 128,
        b'C' => 1152,
        b'S' => 26,
        b'E' => 36,
        b'O' => 22,
        b'Q' => 24,
        _ => return None,
    })
}

/// Whether a dump type has a memory number after its letter (else a space).
fn numbered(kind: u8) -> bool {
    matches!(kind, b'M' | b'E' | b'O' | b'Q')
}

/// The checksum: the two's complement of the sum from the header's 'L' to the
/// last data byte, bit 7 cleared.
pub fn checksum(bytes: &[u8]) -> u8 {
    let sum: u32 = bytes.iter().map(|&b| b as u32).sum();
    ((!sum).wrapping_add(1) & 0x7f) as u8
}

/// A Yamaha bulk dump's frame around `body` (from `LM` to the last data byte):
/// the byte count, the checksum and the EOX. Shared with the 02R and the 01V.
pub fn frame_dump(channel: u8, body: &[u8]) -> Vec<u8> {
    let count = body.len();
    let mut out = vec![0xf0, 0x43, channel & 0xf, 0x7e, (count >> 7) as u8 & 0x7f, count as u8 & 0x7f];
    out.extend_from_slice(body);
    out.push(checksum(body));
    out.push(0xf7);
    out
}

/// A dump's (channel, body), if `msg` is one with `header`: its frame checked
/// (the count and the checksum).
pub fn parse_frame<'a>(msg: &'a [u8], header: &[u8; 8]) -> Option<(u8, &'a [u8])> {
    if msg.len() < 18 || msg[0] != 0xf0 || msg[1] != 0x43 || msg[2] & 0xf0 != 0 || msg[3] != 0x7e || *msg.last()? != 0xf7 {
        return None;
    }
    let count = ((msg[4] as usize) << 7) | msg[5] as usize;
    let body = msg.get(6..6 + count)?;
    if msg.len() != 6 + count + 2 || checksum(body) != msg[6 + count] || &body[..8] != header {
        return None;
    }
    Some((msg[2] & 0xf, body))
}

/// A bulk dump of `kind` (memory `number` where it has one) for `channel`.
pub fn dump(channel: u8, kind: u8, number: u8, data: &[u8]) -> Vec<u8> {
    let mut body = HEADER.to_vec();
    body.push(kind);
    body.push(if numbered(kind) { number } else { b' ' });
    for &d in data {
        for nibble in [d >> 4, d & 0xf] {
            body.push(b"0123456789ABCDEF"[nibble as usize]);
        }
    }
    frame_dump(channel, &body)
}

/// A bulk request of `kind` (memory `number` where it has one).
pub fn request(channel: u8, kind: u8, number: u8) -> Vec<u8> {
    let mut out = vec![0xf0, 0x43, 0x20 | (channel & 0xf), 0x7e];
    out.extend_from_slice(HEADER);
    out.push(kind);
    out.push(if numbered(kind) || kind == b'Y' { number } else { b' ' });
    out.push(0xf7);
    out
}

/// A dump's (channel, kind, number, data), if `msg` is a well-formed one.
pub fn parse_dump(msg: &[u8]) -> Option<(u8, u8, u8, Vec<u8>)> {
    if msg.len() < 18 || msg[0] != 0xf0 || msg[1] != 0x43 || msg[2] & 0xf0 != 0 || msg[3] != 0x7e || *msg.last()? != 0xf7 {
        return None;
    }
    let count = ((msg[4] as usize) << 7) | msg[5] as usize;
    let body = msg.get(6..6 + count)?;
    if msg.len() != 6 + count + 2 || checksum(body) != msg[6 + count] || &body[..8] != HEADER {
        return None;
    }
    let kind = body[8];
    let hex = |c: u8| (c as char).to_digit(16).map(|d| d as u8);
    let (number, digits) = match kind {
        b'K' => return Some((msg[2], kind, 0, body[9..].to_vec())),
        _ => (body[9], &body[10..]),
    };
    if digits.len() % 2 != 0 || data_len(kind) != Some(digits.len() / 2) {
        return None;
    }
    let data = digits.chunks(2).map(|p| Some(hex(p[0])? << 4 | hex(p[1])?)).collect::<Option<Vec<u8>>>()?;
    Some((msg[2], kind, number, data))
}

/// A bulk request's (channel, kind, number).
pub fn parse_request(msg: &[u8]) -> Option<(u8, u8, u8)> {
    if msg.len() < 15 || msg[0] != 0xf0 || msg[1] != 0x43 || msg[2] & 0xf0 != 0x20 || msg[3] != 0x7e || &msg[4..12] != HEADER {
        return None;
    }
    Some((msg[2] & 0xf, msg[12], msg[13]))
}

/// Scene bytes: the Edit Buffer (or a scene) in 408 bytes, Sidecar's layout
/// (the guide gives the size only): bytes 0-21 the levels (parameters 0-21),
/// 22-45 the pans (44-67), 46-113 the sends (68-135), 114-130 the SEND3-4 pans
/// (140-156), 131-310 the EQ (216-395), 311-312 the effects' programs (404-405),
/// 313-352 their ten parameters each as two bytes (high, low), 353-376 the
/// processors' types, patches, keys and parameters (446-454, 461-475), 377-378
/// the oscillator's level and frequency, 379-387 the groups (484-500, two to a
/// byte), 388-402 the switches as bits (ON, CUE, phase, EQ ON, pair flags,
/// PRE / POST, the processors' IN and ON, SEND3-4 stereo, the oscillator's ON
/// and assigns, GANG, the group shown, the patch points, the CUE mode), the
/// rest zero.
pub fn scene_bytes(c: &Console) -> Vec<u8> {
    let mut out = Vec::with_capacity(408);
    let get = |n: u16| params::get(c, n).unwrap_or(0);
    let byte = |out: &mut Vec<u8>, range: std::ops::RangeInclusive<u16>| out.extend(range.map(|n| get(n) as u8));
    byte(&mut out, 0..=21);
    byte(&mut out, 44..=67);
    byte(&mut out, 68..=135);
    byte(&mut out, 140..=156);
    byte(&mut out, 216..=395);
    byte(&mut out, 404..=405);
    for n in (406..=445).step_by(2) {
        let v = get(n);
        out.extend([(v >> 8) as u8, v as u8]);
    }
    byte(&mut out, 446..=454);
    byte(&mut out, 461..=475);
    byte(&mut out, 478..=479);
    for pair in (484..=500u16).collect::<Vec<_>>().chunks(2) {
        out.push(pair.iter().enumerate().fold(0, |b, (i, &n)| b | (get(n) as u8 & 0xf) << (4 * i)));
    }
    let mut bits = Bits::default();
    for (range, width) in BIT_FIELDS {
        for n in range.clone() {
            bits.put(get(n), *width);
        }
    }
    out.extend(bits.bytes);
    out.resize(408, 0);
    out
}

/// The parameters kept as bits in a scene, and their widths.
const BIT_FIELDS: &[(std::ops::RangeInclusive<u16>, u32)] = &[
    (22..=43, 1),
    (157..=178, 1),
    (179..=195, 1),
    (196..=215, 1),
    (396..=403, 1),
    (136..=139, 1),
    (455..=457, 2),
    (458..=460, 1),
    (476..=477, 1),
    (480..=482, 1),
    (501..=501, 1),
    (502..=502, 2),
    (511..=513, 1),
    (483..=483, 2),
];

#[derive(Default)]
struct Bits {
    bytes: Vec<u8>,
    at: u32,
}

impl Bits {
    fn put(&mut self, v: u16, width: u32) {
        for b in 0..width {
            if self.at % 8 == 0 {
                self.bytes.push(0);
            }
            if v >> b & 1 != 0 {
                *self.bytes.last_mut().unwrap() |= 1 << (self.at % 8);
            }
            self.at += 1;
        }
    }

    fn take(bytes: &[u8], at: &mut u32, width: u32) -> u16 {
        let mut v = 0;
        for b in 0..width {
            let bit = bytes.get((*at / 8) as usize).map_or(0, |x| x >> (*at % 8) & 1);
            v |= (bit as u16) << b;
            *at += 1;
        }
        v
    }
}

/// Sets the console from scene bytes (`scene_bytes`' layout): flags and
/// patches first, so the levels and sends land on the pairs as they were.
pub fn load_scene_bytes(c: &mut Console, data: &[u8]) {
    if data.len() < 403 {
        return;
    }
    let set = |c: &mut Console, n: u16, v: u16| params::set(c, n, v, Origin::Midi);
    let mut at = 0u32;
    let flags = &data[388..403];
    for (range, width) in BIT_FIELDS {
        for n in range.clone() {
            let v = Bits::take(flags, &mut at, *width);
            set(c, n, v);
        }
    }
    for (i, n) in (484..=500u16).enumerate() {
        set(c, n, (data[379 + i / 2] >> (4 * (i % 2))) as u16 & 0xf);
    }
    // The processors' types before their parameters, the effects' too.
    let ranges: [(std::ops::RangeInclusive<u16>, usize); 9] = [
        (446..=454, 353),
        (461..=475, 362),
        (404..=405, 311),
        (0..=21, 0),
        (44..=67, 22),
        (68..=135, 46),
        (140..=156, 114),
        (216..=395, 131),
        (478..=479, 377),
    ];
    for (range, start) in ranges {
        for (i, n) in range.enumerate() {
            set(c, n, data[start + i] as u16);
        }
    }
    for (i, n) in (406..=445).step_by(2).enumerate() {
        let v = (data[313 + 2 * i] as u16) << 8 | data[314 + 2 * i] as u16;
        set(c, n, v);
    }
}

/// Scene 00 (p.62): the scene bytes of a new ProMix 01, read once from one.
pub fn initial_bytes() -> &'static [u8] {
    static BYTES: std::sync::OnceLock<Vec<u8>> = std::sync::OnceLock::new();
    BYTES.get_or_init(|| {
        use livedaw_engine::config::{ConsoleConfig, Counts};
        let e = livedaw_engine::Engine::new(0);
        e.configure(&ConsoleConfig { profile: livedaw_engine::Profile::ProMix01, counts: Counts::PROMIX01 });
        let (mut s, lib) = (crate::State::default(), crate::library::Library::default());
        scene_bytes(&Console::new(&e, &mut s, &lib))
    })
}

/// The MIDI protocol's state: its setup and tables, what it sent last, and
/// what waits to go out.
pub struct Midi {
    pub setup: Setup,
    pub cc_map: CcMap,
    pub pc_table: PcTable,
    /// The bank Register mode's CC 98 named last (received).
    rx_bank: u8,
    /// Each parameter's value as last sent or received.
    last: Vec<Option<u16>>,
    primed: bool,
    /// An effect parameter's high byte, waiting for its low byte.
    pending_high: [[Option<u16>; 10]; 2],
    out: VecDeque<Vec<u8>>,
    events: VecDeque<Event>,
}

/// MEMORY CONTROL CHANGE OUT's data types (p.87), in its order.
pub const CC_OUT_CATEGORIES: [&str; 8] = ["ON/OFF", "LEVEL", "EQ", "COMP", "PAN", "SEND", "EFFECT", "OTHERS"];

/// The parameters of each data type: its bit, a range.
const CC_OUT_RANGES: [(u32, std::ops::RangeInclusive<u16>); 10] = [
    (0, 22..=43),
    (1, 0..=21),
    (2, 196..=395),
    (3, 446..=475),
    (4, 44..=67),
    (4, 140..=156),
    (5, 68..=139),
    (6, 404..=445),
    (7, 179..=195),
    (7, 484..=500),
];

impl Default for Midi {
    fn default() -> Self {
        Midi {
            setup: Setup::default(),
            cc_map: CcMap::default(),
            pc_table: PcTable::default(),
            rx_bank: 0,
            last: vec![None; COUNT as usize],
            primed: false,
            pending_high: [[None; 10]; 2],
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

    /// Queues a message to send (after those waiting).
    pub fn push_out(&mut self, msg: Vec<u8>) {
        self.out.push_back(msg);
    }

    /// The next thing for the app to do.
    pub fn take_event(&mut self) -> Option<Event> {
        self.events.pop_front()
    }

    /// Forgets what was sent: the next poll sends every value.
    pub fn resend_all(&mut self) {
        self.last.iter_mut().for_each(|v| *v = None);
        self.primed = true;
    }

    /// Whether a message on MIDI channel `ch` is for us (`omni`: any channel).
    fn for_us(&self, ch: u8, sw: Switches) -> bool {
        sw.rx && (sw.omni || ch == self.setup.rx_channel)
    }

    /// Handles one received message (a channel message or a whole SysEx).
    pub fn receive(&mut self, c: &mut Console, msg: &[u8]) {
        let Some(&status) = msg.first() else { return };
        if status == 0xf0 {
            self.receive_sysex(c, msg);
            return;
        }
        let ch = status & 0xf;
        match status & 0xf0 {
            0xc0 if msg.len() >= 2 => {
                let sw = self.setup.program;
                if sw.echo {
                    self.out.push_back(msg.to_vec());
                }
                if !self.for_us(ch, sw) {
                    return;
                }
                match self.pc_table.scenes[(msg[1] & 0x7f) as usize] {
                    // Scene 00: the initial data, here.
                    Some(0) => load_scene_bytes(c, initial_bytes()),
                    Some(scene) => self.events.push_back(Event::RecallScene(scene)),
                    None => {}
                }
            }
            0xb0 if msg.len() >= 3 => {
                let sw = self.setup.control;
                if sw.echo {
                    self.out.push_back(msg.to_vec());
                }
                let (cc, value) = (msg[1] & 0x7f, msg[2] & 0x7f);
                let bank = if self.setup.register_mode {
                    if !self.for_us(ch, sw) {
                        return;
                    }
                    if cc == CC_BANK {
                        self.rx_bank = value.min(BANKS - 1);
                        return;
                    }
                    self.rx_bank
                } else {
                    // Channel mode: OMNI is ignored (p.83).
                    if !sw.rx {
                        return;
                    }
                    (ch + 16 - self.setup.rx_channel) % 16
                };
                if let Some(n) = self.cc_map.lookup(bank, cc) {
                    self.receive_value(c, n, value);
                }
            }
            _ => {}
        }
    }

    /// Parameter `n`'s MIDI value received.
    fn receive_value(&mut self, c: &mut Console, n: u16, value: u8) {
        match params::param(n) {
            Some(Param::FxParam(slot, k, high)) => {
                let steps = params::steps(c, n);
                let now = to_midi(params::get(c, n).unwrap_or(0), steps, 16383);
                let (hi, lo) = if high {
                    self.pending_high[slot][k] = Some(value as u16);
                    (value as u16, now & 0x7f)
                } else {
                    (self.pending_high[slot][k].take().unwrap_or(now >> 7), value as u16)
                };
                params::set(c, n, from_midi(hi << 7 | lo, steps, 16383), Origin::Midi);
                let v = params::get(c, n);
                // Both bytes hold the value now: neither goes back out.
                self.last[n as usize] = v;
                self.last[(n ^ 1) as usize] = v;
            }
            Some(_) => {
                params::set(c, n, from_midi(value as u16, params::steps(c, n), 127), Origin::Midi);
                self.last[n as usize] = params::get(c, n);
            }
            None => {}
        }
    }

    /// Queues the control change of parameter `n` at MIDI value `value`.
    fn send_cc(&mut self, n: u16, value: u16) {
        let Some((bank, cc)) = self.cc_map.of(n) else { return };
        let tx = self.setup.tx_channel;
        if self.setup.register_mode {
            self.out.push_back(vec![0xb0 | tx, CC_BANK, bank]);
            self.out.push_back(vec![0xb0 | tx, cc, value as u8 & 0x7f]);
        } else {
            self.out.push_back(vec![0xb0 | ((tx + bank) % 16), cc, value as u8 & 0x7f]);
        }
    }

    /// Sends the console's changes since the last poll (the first takes its
    /// values and sends nothing).
    pub fn poll(&mut self, c: &Console) {
        for n in 0..COUNT {
            let Some(v) = params::get(c, n) else { continue };
            if self.last[n as usize] == Some(v) {
                continue;
            }
            self.last[n as usize] = Some(v);
            if !self.primed || !self.setup.control.tx {
                continue;
            }
            match params::param(n) {
                // An effect parameter goes out as its high byte, then its low.
                Some(Param::FxParam(_, _, true)) => {
                    let m = to_midi(v, params::steps(c, n), 16383);
                    self.last[n as usize + 1] = Some(v);
                    self.send_cc(n, m >> 7);
                    self.send_cc(n + 1, m & 0x7f);
                }
                Some(Param::FxParam(_, _, false)) => {}
                _ => self.send_cc(n, to_midi(v, params::steps(c, n), 127)),
            }
        }
        self.primed = true;
    }

    /// Scene `scene` (0-50) was recalled on the console: its program change.
    pub fn scene_recalled(&mut self, scene: u8) {
        if !self.setup.program.tx {
            return;
        }
        if let Some(p) = self.pc_table.program_of(scene) {
            self.out.push_back(vec![0xc0 | self.setup.tx_channel, p]);
        }
    }

    /// MEMORY CONTROL CHANGE OUT (p.87): the control changes of the parameters
    /// of the categories in `mask` (bits of `CC_OUT_CATEGORIES`), with the pair
    /// flags and SEND3,4's configuration flag; nothing while CONTROL Tx is off.
    pub fn send_categories(&mut self, c: &Console, mask: u32) {
        if !self.setup.control.tx {
            return;
        }
        for n in 0..COUNT {
            let always = (396..=403).contains(&n) || n == 476;
            if !always && !CC_OUT_RANGES.iter().any(|&(bit, ref r)| mask & 1 << bit != 0 && r.contains(&n)) {
                continue;
            }
            let Some(v) = params::get(c, n) else { continue };
            self.last[n as usize] = Some(v);
            match params::param(n) {
                Some(Param::FxParam(_, _, true)) => {
                    let m = to_midi(v, params::steps(c, n), 16383);
                    self.send_cc(n, m >> 7);
                    self.send_cc(n + 1, m & 0x7f);
                }
                Some(Param::FxParam(_, _, false)) => {}
                _ => self.send_cc(n, to_midi(v, params::steps(c, n), 127)),
            }
        }
    }

    /// LOCAL OFF (p.86): fader `f` (0-15) moved to `value` (0-127) controls
    /// nothing here and goes out as its control change (parameters 515-530).
    pub fn local_fader(&mut self, f: u16, value: u8) {
        if f < 16 && self.setup.control.tx {
            self.send_cc(515 + f, value as u16);
        }
    }

    /// LOCAL OFF: channel `ch`'s [SEL] (0-15) sends program change `ch`.
    pub fn local_select(&mut self, ch: u8) {
        if ch < 16 && self.setup.program.tx {
            self.out.push_back(vec![0xc0 | self.setup.tx_channel, ch]);
        }
    }

    /// The control changes of every parameter (Memory Control Change Out, p.87).
    pub fn send_all(&mut self, c: &Console) {
        self.resend_all();
        self.poll(c);
    }

    fn receive_sysex(&mut self, c: &mut Console, msg: &[u8]) {
        let sw = self.setup.bulk;
        if let Some((ch, kind, number)) = parse_request(msg) {
            if !self.for_us(ch, sw) || !sw.tx {
                return;
            }
            self.answer_request(c, kind, number);
            return;
        }
        let Some((ch, kind, number, data)) = parse_dump(msg) else { return };
        if !self.for_us(ch, sw) {
            return;
        }
        match kind {
            b'M' if number == 127 => load_scene_bytes(c, &data),
            b'M' if (1..=50).contains(&number) => self.events.push_back(Event::SceneDump(number, data)),
            b'P' => {
                for (s, &d) in self.pc_table.scenes.iter_mut().zip(&data) {
                    *s = (d <= 50).then_some(d);
                }
            }
            b'C' => {
                for (n, pair) in self.cc_map.map.iter_mut().zip(data.chunks(2)) {
                    *n = (pair[0] < BANKS && pair[1] < CCS_PER_BANK).then_some((pair[0], pair[1]));
                }
            }
            b'S' => self.setup = setup_of(&data, self.setup),
            b'K' => {
                if let [key, value, ..] = data[..] {
                    self.key(c, key, value);
                }
            }
            _ => {}
        }
    }

    /// Answers a bulk request (memory 127: the Edit Buffer).
    fn answer_request(&mut self, c: &Console, kind: u8, number: u8) {
        let tx = self.setup.tx_channel;
        match kind {
            b'M' if number == 127 => self.out.push_back(dump(tx, b'M', 127, &scene_bytes(c))),
            b'M' if (1..=50).contains(&number) => self.events.push_back(Event::SceneDumpRequest(number)),
            b'P' => {
                let data: Vec<u8> = self.pc_table.scenes.iter().map(|s| s.unwrap_or(0x7f)).collect();
                self.out.push_back(dump(tx, b'P', 0, &data));
            }
            b'C' => self.out.push_back(dump(tx, b'C', 0, &self.cc_table_bytes())),
            b'S' => self.out.push_back(dump(tx, b'S', 0, &setup_bytes(&self.setup))),
            b'E' => {
                if let Some(p) = c.library.fx_program(30 + number as usize) {
                    self.out.push_back(dump(tx, b'E', number, &fx_program_bytes(&p)));
                }
            }
            b'O' => {
                if let Some(p) = c.library.program(10 + number as usize) {
                    self.out.push_back(dump(tx, b'O', number, &dyn_program_bytes(&p)));
                }
            }
            b'Q' => {
                if let Some(p) = c.library.eq_program(30 + number as usize) {
                    self.out.push_back(dump(tx, b'Q', number, &eq_program_bytes(&p)));
                }
            }
            b'A' => {
                for k in *b"PCS" {
                    self.answer_request(c, k, 0);
                }
                self.answer_request(c, b'M', 127);
            }
            b'Y' if number == 127 => self.send_all(c),
            _ => {}
        }
    }

    /// The control change table's 1152 bytes: 576 entries (the 531 parameters,
    /// then none) of bank and controller, 0x7f 0x7f for none.
    fn cc_table_bytes(&self) -> Vec<u8> {
        (0..576).flat_map(|n| self.cc_map.of(n).map_or([0x7f, 0x7f], |(b, cc)| [b, cc])).collect()
    }

    /// A Key Remote button (Add-23, Add-28): ON buttons switch their channel
    /// (CH1-16, ST IN, ST OUT; RTN / SEND waits for the panel); the others are
    /// the app's (`Event::Key`).
    fn key(&mut self, c: &mut Console, key: u8, value: u8) {
        let strip = match key {
            39..=54 => Some(PmStrip::Ch(key as usize - 39)),
            55 => Some(PmStrip::StIn),
            57 => Some(PmStrip::StOut),
            _ => None,
        };
        match strip {
            Some(s) if value != 0 => {
                let on = c.on(s);
                c.set_on(s, !on);
            }
            Some(_) => {}
            None => self.events.push_back(Event::Key(key, value)),
        }
    }
}

/// MIDI SETUP as 26 bytes (Sidecar's layout): Tx and Rx channels; TX, RX,
/// OMNI, ECHO of program, control and bulk; the mode; the interval.
pub fn setup_bytes(s: &Setup) -> Vec<u8> {
    let mut out = vec![s.tx_channel, s.rx_channel];
    for sw in [s.program, s.control, s.bulk] {
        out.extend([sw.tx, sw.rx, sw.omni, sw.echo].map(u8::from));
    }
    out.extend([s.register_mode as u8, s.interval]);
    out.resize(26, 0);
    out
}

fn setup_of(d: &[u8], keep: Setup) -> Setup {
    if d.len() < 16 {
        return keep;
    }
    let sw = |i: usize| Switches { tx: d[i] != 0, rx: d[i + 1] != 0, omni: d[i + 2] != 0, echo: d[i + 3] != 0 };
    Setup {
        tx_channel: d[0] & 0xf,
        rx_channel: d[1] & 0xf,
        program: sw(2),
        control: sw(6),
        bulk: sw(10),
        register_mode: d[14] != 0,
        interval: d[15].min(10),
    }
}

/// A name in `len` bytes, spaces after it.
fn name_bytes(name: &str, len: usize) -> Vec<u8> {
    let mut out: Vec<u8> = name.bytes().filter(|b| b.is_ascii() && !b.is_ascii_control()).take(len).collect();
    out.resize(len, b' ');
    out
}

/// An effects user program as 36 bytes (Sidecar's layout): its type's index,
/// ten values as their steps in two bytes, its name in 15.
pub fn fx_program_bytes(p: &FxProgram) -> Vec<u8> {
    use livedaw_dsp::promix::FX_MODELS;
    let model = FX_MODELS.iter().position(|m| m.token == p.token).unwrap_or(2);
    let mut out = vec![model as u8];
    for k in 0..10 {
        let step = FX_MODELS[model].params.get(k).zip(p.values.get(k)).map_or(0, |(s, v)| crate::console::param_step(s, *v));
        out.extend([(step >> 8) as u8, step as u8]);
    }
    out.extend(name_bytes(&p.name, 15));
    out
}

/// A dynamics user program as 22 bytes: its type, five steps, its name in 15.
pub fn dyn_program_bytes(p: &crate::library::DynProgram) -> Vec<u8> {
    let spec = &livedaw_dsp::promix::DYN_MODELS[p.model as usize];
    let mut out = vec![p.model];
    out.extend(spec.params.iter().zip(p.values).map(|(s, v)| crate::console::param_step(s, v) as u8));
    out.extend(name_bytes(&p.name, 15));
    out.resize(22, 0);
    out
}

/// An EQ user program as 24 bytes: LOW, MID, HIGH (F's index, G + 15, Q), its name in 15.
pub fn eq_program_bytes(p: &EqProgram) -> Vec<u8> {
    use livedaw_dsp::promix::{HIGH_FREQS, LOW_FREQS, MID_FREQS};
    let mut out = Vec::new();
    for (band, (g, f, q)) in p.bands.iter().enumerate() {
        let table = [LOW_FREQS, MID_FREQS, HIGH_FREQS][band];
        out.extend([table.iter().position(|x| x == f).unwrap_or(0) as u8, (*g + 15.0) as u8, *q]);
    }
    out.extend(name_bytes(&p.name, 15));
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::library::Library;
    use crate::state::State;
    use livedaw_engine::config::{ConsoleConfig, Counts};
    use livedaw_engine::{Engine, Profile};

    fn promix() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::ProMix01, counts: Counts::PROMIX01 });
        e
    }

    #[test]
    fn values_scale_as_the_guide_says() {
        // Pan (33 values): the guide's INTEGER((x - 15) / 3), held within 0-32.
        for x in 0..=127u16 {
            let expect = (x as i32 - 15).div_euclid(3).clamp(0, 32) as u16;
            assert_eq!(from_midi(x, 33, 127), expect, "{x}");
        }
        assert_eq!((to_midi(0, 33, 127), to_midi(16, 33, 127), to_midi(32, 33, 127)), (0, 64, 127), "the centre on 64");
        for n in [2u16, 4, 5, 9, 10, 20, 21, 26, 31, 33, 56, 128] {
            for v in 0..n {
                assert_eq!(from_midi(to_midi(v, n, 127), n, 127), v, "{v} of {n}");
            }
        }
        assert_eq!((to_midi(1, 2, 127), from_midi(63, 2, 127), from_midi(64, 2, 127)), (127, 0, 1));
        for v in [0u16, 1, 1799, 3219] {
            assert_eq!(from_midi(to_midi(v, 3220, 16383), 3220, 16383), v);
        }
    }

    #[test]
    fn the_default_map_and_tables() {
        let map = CcMap::default();
        assert_eq!(map.of(0), Some((0, 0)));
        assert_eq!(map.of(98), Some((1, 2)), "Channel Input 14 to Effect Send 2: bank 1, CC 2");
        assert_eq!(map.of(325), Some((3, 37)), "the guide misprints CC 23-27 for 321-325");
        assert_eq!((map.of(157), map.of(483)), (None, None), "CUE has no control change");
        assert_eq!(map.lookup(5, 50), Some(530));
        let pc = PcTable::default();
        assert_eq!((pc.scenes[0], pc.scenes[49], pc.scenes[50], pc.scenes[127]), (Some(1), Some(50), None, Some(0)));
        assert_eq!(pc.program_of(0), Some(127));
    }

    #[test]
    fn control_changes_in_channel_and_register_mode() {
        let e = promix();
        let (mut s, lib) = (State::default(), Library::default());
        let mut c = Console::new(&e, &mut s, &lib);
        let mut m = Midi::default();
        m.poll(&c);
        assert!(m.take_out().is_none(), "nothing at start");
        // Channel mode, Rx channel 1: bank 2, CC 72 (parameter 264: CH9 LOW Q) on MIDI channel 3.
        m.receive(&mut c, &[0xb2, 72, 0]);
        assert_eq!(params::get(&c, 264), Some(0), "1/6 oct");
        // CH4's fader (bank 0, CC 3) to 0 dB: step 100 is MIDI 100.
        m.receive(&mut c, &[0xb0, 3, 100]);
        assert!(c.strip(PmStrip::Ch(3)).unwrap().params.fader_db.load().abs() < 1e-4);
        m.poll(&c);
        assert!(m.take_out().is_none(), "received values don't echo");
        // A change on the console goes out: CH1 ON off (bank 0, CC 22, value 0).
        c.set_on(PmStrip::Ch(0), false);
        m.poll(&c);
        assert_eq!(m.take_out(), Some(vec![0xb0, 22, 0]));
        // Tx channel 10: bank 5 (CH8's group: 491 = bank 5, CC 11) on channel 15.
        m.setup.tx_channel = 9;
        c.set_group(7, 2);
        m.poll(&c);
        assert_eq!(m.take_out(), Some(vec![0xbe, 11, to_midi(2, 5, 127) as u8]));
        // Register mode: CC 98 names the bank, on the one channel.
        m.setup.register_mode = true;
        m.setup.rx_channel = 9;
        m.receive(&mut c, &[0xb9, CC_BANK, 2]);
        m.receive(&mut c, &[0xb9, 72, 127]);
        assert_eq!(params::get(&c, 264), Some(9), "SHELF");
        m.receive(&mut c, &[0xb3, 72, 0]);
        assert_eq!(params::get(&c, 264), Some(9), "another channel: not ours");
        c.set_pan(PmStrip::Ch(0), 0);
        m.poll(&c);
        assert_eq!((m.take_out(), m.take_out()), (Some(vec![0xb9, CC_BANK, 0]), Some(vec![0xb9, 44, 0])));
    }

    #[test]
    fn effect_parameters_take_14_bits() {
        let e = promix();
        let (mut s, lib) = (State::default(), Library::default());
        let mut c = Console::new(&e, &mut s, &lib);
        let mut m = Midi::default();
        m.poll(&c);
        // Effect 1's REV TIME (parameter 1: 406 high on bank 4 CC 22, 407 low on CC 23).
        let steps = params::steps(&c, 406);
        assert_eq!(steps, 298);
        let x = to_midi(100, steps, 16383);
        m.receive(&mut c, &[0xb4, 22, (x >> 7) as u8]);
        m.receive(&mut c, &[0xb4, 23, (x & 0x7f) as u8]);
        assert_eq!(params::get(&c, 406), Some(100), "10.3 s");
        m.poll(&c);
        assert!(m.take_out().is_none());
        c.set_fx_param(0, 0, 297);
        m.poll(&c);
        assert_eq!((m.take_out(), m.take_out()), (Some(vec![0xb4, 22, 127]), Some(vec![0xb4, 23, 127])));
    }

    #[test]
    fn program_changes_recall_scenes_both_ways() {
        let e = promix();
        let (mut s, lib) = (State::default(), Library::default());
        let mut c = Console::new(&e, &mut s, &lib);
        let mut m = Midi::default();
        m.receive(&mut c, &[0xc0, 4]);
        c.set_level(PmStrip::Ch(0), 115, Origin::Midi);
        m.receive(&mut c, &[0xc0, 127]);
        assert_eq!(c.level(PmStrip::Ch(0)), 0, "scene 00: faders down");
        m.receive(&mut c, &[0xc0, 99]);
        m.receive(&mut c, &[0xc5, 4]);
        assert_eq!((m.take_event(), m.take_event()), (Some(Event::RecallScene(5)), None), "128: scene 00, here");
        m.scene_recalled(12);
        assert_eq!(m.take_out(), Some(vec![0xc0, 11]));
        m.setup.program.echo = true;
        m.receive(&mut c, &[0xc0, 4]);
        assert_eq!(m.take_out(), Some(vec![0xc0, 4]), "ECHO");
    }

    #[test]
    fn memory_cc_out_by_category_and_local_off() {
        let e = promix();
        let (mut s, lib) = (State::default(), Library::default());
        let c = Console::new(&e, &mut s, &lib);
        let mut m = Midi::default();
        // LEVEL alone: the levels 0-21, with the pair flags and SEND3,4's.
        m.send_categories(&c, 1 << 1);
        let mut sent = Vec::new();
        while let Some(msg) = m.take_out() {
            let bank = ((msg[0] & 0xf) + 16 - m.setup.tx_channel) % 16;
            sent.push(m.cc_map.lookup(bank, msg[1]).expect("a parameter's controller"));
        }
        assert!(sent.iter().all(|&n| n <= 21 || (396..=403).contains(&n) || n == 476), "{sent:?}");
        assert!((0..=21).all(|n| sent.contains(&n)));
        // CONTROL Tx off: nothing.
        m.setup.control.tx = false;
        m.send_categories(&c, 0xff);
        m.local_fader(3, 64);
        assert_eq!(m.take_out(), None);
        // LOCAL OFF: a fader's control change (parameter 518), a [SEL]'s program change.
        m.setup.control.tx = true;
        m.local_fader(3, 64);
        let msg = m.take_out().unwrap();
        let bank = ((msg[0] & 0xf) + 16 - m.setup.tx_channel) % 16;
        assert_eq!((m.cc_map.lookup(bank, msg[1]), msg[2]), (Some(518), 64));
        m.local_select(5);
        assert_eq!(m.take_out(), Some(vec![0xc0, 5]));
    }

    #[test]
    fn bulk_dumps_and_requests() {
        // The guide's checksum and framing: a request for the Edit Buffer.
        assert_eq!(
            request(0, b'M', 127),
            [0xf0, 0x43, 0x20, 0x7e, b'L', b'M', b' ', b' ', b'8', b'A', b'4', b'1', b'M', 0x7f, 0xf7]
        );
        let e = promix();
        let (mut s, lib) = (State::default(), Library::default());
        let mut c = Console::new(&e, &mut s, &lib);
        let mut m = Midi::default();
        // An Edit Buffer dump is 826 bytes of count (408 x 2 + 10), as Add-19 says.
        c.set_level(PmStrip::Ch(2), 101, Origin::Panel);
        c.execute_pair(5, 3);
        c.set_eq_value(PmStrip::StOut, 2, 1, 21);
        c.recall_fx_program(1, 27);
        c.recall_dyn_program(2, 8);
        c.set_patch(2, crate::state::Patch::Send4);
        c.set_group(16, 4);
        m.receive(&mut c, &request(0, b'M', 127));
        let d = m.take_out().unwrap();
        assert_eq!((d.len(), d[4], d[5]), (826 + 8, 0x06, 0x3a));
        let (ch, kind, number, data) = parse_dump(&d).unwrap();
        assert_eq!((ch, kind, number, data.len()), (0, b'M', 127, 408));
        let mut broken = d.clone();
        broken[100] ^= 1;
        assert_eq!(parse_dump(&broken), None, "the checksum");
        // Into another console: the same mix.
        let e2 = promix();
        let mut s2 = State::default();
        let mut c2 = Console::new(&e2, &mut s2, &lib);
        let mut m2 = Midi::default();
        m2.receive(&mut c2, &d);
        for n in (0..COUNT).filter(|&n| !(503..=510).contains(&n) && n != 514) {
            assert_eq!(params::get(&c2, n), params::get(&c, n), "{}", params::name(n));
        }
        // The program change table and setup.
        m.receive(&mut c, &request(0, b'P', 0));
        let (_, _, _, pc) = parse_dump(&m.take_out().unwrap()).unwrap();
        assert_eq!((pc[0], pc[127], pc[60]), (1, 0, 0x7f));
        m.setup.register_mode = true;
        m.receive(&mut c, &request(0, b'S', 0));
        let setup = m.take_out().unwrap();
        m2.receive(&mut c2, &setup);
        assert!(m2.setup.register_mode);
        // A scene request goes to the app; Key Remote presses ON.
        m.receive(&mut c, &request(0, b'M', 7));
        assert_eq!(m.take_event(), Some(Event::SceneDumpRequest(7)));
        let key = |k: u8| {
            let mut v = vec![0xf0, 0x43, 0x00, 0x7e, 0x00, 0x0b];
            let body = [HEADER.as_slice(), &[b'K', k, 0x7f]].concat();
            v.extend_from_slice(&body);
            v.push(checksum(&body));
            v.push(0xf7);
            v
        };
        m.receive(&mut c, &key(41));
        assert!(!c.on(PmStrip::Ch(2)), "CH3 ON pressed: off");
        m.receive(&mut c, &key(62));
        assert_eq!(m.take_event(), Some(Event::Key(62, 0x7f)), "ENTER is the panel's");
    }
}
