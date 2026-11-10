//! The 02R's MIDI (MAN pp.141-146, 206-224; V2 pp.159-164, 255-287): its
//! remote protocol, on one port.
//!
//! * Program changes recall scenes through a table (`PcTable`: programs 1-96
//!   scenes 1-96, 97 scene 0 at first: V2 p.160); recalling one on the console
//!   sends its program change.
//! * Control changes (Version 2, receive only: V2 p.164) move the parameters
//!   of the CC assign table (`CcMap`: the 02R preset map of V2 p.256, the
//!   faders, ON keys, pans and AUX 7 sends; editable).
//! * System Exclusive, model `3D`:
//!   * parameter change (`F0 43 1n 3D tt`) type 0 on the edit buffer by
//!     address, a byte (as two nibbles) or a bit; Sidecar's addresses
//!     (`params`), the guides having none; parameter requests (`F0 43 3n 3D`)
//!     answered with type 0 changes;
//!   * type 24 recall and store: scenes (to the app) and the EQ, dynamics and
//!     effects libraries (here);
//!   * type 25 key remote: to the app (the key numbers aren't in the guides);
//!   * type 26 fader and encoder remote: faders 1-21 (MIC 1-16, LINE, STEREO),
//!     the encoders of TAPE 1-16 and EFF1-2 by steps; the others to the app.
//!   * Yamaha bulk dumps and requests (`F0 43 0n 7E`, `F0 43 2n 7E`, the header
//!     `LM  8A67` and a type letter, data as nibble pairs, the ProMix 01's
//!     checksum): scenes (`M`, 127 the edit buffer), the program change table
//!     (`P`), the setup (`S`), the CC assign table (`C`), the EQ, dynamics and
//!     effects library programs (`Q`, `Y`, `E`). The bytes inside are Sidecar's
//!     layout (the guides give only the sizes), so a dump comes back into
//!     Sidecar, not into an 02R.
//! * Setup (MAN p.143, V2 p.159): one Rx / Tx channel; program change Rx, Tx,
//!   OMNI and ECHO; parameter change Rx and Tx; control change Rx.
//!
//! Received values aren't sent back; the console's changes go out as type 0
//! parameter changes (`poll`) once the first poll has taken its values.

use crate::console::{Console, Origin, Y2Strip};
use crate::library::{Kind, Library, Program, PROGRAMS};
use crate::params::{self, SIZE};
use livedaw_dsp::model::ModelSpec;
use livedaw_promix::midi::{frame_dump, parse_frame};
use std::collections::VecDeque;

/// Yamaha's id and the 02R's model id.
const YAMAHA: u8 = 0x43;
const MODEL: u8 = 0x3d;
/// The bulk dumps' header (MAN p.213).
const HEADER: &[u8; 8] = b"LM  8A67";

/// MIDI SETUP (MAN p.143, V2 p.159).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Setup {
    /// The MIDI channel (0-15): received on, sent on.
    pub channel: u8,
    pub pc_rx: bool,
    pub pc_tx: bool,
    pub pc_omni: bool,
    pub pc_echo: bool,
    pub param_rx: bool,
    pub param_tx: bool,
    /// Control changes (Version 2): received only.
    pub cc_rx: bool,
}

impl Default for Setup {
    fn default() -> Self {
        Setup {
            channel: 0,
            pc_rx: true,
            pc_tx: true,
            pc_omni: false,
            pc_echo: false,
            param_rx: true,
            param_tx: true,
            cc_rx: true,
        }
    }
}

/// What a control change moves (V2 p.164's parameter types).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CcTarget {
    /// A strip's fader (a master's too).
    Fader(Y2Strip),
    On(Y2Strip),
    /// A strip's pan: a mono strip's, a stereo strip's side 0 or 1, STEREO's balance.
    Pan(Y2Strip, usize),
    /// An input's send level to AUX 1-8 (0-based).
    Send(Y2Strip, usize),
}

impl CcTarget {
    /// As two bytes (the CC assign table's dump: Sidecar's layout): the kind
    /// (1 fader, 2 ON, 3 pan, 4 send) with a side or aux in its high bits,
    /// and the strip (`params::index_of`).
    pub fn to_bytes(t: Option<CcTarget>) -> [u8; 2] {
        let (kind, extra, s) = match t {
            None => return [0, 0],
            Some(CcTarget::Fader(s)) => (1, 0, s),
            Some(CcTarget::On(s)) => (2, 0, s),
            Some(CcTarget::Pan(s, side)) => (3, side, s),
            Some(CcTarget::Send(s, aux)) => (4, aux, s),
        };
        [kind | (extra.min(7) as u8) << 3, params::index_of(s) as u8]
    }

