//! The Allen & Heath dLive's MIDI over TCP/IP (the maker's protocol document for
//! firmware V2.0): the channel numbering across five MIDI channels, every message
//! of the document parsed ([`Decoder`]) and built ([`Msg::encode`]), and what the
//! engine has a counterpart for mapped to it ([`Midi`]).
//!
//! The value tables follow the document's formulas and printed values; where the
//! two differ, the comments below say which was used. Sidecar's reading where it's
//! silent: a stereo input channel answers to both of its input numbers and speaks
//! as the first; a client's own change isn't echoed to it; a name or colour changed
//! on the console goes out in the form of its Get reply.
//!
//! Parsed and ignored, for want of a counterpart in the engine: mains 2 to 6, the UFX
//! (RackUltra) sends, returns and parameters, the Socket Preamp Pad (its Get answers
//! Off), the Surface's Cue List Recall and Go / Next / Previous (user-assigned ids),
//! MIDI Strips, and the document's TLS ports and authentication (no listener).

use crate::Event;
use livedaw_dsp::fader::FADER_MIN;
use livedaw_engine::profile::DLiveLayout;
use livedaw_engine::{Engine, Profile, Strip, StripKind};
use std::collections::{HashMap, VecDeque};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

/// The manufacturer and model bytes of every SysEx, then the version (major, minor).
const SYSEX_ID: [u8; 6] = [0xF0, 0x00, 0x00, 0x1A, 0x50, 0x10];
const SYSEX_VERSION: [u8; 2] = [0x01, 0x00];

/// The audio channel types are spread over five MIDI channels, N to N + 4.
const SPAN: u8 = 5;

/// The longest name kept (the document gives no limit).
pub const MAX_NAME: usize = 64;

/// The MIDI channels a base channel can be: 1 to 12 (0 to B).
pub const MAX_BASE: u8 = 11;

// ---- the channel numbers -------------------------------------------------------------

/// Something the protocol's channel numbers address, each from 0.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Chan {
    Input(u8),
    Group(u8),
    StGroup(u8),
    Aux(u8),
    StAux(u8),
    Matrix(u8),
    StMatrix(u8),
    FxSend(u8),
    StFxSend(u8),
    FxReturn(u8),
    Main(u8),
    Dca(u8),
    MuteGroup(u8),
    UfxSend(u8),
    UfxReturn(u8),
}

impl Chan {
    /// The channel at `(offset from N, note number)` ("Channel Selection").
    pub fn decode(offset: u8, ch: u8) -> Option<Self> {
        Some(match (offset, ch) {
            (0, 0x00..=0x7F) => Chan::Input(ch),
            (1, 0x00..=0x3D) => Chan::Group(ch),
            (1, 0x40..=0x5E) => Chan::StGroup(ch - 0x40),
            (2, 0x00..=0x3D) => Chan::Aux(ch),
            (2, 0x40..=0x5E) => Chan::StAux(ch - 0x40),
            (3, 0x00..=0x3D) => Chan::Matrix(ch),
            (3, 0x40..=0x5E) => Chan::StMatrix(ch - 0x40),
            (4, 0x00..=0x0F) => Chan::FxSend(ch),
            (4, 0x10..=0x1F) => Chan::StFxSend(ch - 0x10),
            (4, 0x20..=0x2F) => Chan::FxReturn(ch - 0x20),
            (4, 0x30..=0x35) => Chan::Main(ch - 0x30),
            (4, 0x36..=0x4D) => Chan::Dca(ch - 0x36),
            (4, 0x4E..=0x55) => Chan::MuteGroup(ch - 0x4E),
            (4, 0x56..=0x5D) => Chan::UfxSend(ch - 0x56),
            (4, 0x5E..=0x65) => Chan::UfxReturn(ch - 0x5E),
            _ => return None,
        })
    }

    /// Its offset from N and its note number.
    pub fn encode(self) -> (u8, u8) {
        match self {
            Chan::Input(i) => (0, i),
            Chan::Group(i) => (1, i),
            Chan::StGroup(i) => (1, i + 0x40),
            Chan::Aux(i) => (2, i),
            Chan::StAux(i) => (2, i + 0x40),
            Chan::Matrix(i) => (3, i),
            Chan::StMatrix(i) => (3, i + 0x40),
            Chan::FxSend(i) => (4, i),
            Chan::StFxSend(i) => (4, i + 0x10),
            Chan::FxReturn(i) => (4, i + 0x20),
            Chan::Main(i) => (4, i + 0x30),
            Chan::Dca(i) => (4, i + 0x36),
            Chan::MuteGroup(i) => (4, i + 0x4E),
            Chan::UfxSend(i) => (4, i + 0x56),
            Chan::UfxReturn(i) => (4, i + 0x5E),
        }
    }
}

// ---- the values ----------------------------------------------------------------------

/// A fader or send level in dB as LV: INT(((dB + 54) / 64) * 7F), the document's
/// formula, which lands on its table's decimals (0 dB = 107, -40 dB = 27, +10 dB =
/// 127; the table's hex column has 74 for +5 dB, its decimals and the formula 117).
/// A tiny margin keeps a level that came from [`fader_db`] on its step.
pub fn fader_value(db: f32) -> u8 {
    if db <= -54.0 {
        return 0;
    }
    (((f64::from(db) + 54.0) / 64.0 * 127.0) + 1e-3).floor().clamp(0.0, 127.0) as u8
}

/// The level of LV (0 is -inf): the formula's inverse, on a whole dB if that's the
/// same step (0x6B is 0 dB, not -0.08).
pub fn fader_db(v: u8) -> f32 {
    if v == 0 {
        return FADER_MIN;
    }
    let db = f32::from(v.min(127)) * 64.0 / 127.0 - 54.0;
    let whole = db.round();
    if fader_value(whole) == v.min(127) {
        whole
    } else {
        db
    }
}

/// The table's preamp gains (dB, GV). The printed values are the formula
/// INT(((dB - 5) / 55) * 7F) except at +10 and +30 dB, where the table has one
/// more: the table is used, with straight lines between its points.
const GAIN_TABLE: [(f32, f32); 12] = [
    (5.0, 0.0),
    (10.0, 12.0),
    (15.0, 23.0),
    (20.0, 34.0),
    (25.0, 46.0),
    (30.0, 58.0),
    (35.0, 69.0),
    (40.0, 80.0),
    (45.0, 92.0),
    (50.0, 103.0),
    (55.0, 115.0),
    (60.0, 127.0),
];

/// A preamp gain (dB) as GV.
pub fn gain_value(db: f32) -> u8 {
    let db = db.clamp(5.0, 60.0);
    for w in GAIN_TABLE.windows(2) {
        if db <= w[1].0 {
            let t = (db - w[0].0) / (w[1].0 - w[0].0);
            return (w[0].1 + t * (w[1].1 - w[0].1)).round().clamp(0.0, 127.0) as u8;
        }
    }
    127
}

/// The gain of GV, in dB.
pub fn gain_db(v: u8) -> f32 {
    let v = f32::from(v.min(127));
    for w in GAIN_TABLE.windows(2) {
        if v <= w[1].1 {
            let t = (v - w[0].1) / (w[1].1 - w[0].1);
            return w[0].0 + t * (w[1].0 - w[0].0);
        }
    }
    60.0
}

/// A frequency as VV: INT(127 * ((4608 * log2(F / 4)) - 10699) / span), the
/// document's formula with `span` 45922 for the PEQ, 41314 for the HPF.
fn freq_value(hz: f32, span: f32) -> u8 {
    let x = 127.0 * (4608.0 * f64::from(hz.max(1.0) / 4.0).log2() - 10699.0) / f64::from(span);
    (x + 1e-3).floor().clamp(0.0, 127.0) as u8
}

/// The frequency a VV stands for: its step's centre (the ends are the ends).
fn freq_hz(v: u8, span: f32, max: f32) -> f32 {
    let v = v.min(127);
    if v == 0 {
        return 20.0;
    }
    if v == 127 {
        return max;
    }
    let x = (f64::from(v) + 0.5) / 127.0 * f64::from(span) + 10699.0;
    ((4.0 * (x / 4608.0).exp2()) as f32).clamp(20.0, max)
}

const PEQ_SPAN: f32 = 45922.0;
const HPF_SPAN: f32 = 41314.0;
/// The HPF's own range (its screen's: 20 Hz to 2 kHz; the formula runs past it).
const HPF_MAX: f32 = 2000.0;
const PEQ_MAX: f32 = 20000.0;

/// A PEQ band's gain as VV: (GAIN + 15) * 126 / 30, the formula and the table's
/// decimals (+15 dB is 126, though its hex column has 7F; both are taken).
fn eq_gain_value(db: f32) -> u8 {
    (((db + 15.0) * 126.0 / 30.0).round()).clamp(0.0, 127.0) as u8
}

fn eq_gain_db(v: u8) -> f32 {
    (f32::from(v) * 30.0 / 126.0 - 15.0).clamp(-15.0, 15.0)
}

/// The document's width table, in octaves, by VV.
const WIDTHS: [f32; 25] = [
    1.5,
    1.4,
    1.3,
    1.2,
    1.1,
    1.0,
    0.95,
    0.9,
    0.85,
    0.8,
    0.75,
    0.7,
    2.0 / 3.0,
    0.6,
    0.55,
    0.5,
    0.45,
    0.4,
    1.0 / 3.0,
    0.3,
    0.25,
    0.2,
    1.0 / 6.0,
    0.13,
    1.0 / 9.0,
];

