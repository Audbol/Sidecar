//! The Allen & Heath SQ-5's MIDI protocol over the engine (`docs/UNIVERSAL_MIXER.md`),
//! from the SQ MIDI Protocol for firmware V1.6: NRPN mutes, levels, pans and
//! assignments in either fader law, and scene recall by bank and program change.
//! Clean-room: built from that public document (its parameter tables are
//! pictures; [`decode`] holds their formulas), not from the console's firmware.
//!
//! The twelve mixes are split between Auxes and Groups by Bus Config ([`Midi::set_groups`];
//! Reference Guide p.87): the Auxes on the first mixes, the Groups after them. An
//! address of a Group or an Aux the console hasn't got answers nothing. A stereo matrix
//! is Mtx1-3. A Soft Key's note (C3 for key 1) presses and releases it, the app does
//! what it's assigned to ([`Midi::take_soft_key`]). Not here yet: the DAW strips on the
//! next MIDI channel and MIDI Show Control.
//! MIDI over TCP/IP is `livedaw-ahmidi`, over this.

use livedaw_dsp::fader::{FaderLaw, FADER_MIN};
use livedaw_engine::{Engine, Profile, Strip, StripKind};
use std::collections::VecDeque;
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

const INPUTS: usize = 48;
const MIXES: usize = 12;
const FX_RETURNS: usize = 8;
const FX_SENDS: usize = 4;
const MATRICES: usize = 3;

/// Something with a mute or a master level.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Channel {
    Input(usize),
    FxReturn(usize),
    Lr,
    Aux(usize),
    Group(usize),
    FxSend(usize),
    Matrix(usize),
    Dca(usize),
    MuteGroup(usize),
}

/// Where a send starts.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Source {
    Input(usize),
    FxReturn(usize),
    Lr,
    Aux(usize),
    Group(usize),
}

/// Where a send arrives.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Dest {
    Lr,
    Aux(usize),
    Group(usize),
    FxSend(usize),
    Matrix(usize),
}

/// What a parameter number addresses.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Param {
    Mute(Channel),
    Master(Channel),
    Level(Source, Dest),
    Pan(Source, Dest),
    Assign(Source, Dest),
    /// A mix's output balance.
    Balance(Channel),
}

/// The send a level address names (its pan is 0x800 on, its assignment 0x1000 on).
fn send(a: u16) -> Option<(Source, Dest)> {
    let a = a as usize;
    let pair = |base: usize, width: usize, count: usize| {
        let v = a - base;
        (v / width < count).then_some((v / width, v % width))
    };
    Some(match a {
        0x2000..=0x202F => (Source::Input(a - 0x2000), Dest::Lr),
        0x2030..=0x203B => (Source::Group(a - 0x2030), Dest::Lr),
        0x203C..=0x2043 => (Source::FxReturn(a - 0x203C), Dest::Lr),
        0x2044..=0x2283 => {
            let (s, d) = pair(0x2044, MIXES, INPUTS)?;
            (Source::Input(s), Dest::Aux(d))
        }
        // A triangle: Group g reaches the Auxes that can exist beside it (p.23's dashes).
        0x2284..=0x2313 => {
            let (g, x) = pair(0x2284, MIXES, MIXES)?;
            if g + x >= MIXES - 1 {
                return None;
            }
            (Source::Group(g), Dest::Aux(x))
        }
        0x2314..=0x2373 => {
            let (s, d) = pair(0x2314, MIXES, FX_RETURNS)?;
            (Source::FxReturn(s), Dest::Aux(d))
        }
        0x2614..=0x26D3 => {
            let (s, d) = pair(0x2614, FX_SENDS, INPUTS)?;
            (Source::Input(s), Dest::FxSend(d))
        }
        0x26D4..=0x2703 => {
            let (s, d) = pair(0x26D4, FX_SENDS, MIXES)?;
            (Source::Group(s), Dest::FxSend(d))
        }
        0x2704..=0x2723 => {
            let (s, d) = pair(0x2704, FX_SENDS, FX_RETURNS)?;
            (Source::FxReturn(s), Dest::FxSend(d))
        }
        0x2724..=0x2726 => (Source::Lr, Dest::Matrix(a - 0x2724)),
        0x2727..=0x274A => {
            let (s, d) = pair(0x2727, MATRICES, MIXES)?;
            (Source::Aux(s), Dest::Matrix(d))
        }
        0x274B..=0x276E => {
            let (s, d) = pair(0x274B, MATRICES, MIXES)?;
            (Source::Group(s), Dest::Matrix(d))
        }
        _ => return None,
    })
}