    pub fn of_bytes(b: [u8; 2]) -> Option<CcTarget> {
        let s = params::strip(b[1] as usize)?;
        let extra = (b[0] >> 3) as usize;
        Some(match b[0] & 7 {
            1 => CcTarget::Fader(s),
            2 => CcTarget::On(s),
            3 => CcTarget::Pan(s, extra.min(1)),
            4 => CcTarget::Send(s, extra.min(7)),
            _ => return None,
        })
    }
}

/// The CC assign table: controllers 0-95 and 102-119 (96-101 can't be used).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CcMap {
    pub map: [Option<CcTarget>; 128],
}

impl Default for CcMap {
    /// The 02R preset map (V2 p.256).
    fn default() -> Self {
        use CcTarget::*;
        let mut map = [None; 128];
        for i in 0..16 {
            map[1 + i] = Some(Fader(Y2Strip::Mic(i)));
            map[41 + i] = Some(On(Y2Strip::Mic(i)));
            map[64 + i] = Some(Pan(Y2Strip::Mic(i), 0));
            map[102 + i] = Some(Send(Y2Strip::Mic(i), 6));
        }
        for l in 0..4 {
            map[17 + l] = Some(Fader(Y2Strip::Line(l)));
            map[57 + l] = Some(On(Y2Strip::Line(l)));
            map[80 + 2 * l] = Some(Pan(Y2Strip::Line(l), 0));
            map[81 + 2 * l] = Some(Pan(Y2Strip::Line(l), 1));
        }
        for e in 0..2 {
            map[21 + e] = Some(Fader(Y2Strip::Eff(e)));
            map[61 + e] = Some(On(Y2Strip::Eff(e)));
            map[88 + 2 * e] = Some(Pan(Y2Strip::Eff(e), 0));
            map[89 + 2 * e] = Some(Pan(Y2Strip::Eff(e), 1));
        }
        map[23] = Some(Fader(Y2Strip::Stereo));
        map[63] = Some(On(Y2Strip::Stereo));
        map[92] = Some(Pan(Y2Strip::Stereo, 0));
        for k in 0..8 {
            map[24 + k] = Some(Fader(Y2Strip::Aux(k)));
            map[33 + k] = Some(Fader(Y2Strip::Bus(k)));
        }
        CcMap { map }
    }
}

/// The 114 assignable controllers: 0-95 and 102-119 (V2 p.164).
fn assignable() -> impl Iterator<Item = usize> {
    (0..120).filter(|cc| !(96..102).contains(cc))
}

/// The program change table: program 0-127 (MIDI's numbering) to scene 0-96, or none.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PcTable {
    pub scenes: [Option<u8>; 128],
}

impl Default for PcTable {
    /// Programs 1-96 recall scenes 1-96, program 97 scene 0 (V2 p.160).
    fn default() -> Self {
        let mut scenes = [None; 128];
        for (p, s) in scenes.iter_mut().enumerate().take(96) {
            *s = Some(p as u8 + 1);
        }
        scenes[96] = Some(0);
        PcTable { scenes }
    }
}

impl PcTable {
    /// The first program that recalls `scene`.
    pub fn program_of(&self, scene: u8) -> Option<u8> {
        self.scenes.iter().position(|s| *s == Some(scene)).map(|p| p as u8)
    }
}

/// What the app should do on the console's behalf.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Event {
    /// Recall scene 1-96 (the protocol recalls scene 0, the initial data, itself).
    RecallScene(u8),
    /// Store the console into scene 1-96.
    StoreScene(u8),
    /// A bulk request for scene 1-96: the app answers with `scene_dump`.
    SceneDumpRequest(u8),
    /// A dump of scene 1-96: its title and edit buffer bytes to store there.
    SceneDump(u8, String, Vec<u8>),
    /// A key remote press (type 25): the key's number and on.
    Key(u16, bool),
    /// An encoder the console doesn't move by itself (type 26 No. 82-87: AUX,
    /// PAN, Q, F, G, ENTRY): its number and steps (-64..63).
    Encoder(u8, i8),
}

/// The bulk formats' data bytes (MAN pp.213-221; Sidecar's sizes where the
/// guide's don't fit its layout: the setup's 128 bytes).
fn data_len(kind: u8) -> Option<usize> {
    Some(match kind {
        b'M' => SIZE,
        b'P' | b'S' => 128,
        b'C' => 228,
        b'E' => 23,
        b'Q' => 18,
        b'Y' => 9,
        _ => return None,
    })
}