fn width_value(octaves: f32) -> u8 {
    let (mut best, mut err) = (0, f32::MAX);
    for (i, w) in WIDTHS.iter().enumerate() {
        let e = (octaves / w).ln().abs();
        if e < err {
            (best, err) = (i, e);
        }
    }
    best as u8
}

fn width_octaves(v: u8) -> Option<f32> {
    WIDTHS.get(usize::from(v)).copied()
}

// ---- the messages --------------------------------------------------------------------

/// One message of the protocol, parsed or to be built.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Msg {
    Mute {
        chan: Chan,
        on: bool,
    },
    GetMute(Chan),
    Fader {
        chan: Chan,
        level: u8,
    },
    GetFader(Chan),
    MainAssign {
        chan: Chan,
        on: bool,
    },
    GetMainAssign(Chan),
    /// To an Aux, FX send or Matrix.
    SendLevel {
        src: Chan,
        dest: Chan,
        level: u8,
    },
    GetSendLevel {
        src: Chan,
        dest: Chan,
    },
    /// To a Group or Aux.
    SendOn {
        src: Chan,
        dest: Chan,
        on: bool,
    },
    GetSendOn {
        src: Chan,
        dest: Chan,
    },
    DcaAssign {
        chan: Chan,
        dca: u8,
        on: bool,
    },
    MuteGroupAssign {
        chan: Chan,
        group: u8,
        on: bool,
    },
    PreampGain {
        socket: u8,
        gv: u8,
    },
    GetPreampGain(u8),
    PreampPad {
        socket: u8,
        on: bool,
    },
    GetPreampPad(u8),
    PreampPadReply {
        socket: u8,
        on: bool,
    },
    Phantom {
        socket: u8,
        on: bool,
    },
    GetPhantom(u8),
    PhantomReply {
        socket: u8,
        on: bool,
    },
    SetName {
        chan: Chan,
        name: String,
    },
    GetName(Chan),
    NameReply {
        chan: Chan,
        name: String,
    },
    SetColour {
        chan: Chan,
        colour: u8,
    },
    GetColour(Chan),
    ColourReply {
        chan: Chan,
        colour: u8,
    },
    /// Scene 1 to 500 (MixRack only).
    SceneRecall(u16),
    /// Recall Id 0 to 1999 (Surface only).
    CueRecall(u16),
    /// A parameter ID 1A to 29, by [`eq_field`].
    Eq {
        chan: Chan,
        param: u8,
        value: u8,
    },
    GetEq {
        chan: Chan,
        param: u8,
    },
    HpfFreq {
        chan: Chan,
        value: u8,
    },
    GetHpfFreq(Chan),
    HpfOn {
        chan: Chan,
        on: bool,
    },
    GetHpfOn(Chan),
    UfxKey(u8),
    UfxScale(u8),
}

fn bool7(on: bool) -> u8 {
    if on {
        0x7F
    } else {
        0x00
    }
}

impl Msg {
    /// The bytes on the wire, with N = `base`. Status bytes are left out inside a
    /// message as the document shows them in brackets (running status).
    pub fn encode(&self, base: u8) -> Vec<u8> {
        let status = |s: u8, chan: Chan| s | (base + chan.encode().0);
        let nrpn = |chan: Chan, param: u8, value: u8| -> Vec<u8> {
            vec![status(0xB0, chan), 0x63, chan.encode().1, 0x62, param, 0x06, value]
        };
        let sysex = |n: u8, body: &[u8]| {
            let mut v = Vec::with_capacity(SYSEX_ID.len() + SYSEX_VERSION.len() + 2 + body.len());
            v.extend(SYSEX_ID);
            v.extend(SYSEX_VERSION);
            v.push(n);
            v.extend(body);
            v.push(0xF7);
            v
        };
        let n = |c: Chan| base + c.encode().0;
        let ch = |c: Chan| c.encode().1;
        let ascii = |s: &str| s.bytes().map(|b| if (0x20..0x7F).contains(&b) { b } else { b'?' }).collect::<Vec<u8>>();
        match self {
            Msg::Mute { chan, on } => {
                let v = if *on { 0x7F } else { 0x3F };
                vec![status(0x90, *chan), ch(*chan), v, ch(*chan), 0x00]
            }
            Msg::GetMute(c) => sysex(n(*c), &[0x05, 0x09, ch(*c)]),
            Msg::Fader { chan, level } => nrpn(*chan, 0x17, *level),
            Msg::GetFader(c) => sysex(n(*c), &[0x05, 0x0B, 0x17, ch(*c)]),
            Msg::MainAssign { chan, on } => nrpn(*chan, 0x18, if *on { 0x7F } else { 0x3F }),
            Msg::GetMainAssign(c) => sysex(n(*c), &[0x05, 0x0B, 0x18, ch(*c)]),
            Msg::SendLevel { src, dest, level } => sysex(n(*src), &[0x0D, ch(*src), n(*dest), ch(*dest), *level]),
            Msg::GetSendLevel { src, dest } => sysex(n(*src), &[0x05, 0x0F, 0x0D, ch(*src), n(*dest), ch(*dest)]),
            Msg::SendOn { src, dest, on } => {
                sysex(n(*src), &[0x0E, ch(*src), n(*dest), ch(*dest), if *on { 0x7F } else { 0x3F }])
            }
            Msg::GetSendOn { src, dest } => sysex(n(*src), &[0x05, 0x0F, 0x0E, ch(*src), n(*dest), ch(*dest)]),
            Msg::DcaAssign { chan, dca, on } => nrpn(*chan, 0x40, if *on { 0x40 + *dca } else { *dca }),
            Msg::MuteGroupAssign { chan, group, on } => nrpn(*chan, 0x40, if *on { 0x58 + *group } else { 0x18 + *group }),
            Msg::PreampGain { socket, gv } => vec![0xE0 | base, *socket, *gv],
            Msg::GetPreampGain(s) => sysex(base, &[0x05, 0x0B, 0x19, *s]),
            Msg::PreampPad { socket, on } => sysex(base, &[0x09, *socket, if *on { 0x7F } else { 0x3F }]),
            Msg::GetPreampPad(s) => sysex(base, &[0x07, *s]),
            Msg::PreampPadReply { socket, on } => sysex(base, &[0x08, *socket, bool7(*on)]),
            Msg::Phantom { socket, on } => sysex(base, &[0x0C, *socket, if *on { 0x7F } else { 0x3F }]),
            Msg::GetPhantom(s) => sysex(base, &[0x0A, *s]),
            Msg::PhantomReply { socket, on } => sysex(base, &[0x0B, *socket, bool7(*on)]),
            Msg::SetName { chan, name } => {
                let mut body = vec![0x03, ch(*chan)];
                body.extend(ascii(name));
                sysex(n(*chan), &body)
            }
            Msg::GetName(c) => sysex(n(*c), &[0x01, ch(*c)]),
            Msg::NameReply { chan, name } => {
                let mut body = vec![0x02, ch(*chan)];
                body.extend(ascii(name));
                sysex(n(*chan), &body)
            }
            Msg::SetColour { chan, colour } => sysex(n(*chan), &[0x06, ch(*chan), *colour]),
            Msg::GetColour(c) => sysex(n(*c), &[0x04, ch(*c)]),
            Msg::ColourReply { chan, colour } => sysex(n(*chan), &[0x05, ch(*chan), *colour]),
            Msg::SceneRecall(scene) => {
                let i = scene.saturating_sub(1);
                vec![0xB0 | base, 0x00, (i / 128) as u8, 0xC0 | base, (i % 128) as u8]
            }
            Msg::CueRecall(id) => vec![0xB0 | base, 0x00, (id / 128) as u8, 0xC0 | base, (id % 128) as u8],
            Msg::Eq { chan, param, value } => nrpn(*chan, *param, *value),
            Msg::GetEq { chan, param } => sysex(n(*chan), &[0x05, 0x0B, *param, ch(*chan)]),
            Msg::HpfFreq { chan, value } => nrpn(*chan, 0x30, *value),
            Msg::GetHpfFreq(c) => sysex(n(*c), &[0x05, 0x0B, 0x30, ch(*c)]),
            Msg::HpfOn { chan, on } => nrpn(*chan, 0x31, if *on { 0x7F } else { 0x3F }),
            Msg::GetHpfOn(c) => sysex(n(*c), &[0x05, 0x0B, 0x31, ch(*c)]),
            Msg::UfxKey(k) => vec![0xB0 | base, 0x0C, *k],
            Msg::UfxScale(s) => vec![0xB0 | base, 0x0D, *s],
        }
    }
}

/// The NRPN latches of one MIDI channel.
#[derive(Clone, Copy, Debug, Default)]
struct Nrpn {
    /// `63`: the channel number.
    ch: u8,
    /// `62`: the parameter ID.
    param: u8,
}

/// Parses whole MIDI messages ([`crate::stream`]) into [`Msg`]s: NRPN latches per
/// MIDI channel, the bank select; `surface` is which port it arrived on (Program
/// Change recalls a Scene at the MixRack, a Cue at the Surface).
#[derive(Clone, Debug)]
pub struct Decoder {
    pub base: u8,
    pub surface: bool,
    nrpn: [Nrpn; 16],
    bank: u8,
}