/// What the 14-bit parameter number `a` (MSB * 128 + LSB) addresses, if Sidecar has it.
pub fn decode(a: u16) -> Option<Param> {
    let n = a as usize;
    Some(match n {
        0x00..=0x2F => Param::Mute(Channel::Input(n)),
        0x30..=0x3B => Param::Mute(Channel::Group(n - 0x30)),
        0x3C..=0x43 => Param::Mute(Channel::FxReturn(n - 0x3C)),
        0x44 => Param::Mute(Channel::Lr),
        0x45..=0x50 => Param::Mute(Channel::Aux(n - 0x45)),
        0x51..=0x54 => Param::Mute(Channel::FxSend(n - 0x51)),
        0x55..=0x57 => Param::Mute(Channel::Matrix(n - 0x55)),
        0x100..=0x107 => Param::Mute(Channel::Dca(n - 0x100)),
        0x200..=0x207 => Param::Mute(Channel::MuteGroup(n - 0x200)),
        0x2780 => Param::Master(Channel::Lr),
        0x2781..=0x278C => Param::Master(Channel::Aux(n - 0x2781)),
        0x278D..=0x2790 => Param::Master(Channel::FxSend(n - 0x278D)),
        0x2791..=0x2793 => Param::Master(Channel::Matrix(n - 0x2791)),
        0x27A0..=0x27A7 => Param::Master(Channel::Dca(n - 0x27A0)),
        0x2000..=0x277F => {
            let (s, d) = send(a)?;
            Param::Level(s, d)
        }
        0x2800..=0x2F7F => {
            let (s, d) = send(a - 0x800)?;
            Param::Pan(s, d)
        }
        0x2F80 => Param::Balance(Channel::Lr),
        0x2F81..=0x2F8C => Param::Balance(Channel::Aux(n - 0x2F81)),
        0x2F91..=0x2F93 => Param::Balance(Channel::Matrix(n - 0x2F91)),
        // Inputs to Groups (p.29) and FX Returns to Groups (p.30): assignments alone.
        0x3374..=0x35B3 => Param::Assign(Source::Input((n - 0x3374) / MIXES), Dest::Group((n - 0x3374) % MIXES)),
        0x35B4..=0x3613 => Param::Assign(Source::FxReturn((n - 0x35B4) / MIXES), Dest::Group((n - 0x35B4) % MIXES)),
        0x3000..=0x377F => {
            let (s, d) = send(a - 0x1000)?;
            Param::Assign(s, d)
        }
        _ => return None,
    })
}

/// The NRPN fader law (Utility > General > MIDI).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Taper {
    /// 16384 values, straight in dB: the standard mode.
    #[default]
    Linear,
    /// 255 positions, as the SQ's faders.
    Audio,
}

impl Taper {
    /// The 14-bit value of a level.
    pub fn value(self, db: f32) -> u16 {
        if db <= -89.5 {
            return 0;
        }
        match self {
            // The table's "Example Linear Taper Level Values": 0 dB is 76 5C, 118.7 a dB.
            // (The document's worked examples differ from its table by up to 0.65 dB,
            // -20 dB as 63 49 where the table has 64 16: Sidecar follows the table.)
            Taper::Linear => (15196.0 + 118.7 * db).round().clamp(1.0, 16383.0) as u16,
            Taper::Audio => ((FaderLaw::SQ.to_pos(db) * 255.0).round() as u16) << 6,
        }
    }

    /// The level of a 14-bit value.
    pub fn db(self, value: u16) -> f32 {
        if value == 0 {
            return FADER_MIN;
        }
        match self {
            Taper::Linear => ((value as f32 - 15196.0) / 118.7).min(10.0),
            Taper::Audio => FaderLaw::SQ.to_db((value >> 6) as f32 / 255.0),
        }
    }
}

/// A pan (-100..100) as the protocol's value: 00 00 full left, 3F 7F the centre, 7F 7F full right.
fn pan_value(pan: f32) -> u16 {
    if pan.abs() < 0.05 {
        return 0x1FFF;
    }
    (((pan + 100.0) / 200.0 * 16383.0).round() as u16).min(16383)
}

fn pan_of(value: u16) -> f32 {
    if value == 0x1FFF {
        return 0.0;
    }
    (value as f32 / 16383.0 * 200.0 - 100.0).clamp(-100.0, 100.0)
}

/// The engine's strips, looked up once.
struct Console<'a> {
    engine: &'a Engine,
    inputs: Vec<Arc<Strip>>,
    returns: Vec<Arc<Strip>>,
    buses: Vec<Arc<Strip>>,
    mains: Vec<Arc<Strip>>,
    matrices: Vec<Arc<Strip>>,
    /// How many of the twelve mixes are Groups (the last ones).
    groups: usize,
}

impl<'a> Console<'a> {
    fn new(engine: &'a Engine, groups: usize) -> Option<Self> {
        (engine.profile() == Profile::AhSq5).then(|| Self {
            engine,
            inputs: engine.strips(StripKind::Channel),
            returns: engine.strips(StripKind::Aux),
            buses: engine.strips(StripKind::Bus),
            mains: engine.strips(StripKind::Main),
            matrices: engine.strips(StripKind::Matrix),
            groups,
        })
    }

    /// Aux `i`, if there is one under this Bus Config.
    fn aux(&self, i: usize) -> Option<&Arc<Strip>> {
        (i < MIXES - self.groups).then(|| self.buses.get(i)).flatten()
    }

    /// Group `g`, if there is one: they come after the Auxes (p.87).
    fn group(&self, g: usize) -> Option<&Arc<Strip>> {
        (g < self.groups).then(|| self.buses.get(MIXES - self.groups + g)).flatten()
    }