/// Whether a dump type has a memory number and a title (scenes, the library
/// programs), else a second letter (a space; the CC table's 0x20).
fn numbered(kind: u8) -> bool {
    matches!(kind, b'M' | b'E' | b'Q' | b'Y')
}

/// A bulk dump of `kind` for `channel`: memory `number` and `title` where it
/// has them (a scene's 16 id bytes, zero, before its title), the data as nibbles.
pub fn dump(channel: u8, kind: u8, number: u8, title: &str, data: &[u8]) -> Vec<u8> {
    let mut body = HEADER.to_vec();
    body.push(kind);
    if numbered(kind) {
        body.push(number & 0x7f);
        if kind == b'M' {
            body.extend([0; 16]);
        }
        let mut t: Vec<u8> = title.bytes().map(|b| if (0x20..0x7f).contains(&b) { b } else { b' ' }).take(16).collect();
        t.resize(16, b' ');
        body.extend(t);
    } else {
        body.push(b' ');
    }
    for &d in data {
        body.push(d >> 4);
        body.push(d & 0xf);
    }
    frame_dump(channel, &body)
}

/// A bulk request of `kind` (memory `number` where it has one).
pub fn request(channel: u8, kind: u8, number: u8) -> Vec<u8> {
    let mut out = vec![0xf0, YAMAHA, 0x20 | (channel & 0xf), 0x7e];
    out.extend_from_slice(HEADER);
    out.push(kind);
    out.push(if numbered(kind) { number } else { b' ' });
    out.push(0xf7);
    out
}

/// A dump's (channel, kind, number, title, data), if `msg` is a well-formed one.
pub fn parse_dump(msg: &[u8]) -> Option<(u8, u8, u8, String, Vec<u8>)> {
    let (channel, body) = parse_frame(msg, HEADER)?;
    let kind = body[8];
    let (number, title, nibbles) = if numbered(kind) {
        let skip = if kind == b'M' { 16 } else { 0 };
        let title = body.get(10 + skip..26 + skip)?;
        (body[9], String::from_utf8_lossy(title).trim_end().to_string(), body.get(26 + skip..)?)
    } else {
        (0, String::new(), &body[10..])
    };
    if nibbles.len() % 2 != 0 || data_len(kind) != Some(nibbles.len() / 2) || nibbles.iter().any(|&n| n > 0xf) {
        return None;
    }
    Some((channel, kind, number, title, nibbles.chunks(2).map(|p| p[0] << 4 | p[1]).collect()))
}

/// A bulk request's (channel, kind, number).
pub fn parse_request(msg: &[u8]) -> Option<(u8, u8, u8)> {
    if msg.len() < 15 || msg[0] != 0xf0 || msg[1] != YAMAHA || msg[2] & 0xf0 != 0x20 || msg[3] != 0x7e || &msg[4..12] != HEADER {
        return None;
    }
    Some((msg[2] & 0xf, msg[12], msg[13]))
}

/// A type 0 parameter change of address `a` to byte `v` (byte operation).
pub fn param_change(channel: u8, a: usize, v: u8) -> Vec<u8> {
    vec![0xf0, YAMAHA, 0x10 | (channel & 0xf), MODEL, 0x00, (a >> 7) as u8 & 0x7f, a as u8 & 0x7f, v >> 4, v & 0xf, 0xf7]
}

/// A type 26 fader remote message: fader `n` (0-20) to `step`.
pub fn fader_remote(channel: u8, n: u8, step: u8) -> Vec<u8> {
    vec![0xf0, YAMAHA, 0x10 | (channel & 0xf), MODEL, 0x1a, n & 0x7f, step & 0x7f, 0xf7]
}

/// The strip fader `n` (0-20) of the panel moves: MIC 1-16, LINE 17/18-23/24, STEREO.
pub fn panel_fader(n: u8) -> Option<Y2Strip> {
    Some(match n {
        0..=15 => Y2Strip::Mic(n as usize),
        16..=19 => Y2Strip::Line(n as usize - 16),
        20 => Y2Strip::Stereo,
        _ => return None,
    })
}

/// A type 24 channel byte's strip (MAN p.211): 0-15 MIC, 16-31 TAPE, 32-35
/// LINE, 36-37 EFF, 38 STEREO, 39-46 BUS 1-8.
pub fn recall_channel(ch: u8) -> Option<Y2Strip> {
    Some(match ch {
        0..=15 => Y2Strip::Mic(ch as usize),
        16..=31 => Y2Strip::Tape(ch as usize - 16),
        32..=35 => Y2Strip::Line(ch as usize - 32),
        36 | 37 => Y2Strip::Eff(ch as usize - 36),
        38 => Y2Strip::Stereo,
        39..=46 => Y2Strip::Bus(ch as usize - 39),
        _ => return None,
    })
}