impl Decoder {
    pub fn new(base: u8, surface: bool) -> Self {
        Self { base, surface, nrpn: [Nrpn::default(); 16], bank: 0 }
    }

    /// The MIDI channel's offset from N, if it's one of the five.
    fn offset(&self, n: u8) -> Option<u8> {
        n.checked_sub(self.base).filter(|o| *o < SPAN)
    }

    /// One whole message's meaning, if it has one here.
    pub fn decode(&mut self, m: &[u8]) -> Option<Msg> {
        let status = *m.first()?;
        if status == 0xF0 {
            return self.decode_sysex(m);
        }
        let channel = status & 0x0F;
        let offset = self.offset(channel);
        let d1 = m.get(1).copied();
        let d2 = m.get(2).copied();
        match status & 0xF0 {
            0x90 => {
                let vel = d2?;
                if vel == 0 {
                    return None; // "Velocity 00 and NOTE OFF messages are ignored"
                }
                Some(Msg::Mute { chan: Chan::decode(offset?, d1?)?, on: vel >= 0x40 })
            }
            0xB0 => match d1? {
                0x63 => {
                    self.nrpn[usize::from(channel)].ch = d2?;
                    None
                }
                0x62 => {
                    self.nrpn[usize::from(channel)].param = d2?;
                    None
                }
                0x06 => {
                    let n = self.nrpn[usize::from(channel)];
                    self.decode_nrpn(Chan::decode(offset?, n.ch)?, n.param, d2?)
                }
                0x00 if offset == Some(0) => {
                    self.bank = d2?;
                    None
                }
                0x0C if offset == Some(0) => Some(Msg::UfxKey(d2?)),
                0x0D if offset == Some(0) => Some(Msg::UfxScale(d2?)),
                _ => None, // Go / Next / Previous, MIDI Strips, UFX unit CCs: nothing here
            },
            0xC0 if offset == Some(0) => {
                let ss = u16::from(d1?);
                let bank = u16::from(self.bank);
                if self.surface {
                    let id = bank * 128 + ss;
                    (bank <= 0x0F && id <= 1999).then_some(Msg::CueRecall(id))
                } else {
                    let scene = bank * 128 + ss + 1;
                    (bank <= 3 && scene <= 500).then_some(Msg::SceneRecall(scene))
                }
            }
            0xE0 if offset == Some(0) => Some(Msg::PreampGain { socket: d1?, gv: d2? }),
            _ => None,
        }
    }

    fn decode_nrpn(&self, chan: Chan, param: u8, value: u8) -> Option<Msg> {
        Some(match param {
            0x17 => Msg::Fader { chan, level: value },
            0x18 => Msg::MainAssign { chan, on: value >= 0x40 },
            0x40 => match value {
                0x00..=0x17 => Msg::DcaAssign { chan, dca: value, on: false },
                0x18..=0x1F => Msg::MuteGroupAssign { chan, group: value - 0x18, on: false },
                0x40..=0x57 => Msg::DcaAssign { chan, dca: value - 0x40, on: true },
                0x58..=0x5F => Msg::MuteGroupAssign { chan, group: value - 0x58, on: true },
                _ => return None,
            },
            0x1A..=0x29 => Msg::Eq { chan, param, value },
            0x30 => Msg::HpfFreq { chan, value },
            0x31 => Msg::HpfOn { chan, on: value >= 0x40 },
            _ => return None,
        })
    }

    fn decode_sysex(&self, m: &[u8]) -> Option<Msg> {
        let body = m.strip_prefix(&SYSEX_ID)?.strip_suffix(&[0xF7])?;
        // The two version bytes (01, 00) are taken as they come.
        let body = body.get(SYSEX_VERSION.len()..)?;
        let n = *body.first()?;
        let offset = self.offset(n)?;
        let cmd = *body.get(1)?;
        let args = body.get(2..)?;
        let at = |i: usize| args.get(i).copied();
        let chan = |i: usize| at(i).and_then(|ch| Chan::decode(offset, ch));
        // A send's destination, by the MIDI channel and note number it names.
        let dest = |i: usize| -> Option<Chan> { Chan::decode(self.offset(at(i)?)?, at(i + 1)?) };
        match cmd {
            0x01 => Some(Msg::GetName(chan(0)?)),
            0x03 => {
                let chan = chan(0)?;
                let name: String =
                    args[1..].iter().take(MAX_NAME).filter(|b| (0x20..0x7F).contains(*b)).map(|b| char::from(*b)).collect();
                Some(Msg::SetName { chan, name })
            }
            0x04 => Some(Msg::GetColour(chan(0)?)),
            0x05 => match at(0)? {
                0x09 => Some(Msg::GetMute(chan(1)?)),
                0x0B => {
                    let nn = at(1)?;
                    match nn {
                        0x17 => Some(Msg::GetFader(chan(2)?)),
                        0x18 => Some(Msg::GetMainAssign(chan(2)?)),
                        0x19 => (offset == 0).then_some(Msg::GetPreampGain(at(2)?)),
                        0x1A..=0x29 => Some(Msg::GetEq { chan: chan(2)?, param: nn }),
                        0x30 => Some(Msg::GetHpfFreq(chan(2)?)),
                        0x31 => Some(Msg::GetHpfOn(chan(2)?)),
                        _ => None,
                    }
                }
                0x0F => {
                    let which = at(1)?;
                    let src = chan(2)?;
                    let dest = dest(3)?;
                    match which {
                        0x0D => Some(Msg::GetSendLevel { src, dest }),
                        0x0E => Some(Msg::GetSendOn { src, dest }),
                        _ => None,
                    }
                }
                _ => None,
            },
            0x06 => Some(Msg::SetColour { chan: chan(0)?, colour: at(1)? }),
            0x07 => (offset == 0).then_some(Msg::GetPreampPad(at(0)?)),
            0x09 => (offset == 0).then_some(Msg::PreampPad { socket: at(0)?, on: at(1)? >= 0x40 }),
            0x0A => (offset == 0).then_some(Msg::GetPhantom(at(0)?)),
            0x0C => (offset == 0).then_some(Msg::Phantom { socket: at(0)?, on: at(1)? >= 0x40 }),
            0x0D => Some(Msg::SendLevel { src: chan(0)?, dest: dest(1)?, level: at(3)? }),
            0x0E => Some(Msg::SendOn { src: chan(0)?, dest: dest(1)?, on: at(3)? >= 0x40 }),
            _ => None, // replies (02, 05 with a colour, 08, 0B) go the other way
        }
    }
}

// ---- the console ----------------------------------------------------------------------

/// The PEQ's parameter IDs by field and band (`livedaw_dsp::sq::PEQ_PARAMS`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum EqField {
    Type,
    Freq,
    Width,
    Gain,
}

/// A parameter ID 1A to 29 as its band (0-3) and field.
fn eq_field(param: u8) -> Option<(usize, EqField)> {
    let i = usize::from(param.checked_sub(0x1A)?);
    let band = i / 4;
    if band > 3 {
        return None;
    }
    Some((band, [EqField::Type, EqField::Freq, EqField::Width, EqField::Gain][i % 4]))
}

const EQ_FREQ: [&str; 4] = ["lf", "1f", "2f", "hf"];
const EQ_GAIN: [&str; 4] = ["lg", "1g", "2g", "hg"];
const EQ_WIDTH: [&str; 4] = ["lq", "1q", "2q", "hq"];
/// Types (VV): Bell 0, LF Shelf 1, HF Shelf 2, L Pass 3, H Pass 4. The engine's
/// LF/HF modes are SHELF 0, BELL 1, HPF (LPF) 2; bands 1 and 2 have none (always Bell).
const EQ_MODE: [Option<&str>; 4] = [Some("lm"), None, None, Some("hm")];

/// A parameter that can change on the console.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
enum Param {
    Mute(Chan),
    Fader(Chan),
    MainAssign(Chan),
    SendLevel(Chan, Chan),
    SendOn(Chan, Chan),
    DcaAssign(Chan, u8),
    MuteGroupAssign(Chan, u8),
    Gain(u8),
    Phantom(u8),
    Name(Chan),
    Colour(Chan),
    Eq(Chan, u8),
    HpfFreq(Chan),
    HpfOn(Chan),
}

/// A value on the wire: seven bits, or a name.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Val {
    U7(u8),
    Text(String),
}

fn flag(on: bool) -> Val {
    Val::U7(u8::from(on))
}

/// The engine's strips, as the layout places them.
struct Console<'a> {
    engine: &'a Engine,
    layout: DLiveLayout,
    inputs: Vec<Arc<Strip>>,
    returns: Vec<Arc<Strip>>,
    buses: Vec<Arc<Strip>>,
    mains: Vec<Arc<Strip>>,
    matrices: Vec<Arc<Strip>>,
    /// By input number: its channel's slot and that channel's first input.
    slot_of: Vec<Option<(usize, u8)>>,
}

impl<'a> Console<'a> {
    fn new(engine: &'a Engine) -> Option<Self> {
        let Profile::AhDLive(layout) = engine.profile() else { return None };
        let mut slot_of = vec![None; 128];
        for (slot, (first, stereo)) in layout.channel_inputs().into_iter().enumerate() {
            slot_of[first] = Some((slot, first as u8));
            if stereo {
                slot_of[first + 1] = Some((slot, first as u8));
            }
        }
        Some(Self {
            engine,
            layout,
            inputs: engine.strips(StripKind::Channel),
            returns: engine.strips(StripKind::Aux),
            buses: engine.strips(StripKind::Bus),
            mains: engine.strips(StripKind::Main),
            matrices: engine.strips(StripKind::Matrix),
            slot_of,
        })
    }