    fn channel(&self, c: Channel) -> Option<&Arc<Strip>> {
        match c {
            Channel::Input(i) => self.inputs.get(i),
            Channel::FxReturn(i) => self.returns.get(i),
            Channel::Lr => self.mains.first(),
            Channel::Aux(i) => self.aux(i),
            Channel::Group(g) => self.group(g),
            Channel::FxSend(i) => self.buses.get(MIXES + i),
            Channel::Matrix(i) => self.matrices.get(i),
            Channel::Dca(_) | Channel::MuteGroup(_) => None,
        }
    }

    fn source(&self, s: Source) -> Option<&Arc<Strip>> {
        match s {
            Source::Input(i) => self.inputs.get(i),
            Source::FxReturn(i) => self.returns.get(i),
            Source::Lr => self.mains.first(),
            Source::Aux(i) => self.aux(i),
            Source::Group(g) => self.group(g),
        }
    }

    /// The bus slot a destination is the send to, if it's a bus.
    fn bus_slot(&self, d: Dest) -> Option<usize> {
        let bus = match d {
            Dest::Aux(i) => self.aux(i),
            Dest::Group(g) => self.group(g),
            Dest::FxSend(i) => self.buses.get(MIXES + i),
            Dest::Lr | Dest::Matrix(_) => None,
        }?;
        usize::try_from(bus.id()).ok()
    }

    fn read(&self, p: Param, taper: Taper) -> Option<u16> {
        let dcas = &self.engine.controls.dcas;
        Some(match p {
            Param::Mute(Channel::Dca(i)) => dcas.get(i)?.mute.load(Relaxed) as u16,
            Param::Mute(Channel::MuteGroup(i)) => self.engine.controls.mute_groups.contains(i) as u16,
            Param::Mute(c) => self.channel(c)?.params.mute.load(Relaxed) as u16,
            Param::Master(Channel::Dca(i)) => taper.value(dcas.get(i)?.fader_db.load()),
            Param::Master(c) => taper.value(self.channel(c)?.params.fader_db.load()),
            Param::Level(s, Dest::Lr) => taper.value(self.source(s)?.params.fader_db.load()),
            Param::Level(s, Dest::Matrix(m)) => taper.value(self.source(s)?.matrix_send(m)?.level_db.load()),
            Param::Level(s, d) => {
                let slot = self.bus_slot(d)?;
                taper.value(self.source(s)?.sends.find(slot).map_or(FADER_MIN, |p| p.level_db.load()))
            }
            Param::Balance(c) => pan_value(self.channel(c)?.params.pan.load()),
            Param::Pan(s, Dest::Lr) => pan_value(self.source(s)?.params.pan.load()),
            Param::Pan(s, Dest::Matrix(m)) => pan_value(self.source(s)?.matrix_send(m)?.pan.load()),
            Param::Pan(s, d) => {
                let slot = self.bus_slot(d)?;
                pan_value(self.source(s)?.sends.find(slot).map_or(0.0, |p| p.pan.load()))
            }
            Param::Assign(s, Dest::Lr) => self.source(s)?.main_send(0)?.on.load(Relaxed) as u16,
            Param::Assign(s, Dest::Matrix(m)) => self.source(s)?.matrix_send(m)?.on.load(Relaxed) as u16,
            Param::Assign(s, d) => {
                let slot = self.bus_slot(d)?;
                self.source(s)?.sends.find(slot).is_some_and(|p| p.enabled.load(Relaxed)) as u16
            }
        })
    }

    fn write(&self, p: Param, taper: Taper, v: u16) -> Option<()> {
        let dcas = &self.engine.controls.dcas;
        let on = v != 0;
        match p {
            Param::Mute(Channel::Dca(i)) => dcas.get(i)?.mute.store(on, Relaxed),
            Param::Mute(Channel::MuteGroup(i)) => self.engine.controls.mute_groups.set(i, on),
            Param::Mute(c) => self.channel(c)?.params.mute.store(on, Relaxed),
            Param::Master(Channel::Dca(i)) => dcas.get(i)?.fader_db.store(taper.db(v)),
            Param::Master(c) => self.channel(c)?.params.fader_db.store(taper.db(v)),
            Param::Level(s, Dest::Lr) => self.source(s)?.params.fader_db.store(taper.db(v)),
            Param::Level(s, Dest::Matrix(m)) => self.source(s)?.matrix_send(m)?.level_db.store(taper.db(v)),
            Param::Level(s, d) => self.source(s)?.sends.get_or_create(self.bus_slot(d)?)?.level_db.store(taper.db(v)),
            Param::Balance(c) => self.channel(c)?.params.pan.store(pan_of(v)),
            Param::Pan(s, Dest::Lr) => {
                // "Inputs to LR (+Groups)": one pan, for both.
                let strip = self.source(s)?;
                strip.params.pan.store(pan_of(v));
                for g in 0..self.groups {
                    if let Some(send) = self.bus_slot(Dest::Group(g)).and_then(|slot| strip.sends.find(slot)) {
                        send.pan.store(pan_of(v));
                    }
                }
            }
            Param::Pan(s, Dest::Matrix(m)) => self.source(s)?.matrix_send(m)?.pan.store(pan_of(v)),
            Param::Pan(s, d) => self.source(s)?.sends.get_or_create(self.bus_slot(d)?)?.pan.store(pan_of(v)),
            Param::Assign(s, Dest::Lr) => self.source(s)?.main_send(0)?.on.store(on, Relaxed),
            Param::Assign(s, Dest::Matrix(m)) => self.source(s)?.matrix_send(m)?.on.store(on, Relaxed),
            Param::Assign(s, d) => self.source(s)?.sends.get_or_create(self.bus_slot(d)?)?.enabled.store(on, Relaxed),
        }
        Some(())
    }
}