/// A library program in a dump's bytes (Sidecar's layout): its model's index
/// in its table, then each value's step, as one byte (EQ and dynamics) or two
/// (the effects: high 7 bits, low 7 bits).
fn program_bytes(kind: Kind, p: &Program) -> Vec<u8> {
    let (table, wide) = tables(kind);
    let model = table.iter().position(|m| m.token == p.token).unwrap_or(0);
    let mut out = vec![model as u8];
    let spec = &table[model];
    for (ps, v) in spec.params.iter().zip(&p.values) {
        let step = crate::console::param_step(ps, *v);
        if wide {
            out.extend([(step >> 7) as u8 & 0x7f, step as u8 & 0x7f]);
        } else {
            out.push(step.min(255) as u8);
        }
    }
    out.resize(data_len(kind_letter(kind)).unwrap_or(0), 0);
    out
}

fn program_of_bytes(kind: Kind, title: &str, data: &[u8]) -> Option<Program> {
    let (table, wide) = tables(kind);
    let spec = table.get(*data.first()? as usize)?;
    let mut values = Vec::new();
    for (k, ps) in spec.params.iter().enumerate() {
        let step =
            if wide { (*data.get(1 + 2 * k)? as u16) << 7 | *data.get(2 + 2 * k)? as u16 } else { *data.get(1 + k)? as u16 };
        values.push(crate::console::step_param(ps, step));
    }
    Some(Program { name: title.into(), token: spec.token, values })
}

fn tables(kind: Kind) -> (&'static [ModelSpec], bool) {
    match kind {
        Kind::Eq => (livedaw_dsp::y02r::EQ, false),
        Kind::Dynamics => (livedaw_dsp::y02r::DYN_MODELS, false),
        Kind::Effects => (&livedaw_dsp::y02r::fx::FX_MODELS, true),
    }
}

fn kind_letter(kind: Kind) -> u8 {
    match kind {
        Kind::Eq => b'Q',
        Kind::Dynamics => b'Y',
        Kind::Effects => b'E',
    }
}

fn kind_of_letter(letter: u8) -> Option<Kind> {
    Some(match letter {
        b'Q' => Kind::Eq,
        b'Y' => Kind::Dynamics,
        b'E' => Kind::Effects,
        _ => return None,
    })
}

/// The MIDI SETUP as the setup dump's bytes (Sidecar's layout).
pub fn setup_bytes(s: &Setup) -> Vec<u8> {
    let mut out = vec![
        s.channel,
        s.pc_rx as u8,
        s.pc_tx as u8,
        s.pc_omni as u8,
        s.pc_echo as u8,
        s.param_rx as u8,
        s.param_tx as u8,
        s.cc_rx as u8,
    ];
    out.resize(128, 0);
    out
}

fn setup_of_bytes(d: &[u8]) -> Setup {
    let b = |i: usize| d.get(i).is_some_and(|&x| x != 0);
    Setup {
        channel: d.first().copied().unwrap_or(0) & 0xf,
        pc_rx: b(1),
        pc_tx: b(2),
        pc_omni: b(3),
        pc_echo: b(4),
        param_rx: b(5),
        param_tx: b(6),
        cc_rx: b(7),
    }
}

/// The 02R's MIDI: its setup and tables, the edit buffer as last sent, and
/// what waits to go out.
pub struct Midi {
    pub setup: Setup,
    pub cc_map: CcMap,
    pub pc_table: PcTable,
    last: Vec<u8>,
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

    fn is_ours(&self, status: u8) -> bool {
        status & 0xf == self.setup.channel
    }

    /// Handles one received message (a channel message or a whole SysEx).
    pub fn receive(&mut self, c: &mut Console, lib: &mut Library, msg: &[u8]) {
        let Some(&status) = msg.first() else { return };
        match status & 0xf0 {
            0xf0 if status == 0xf0 => self.receive_sysex(c, lib, msg),
            0xc0 if msg.len() >= 2 => {
                if self.setup.pc_echo {
                    self.out.push_back(msg.to_vec());
                }
                if !self.setup.pc_rx || !(self.setup.pc_omni || self.is_ours(status)) {
                    return;
                }
                match self.pc_table.scenes[(msg[1] & 0x7f) as usize] {
                    // Scene 0: the initial data, here.
                    Some(0) => self.recall_initial(c),
                    Some(scene) => self.events.push_back(Event::RecallScene(scene)),
                    None => {}
                }
            }
            0xb0 if msg.len() >= 3 && self.setup.cc_rx && self.is_ours(status) => {
                if let Some(t) = self.cc_map.map[(msg[1] & 0x7f) as usize] {
                    self.receive_cc(c, t, msg[2] & 0x7f);
                    self.remember(c);
                }
            }
            _ => {}
        }
    }