    /// The bus slot of a Group, Aux or FX send, if the layout has it.
    fn bus_slot(&self, c: Chan) -> Option<usize> {
        let l = self.layout;
        let (i, count, start) = match c {
            Chan::Group(i) => (i, l.grp, 0),
            Chan::StGroup(i) => (i, l.st_grp, l.st_grp_at()),
            Chan::Aux(i) => (i, l.aux, l.aux_at()),
            Chan::StAux(i) => (i, l.st_aux, l.st_aux_at()),
            Chan::FxSend(i) => (i, l.fx, l.fx_at()),
            Chan::StFxSend(i) => (i, l.st_fx, l.st_fx_at()),
            _ => return None,
        };
        (i < count).then_some(start + usize::from(i))
    }

    /// The matrix index of a Matrix, if the layout has it.
    fn matrix_index(&self, c: Chan) -> Option<usize> {
        match c {
            Chan::Matrix(i) if i < self.layout.mtx => Some(usize::from(i)),
            Chan::StMatrix(i) if i < self.layout.st_mtx => Some(usize::from(self.layout.mtx) + usize::from(i)),
            _ => None,
        }
    }

    fn strip(&self, c: Chan) -> Option<&Arc<Strip>> {
        match c {
            Chan::Input(i) => self.inputs.get(self.slot_of.get(usize::from(i)).copied().flatten()?.0),
            Chan::FxReturn(i) => self.returns.get(usize::from(i)),
            Chan::Main(0) => self.mains.first(),
            Chan::Matrix(_) | Chan::StMatrix(_) => self.matrices.get(self.matrix_index(c)?),
            _ => self.buses.get(self.bus_slot(c)?),
        }
    }

    /// A stereo input's channel number is its first input's.
    fn canon(&self, c: Chan) -> Chan {
        match c {
            Chan::Input(i) => Chan::Input(self.slot_of[usize::from(i).min(127)].map_or(i, |s| s.1)),
            _ => c,
        }
    }

    fn canon_param(&self, p: Param) -> Param {
        match p {
            Param::Mute(c) => Param::Mute(self.canon(c)),
            Param::Fader(c) => Param::Fader(self.canon(c)),
            Param::MainAssign(c) => Param::MainAssign(self.canon(c)),
            Param::SendLevel(s, d) => Param::SendLevel(self.canon(s), d),
            Param::SendOn(s, d) => Param::SendOn(self.canon(s), d),
            Param::DcaAssign(c, i) => Param::DcaAssign(self.canon(c), i),
            Param::MuteGroupAssign(c, i) => Param::MuteGroupAssign(self.canon(c), i),
            Param::Name(c) => Param::Name(self.canon(c)),
            Param::Colour(c) => Param::Colour(self.canon(c)),
            Param::Eq(c, n) => Param::Eq(self.canon(c), n),
            Param::HpfFreq(c) => Param::HpfFreq(self.canon(c)),
            Param::HpfOn(c) => Param::HpfOn(self.canon(c)),
            Param::Gain(_) | Param::Phantom(_) => p,
        }
    }

    fn dca(&self, i: u8) -> Option<&livedaw_engine::params::DcaParams> {
        (usize::from(i) < self.engine.num_dcas()).then(|| self.engine.controls.dcas.get(usize::from(i)))?
    }

    fn mute_group_ok(&self, i: u8) -> bool {
        usize::from(i) < self.engine.num_mute_groups()
    }

    /// Whether the strip has a PEQ (every strip but the FX sends').
    fn has_eq(c: Chan) -> bool {
        !matches!(
            c,
            Chan::FxSend(_) | Chan::StFxSend(_) | Chan::Dca(_) | Chan::MuteGroup(_) | Chan::UfxSend(_) | Chan::UfxReturn(_)
        )
    }

    /// Reads a parameter; None if the console has no such thing.
    fn read(&self, p: Param) -> Option<Val> {
        let dcas = |i: u8| self.dca(i);
        Some(match p {
            Param::Mute(Chan::Dca(i)) => flag(dcas(i)?.mute.load(Relaxed)),
            Param::Mute(Chan::MuteGroup(i)) => {
                if !self.mute_group_ok(i) {
                    return None;
                }
                flag(self.engine.controls.mute_groups.contains(usize::from(i)))
            }
            Param::Mute(c) => flag(self.strip(c)?.params.mute.load(Relaxed)),
            Param::Fader(Chan::Dca(i)) => Val::U7(fader_value(dcas(i)?.fader_db.load())),
            Param::Fader(c) => Val::U7(fader_value(self.strip(c)?.params.fader_db.load())),
            Param::MainAssign(c) => flag(self.strip(c)?.main_send(0)?.on.load(Relaxed)),
            Param::SendLevel(s, d) => {
                let src = self.strip(s)?;
                if let Some(m) = self.matrix_index(d) {
                    Val::U7(fader_value(src.matrix_send(m)?.level_db.load()))
                } else {
                    let slot = self.buses.get(self.bus_slot(d)?)?.id() as usize;
                    Val::U7(fader_value(src.sends.find(slot).map_or(FADER_MIN, |p| p.level_db.load())))
                }
            }
            Param::SendOn(s, d) => {
                let src = self.strip(s)?;
                if let Some(m) = self.matrix_index(d) {
                    flag(src.matrix_send(m)?.on.load(Relaxed))
                } else {
                    let slot = self.buses.get(self.bus_slot(d)?)?.id() as usize;
                    flag(src.sends.find(slot).is_some_and(|p| p.enabled.load(Relaxed)))
                }
            }
            Param::DcaAssign(c, i) => flag(self.strip(c)?.params.dca_mask.contains(usize::from(i))),
            Param::MuteGroupAssign(c, i) => flag(self.strip(c)?.params.mute_group_mask.contains(usize::from(i))),
            Param::Gain(s) => Val::U7(gain_value(self.engine.sources().get(usize::from(s))?.gain_db.load())),
            Param::Phantom(s) => flag(self.engine.sources().get(usize::from(s))?.phantom.load(Relaxed)),
            Param::Name(Chan::Dca(i)) => {
                dcas(i)?;
                Val::Text(self.engine.dca_name(usize::from(i)))
            }
            Param::Name(Chan::MuteGroup(i)) => {
                if !self.mute_group_ok(i) {
                    return None;
                }
                Val::Text(self.engine.mute_group_name(usize::from(i)))
            }
            Param::Name(c) => Val::Text(self.strip(c)?.name()),
            Param::Colour(Chan::Dca(i)) => Val::U7((dcas(i)?.colour.load(Relaxed).clamp(1, 8) - 1) as u8),
            Param::Colour(c) => Val::U7((self.strip(c)?.params.colour.load(Relaxed).clamp(1, 8) - 1) as u8),
            Param::Eq(c, nn) => Val::U7(self.read_eq(self.strip(c)?, c, nn)?),
            Param::HpfFreq(c) => {
                if !matches!(c, Chan::Input(_)) {
                    return None;
                }
                Val::U7(freq_value(self.strip(c)?.params.filter().low_cut_freq.load(), HPF_SPAN))
            }
            Param::HpfOn(c) => {
                if !matches!(c, Chan::Input(_)) {
                    return None;
                }
                flag(self.strip(c)?.params.filter().low_cut.load(Relaxed))
            }
        })
    }

    fn read_eq(&self, s: &Strip, c: Chan, nn: u8) -> Option<u8> {
        if !Self::has_eq(c) {
            return None;
        }
        let (band, field) = eq_field(nn)?;
        let eq = s.params.eq();
        Some(match field {
            EqField::Freq => freq_value(eq.param(EQ_FREQ[band])?.load(), PEQ_SPAN),
            EqField::Gain => eq_gain_value(eq.param(EQ_GAIN[band])?.load()),
            EqField::Width => width_value(eq.param(EQ_WIDTH[band])?.load()),
            EqField::Type => match EQ_MODE[band] {
                None => 0,
                Some(id) => match (eq.param(id)?.load().round() as i32, band) {
                    (0, 0) => 1,
                    (0, _) => 2,
                    (2, 0) => 4,
                    (2, _) => 3,
                    _ => 0,
                },
            },
        })
    }