/// What the MIDI asks of the app.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Event {
    /// Recall scene `n` (1-300).
    RecallScene(u16),
}

/// The protocol's state: what it has received, and sent.
pub struct Midi {
    /// The SQ's MIDI channel (0-15).
    pub channel: u8,
    pub taper: Taper,
    msb: u8,
    lsb: u8,
    coarse: u8,
    bank: u8,
    /// Every address Sidecar has, and the value last sent or received.
    params: Vec<(u16, Param)>,
    last: Vec<Option<u16>>,
    out: VecDeque<Vec<u8>>,
    events: VecDeque<Event>,
    soft_keys: VecDeque<(u8, bool)>,
    primed: bool,
    /// How many of the twelve mixes are Groups (Bus Config).
    groups: usize,
}

impl Default for Midi {
    fn default() -> Self {
        let params: Vec<(u16, Param)> = (0..0x3800).filter_map(|a| decode(a).map(|p| (a, p))).collect();
        let last = vec![None; params.len()];
        Self {
            channel: 0,
            taper: Taper::Linear,
            msb: 0,
            lsb: 0,
            coarse: 0,
            bank: 0,
            params,
            last,
            out: VecDeque::new(),
            events: VecDeque::new(),
            soft_keys: VecDeque::new(),
            primed: false,
            groups: 0,
        }
    }
}

impl Midi {
    pub fn take_out(&mut self) -> Option<Vec<u8>> {
        self.out.pop_front()
    }

    pub fn take_event(&mut self) -> Option<Event> {
        self.events.pop_front()
    }

    /// A Soft Key's note arrived: its number (0-7) and whether it's pressed.
    pub fn take_soft_key(&mut self) -> Option<(u8, bool)> {
        self.soft_keys.pop_front()
    }

    /// Bus Config: `n` of the twelve mixes are Groups (p.87).
    pub fn set_groups(&mut self, n: usize) {
        self.groups = n.min(MIXES);
    }

    fn address(&self) -> u16 {
        (self.msb as u16) << 7 | self.lsb as u16
    }

    fn slot(&self, a: u16) -> Option<usize> {
        self.params.binary_search_by_key(&a, |p| p.0).ok()
    }

    /// Handles one received MIDI message on the console.
    pub fn receive(&mut self, engine: &Engine, msg: &[u8]) {
        let Some(c) = Console::new(engine, self.groups) else { return };
        let (Some(&status), true) = (msg.first(), msg.len() >= 2) else { return };
        if status & 0x0F != self.channel {
            return;
        }
        match (status & 0xF0, msg.get(1).copied(), msg.get(2).copied()) {
            (0xB0, Some(0x00), Some(bank)) => self.bank = bank,
            (0xB0, Some(0x63), Some(v)) => self.msb = v,
            (0xB0, Some(0x62), Some(v)) => self.lsb = v,
            (0xB0, Some(0x06), Some(v)) => self.coarse = v,
            (0xB0, Some(0x26), Some(fine)) => self.apply(&c, (self.coarse as u16) << 7 | fine as u16),
            (0xB0, Some(0x60), Some(0x7F)) => self.get(&c),
            (0xB0, Some(0x60), Some(_)) => self.step(&c, true),
            (0xB0, Some(0x61), Some(_)) => self.step(&c, false),
            // A Soft Key by its note, C3 for key 1 (protocol section 3.2): both note-off forms.
            (0x90, Some(note @ 0x30..=0x37), Some(velocity)) => self.soft_keys.push_back((note - 0x30, velocity != 0)),
            (0x80, Some(note @ 0x30..=0x37), _) => self.soft_keys.push_back((note - 0x30, false)),
            (0xC0, Some(program), _) => {
                // Banks 0-2 scenes 1-300 by number; 0A-0C the Cue List's positions.
                let bank = if (0x0A..=0x0C).contains(&self.bank) { self.bank - 0x0A } else { self.bank };
                if bank <= 2 {
                    let n = bank as u16 * 128 + program as u16 + 1;
                    if n <= 300 {
                        self.events.push_back(Event::RecallScene(n));
                    }
                }
            }
            _ => {}
        }
    }