    /// A control change's value onto its target: faders and sends on their
    /// steps, ON at 64 and up, pans 0-127 onto 0-32.
    fn receive_cc(&mut self, c: &mut Console, t: CcTarget, v: u8) {
        let pan = ((v as u32 * 32 + 63) / 127) as u16;
        match t {
            CcTarget::Fader(s) => c.set_level(s, v as u16, Origin::Remote),
            CcTarget::On(s) => c.set_on(s, v >= 64, Origin::Remote),
            CcTarget::Pan(s, side) if s.stereo_index().is_some() => c.set_stereo_pan(s, side, pan),
            CcTarget::Pan(s, _) => c.set_pan(s, pan, Origin::Remote),
            CcTarget::Send(s, aux) => c.set_aux_level(s, aux, v as u16),
        }
    }

    /// Recalls scene 0, the initial data.
    pub fn recall_initial(&mut self, c: &mut Console) {
        params::load(c, params::initial_bytes());
        self.remember(c);
    }

    /// Takes the edit buffer as it is (a received change isn't sent back).
    fn remember(&mut self, c: &Console) {
        if self.primed {
            self.last = params::bytes(c);
        }
    }

    fn receive_sysex(&mut self, c: &mut Console, lib: &mut Library, msg: &[u8]) {
        if let Some((ch, kind, number)) = parse_request(msg) {
            if ch == self.setup.channel {
                self.answer_request(c, lib, kind, number);
            }
            return;
        }
        if let Some((ch, kind, number, title, data)) = parse_dump(msg) {
            if ch == self.setup.channel {
                self.take_dump(c, lib, kind, number, title, data);
            }
            return;
        }
        if msg.len() < 6 || msg[1] != YAMAHA || msg[3] != MODEL || msg[2] & 0xf != self.setup.channel {
            return;
        }
        match msg[2] & 0xf0 {
            0x10 if self.setup.param_rx => self.parameter_change(c, lib, &msg[4..msg.len() - 1]),
            0x30 if self.setup.param_tx && msg.len() >= 9 && msg[4] & 0x3f == 0 => {
                // A request of `count` bytes from an address: their changes.
                let a = (msg[5] as usize) << 7 | msg[6] as usize;
                for addr in a..(a + msg[7].max(1) as usize).min(SIZE) {
                    self.out.push_back(param_change(self.setup.channel, addr, params::get(c, addr)));
                }
            }
            _ => {}
        }
    }

    /// A parameter change's bytes after the header (`tt` first, `F7` gone).
    fn parameter_change(&mut self, c: &mut Console, lib: &mut Library, d: &[u8]) {
        let Some(&tt) = d.first() else { return };
        match tt {
            // Type 0, a byte operation: an address and nibble pairs for it and the next ones.
            0x00 if d.len() >= 5 => {
                let a = (d[1] as usize) << 7 | d[2] as usize;
                for (k, pair) in d[3..].chunks_exact(2).enumerate() {
                    if a + k < SIZE {
                        params::set(c, a + k, pair[0] << 4 | (pair[1] & 0xf));
                    }
                }
                self.remember(c);
            }
            // A bit operation: bits 0-3 the bit, bit 4 set or reset.
            0x40 if d.len() >= 4 => {
                let a = (d[1] as usize) << 7 | d[2] as usize;
                if a < SIZE {
                    let bit = d[3] & 0xf;
                    let byte = params::get(c, a);
                    let v = if d[3] & 0x10 != 0 { byte | 1 << bit.min(7) } else { byte & !(1 << bit.min(7)) };
                    params::set(c, a, v);
                    self.remember(c);
                }
            }
            0x18 if d.len() >= 4 => self.recall_store(c, lib, d[1], d[2], d[3]),
            0x19 if d.len() >= 4 => self.events.push_back(Event::Key((d[1] as u16) << 7 | d[2] as u16, d[3] != 0)),
            0x1a if d.len() >= 3 => {
                let (n, v) = (d[1], d[2] & 0x7f);
                match n {
                    0..=20 => {
                        if let Some(s) = panel_fader(n) {
                            c.set_level(s, v as u16, Origin::Remote);
                        }
                    }
                    64..=81 => {
                        let s = if n < 80 { Y2Strip::Tape(n as usize - 64) } else { Y2Strip::Eff(n as usize - 80) };
                        let delta = if v >= 64 { v as i32 - 128 } else { v as i32 };
                        let to = (c.level(s) as i32 + delta).clamp(0, 127) as u16;
                        c.set_level(s, to, Origin::Remote);
                    }
                    82..=87 => self.events.push_back(Event::Encoder(n, if v >= 64 { v as i8 - 64 - 64 } else { v as i8 })),
                    _ => {}
                }
                self.remember(c);
            }
            _ => {}
        }
    }