    fn write(&self, p: Param, v: &Val) -> Option<()> {
        let dcas = |i: u8| self.dca(i);
        match (p, v) {
            (Param::Mute(Chan::Dca(i)), Val::U7(x)) => dcas(i)?.mute.store(*x != 0, Relaxed),
            (Param::Mute(Chan::MuteGroup(i)), Val::U7(x)) => {
                if !self.mute_group_ok(i) {
                    return None;
                }
                self.engine.controls.mute_groups.set(usize::from(i), *x != 0);
            }
            (Param::Mute(c), Val::U7(x)) => self.strip(c)?.params.mute.store(*x != 0, Relaxed),
            (Param::Fader(Chan::Dca(i)), Val::U7(x)) => dcas(i)?.fader_db.store(fader_db(*x)),
            (Param::Fader(c), Val::U7(x)) => self.strip(c)?.params.fader_db.store(fader_db(*x)),
            (Param::MainAssign(c), Val::U7(x)) => self.strip(c)?.main_send(0)?.on.store(*x != 0, Relaxed),
            (Param::SendLevel(s, d), Val::U7(x)) => {
                let src = self.strip(s)?;
                if s == d {
                    return None;
                }
                if let Some(m) = self.matrix_index(d) {
                    src.matrix_send(m)?.level_db.store(fader_db(*x));
                } else {
                    let slot = self.buses.get(self.bus_slot(d)?)?.id() as usize;
                    src.sends.get_or_create(slot)?.level_db.store(fader_db(*x));
                }
            }
            (Param::SendOn(s, d), Val::U7(x)) => {
                let src = self.strip(s)?;
                if s == d {
                    return None;
                }
                if let Some(m) = self.matrix_index(d) {
                    src.matrix_send(m)?.on.store(*x != 0, Relaxed);
                } else {
                    let slot = self.buses.get(self.bus_slot(d)?)?.id() as usize;
                    src.sends.get_or_create(slot)?.enabled.store(*x != 0, Relaxed);
                }
            }
            (Param::DcaAssign(c, i), Val::U7(x)) => self.strip(c)?.params.dca_mask.set(usize::from(i), *x != 0),
            (Param::MuteGroupAssign(c, i), Val::U7(x)) => self.strip(c)?.params.mute_group_mask.set(usize::from(i), *x != 0),
            (Param::Gain(s), Val::U7(x)) => self.engine.sources().get(usize::from(s))?.gain_db.store(gain_db(*x)),
            (Param::Phantom(s), Val::U7(x)) => self.engine.sources().get(usize::from(s))?.phantom.store(*x != 0, Relaxed),
            (Param::Name(Chan::Dca(i)), Val::Text(t)) => {
                dcas(i)?;
                self.engine.set_dca_name(usize::from(i), t);
            }
            (Param::Name(Chan::MuteGroup(i)), Val::Text(t)) => {
                if !self.mute_group_ok(i) {
                    return None;
                }
                self.engine.set_mute_group_name(usize::from(i), t);
            }
            (Param::Name(c), Val::Text(t)) => self.strip(c)?.set_name(t),
            (Param::Colour(Chan::Dca(i)), Val::U7(x)) => dcas(i)?.colour.store(i32::from(*x.min(&7)) + 1, Relaxed),
            (Param::Colour(c), Val::U7(x)) => self.strip(c)?.params.colour.store(i32::from(*x.min(&7)) + 1, Relaxed),
            (Param::Eq(c, nn), Val::U7(x)) => self.write_eq(self.strip(c)?, c, nn, *x)?,
            (Param::HpfFreq(c), Val::U7(x)) => {
                if !matches!(c, Chan::Input(_)) {
                    return None;
                }
                self.strip(c)?.params.filter().low_cut_freq.store(freq_hz(*x, HPF_SPAN, HPF_MAX));
            }
            (Param::HpfOn(c), Val::U7(x)) => {
                if !matches!(c, Chan::Input(_)) {
                    return None;
                }
                self.strip(c)?.params.filter().low_cut.store(*x != 0, Relaxed);
            }
            _ => return None,
        }
        Some(())
    }

    fn write_eq(&self, s: &Strip, c: Chan, nn: u8, v: u8) -> Option<()> {
        if !Self::has_eq(c) {
            return None;
        }
        let (band, field) = eq_field(nn)?;
        let eq = s.params.eq();
        match field {
            EqField::Freq => eq.param(EQ_FREQ[band])?.store(freq_hz(v, PEQ_SPAN, PEQ_MAX)),
            EqField::Gain => eq.param(EQ_GAIN[band])?.store(eq_gain_db(v)),
            EqField::Width => eq.param(EQ_WIDTH[band])?.store(width_octaves(v)?),
            EqField::Type => {
                // Bell (0) on bands 0 and 3, LF Shelf (1) and H Pass (4) on 0, HF Shelf (2) and L Pass (3) on 3.
                let mode = match (band, v) {
                    (0 | 3, 0) => 1.0,
                    (0, 1) | (3, 2) => 0.0,
                    (0, 4) | (3, 3) => 2.0,
                    _ => return None,
                };
                eq.param(EQ_MODE[band]?)?.store(mode);
            }
        }
        Some(())
    }
}

/// The parameters of `c`'s console, for watching them change.
fn watched(c: &Console) -> Vec<Param> {
    let l = c.layout;
    let mut strips: Vec<Chan> = Vec::new();
    for (slot, &(first, _)) in l.channel_inputs().iter().enumerate() {
        if slot < c.inputs.len() {
            strips.push(Chan::Input(first as u8));
        }
    }
    for i in 0..c.returns.len().min(16) {
        strips.push(Chan::FxReturn(i as u8));
    }
    let ranges = |make: fn(u8) -> Chan, n: u8, out: &mut Vec<Chan>| out.extend((0..n).map(make));
    ranges(Chan::Group, l.grp, &mut strips);
    ranges(Chan::StGroup, l.st_grp, &mut strips);
    ranges(Chan::Aux, l.aux, &mut strips);
    ranges(Chan::StAux, l.st_aux, &mut strips);
    ranges(Chan::FxSend, l.fx, &mut strips);
    ranges(Chan::StFxSend, l.st_fx, &mut strips);
    ranges(Chan::Matrix, l.mtx, &mut strips);
    ranges(Chan::StMatrix, l.st_mtx, &mut strips);
    if !c.mains.is_empty() {
        strips.push(Chan::Main(0));
    }
    let dcas = c.engine.num_dcas().min(24) as u8;
    let mute_groups = c.engine.num_mute_groups().min(8) as u8;

    let level_dests: Vec<Chan> = (0..l.aux)
        .map(Chan::Aux)
        .chain((0..l.st_aux).map(Chan::StAux))
        .chain((0..l.fx).map(Chan::FxSend))
        .chain((0..l.st_fx).map(Chan::StFxSend))
        .chain((0..l.mtx).map(Chan::Matrix))
        .chain((0..l.st_mtx).map(Chan::StMatrix))
        .collect();
    let on_dests: Vec<Chan> = (0..l.grp)
        .map(Chan::Group)
        .chain((0..l.st_grp).map(Chan::StGroup))
        .chain((0..l.aux).map(Chan::Aux))
        .chain((0..l.st_aux).map(Chan::StAux))
        .collect();

    let mut params = Vec::new();
    for &chan in &strips {
        params.extend([Param::Mute(chan), Param::Fader(chan), Param::Name(chan), Param::Colour(chan)]);
        if !matches!(chan, Chan::Main(_)) {
            params.push(Param::MainAssign(chan));
        }
        params.extend((0..dcas).map(|i| Param::DcaAssign(chan, i)));
        params.extend((0..mute_groups).map(|i| Param::MuteGroupAssign(chan, i)));
        if Console::has_eq(chan) {
            params.extend((0x1A..=0x29).map(|n| Param::Eq(chan, n)));
        }
        if matches!(chan, Chan::Input(_)) {
            params.extend([Param::HpfFreq(chan), Param::HpfOn(chan)]);
        }
        let can_send = matches!(
            chan,
            Chan::Input(_)
                | Chan::FxReturn(_)
                | Chan::Group(_)
                | Chan::StGroup(_)
                | Chan::Aux(_)
                | Chan::StAux(_)
                | Chan::Main(_)
        );
        if can_send {
            for &d in &level_dests {
                params.push(Param::SendLevel(chan, d));
            }
            for &d in &on_dests {
                params.push(Param::SendOn(chan, d));
            }
        }
    }
    for i in 0..dcas {
        let chan = Chan::Dca(i);
        params.extend([Param::Mute(chan), Param::Fader(chan), Param::Name(chan), Param::Colour(chan)]);
    }
    for i in 0..mute_groups {
        params.extend([Param::Mute(Chan::MuteGroup(i)), Param::Name(Chan::MuteGroup(i))]);
    }
    for s in 0..=127u8 {
        params.extend([Param::Gain(s), Param::Phantom(s)]);
    }
    params
}

/// The message a parameter's value goes out as.
fn to_msg(p: Param, v: &Val) -> Option<Msg> {
    let (u, t) = match v {
        Val::U7(x) => (*x, ""),
        Val::Text(s) => (0, s.as_str()),
    };
    let on = u != 0;
    Some(match p {
        Param::Mute(chan) => Msg::Mute { chan, on },
        Param::Fader(chan) => Msg::Fader { chan, level: u },
        Param::MainAssign(chan) => Msg::MainAssign { chan, on },
        Param::SendLevel(src, dest) => Msg::SendLevel { src, dest, level: u },
        Param::SendOn(src, dest) => Msg::SendOn { src, dest, on },
        Param::DcaAssign(chan, dca) => Msg::DcaAssign { chan, dca, on },
        Param::MuteGroupAssign(chan, group) => Msg::MuteGroupAssign { chan, group, on },
        Param::Gain(socket) => Msg::PreampGain { socket, gv: u },
        Param::Phantom(socket) => Msg::PhantomReply { socket, on },
        Param::Name(chan) => Msg::NameReply { chan, name: t.to_string() },
        Param::Colour(chan) => Msg::ColourReply { chan, colour: u },
        Param::Eq(chan, param) => Msg::Eq { chan, param, value: u },
        Param::HpfFreq(chan) => Msg::HpfFreq { chan, value: u },
        Param::HpfOn(chan) => Msg::HpfOn { chan, on },
    })
}