    /// An absolute value for the selected parameter.
    fn apply(&mut self, c: &Console, value: u16) {
        let a = self.address();
        let Some(i) = self.slot(a) else { return };
        let p = self.params[i].1;
        if c.write(p, self.taper, value).is_some() {
            // What it holds now, so the poll doesn't echo it.
            self.last[i] = c.read(p, self.taper);
        }
    }

    /// A 'get' (a data increment of 7F): the value goes out as an absolute message.
    fn get(&mut self, c: &Console) {
        let a = self.address();
        let Some(i) = self.slot(a) else { return };
        if let Some(v) = c.read(self.params[i].1, self.taper) {
            self.send(a, v);
        }
    }

    /// A relative message: a level by 1 dB, a pan by 1 %, a mute or assignment toggled.
    fn step(&mut self, c: &Console, up: bool) {
        let a = self.address();
        let Some(i) = self.slot(a) else { return };
        let p = self.params[i].1;
        let Some(now) = c.read(p, self.taper) else { return };
        let next = match p {
            Param::Mute(_) | Param::Assign(..) => (now == 0) as u16,
            Param::Pan(..) | Param::Balance(_) => pan_value((pan_of(now) + if up { 1.0 } else { -1.0 }).clamp(-100.0, 100.0)),
            Param::Master(_) | Param::Level(..) => {
                let db = self.taper.db(now);
                let db = if db <= FADER_MIN {
                    if up {
                        -89.0
                    } else {
                        return;
                    }
                } else {
                    db + if up { 1.0 } else { -1.0 }
                };
                self.taper.value(db.min(10.0))
            }
        };
        if c.write(p, self.taper, next).is_some() {
            self.last[i] = c.read(p, self.taper);
        }
    }

    /// Queues the console's changes since the last poll (the first takes the
    /// console's values and sends nothing).
    pub fn poll(&mut self, engine: &Engine) {
        let Some(c) = Console::new(engine, self.groups) else { return };
        for i in 0..self.params.len() {
            let (a, p) = self.params[i];
            let now = c.read(p, self.taper);
            if now != self.last[i] {
                self.last[i] = now;
                if let (true, Some(v)) = (self.primed, now) {
                    self.send(a, v);
                }
            }
        }
        self.primed = true;
    }

    /// Sends every value again (the next poll sends what differs from nothing).
    pub fn resend_all(&mut self) {
        self.last.fill(None);
    }

    fn send(&mut self, a: u16, v: u16) {
        let cc = 0xB0 | self.channel;
        for (n, b) in [(0x63, a >> 7), (0x62, a & 0x7F), (0x06, v >> 7), (0x26, v & 0x7F)] {
            self.out.push_back(vec![cc, n, b as u8]);
        }
    }