    /// Type 24 (MAN p.211): recall (0-4) or store (0x10-0x14) a scene or a
    /// library program; the channel names the strip (or effect) it is for.
    fn recall_store(&mut self, c: &mut Console, lib: &mut Library, instr: u8, number: u8, ch: u8) {
        if ch >= 64 && ch != 62 {
            return; // the undos
        }
        let n = number as usize;
        let strip = recall_channel(ch);
        let fx = matches!(ch, 36 | 37).then(|| ch as usize - 36);
        match instr {
            0x00 if number == 0 => self.recall_initial(c),
            0x00 if number <= 96 => self.events.push_back(Event::RecallScene(number)),
            0x10 if (1..=96).contains(&number) => self.events.push_back(Event::StoreScene(number)),
            0x01 => {
                if let Some(s) = strip {
                    c.recall_eq(lib, s, n);
                }
            }
            0x02 => {
                if let Some(s) = strip {
                    c.recall_dyn(lib, s, n);
                }
            }
            0x03 => {
                if let Some(slot) = fx {
                    c.recall_fx(lib, slot, n);
                }
            }
            0x11 => {
                if let Some(s) = strip {
                    c.store_eq(lib, s, n, &format!("EQ {}", n + 1));
                }
            }
            0x12 => {
                if let Some(s) = strip {
                    c.store_dyn(lib, s, n, &format!("DYN {}", n + 1));
                }
            }
            0x13 => {
                if let Some(slot) = fx {
                    c.store_fx(lib, slot, n, &format!("EFFECT {}", n + 1));
                }
            }
            _ => {}
        }
        self.remember(c);
    }

    fn answer_request(&mut self, c: &Console, lib: &Library, kind: u8, number: u8) {
        let ch = self.setup.channel;
        let msg = match kind {
            b'M' if number == 127 => dump(ch, b'M', 127, "", &params::bytes(c)),
            b'M' if (1..=96).contains(&number) => {
                self.events.push_back(Event::SceneDumpRequest(number));
                return;
            }
            b'P' => dump(ch, b'P', 0, "", &self.pc_table.scenes.map(|s| s.unwrap_or(0x7f))),
            b'S' => dump(ch, b'S', 0, "", &setup_bytes(&self.setup)),
            b'C' => {
                let data: Vec<u8> = assignable().flat_map(|cc| CcTarget::to_bytes(self.cc_map.map[cc])).collect();
                dump(ch, b'C', 0, "", &data)
            }
            k => {
                let Some(kind) = kind_of_letter(k) else { return };
                let Some(p) = lib.get(kind, number as usize) else { return };
                dump(ch, k, number, &p.name, &program_bytes(kind, p))
            }
        };
        self.out.push_back(msg);
    }

    fn take_dump(&mut self, c: &mut Console, lib: &mut Library, kind: u8, number: u8, title: String, data: Vec<u8>) {
        match kind {
            b'M' if number == 127 => {
                params::load(c, &data);
                self.remember(c);
            }
            b'M' if (1..=96).contains(&number) => self.events.push_back(Event::SceneDump(number, title, data)),
            b'P' => {
                for (s, &d) in self.pc_table.scenes.iter_mut().zip(&data) {
                    *s = (d <= 96).then_some(d);
                }
            }
            b'S' => self.setup = setup_of_bytes(&data),
            b'C' => {
                for (cc, pair) in assignable().zip(data.chunks_exact(2)) {
                    self.cc_map.map[cc] = CcTarget::of_bytes([pair[0], pair[1]]);
                }
            }
            k => {
                let Some(kind) = kind_of_letter(k) else { return };
                if (number as usize) < PROGRAMS {
                    if let Some(p) = program_of_bytes(kind, &title, &data) {
                        lib.store(kind, number as usize, p);
                    }
                }
            }
        }
    }

    /// Sends the console's changes since the last poll as type 0 parameter
    /// changes (the first poll takes its values and sends nothing).
    pub fn poll(&mut self, c: &Console) {
        let now = params::bytes(c);
        if self.primed && self.setup.param_tx {
            for (a, (&v, &was)) in now.iter().zip(&self.last).enumerate() {
                if v != was {
                    self.out.push_back(param_change(self.setup.channel, a, v));
                }
            }
        }
        self.last = now;
        self.primed = true;
    }