/// The parameter a Set message writes, and its value.
fn set_of(m: &Msg) -> Option<(Param, Val)> {
    Some(match m {
        Msg::Mute { chan, on } => (Param::Mute(*chan), flag(*on)),
        Msg::Fader { chan, level } => (Param::Fader(*chan), Val::U7(*level)),
        Msg::MainAssign { chan, on } => (Param::MainAssign(*chan), flag(*on)),
        Msg::SendLevel { src, dest, level } => (Param::SendLevel(*src, *dest), Val::U7(*level)),
        Msg::SendOn { src, dest, on } => (Param::SendOn(*src, *dest), flag(*on)),
        Msg::DcaAssign { chan, dca, on } => (Param::DcaAssign(*chan, *dca), flag(*on)),
        Msg::MuteGroupAssign { chan, group, on } => (Param::MuteGroupAssign(*chan, *group), flag(*on)),
        Msg::PreampGain { socket, gv } => (Param::Gain(*socket), Val::U7(*gv)),
        Msg::Phantom { socket, on } => (Param::Phantom(*socket), flag(*on)),
        Msg::SetName { chan, name } => (Param::Name(*chan), Val::Text(name.clone())),
        Msg::SetColour { chan, colour } => (Param::Colour(*chan), Val::U7(*colour)),
        Msg::Eq { chan, param, value } => (Param::Eq(*chan, *param), Val::U7(*value)),
        Msg::HpfFreq { chan, value } => (Param::HpfFreq(*chan), Val::U7(*value)),
        Msg::HpfOn { chan, on } => (Param::HpfOn(*chan), flag(*on)),
        _ => return None,
    })
}

/// The parameter a Get message reads, and the channel to answer about.
fn get_of(m: &Msg) -> Option<Param> {
    Some(match m {
        Msg::GetMute(c) => Param::Mute(*c),
        Msg::GetFader(c) => Param::Fader(*c),
        Msg::GetMainAssign(c) => Param::MainAssign(*c),
        Msg::GetSendLevel { src, dest } => Param::SendLevel(*src, *dest),
        Msg::GetSendOn { src, dest } => Param::SendOn(*src, *dest),
        Msg::GetPreampGain(s) => Param::Gain(*s),
        Msg::GetPhantom(s) => Param::Phantom(*s),
        Msg::GetName(c) => Param::Name(*c),
        Msg::GetColour(c) => Param::Colour(*c),
        Msg::GetEq { chan, param } => Param::Eq(*chan, *param),
        Msg::GetHpfFreq(c) => Param::HpfFreq(*c),
        Msg::GetHpfOn(c) => Param::HpfOn(*c),
        _ => return None,
    })
}

/// The protocol's state for one client: its parser, and what it's been told.
pub struct Midi {
    decoder: Decoder,
    layout: Option<DLiveLayout>,
    params: Vec<Param>,
    last: Vec<Option<Val>>,
    index: HashMap<Param, usize>,
    out: VecDeque<Vec<u8>>,
    events: VecDeque<Event>,
}

impl Midi {
    /// A client on the MixRack's port, or the Surface's.
    pub fn new(surface: bool) -> Self {
        Self {
            decoder: Decoder::new(0, surface),
            layout: None,
            params: Vec::new(),
            last: Vec::new(),
            index: HashMap::new(),
            out: VecDeque::new(),
            events: VecDeque::new(),
        }
    }

    /// N, the lowest of its five MIDI channels (0 to 11).
    pub fn set_base(&mut self, base: u8) {
        self.decoder.base = base.min(MAX_BASE);
    }

    fn base(&self) -> u8 {
        self.decoder.base
    }

    pub fn take_out(&mut self) -> Option<Vec<u8>> {
        self.out.pop_front()
    }

    pub fn take_event(&mut self) -> Option<Event> {
        self.events.pop_front()
    }

    fn send(&mut self, m: &Msg) {
        let bytes = m.encode(self.base());
        self.out.push_back(bytes);
    }

    /// Handles one whole received MIDI message.
    pub fn receive(&mut self, engine: &Engine, msg: &[u8]) {
        let Some(c) = Console::new(engine) else { return };
        let Some(m) = self.decoder.decode(msg) else { return };
        self.refresh(&c);
        self.apply(&c, &m);
    }

    fn apply(&mut self, c: &Console, m: &Msg) {
        if let Some((p, v)) = set_of(m) {
            if c.write(p, &v).is_some() {
                let key = c.canon_param(p);
                if let Some(&i) = self.index.get(&key) {
                    self.last[i] = c.read(key);
                }
            }
            return;
        }
        if let Some(p) = get_of(m) {
            if let Some(v) = c.read(p) {
                if let Some(reply) = to_msg(p, &v) {
                    self.send(&reply);
                }
            }
            return;
        }
        match m {
            // The engine has no pad: its Get answers Off.
            Msg::GetPreampPad(socket) => self.send(&Msg::PreampPadReply { socket: *socket, on: false }),
            Msg::SceneRecall(n) => self.events.push_back(Event::RecallScene(*n)),
            _ => {} // Pad, UFX, Cue Recall: parsed and ignored
        }
    }

    /// Picks up a new layout (the parameters watched change with it).
    fn refresh(&mut self, c: &Console) {
        if self.layout != Some(c.layout) {
            self.layout = Some(c.layout);
            self.params = watched(c);
            self.index = self.params.iter().enumerate().map(|(i, p)| (*p, i)).collect();
            self.last = self.params.iter().map(|p| c.read(*p)).collect();
        }
    }

    /// Queues the console's changes since the last poll (the first takes the
    /// console's values and sends nothing).
    pub fn poll(&mut self, engine: &Engine) {
        let Some(c) = Console::new(engine) else { return };
        self.refresh(&c);
        for i in 0..self.params.len() {
            let now = c.read(self.params[i]);
            if now != self.last[i] {
                if let Some(m) = now.as_ref().and_then(|v| to_msg(self.params[i], v)) {
                    self.send(&m);
                }
                self.last[i] = now;
            }
        }
    }