    /// Scene `n` (1-300) was recalled on the console: its bank and program change go out.
    pub fn scene_recalled(&mut self, n: u16) {
        if !(1..=300).contains(&n) {
            return;
        }
        let i = n - 1;
        self.out.push_back(vec![0xB0 | self.channel, 0x00, (i / 128) as u8]);
        self.out.push_back(vec![0xC0 | self.channel, (i % 128) as u8]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use livedaw_engine::{ConsoleConfig, Counts};

    fn sq() -> Engine {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig { profile: Profile::AhSq5, counts: Counts::SQ5 });
        e
    }

    fn nrpn(m: &mut Midi, e: &Engine, bytes: &[u8]) {
        for msg in bytes.chunks(3) {
            m.receive(e, msg);
        }
    }

    #[test]
    fn the_tables_formulas_land_on_the_tables_addresses() {
        let at = |msb: u16, lsb: u16| decode(msb << 7 | lsb);
        // The mute table.
        assert_eq!(at(0x00, 0x2F), Some(Param::Mute(Channel::Input(47))));
        assert_eq!(at(0x00, 0x3C), Some(Param::Mute(Channel::FxReturn(0))));
        assert_eq!(at(0x00, 0x44), Some(Param::Mute(Channel::Lr)));
        assert_eq!(at(0x00, 0x50), Some(Param::Mute(Channel::Aux(11))));
        assert_eq!(at(0x00, 0x54), Some(Param::Mute(Channel::FxSend(3))));
        assert_eq!(at(0x02, 0x07), Some(Param::Mute(Channel::Dca(7))));
        assert_eq!(at(0x04, 0x07), Some(Param::Mute(Channel::MuteGroup(7))));
        assert_eq!(at(0x00, 0x30), Some(Param::Mute(Channel::Group(0))));
        // The level tables' corners.
        assert_eq!(at(0x40, 0x2F), Some(Param::Level(Source::Input(47), Dest::Lr)));
        assert_eq!(at(0x40, 0x44), Some(Param::Level(Source::Input(0), Dest::Aux(0))));
        assert_eq!(at(0x41, 0x00), Some(Param::Level(Source::Input(5), Dest::Aux(0))));
        assert_eq!(at(0x45, 0x03), Some(Param::Level(Source::Input(47), Dest::Aux(11))));
        assert_eq!(at(0x46, 0x14), Some(Param::Level(Source::FxReturn(0), Dest::Aux(0))));
        assert_eq!(at(0x46, 0x73), Some(Param::Level(Source::FxReturn(7), Dest::Aux(11))));
        assert_eq!(at(0x4C, 0x14), Some(Param::Level(Source::Input(0), Dest::FxSend(0))));
        assert_eq!(at(0x4D, 0x53), Some(Param::Level(Source::Input(47), Dest::FxSend(3))));
        assert_eq!(at(0x4E, 0x23), Some(Param::Level(Source::FxReturn(7), Dest::FxSend(3))));
        assert_eq!(at(0x4E, 0x24), Some(Param::Level(Source::Lr, Dest::Matrix(0))));
        assert_eq!(at(0x4E, 0x4A), Some(Param::Level(Source::Aux(11), Dest::Matrix(2))));
        assert_eq!(at(0x4F, 0x00), Some(Param::Master(Channel::Lr)));
        assert_eq!(at(0x4F, 0x0C), Some(Param::Master(Channel::Aux(11))));
        assert_eq!(at(0x4F, 0x10), Some(Param::Master(Channel::FxSend(3))));
        assert_eq!(at(0x4F, 0x11), Some(Param::Master(Channel::Matrix(0))));
        assert_eq!(at(0x4F, 0x27), Some(Param::Master(Channel::Dca(7))));
        // Pan and assignment: the same map 0x800 and 0x1000 on.
        assert_eq!(at(0x55, 0x03), Some(Param::Pan(Source::Input(47), Dest::Aux(11))));
        assert_eq!(at(0x65, 0x03), Some(Param::Assign(Source::Input(47), Dest::Aux(11))));
        assert_eq!(at(0x60, 0x00), Some(Param::Assign(Source::Input(0), Dest::Lr)));
    }

    #[test]
    fn both_fader_laws_match_the_tables() {
        // (dB, VC, VF) of "Example Linear Taper Level Values".
        for (db, vc, vf) in [(0.0, 0x76, 0x5C), (-20.0, 0x64, 0x16), (-40.0, 0x51, 0x4F), (10.0, 0x7F, 0x7F), (-89.0, 0x24, 0x16)]
        {
            let v = Taper::Linear.value(db);
            assert!((v as i32 - (vc << 7 | vf)).abs() <= 2, "{db}: {v}");
            assert!((Taper::Linear.db(v) - db).abs() < 0.03, "{db}");
        }
        assert_eq!(Taper::Linear.value(FADER_MIN), 0);
        assert_eq!(Taper::Linear.db(0), FADER_MIN);
        // "Approximate Audio Taper Level Values".
        for (db, vc, vf) in [(0.0, 0x62, 0x00), (-20.0, 0x2E, 0x40), (10.0, 0x7F, 0x40)] {
            assert_eq!(Taper::Audio.value(db), (vc << 7 | vf) as u16, "{db}");
        }
        // "Example Pan/Balance Values".
        assert_eq!(pan_value(-100.0), 0);
        assert_eq!(pan_value(0.0), 0x3F << 7 | 0x7F);
        assert_eq!(pan_value(100.0), 0x7F << 7 | 0x7F);
        assert!((pan_of(0x06 << 7 | 0x33) + 90.0).abs() < 0.1);
    }

    #[test]
    fn the_protocols_examples_do_what_it_says() {
        let e = sq();
        let mut m = Midi::default();
        let ch = |i: usize| e.strips(StripKind::Channel)[i].clone();
        // Ip1, Mute On / Off / Toggle.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x00, 0xB0, 0x62, 0x00, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        assert!(ch(0).params.mute.load(Relaxed));
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x00, 0xB0, 0x62, 0x00, 0xB0, 0x60, 0x00]);
        assert!(!ch(0).params.mute.load(Relaxed), "an increment toggles it");
        // Ip1 to LR, -20 dB.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x40, 0xB0, 0x62, 0x00, 0xB0, 0x06, 0x64, 0xB0, 0x26, 0x16]);
        assert!((ch(0).params.fader_db.load() + 20.0).abs() < 0.02);
        // Ip40 to Aux5, -20 dB (the level table's value).
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x44, 0xB0, 0x62, 0x1C, 0xB0, 0x06, 0x64, 0xB0, 0x26, 0x16]);
        let aux5 = e.bus_in_slot(4).unwrap().id() as usize;
        assert!((ch(39).sends.find(aux5).unwrap().level_db.load() + 20.0).abs() < 0.02);
        // Relative: +1 dB, the same parameter.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x44, 0xB0, 0x62, 0x1C, 0xB0, 0x60, 0x00]);
        assert!((ch(39).sends.find(aux5).unwrap().level_db.load() + 19.0).abs() < 0.03);
        // Ip1 to LR, L100%.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x50, 0xB0, 0x62, 0x00, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x00]);
        assert_eq!(ch(0).params.pan.load(), -100.0);
        // Another MIDI channel: ignored.
        nrpn(&mut m, &e, &[0xB3, 0x63, 0x00, 0xB3, 0x62, 0x00, 0xB3, 0x06, 0x00, 0xB3, 0x26, 0x01]);
        assert!(!ch(0).params.mute.load(Relaxed));
        // Mute Grp 4, DCA 1.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x04, 0xB0, 0x62, 0x03, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        assert!(e.controls.mute_groups.contains(3));
    }

    #[test]
    fn a_get_answers_with_the_value_and_changes_nothing() {
        let e = sq();
        let mut m = Midi::default();
        let ch = e.strips(StripKind::Channel)[0].clone();
        ch.params.mute.store(true, Relaxed);
        // Ip1 Mute.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x00, 0xB0, 0x62, 0x00, 0xB0, 0x60, 0x7F]);
        assert!(ch.params.mute.load(Relaxed), "not a toggle");
        let sent: Vec<_> = std::iter::from_fn(|| m.take_out()).collect();
        assert_eq!(sent, vec![vec![0xB0, 0x63, 0x00], vec![0xB0, 0x62, 0x00], vec![0xB0, 0x06, 0x00], vec![0xB0, 0x26, 0x01]]);
    }

    #[test]
    fn scenes_by_bank_and_program_as_the_examples() {
        let e = sq();
        let mut m = Midi::default();
        for (bytes, n) in [
            ([0xB0, 0x00, 0x00, 0xC0, 0x06], 7),
            ([0xB0, 0x00, 0x00, 0xC0, 0x77], 120),
            ([0xB0, 0x00, 0x01, 0xC0, 0x1B], 156),
            ([0xB0, 0x00, 0x0B, 0xC0, 0x1B], 156), // cue 156
        ] {
            m.receive(&e, &bytes[..3]);
            m.receive(&e, &bytes[3..]);
            assert_eq!(m.take_event(), Some(Event::RecallScene(n)));
        }
        m.scene_recalled(156);
        assert_eq!(m.take_out(), Some(vec![0xB0, 0x00, 0x01]));
        assert_eq!(m.take_out(), Some(vec![0xC0, 0x1B]));
    }

    #[test]
    fn console_changes_go_out_received_values_dont_echo() {
        let e = sq();
        let mut m = Midi::default();
        m.poll(&e);
        assert_eq!(m.take_out(), None, "nothing at start");
        let ch = e.strips(StripKind::Channel)[0].clone();
        ch.params.fader_db.store(0.0);
        m.poll(&e);
        let sent: Vec<_> = std::iter::from_fn(|| m.take_out()).collect();
        // Ip1 to LR at 0 dB: 40 00, then 76 5C.
        assert_eq!(sent, vec![vec![0xB0, 0x63, 0x40], vec![0xB0, 0x62, 0x00], vec![0xB0, 0x06, 0x76], vec![0xB0, 0x26, 0x5C]]);
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x40, 0xB0, 0x62, 0x00, 0xB0, 0x06, 0x64, 0xB0, 0x26, 0x16]);
        m.poll(&e);
        assert_eq!(m.take_out(), None, "what came in doesn't go back");
    }

    /// The 14-bit number of a table's MB LB pair.
    fn at(mb: u16, lb: u16) -> u16 {
        mb << 7 | lb
    }

    #[test]
    fn the_group_tables_land_on_their_addresses() {
        // Mutes (p.21), levels (pp.23-24), balances (p.27), assignments (pp.29-31).
        assert_eq!(decode(at(0x00, 0x30)), Some(Param::Mute(Channel::Group(0))));
        assert_eq!(decode(at(0x00, 0x3B)), Some(Param::Mute(Channel::Group(11))));
        assert_eq!(decode(at(0x40, 0x30)), Some(Param::Level(Source::Group(0), Dest::Lr)));
        assert_eq!(decode(at(0x45, 0x04)), Some(Param::Level(Source::Group(0), Dest::Aux(0))));
        assert_eq!(decode(at(0x45, 0x0E)), Some(Param::Level(Source::Group(0), Dest::Aux(10))));
        assert_eq!(decode(at(0x45, 0x0F)), None, "Grp1 has no way to Aux12");
        assert_eq!(decode(at(0x45, 0x10)), Some(Param::Level(Source::Group(1), Dest::Aux(0))));
        assert_eq!(decode(at(0x45, 0x19)), Some(Param::Level(Source::Group(1), Dest::Aux(9))));
        assert_eq!(decode(at(0x45, 0x5B)), Some(Param::Level(Source::Group(7), Dest::Aux(3))));
        assert_eq!(decode(at(0x4D, 0x54)), Some(Param::Level(Source::Group(0), Dest::FxSend(0))));
        assert_eq!(decode(at(0x4D, 0x5B)), Some(Param::Level(Source::Group(1), Dest::FxSend(3))));
        assert_eq!(decode(at(0x4E, 0x54)), Some(Param::Level(Source::Group(3), Dest::Matrix(0))));
        assert_eq!(decode(at(0x4E, 0x6E)), Some(Param::Level(Source::Group(11), Dest::Matrix(2))));
        assert_eq!(decode(at(0x55, 0x04)), Some(Param::Pan(Source::Group(0), Dest::Aux(0))));
        assert_eq!(decode(at(0x5E, 0x4B)), Some(Param::Pan(Source::Group(0), Dest::Matrix(0))));
        assert_eq!(decode(at(0x5F, 0x00)), Some(Param::Balance(Channel::Lr)));
        assert_eq!(decode(at(0x5F, 0x0C)), Some(Param::Balance(Channel::Aux(11))));
        assert_eq!(decode(at(0x5F, 0x13)), Some(Param::Balance(Channel::Matrix(2))));
        assert_eq!(decode(at(0x60, 0x30)), Some(Param::Assign(Source::Group(0), Dest::Lr)));
        assert_eq!(decode(at(0x66, 0x74)), Some(Param::Assign(Source::Input(0), Dest::Group(0))));
        assert_eq!(decode(at(0x66, 0x7F)), Some(Param::Assign(Source::Input(0), Dest::Group(11))));
        assert_eq!(decode(at(0x67, 0x00)), Some(Param::Assign(Source::Input(1), Dest::Group(0))));
        assert_eq!(decode(at(0x6B, 0x34)), Some(Param::Assign(Source::FxReturn(0), Dest::Group(0))));
        assert_eq!(decode(at(0x6C, 0x08)), Some(Param::Assign(Source::FxReturn(7), Dest::Group(0))));
        assert_eq!(decode(at(0x6D, 0x54)), Some(Param::Assign(Source::Group(0), Dest::FxSend(0))));
        assert_eq!(decode(at(0x6E, 0x6E)), Some(Param::Assign(Source::Group(11), Dest::Matrix(2))));
        // What was there before is where it was.
        assert_eq!(decode(at(0x66, 0x14)), Some(Param::Assign(Source::FxReturn(0), Dest::Aux(0))));
    }

    #[test]
    fn groups_follow_the_bus_config() {
        let e = sq();
        let mut m = Midi::default();
        let buses = e.strips(StripKind::Bus);
        // All Auxes: Grp1's mute answers nothing.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x00, 0xB0, 0x62, 0x30, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        assert!(buses.iter().all(|b| !b.params.mute.load(Relaxed)));
        // 5 Groups, 7 Auxes (p.87's example): Grp1 is the eighth mix, Aux8 is gone.
        m.set_groups(5);
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x00, 0xB0, 0x62, 0x30, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        assert!(buses[7].params.mute.load(Relaxed));
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x00, 0xB0, 0x62, 0x4C, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        assert!(!buses[8].params.mute.load(Relaxed), "Aux8 isn't an Aux now");
        // Ip2 to Grp2 is the send to the ninth mix.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x67, 0xB0, 0x62, 0x01, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        let slot = buses[8].id() as usize;
        assert!(e.strips(StripKind::Channel)[1].sends.find(slot).unwrap().enabled.load(Relaxed));
        // Grp1 to LR is its master, and to LR it's assigned on the main send.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x40, 0xB0, 0x62, 0x30, 0xB0, 0x06, 0x64, 0xB0, 0x26, 0x16]);
        assert!((buses[7].params.fader_db.load() + 20.0).abs() < 0.1);
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x60, 0xB0, 0x62, 0x30, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        assert!(buses[7].main_send(0).unwrap().on.load(Relaxed));
        // A channel's pan to LR is its pan to the Groups.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x50, 0xB0, 0x62, 0x01, 0xB0, 0x06, 0x7F, 0xB0, 0x26, 0x7F]);
        assert!((e.strips(StripKind::Channel)[1].sends.find(slot).unwrap().pan.load() - 100.0).abs() < 0.1);
        // The Output balance table.
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x5F, 0xB0, 0x62, 0x01, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x00]);
        assert!((buses[0].params.pan.load() + 100.0).abs() < 0.1);
    }

    #[test]
    fn soft_keys_by_their_notes() {
        let e = sq();
        let mut m = Midi::default();
        m.receive(&e, &[0x90, 0x30, 0x7F]);
        m.receive(&e, &[0x80, 0x30, 0x00]);
        m.receive(&e, &[0x90, 0x36, 0x7F]);
        m.receive(&e, &[0x90, 0x36, 0x00]);
        m.receive(&e, &[0x90, 0x38, 0x7F]); // Soft Key 9: the SQ-5 has 8
        m.receive(&e, &[0x91, 0x30, 0x7F]); // another channel
        assert_eq!(m.take_soft_key(), Some((0, true)));
        assert_eq!(m.take_soft_key(), Some((0, false)));
        assert_eq!(m.take_soft_key(), Some((6, true)));
        assert_eq!(m.take_soft_key(), Some((6, false)));
        assert_eq!(m.take_soft_key(), None);
    }

    #[test]
    fn another_console_is_left_alone() {
        let e = Engine::new(0);
        e.configure(&ConsoleConfig::wing());
        let mut m = Midi::default();
        let before = e.strips(StripKind::Channel)[0].params.mute.load(Relaxed);
        nrpn(&mut m, &e, &[0xB0, 0x63, 0x00, 0xB0, 0x62, 0x00, 0xB0, 0x06, 0x00, 0xB0, 0x26, 0x01]);
        assert_eq!(e.strips(StripKind::Channel)[0].params.mute.load(Relaxed), before);
        m.poll(&e);
        assert_eq!(m.take_out(), None);
    }
}