    /// Scene `scene` (0-96) was recalled on the console: its program change.
    pub fn scene_recalled(&mut self, scene: u8) {
        if !self.setup.pc_tx {
            return;
        }
        if let Some(p) = self.pc_table.program_of(scene) {
            self.out.push_back(vec![0xc0 | self.setup.channel, p]);
        }
    }

    /// A scene's dump (answering `Event::SceneDumpRequest`).
    pub fn scene_dump(&mut self, scene: u8, title: &str, data: &[u8]) {
        self.out.push_back(dump(self.setup.channel, b'M', scene, title, data));
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

    fn drain(m: &mut Midi) -> Vec<Vec<u8>> {
        std::iter::from_fn(|| m.take_out()).collect()
    }

    #[test]
    fn dumps_frame_and_parse() {
        let d = dump(3, b'Q', 40, "MY EQ", &[0xab; 18]);
        assert_eq!(&d[..4], &[0xf0, 0x43, 0x03, 0x7e]);
        let count = ((d[4] as usize) << 7) | d[5] as usize;
        assert_eq!(count, 62, "the EQ library's count (MAN p.218)");
        assert_eq!(parse_dump(&d), Some((3, b'Q', 40, "MY EQ".into(), vec![0xab; 18])));
        let scene = dump(0, b'M', 5, "SONG 1", &[1; SIZE]);
        assert_eq!(((scene[4] as usize) << 7) | scene[5] as usize, 4074, "a scene's count (MAN p.213)");
        assert_eq!(parse_dump(&scene).unwrap().3, "SONG 1");
        let pc = dump(0, b'P', 0, "", &[0; 128]);
        assert_eq!(((pc[4] as usize) << 7) | pc[5] as usize, 266);
        let mut bad = d.clone();
        bad[20] ^= 1;
        assert_eq!(parse_dump(&bad), None, "the checksum");
        assert_eq!(parse_request(&request(2, b'M', 127)), Some((2, b'M', 127)));
    }

    #[test]
    fn program_and_control_changes() {
        let e = y02r();
        let (mut s, mut lib, mut m) = (State::default(), Library::default(), Midi::default());
        let mut c = Console::new(&e, &mut s);
        m.receive(&mut c, &mut lib, &[0xc0, 4]);
        m.receive(&mut c, &mut lib, &[0xc1, 4]);
        assert_eq!(std::iter::from_fn(|| m.take_event()).collect::<Vec<_>>(), [Event::RecallScene(5)]);
        // Program 97: scene 0, the initial data, here.
        c.set_level(Y2Strip::Mic(0), 100, Origin::Panel);
        m.receive(&mut c, &mut lib, &[0xc0, 96]);
        assert_eq!((c.level(Y2Strip::Mic(0)), m.take_event()), (0, None));
        // The 02R preset map: CC 3 MIC 3's fader, CC 63 STEREO ON, CC 81 LINE 17/18's R pan, CC 110 MIC 9's AUX 7 send.
        m.receive(&mut c, &mut lib, &[0xb0, 3, 103]);
        m.receive(&mut c, &mut lib, &[0xb0, 63, 0]);
        m.receive(&mut c, &mut lib, &[0xb0, 81, 127]);
        m.receive(&mut c, &mut lib, &[0xb0, 110, 90]);
        assert_eq!((c.level(Y2Strip::Mic(2)), c.on(Y2Strip::Stereo)), (103, false));
        assert_eq!((c.stereo_pan(Y2Strip::Line(0), 1), c.aux_level(Y2Strip::Mic(8), 6)), (32, 90));
        assert_eq!(m.pc_table.program_of(96), Some(95));
    }

    #[test]
    fn parameter_changes_and_the_console_changes() {
        let e = y02r();
        let (mut s, mut lib, mut m) = (State::default(), Library::default(), Midi::default());
        let mut c = Console::new(&e, &mut s);
        m.poll(&c);
        assert!(drain(&mut m).is_empty(), "nothing at first");
        // Type 0: TAPE 1's fader (strip 20, item 0) to 100; not sent back.
        let a = 20 * params::PER_STRIP;
        m.receive(&mut c, &mut lib, &param_change(0, a, 100));
        assert_eq!(c.level(Y2Strip::Tape(0)), 100);
        m.poll(&c);
        assert!(drain(&mut m).is_empty(), "received, not echoed");
        // A bit operation: MIC 1's phase (switches, bit 0) set.
        m.receive(&mut c, &mut lib, &[0xf0, 0x43, 0x10, 0x3d, 0x40, 0, 5, 0x10, 0xf7]);
        assert!(c.phase(Y2Strip::Mic(0)));
        // Type 26: fader 21 (STEREO) and TAPE 3's encoder by -3 steps.
        m.receive(&mut c, &mut lib, &fader_remote(0, 20, 127));
        c.set_level(Y2Strip::Tape(2), 50, Origin::Panel);
        m.poll(&c);
        assert_eq!(drain(&mut m), vec![param_change(0, 22 * params::PER_STRIP, 50)], "the console's change goes out");
        m.receive(&mut c, &mut lib, &[0xf0, 0x43, 0x10, 0x3d, 0x1a, 66, 125, 0xf7]);
        assert_eq!((c.level(Y2Strip::Stereo), c.level(Y2Strip::Tape(2))), (127, 47));
        // A request of 2 bytes from MIC 1's fader.
        m.receive(&mut c, &mut lib, &[0xf0, 0x43, 0x30, 0x3d, 0x00, 0, 0, 2, 0xf7]);
        assert_eq!(drain(&mut m).len(), 2);
        // Another channel: nothing.
        m.receive(&mut c, &mut lib, &param_change(5, a, 1));
        assert_eq!(c.level(Y2Strip::Tape(0)), 100);
    }

    #[test]
    fn recall_store_and_library_dumps() {
        let e = y02r();
        let (mut s, mut lib, mut m) = (State::default(), Library::default(), Midi::default());
        let mut c = Console::new(&e, &mut s);
        // Type 24: EQ program 9 onto TAPE 1 (channel 16); effects program 28 into EFF2; scene 3.
        m.receive(&mut c, &mut lib, &[0xf0, 0x43, 0x10, 0x3d, 0x18, 0x01, 8, 16, 0xf7]);
        assert_eq!(c.eq_value(Y2Strip::Tape(0), 0, 2), 41);
        m.receive(&mut c, &mut lib, &[0xf0, 0x43, 0x10, 0x3d, 0x18, 0x03, 27, 37, 0xf7]);
        assert_eq!(c.state.fx_programs[1], 27);
        m.receive(&mut c, &mut lib, &[0xf0, 0x43, 0x10, 0x3d, 0x18, 0x00, 3, 0, 0xf7]);
        m.receive(&mut c, &mut lib, &[0xf0, 0x43, 0x10, 0x3d, 0x18, 0x10, 4, 0, 0xf7]);
        assert_eq!(std::iter::from_fn(|| m.take_event()).collect::<Vec<_>>(), [Event::RecallScene(3), Event::StoreScene(4)]);
        // An effects program's dump comes back as the same program.
        m.receive(&mut c, &mut lib, &request(0, b'E', 22));
        let out = drain(&mut m);
        let (_, kind, n, title, data) = parse_dump(&out[0]).unwrap();
        assert_eq!((kind, n, title.as_str(), data.len()), (b'E', 22, "DELAY L-C-R 1", 23));
        let back = program_of_bytes(Kind::Effects, &title, &data).unwrap();
        let orig = lib.get(Kind::Effects, 22).unwrap();
        assert_eq!(back.token, orig.token);
        for (a, b) in back.values.iter().zip(&orig.values) {
            assert!((a - b).abs() < 0.051, "{a} {b}");
        }
        // Into a user program.
        let user = dump(0, b'E', 50, "MY DELAY", &data);
        m.receive(&mut c, &mut lib, &user);
        assert_eq!(lib.get(Kind::Effects, 50).unwrap().name, "MY DELAY");
        // The edit buffer, the PC table and the CC table round trip.
        c.set_level(Y2Strip::Mic(0), 77, Origin::Panel);
        m.receive(&mut c, &mut lib, &request(0, b'M', 127));
        m.receive(&mut c, &mut lib, &request(0, b'C', 0));
        m.receive(&mut c, &mut lib, &request(0, b'P', 0));
        let dumps = drain(&mut m);
        assert_eq!(dumps.len(), 3);
        let (mut s2, mut m2) = (State::default(), Midi::default());
        m2.cc_map.map = [None; 128];
        m2.pc_table.scenes = [None; 128];
        let e2 = y02r();
        let mut c2 = Console::new(&e2, &mut s2);
        for d in &dumps {
            m2.receive(&mut c2, &mut lib, d);
        }
        assert_eq!(c2.level(Y2Strip::Mic(0)), 77);
        assert_eq!(m2.cc_map, CcMap::default());
        assert_eq!(m2.pc_table, PcTable::default());
        // A scene request goes to the app.
        m.receive(&mut c, &mut lib, &request(0, b'M', 7));
        assert_eq!(m.take_event(), Some(Event::SceneDumpRequest(7)));
    }
}