    /// Scene `n` (1-500) was recalled on the console: the MixRack's clients hear of it.
    pub fn scene_recalled(&mut self, n: u16) {
        if !self.decoder.surface && (1..=500).contains(&n) {
            self.send(&Msg::SceneRecall(n));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::{ConsoleConfig, Counts};

    fn dlive() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: Counts::DLIVE });
        e
    }

    fn feed(m: &mut Midi, e: &Engine, bytes: &[u8]) {
        let mut p = crate::stream::Parser::default();
        p.feed(bytes, &mut |msg| m.receive(e, msg));
    }

    #[test]
    fn channels_land_where_the_table_says() {
        for (offset, ch, want) in [
            (0, 0x00, Chan::Input(0)),
            (0, 0x7F, Chan::Input(127)),
            (1, 0x3D, Chan::Group(61)),
            (1, 0x5E, Chan::StGroup(30)),
            (2, 0x00, Chan::Aux(0)),
            (3, 0x41, Chan::StMatrix(1)),
            (4, 0x0F, Chan::FxSend(15)),
            (4, 0x10, Chan::StFxSend(0)),
            (4, 0x20, Chan::FxReturn(0)),
            (4, 0x2F, Chan::FxReturn(15)),
            (4, 0x30, Chan::Main(0)),
            (4, 0x35, Chan::Main(5)),
            (4, 0x36, Chan::Dca(0)),
            (4, 0x4D, Chan::Dca(23)),
            (4, 0x4E, Chan::MuteGroup(0)),
            (4, 0x55, Chan::MuteGroup(7)),
            (4, 0x56, Chan::UfxSend(0)),
            (4, 0x65, Chan::UfxReturn(7)),
        ] {
            let c = Chan::decode(offset, ch).unwrap();
            assert_eq!(c, want);
            assert_eq!(c.encode(), (offset, ch));
        }
        assert_eq!(Chan::decode(1, 0x3E), None, "between the mono and stereo Groups");
        assert_eq!(Chan::decode(4, 0x66), None);
        assert_eq!(Chan::decode(5, 0), None);
    }

    #[test]
    fn the_fader_table() {
        for (db, v) in [
            (10.0, 0x7F),
            (5.0, 117),
            (0.0, 0x6B),
            (-5.0, 0x61),
            (-10.0, 0x57),
            (-15.0, 0x4D),
            (-20.0, 0x43),
            (-25.0, 0x39),
            (-30.0, 0x2F),
            (-35.0, 0x25),
            (-40.0, 0x1B),
            (-45.0, 0x11),
        ] {
            assert_eq!(fader_value(db), v, "{db}");
            assert_eq!(fader_db(v), db, "{v:02x}");
        }
        assert_eq!(fader_value(FADER_MIN), 0);
        assert_eq!(fader_db(0), FADER_MIN);
        for v in 0..=127u8 {
            assert_eq!(fader_value(fader_db(v)), v, "{v}");
        }
    }

    #[test]
    fn the_preamp_gain_table() {
        for (db, v) in [
            (60.0, 0x7F),
            (55.0, 0x73),
            (50.0, 0x67),
            (45.0, 0x5C),
            (40.0, 0x50),
            (35.0, 0x45),
            (30.0, 0x3A),
            (25.0, 0x2E),
            (20.0, 0x22),
            (15.0, 0x17),
            (10.0, 0x0C),
            (5.0, 0x00),
        ] {
            assert_eq!(gain_value(db), v, "{db}");
        }
        for v in 0..=127u8 {
            assert_eq!(gain_value(gain_db(v)), v, "{v}");
        }
        assert_eq!(gain_value(0.0), 0, "below the table");
    }

    #[test]
    fn the_peq_and_hpf_scales() {
        // The document's examples.
        for (hz, v) in [(20.0, 0), (50.0, 16), (100.0, 29), (500.0, 59), (1000.0, 71), (10000.0, 114), (20000.0, 127)] {
            assert_eq!(freq_value(hz, PEQ_SPAN), v, "{hz}");
        }
        for v in 0..=127u8 {
            assert_eq!(freq_value(freq_hz(v, PEQ_SPAN, PEQ_MAX), PEQ_SPAN), v, "{v}");
        }
        assert_eq!(freq_hz(127, PEQ_SPAN, PEQ_MAX), 20000.0);
        assert_eq!(freq_value(20.0, HPF_SPAN), 0);
        assert_eq!(freq_hz(127, HPF_SPAN, HPF_MAX), 2000.0, "the HPF's screen stops at 2 kHz");
        for (db, v) in [(-15.0, 0), (-10.0, 21), (-5.0, 42), (0.0, 63), (5.0, 84), (10.0, 105), (15.0, 126)] {
            assert_eq!(eq_gain_value(db), v, "{db}");
            assert_eq!(eq_gain_db(v), db, "{v}");
        }
        assert_eq!(eq_gain_db(127), 15.0);
        assert_eq!(width_value(1.0), 5);
        assert_eq!(width_value(1.0 / 3.0), 18);
        assert_eq!(width_octaves(24), Some(1.0 / 9.0));
        assert_eq!(width_octaves(25), None);
    }

    #[test]
    fn the_documents_messages_as_bytes() {
        let n = 0x0B; // MIDI channel 12
        assert_eq!(
            Msg::Mute { chan: Chan::Input(0), on: true }.encode(n),
            vec![0x9B, 0x00, 0x7F, 0x00, 0x00],
            "note on, then off at velocity 0"
        );
        assert_eq!(Msg::Mute { chan: Chan::Input(0), on: false }.encode(n), vec![0x9B, 0x00, 0x3F, 0x00, 0x00]);
        assert_eq!(Msg::Fader { chan: Chan::Input(5), level: 0x6B }.encode(0), vec![0xB0, 0x63, 0x05, 0x62, 0x17, 0x06, 0x6B]);
        assert_eq!(
            Msg::SendLevel { src: Chan::Input(0), dest: Chan::Aux(1), level: 0x40 }.encode(0),
            vec![0xF0, 0x00, 0x00, 0x1A, 0x50, 0x10, 0x01, 0x00, 0x00, 0x0D, 0x00, 0x02, 0x01, 0x40, 0xF7]
        );
        assert_eq!(Msg::PreampGain { socket: 0x10, gv: 0x3A }.encode(2), vec![0xE2, 0x10, 0x3A]);
        // A scene's bank and program.
        assert_eq!(Msg::SceneRecall(1).encode(0), vec![0xB0, 0x00, 0x00, 0xC0, 0x00]);
        assert_eq!(Msg::SceneRecall(129).encode(0), vec![0xB0, 0x00, 0x01, 0xC0, 0x00]);
        assert_eq!(Msg::SceneRecall(500).encode(0), vec![0xB0, 0x00, 0x03, 0xC0, 0x73]);
        assert_eq!(Msg::CueRecall(1999).encode(0), vec![0xB0, 0x00, 0x0F, 0xC0, 0x4F]);
    }

    #[test]
    fn parsing_and_building_agree() {
        let msgs = [
            Msg::Mute { chan: Chan::Input(3), on: true },
            Msg::GetMute(Chan::Dca(2)),
            Msg::Fader { chan: Chan::FxReturn(1), level: 90 },
            Msg::GetFader(Chan::Main(0)),
            Msg::MainAssign { chan: Chan::Input(9), on: false },
            Msg::GetMainAssign(Chan::Input(9)),
            Msg::SendLevel { src: Chan::Input(0), dest: Chan::Matrix(1), level: 10 },
            Msg::GetSendLevel { src: Chan::Input(0), dest: Chan::StAux(3) },
            Msg::SendOn { src: Chan::Input(0), dest: Chan::Group(1), on: true },
            Msg::GetSendOn { src: Chan::Input(0), dest: Chan::Group(1) },
            Msg::DcaAssign { chan: Chan::Input(0), dca: 23, on: true },
            Msg::DcaAssign { chan: Chan::Input(0), dca: 0, on: false },
            Msg::MuteGroupAssign { chan: Chan::Input(0), group: 7, on: true },
            Msg::MuteGroupAssign { chan: Chan::Input(0), group: 0, on: false },
            Msg::PreampGain { socket: 127, gv: 3 },
            Msg::GetPreampGain(5),
            Msg::PreampPad { socket: 1, on: true },
            Msg::GetPreampPad(1),
            Msg::Phantom { socket: 2, on: false },
            Msg::GetPhantom(2),
            Msg::SetName { chan: Chan::Input(0), name: "Kick A".into() },
            Msg::GetName(Chan::Aux(3)),
            Msg::SetColour { chan: Chan::Input(0), colour: 4 },
            Msg::GetColour(Chan::Input(0)),
            Msg::SceneRecall(257),
            Msg::Eq { chan: Chan::Input(0), param: 0x1F, value: 12 },
            Msg::GetEq { chan: Chan::Input(0), param: 0x29 },
            Msg::HpfFreq { chan: Chan::Input(0), value: 30 },
            Msg::GetHpfFreq(Chan::Input(0)),
            Msg::HpfOn { chan: Chan::Input(0), on: true },
            Msg::GetHpfOn(Chan::Input(0)),
            Msg::UfxKey(3),
            Msg::UfxScale(1),
        ];
        for base in [0u8, 5, 11] {
            let mut d = Decoder::new(base, false);
            for m in &msgs {
                let mut p = crate::stream::Parser::default();
                let mut got = Vec::new();
                p.feed(&m.encode(base), &mut |raw| got.extend(d.decode(raw)));
                assert_eq!(got, vec![m.clone()], "base {base}");
            }
            // The Surface reads the same bytes as a Cue.
            let mut d = Decoder::new(base, true);
            let mut got = Vec::new();
            for raw in [&Msg::CueRecall(300).encode(base)[..3], &Msg::CueRecall(300).encode(base)[3..]] {
                got.extend(d.decode(raw));
            }
            assert_eq!(got, vec![Msg::CueRecall(300)]);
        }
    }

    #[test]
    fn out_of_range_and_other_channels_are_nothing() {
        let mut d = Decoder::new(2, false);
        assert_eq!(d.decode(&[0x91, 0x00, 0x7F]), None, "below the base channel");
        assert_eq!(d.decode(&[0x97, 0x00, 0x7F]), None, "above N + 4");
        assert_eq!(d.decode(&[0x82, 0x00, 0x00]), None, "Note Off is ignored");
        assert_eq!(d.decode(&[0x92, 0x00, 0x00]), None, "velocity 0 is ignored");
        assert_eq!(d.decode(&[0xB2, 0x00, 0x04]), None, "the bank");
        assert_eq!(d.decode(&[0xC2, 0x00]), None, "bank 4: no such scene");
        assert_eq!(d.decode(&[0xF0, 0x01, 0xF7]), None, "another manufacturer's");
    }

    #[test]
    fn mute_on_an_input_and_a_dca_and_mute_groups() {
        let e = dlive();
        let mut m = Midi::new(false);
        feed(&mut m, &e, &[0x90, 0x05, 0x7F, 0x90, 0x05, 0x00]);
        assert!(e.strips(StripKind::Channel)[5].params.mute.load(Relaxed));
        feed(&mut m, &e, &[0x90, 0x05, 0x01]);
        assert!(!e.strips(StripKind::Channel)[5].params.mute.load(Relaxed), "Velocity 01 to 3F");
        feed(&mut m, &e, &[0x90, 0x05, 0x40]);
        assert!(e.strips(StripKind::Channel)[5].params.mute.load(Relaxed));
        // DCA 3, Mute Group 2, on N + 4.
        feed(&mut m, &e, &[0x94, 0x38, 0x7F, 0x94, 0x4F, 0x7F]);
        assert!(e.controls.dcas[2].mute.load(Relaxed));
        assert!(e.controls.mute_groups.contains(1));
        // Assignments on Input 1: DCA 24 on, Mute Group 8 on, DCA 24 off.
        for v in [0x57, 0x5F, 0x17] {
            feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x40, 0x06, v]);
        }
        let s = e.strips(StripKind::Channel)[0].clone();
        assert!(!s.params.dca_mask.contains(23));
        assert!(s.params.mute_group_mask.contains(7));
    }

    #[test]
    fn sends_faders_names_colours_and_the_preamp() {
        let e = dlive();
        let mut m = Midi::new(false);
        let input = e.strips(StripKind::Channel)[0].clone();
        // Fader -20 dB.
        feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x17, 0x06, 0x43]);
        assert_eq!(input.params.fader_db.load(), -20.0);
        // To Aux 2 at 0 dB: SndN = N + 2, SndCH = 1.
        let sysex = Msg::SendLevel { src: Chan::Input(0), dest: Chan::Aux(1), level: 0x6B }.encode(0);
        feed(&mut m, &e, &sysex);
        let aux = e.bus_in_slot(DLiveLayout::FOH.aux_at() as i32 + 1).unwrap();
        let slot = aux.id() as usize;
        assert_eq!(input.sends.find(slot).unwrap().level_db.load(), 0.0);
        let name = Msg::SetName { chan: Chan::Input(0), name: "Kick".into() }.encode(0);
        feed(&mut m, &e, &name);
        assert_eq!(input.name(), "Kick");
        feed(&mut m, &e, &Msg::SetColour { chan: Chan::Input(0), colour: 4 }.encode(0));
        assert_eq!(input.params.colour.load(Relaxed), 5, "Blue");
        // Socket 3: +60 dB, 48V on.
        feed(&mut m, &e, &[0xE0, 0x02, 0x7F]);
        assert_eq!(e.sources().get(2).unwrap().gain_db.load(), 60.0);
        feed(&mut m, &e, &Msg::Phantom { socket: 2, on: true }.encode(0));
        assert!(e.sources().get(2).unwrap().phantom.load(Relaxed));
        assert_eq!(m.take_out(), None, "sets aren't answered");
    }

    #[test]
    fn gets_are_answered_in_the_documents_form() {
        let e = dlive();
        let mut m = Midi::new(false);
        let input = e.strips(StripKind::Channel)[2].clone();
        input.params.mute.store(true, Relaxed);
        feed(&mut m, &e, &Msg::GetMute(Chan::Input(2)).encode(0));
        assert_eq!(m.take_out(), Some(vec![0x90, 0x02, 0x7F, 0x02, 0x00]));
        input.params.fader_db.store(0.0);
        feed(&mut m, &e, &Msg::GetFader(Chan::Input(2)).encode(0));
        assert_eq!(m.take_out(), Some(vec![0xB0, 0x63, 0x02, 0x62, 0x17, 0x06, 0x6B]));
        input.set_name("Sn");
        feed(&mut m, &e, &Msg::GetName(Chan::Input(2)).encode(0));
        assert_eq!(m.take_out(), Some(Msg::NameReply { chan: Chan::Input(2), name: "Sn".into() }.encode(0)));
        // Pad: nothing to read, its Get still answers.
        feed(&mut m, &e, &Msg::GetPreampPad(4).encode(0));
        assert_eq!(m.take_out(), Some(Msg::PreampPadReply { socket: 4, on: false }.encode(0)));
    }

    #[test]
    fn a_stereo_input_answers_to_both_numbers_and_speaks_as_the_first() {
        let e = dlive();
        let mut m = Midi::new(false);
        // 127/128 are stereo in the FoH layout: slot 123, its two input numbers 0x7E and 0x7F.
        let s = e.strips(StripKind::Channel)[123].clone();
        feed(&mut m, &e, &[0x90, 0x7F, 0x7F]);
        assert!(s.params.mute.load(Relaxed));
        assert_eq!(m.take_out(), None);
        m.poll(&e);
        assert_eq!(m.take_out(), None, "not echoed, whichever half it came from");
        s.params.mute.store(false, Relaxed);
        m.poll(&e);
        assert_eq!(m.take_out(), Some(vec![0x90, 0x7E, 0x3F, 0x7E, 0x00]));
    }

    #[test]
    fn console_changes_go_out_received_values_dont_echo() {
        let e = dlive();
        let mut m = Midi::new(false);
        m.poll(&e);
        assert_eq!(m.take_out(), None, "nothing at start");
        let s = e.strips(StripKind::Channel)[0].clone();
        s.params.fader_db.store(0.0);
        m.poll(&e);
        assert_eq!(m.take_out(), Some(vec![0xB0, 0x63, 0x00, 0x62, 0x17, 0x06, 0x6B]));
        // From the client: no echo.
        feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x17, 0x06, 0x43]);
        m.poll(&e);
        assert_eq!(m.take_out(), None);
        // A DCA assignment, a colour, a name, 48V, on the console.
        s.params.dca_mask.set(1, true);
        s.params.colour.store(2, Relaxed);
        s.set_name("Vox");
        e.sources().get(0).unwrap().phantom.store(true, Relaxed);
        m.poll(&e);
        let sent: Vec<_> = std::iter::from_fn(|| m.take_out()).collect();
        assert_eq!(sent.len(), 4, "{sent:?}");
        assert!(sent.contains(&Msg::DcaAssign { chan: Chan::Input(0), dca: 1, on: true }.encode(0)));
        assert!(sent.contains(&Msg::ColourReply { chan: Chan::Input(0), colour: 1 }.encode(0)));
        assert!(sent.contains(&Msg::NameReply { chan: Chan::Input(0), name: "Vox".into() }.encode(0)));
        assert!(sent.contains(&Msg::PhantomReply { socket: 0, on: true }.encode(0)));
    }

    #[test]
    fn peq_and_hpf() {
        let e = dlive();
        let mut m = Midi::new(false);
        let s = e.strips(StripKind::Channel)[0].clone();
        let eq = s.params.eq();
        // Band 0 Type: H Pass, band 3: L Pass, band 1: only ever Bell.
        feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x1A, 0x06, 0x04]);
        assert_eq!(eq.param("lm").unwrap().load(), 2.0);
        feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x26, 0x06, 0x03]);
        assert_eq!(eq.param("hm").unwrap().load(), 2.0);
        // Band 2 Gain -5 dB, Frequency 1 kHz.
        feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x25, 0x06, 0x2A]);
        assert_eq!(eq.param("2g").unwrap().load(), -5.0);
        feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x23, 0x06, 0x47]);
        assert!((eq.param("2f").unwrap().load() - 977.0).abs() < 40.0);
        // Width 1/3.
        feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x24, 0x06, 0x12]);
        assert!((eq.param("2q").unwrap().load() - 1.0 / 3.0).abs() < 1e-5);
        // HPF: 100 Hz, in.
        feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x31, 0x06, 0x7F]);
        assert!(s.params.filter().low_cut.load(Relaxed));
        feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x30, 0x06, 0x20]);
        let hz = s.params.filter().low_cut_freq.load();
        assert_eq!(freq_value(hz, HPF_SPAN), 0x20);
        // A Get of the same.
        feed(&mut m, &e, &Msg::GetEq { chan: Chan::Input(0), param: 0x25 }.encode(0));
        assert_eq!(m.take_out(), Some(vec![0xB0, 0x63, 0x00, 0x62, 0x25, 0x06, 0x2A]));
    }

    #[test]
    fn scene_recall_at_the_mixrack_and_the_surface_doesnt() {
        let e = dlive();
        let mut m = Midi::new(false);
        feed(&mut m, &e, &[0xB0, 0x00, 0x02, 0xC0, 0x06]);
        assert_eq!(m.take_event(), Some(Event::RecallScene(263)));
        m.scene_recalled(263);
        assert_eq!(m.take_out(), Some(vec![0xB0, 0x00, 0x02, 0xC0, 0x06]));
        let mut s = Midi::new(true);
        feed(&mut s, &e, &[0xB0, 0x00, 0x00, 0xC0, 0x06]);
        assert_eq!(s.take_event(), None, "a cue id: the show has none");
        s.scene_recalled(5);
        assert_eq!(s.take_out(), None);
    }

    #[test]
    fn what_has_no_counterpart_is_ignored() {
        let e = dlive();
        let mut m = Midi::new(false);
        // UFX mute, mains 2, the pad, the global key, an unknown NRPN.
        feed(&mut m, &e, &[0x94, 0x56, 0x7F, 0x94, 0x31, 0x7F]);
        feed(&mut m, &e, &Msg::PreampPad { socket: 0, on: true }.encode(0));
        feed(&mut m, &e, &[0xB0, 0x0C, 0x03, 0xB0, 0x0D, 0x01]);
        feed(&mut m, &e, &[0xB0, 0x63, 0x00, 0x62, 0x70, 0x06, 0x10]);
        assert_eq!(m.take_out(), None);
    }

    #[test]
    fn another_console_is_left_alone() {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig::wing());
        let mut m = Midi::new(false);
        let before = e.strips(StripKind::Channel)[0].params.mute.load(Relaxed);
        feed(&mut m, &e, &[0x90, 0x00, 0x7F]);
        assert_eq!(e.strips(StripKind::Channel)[0].params.mute.load(Relaxed), before);
        m.poll(&e);
        assert_eq!(m.take_out(), None);
    }

    #[test]
    fn a_new_layout_is_picked_up_without_a_flood() {
        let e = dlive();
        let mut m = Midi::new(false);
        m.poll(&e);
        let layout = DLiveLayout { aux: 10, st_aux: 7, ..DLiveLayout::FOH };
        assert!(layout.is_valid());
        e.configure(&ConsoleConfig { profile: Profile::AhDLive(layout), counts: layout.counts() });
        m.poll(&e);
        assert_eq!(m.take_out(), None);
        // Aux 10 at its new slot.
        let aux = e.bus_in_slot(layout.aux_at() as i32 + 9).unwrap();
        feed(&mut m, &e, &Msg::Mute { chan: Chan::Aux(9), on: true }.encode(0));
        assert!(aux.params.mute.load(Relaxed));
    }
}
